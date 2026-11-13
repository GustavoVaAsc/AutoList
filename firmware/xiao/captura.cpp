// Endpoint /capture de la XIAO: cámara, firma HMAC y respuesta con la foto.
//
// El servidor HTTP va en su propia tarea de FreeRTOS (la que crea el servidor
// HTTP del IDF, httpd): el manejador de /capture saca la foto y la sirve ahí
// dentro, así que el lazo de Arduino, donde vive el MQTT, no se bloquea. La
// alternativa (el servidor web de Arduino, que atiende en loop()) dejaría el
// MQTT sin responder mientras dura la captura. httpd atiende las peticiones EN
// SERIE (una sola tarea): dos capturas no pueden correr a la vez por sí mismas.
//
// La cámara está APAGADA salvo mientras se usa (captura.h): se inicializa en el
// manejador, con la primera petición firmada, y la tarea de mantenimiento (una
// propia, cada segundo, independiente de loop()) la apaga tras
// kInactividadCamaraMs sin capturas. Un candado (g_candado) protege la cámara y
// todo el estado de la protección, porque lo tocan dos tareas: la de httpd (el
// manejador) y la de mantenimiento. El candado se toma SOLO mientras se usa la
// cámara y se copia la foto: el envío por la red va fuera, para que un cliente
// lento no pueda bloquear el apagado por calor ni por inactividad.
//
// Orden de las comprobaciones del manejador, de la más barata a la que toca la
// cámara; ninguna de las primeras la toca:
//   1. firma y anti-replay  -> 401 / 403
//   2. candado              -> 503 "ocupada"
//   3. temperatura          -> 503 "temperatura"
//   4. presupuesto de encendido -> 503 "enfriamiento"
//   5. reposo tras un fallo -> 503 "camara no lista"
//   6. límite de capturas   -> 429
//   7. cámara: encender si hace falta, resolución, descartar cuadros, copiar la foto
//   8. (sin candado) cifrar la copia en sitio (AES-256-GCM) y enviarla con plazo
// "ocupada" solo sale si la tarea de mantenimiento tiene el candado (unos
// milisegundos: apagar la cámara o leer la temperatura) o si otra captura sigue
// con la cámara; httpd, al ser una sola tarea, no la solapa.
//
// El límite va DESPUÉS de la firma a propósito: así quien no sabe firmar no
// puede agotar el límite del cliente legítimo (solo cuentan las peticiones
// autenticadas), y como la firma se comprueba antes de llegar a la cámara,
// una petición sin firma nunca la toca ni la enciende. El costo es que cada
// petición sin firma gasta un HMAC (una fracción de milisegundo); no hay límite
// para las que fallan la firma, solo el de su línea del registro. Un efecto
// lateral: el nonce de una petición firmada que recibe 429 o 503 queda gastado,
// así que reenviarla da 403; el cliente reintenta con un nonce nuevo.
//
// La SCCB de la cámara va por GPIO39/40. Aquí no se inicializa ningún bus de
// I2C ni se llama a Wire.begin(): el driver de cámara instala el suyo.
// OJO: el esp32-camera que viene precompilado en el core 3.3.12 está hecho con
// CONFIG_SCCB_HARDWARE_I2C_PORT1=y, así que su bus de SCCB cae en el
// peripheral I2C1 (no en el I2C0). En el core, Wire es el I2C0 y sus pines por
// defecto en esta placa son GPIO5/6 (D4/D5), justo donde irá el INA219; por
// eso el INA219 debería ir con Wire.begin(5,6) y no con Wire1. Ver el informe
// de esta fase.
//
// Protocolo de la petición (tiene que coincidir byte a byte con el del
// servicio de visión):
//   GET /capture?res=SVGA
//   X-Captura-TS:    epoch en segundos del reloj del BORDE
//   X-Captura-Nonce: entero aleatorio de 64 bits
//   X-Captura-HMAC:  64 caracteres hex
// Respuestas: 200 con la JPEG CIFRADA (AES-256-GCM, application/octet-stream,
// IV en X-Captura-IV; ver logica_camara.h), 401 si falta una cabecera o la firma
// no cuadra, 403 si el nonce se repitió o el ts es antiguo o futuro, 429 si se pasó el
// límite de capturas (con Retry-After), 503 si la cámara no está lista, está
// ocupada, hace demasiado calor ("temperatura") o está en enfriamiento. Las
// respuestas de error llevan Connection: close.
#include <Arduino.h>
#include <Preferences.h>
#include <esp_camera.h>
#include <esp_heap_caps.h>
#include <esp_attr.h>
#include <esp_http_server.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lwip/sockets.h>
#include <mbedtls/gcm.h>
#include <mbedtls/platform_util.h>
#include <math.h>
#include <string.h>

#include <atomic>

#include "captura.h"
#include "logica_camara.h"
#include "logica_proteccion.h"
#include "vectores_gcm.h"

namespace captura {
namespace {

using logica_proteccion::Perfil;
using logica_proteccion::Resolucion;
using logica_proteccion::Transicion;

// Cuánto espera el manejador al candado antes de contestar 503 "ocupada". La
// tarea de mantenimiento lo toma unos milisegundos para apagar la cámara o leer
// la temperatura: 100 ms evitan un 503 por ese cruce, pero otra captura con la
// cámara en la mano (cientos de ms o más) sí lo agota.
constexpr TickType_t kEsperaCandado = pdMS_TO_TICKS(100);

// El envío de la foto: en trozos de este tamaño, con un plazo total que se mira
// entre trozos. Pasado el plazo se cierra la sesión: un cliente que lee a
// cuentagotas no retiene la tarea de httpd (que atiende a todos).
//
// El peor caso real NO es el plazo, es el plazo más lo que tarde el trozo en
// curso: una llamada a httpd_resp_send_chunk hace hasta tres send() (el largo
// del trozo, los datos y el CRLF), y cada uno espera como mucho
// send_wait_timeout (1 s). O sea, hasta 3 s de plazo + 3 s del último trozo, unos
// 6 s en el peor caso (un cliente que deja de leer justo antes del plazo); con un
// cliente que lee a un ritmo constante, el plazo se cumple casi al milisegundo.
constexpr size_t kTrozoEnvioBytes = 4096;
constexpr uint32_t kPlazoEnvioMs = 3000;

// Cada cuánto corre la tarea de mantenimiento (apagado por inactividad, por
// presupuesto o por calor, y la lectura de temperatura de cada 30 s).
constexpr uint32_t kPeriodoMantenimientoMs = 1000;

// Estado del módulo. El servidor HTTP se guarda para poder pararlo.
httpd_handle_t g_servidor = nullptr;
TaskHandle_t g_tarea = nullptr;
std::atomic<bool> g_pararTarea{false};

// El candado de la cámara y del estado de la protección. Se crea una vez en
// iniciar() y no se destruye.
SemaphoreHandle_t g_candado = nullptr;

// Lo siguiente solo se modifica con g_candado tomado. Los atómicos además los
// lee quien no tiene el candado (el latido de salud).
std::atomic<bool> g_camaraLista{false};
std::atomic<uint32_t> g_ultimaActividadMs{0};  // arranque de la cámara o fin de la última captura
std::atomic<uint32_t> g_ultimaLecturaMs{0};    // última lectura de temperatura
std::atomic<float> g_temperatura{NAN};
std::atomic<bool> g_corteTermico{false};       // temperatura en corte o lectura no válida
std::atomic<bool> g_enfriando{false};          // dentro del enfriamiento del presupuesto
Resolucion g_resolucionActual = logica_proteccion::kResolucionMaxima;  // la del sensor ahora
logica_proteccion::LimitadorCapturas g_limitador;
logica_proteccion::GuardaTermica g_termica;
logica_proteccion::PresupuestoCamara g_presupuesto;
logica_proteccion::ReposoTrasFallo g_reposo;

// El validador de la firma: recuerda los nonces ya usados y el último ts
// aceptado, así que tiene que sobrevivir entre peticiones. Solo lo usa la tarea
// de httpd (una sola, que atiende las peticiones en serie).
logica_camara::Validador* g_validador = nullptr;

// El secreto viene de secrets.h (CAPTURA_HMAC_SECRETO), que es estático: el
// puntero vale toda la vida del firmware.
const char* g_secreto = nullptr;

// El piso de ts guardado en la flash (NVS). Solo lo toca la tarea de httpd.
Preferences g_prefs;
bool g_prefsAbierto = false;
// Qué se guarda y cuándo (con el tope por arranque): ver PersistenciaTs.
logica_camara::PersistenciaTs g_persistencia;
// Si el NVS funciona (abierto y sin fallo de escritura): lo publica el latido.
std::atomic<bool> g_nvsBien{false};
// Reintentos del NVS: abrirlo de nuevo o escribir tras un fallo, como mucho una
// vez por minuto (y sin inundar el registro).
logica_proteccion::ReposoTrasFallo g_reposoNvs(60000);
logica_proteccion::LimitadorMensajes g_logNvs;
constexpr const char* kNvsEspacio = "captura";
constexpr const char* kNvsClaveTs = "ts";

// La clave AES-256 del cifrado de la respuesta (derivada del secreto en
// iniciar()). Vive solo en RAM.
uint8_t g_claveRespuesta[logica_camara::kLongitudClaveRespuesta] = {0};

// El estado de seguridad que sobrevive a un reinicio (ver EstadoSeguridadRtc).
// Solo se toca con g_candado tomado (o antes de arrancar las tareas).
RTC_NOINIT_ATTR logica_proteccion::EstadoSeguridadRtc g_estadoRtc;

// El origen permitido (opcional): una IPv4. Solo la tarea de httpd lo lee.
bool g_hayOrigen = false;
uint8_t g_origen[4] = {0, 0, 0, 0};

// Límites del registro: sin firma, cualquiera en la LAN puede generar miles de
// líneas del serie. Los dos los usa solo la tarea de httpd.
logica_proteccion::LimitadorMensajes g_logAutenticacion;
logica_proteccion::LimitadorMensajes g_logOrigen;

// Toma el candado mientras viva y lo suelta al salir del ámbito, por cualquier
// camino de retorno.
class Candado {
 public:
  explicit Candado(TickType_t espera) : tomado_(xSemaphoreTake(g_candado, espera) == pdTRUE) {}
  ~Candado() {
    if (tomado_) xSemaphoreGive(g_candado);
  }
  Candado(const Candado&) = delete;
  Candado& operator=(const Candado&) = delete;
  explicit operator bool() const { return tomado_; }

 private:
  bool tomado_;
};

// La copia de la foto en PSRAM, con sitio detrás para la etiqueta de 16 bytes del
// cifrado: la foto se cifra EN SITIO y el cuerpo que se envía es el texto
// cifrado seguido de la etiqueta. Se libera sola al salir, por cualquier camino.
class CopiaFoto {
 public:
  CopiaFoto() = default;
  ~CopiaFoto() { liberar(); }
  CopiaFoto(const CopiaFoto&) = delete;
  CopiaFoto& operator=(const CopiaFoto&) = delete;

  bool copiar(const uint8_t* origen, size_t largo) {
    liberar();
    datos_ = static_cast<uint8_t*>(
        heap_caps_malloc(largo + logica_camara::kLongitudEtiquetaGcm, MALLOC_CAP_SPIRAM));
    if (datos_ == nullptr) return false;
    memcpy(datos_, origen, largo);
    largo_ = largo;
    return true;
  }
  uint8_t* datos() { return datos_; }
  size_t largo() const { return largo_; }  // la foto, sin la etiqueta
  size_t largoConEtiqueta() const { return largo_ + logica_camara::kLongitudEtiquetaGcm; }

 private:
  void liberar() {
    // La foto en claro no se queda en la PSRAM: se borra antes de devolverla al
    // montón (mbedtls_platform_zeroize no lo elimina el optimizador).
    if (datos_ != nullptr) {
      mbedtls_platform_zeroize(datos_, largo_ + logica_camara::kLongitudEtiquetaGcm);
      heap_caps_free(datos_);
    }
    datos_ = nullptr;
    largo_ = 0;
  }
  uint8_t* datos_ = nullptr;
  size_t largo_ = 0;
};

// --- Cifrado de la respuesta (AES-256-GCM, mbedTLS) ------------------------------------
// mbedTLS del core 3.3.12 (mbedtls 3.6.x, MBEDTLS_GCM_C y AES por hardware
// activos). mbedtls_gcm_crypt_and_tag admite entrada = salida: la foto se cifra
// en la propia copia de PSRAM, sin otro búfer.

// Cifra en sitio `buf` (`largo` bytes de foto, con sitio para la etiqueta de 16
// bytes detrás) con la clave `clave`, el IV y el AAD dados. 0 si salió bien.
int cifrarEnSitio(const uint8_t* clave, const uint8_t* iv, const char* aad, size_t largoAad, uint8_t* buf,
                  size_t largo) {
  mbedtls_gcm_context ctx;
  mbedtls_gcm_init(&ctx);
  int r = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, clave, 256);
  if (r == 0) {
    r = mbedtls_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT, largo, iv, logica_camara::kLongitudIv,
                                  reinterpret_cast<const unsigned char*>(aad), largoAad, buf, buf,
                                  logica_camara::kLongitudEtiquetaGcm, buf + largo);
  }
  mbedtls_gcm_free(&ctx);
  return r;
}

// Descifra y comprueba la etiqueta (solo para el autodiagnóstico). 0 si autentica.
int descifrarEnSitio(const uint8_t* clave, const uint8_t* iv, const char* aad, size_t largoAad, uint8_t* buf,
                     size_t largo) {
  mbedtls_gcm_context ctx;
  mbedtls_gcm_init(&ctx);
  int r = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, clave, 256);
  if (r == 0) {
    r = mbedtls_gcm_auth_decrypt(&ctx, largo, iv, logica_camara::kLongitudIv,
                                 reinterpret_cast<const unsigned char*>(aad), largoAad,
                                 buf + largo, logica_camara::kLongitudEtiquetaGcm, buf, buf);
  }
  mbedtls_gcm_free(&ctx);
  return r;
}

// Cifra la copia de la foto para la petición (ts, nonce) con un IV nuevo:
// 12 bytes del generador de números aleatorios por hardware (esp_fill_random;
// con el Wi-Fi encendido, que lo está mientras se atiende una petición, es un
// TRNG de verdad). `ivHex` recibe los 24 caracteres hex para X-Captura-IV.
bool cifrarFoto(CopiaFoto& copia, int64_t ts, uint64_t nonce, char* ivHex) {
  uint8_t iv[logica_camara::kLongitudIv];
  esp_fill_random(iv, sizeof iv);
  char aad[logica_camara::kLongitudAadRespuestaMax];
  const size_t largoAad = logica_camara::construirAadRespuesta(aad, sizeof aad, ts, nonce);
  if (largoAad == 0) return false;
  if (cifrarEnSitio(g_claveRespuesta, iv, aad, largoAad, copia.datos(), copia.largo()) != 0) return false;
  logica_camara::ivAHex(iv, ivHex);
  return true;
}

// Autodiagnóstico del cifrado, al arrancar: deriva K del secreto del vector,
// cifra los vectores de vectores_gcm.h (generados con Python "cryptography") y
// compara; luego cifra y descifra un bloque en PSRAM (por donde irá la foto
// real) para comprobar que el hardware de AES lo maneja. Si falla, la cámara no
// arranca: no se sirve nada que no se haya podido cifrar bien.
bool autodiagnosticoGcm() {
  const uint32_t inicio = millis();
  bool ok = true;

  uint8_t k[logica_camara::kLongitudClaveRespuesta];
  logica_camara::derivarClaveRespuesta(vectores_gcm::kSecreto, strlen(vectores_gcm::kSecreto), k);
  if (memcmp(k, vectores_gcm::kClave, sizeof k) != 0) {
    Serial.println("[captura] autodiagnóstico GCM: la derivación de K no coincide");
    ok = false;
  }

  char aad[logica_camara::kLongitudAadRespuestaMax];
  uint8_t buf[sizeof vectores_gcm::kTexto1 + logica_camara::kLongitudEtiquetaGcm];

  // Vector 1, en la RAM interna.
  size_t largoAad = logica_camara::construirAadRespuesta(aad, sizeof aad, vectores_gcm::kTs1, vectores_gcm::kNonce1);
  memcpy(buf, vectores_gcm::kTexto1, sizeof vectores_gcm::kTexto1);
  if (largoAad == 0 || strcmp(aad, vectores_gcm::kAad1) != 0 ||
      cifrarEnSitio(k, vectores_gcm::kIv, aad, largoAad, buf, sizeof vectores_gcm::kTexto1) != 0 ||
      memcmp(buf, vectores_gcm::kSalida1, sizeof buf) != 0) {
    Serial.println("[captura] autodiagnóstico GCM: el vector 1 no coincide");
    ok = false;
  }

  // Vector 2, texto vacío (solo la etiqueta).
  uint8_t soloEtiqueta[logica_camara::kLongitudEtiquetaGcm];
  largoAad = logica_camara::construirAadRespuesta(aad, sizeof aad, vectores_gcm::kTs2, vectores_gcm::kNonce2);
  if (largoAad == 0 || strcmp(aad, vectores_gcm::kAad2) != 0 ||
      cifrarEnSitio(k, vectores_gcm::kIv, aad, largoAad, soloEtiqueta, 0) != 0 ||
      memcmp(soloEtiqueta, vectores_gcm::kSalida2, sizeof soloEtiqueta) != 0) {
    Serial.println("[captura] autodiagnóstico GCM: el vector 2 no coincide");
    ok = false;
  }

  // Un bloque en PSRAM, de un largo que no es múltiplo de 16: ida y vuelta con
  // verificación de la etiqueta, y el texto cifrado tiene que diferir del plano.
  constexpr size_t kLargoPsram = 1001;
  uint8_t* psram = static_cast<uint8_t*>(
      heap_caps_malloc(kLargoPsram + logica_camara::kLongitudEtiquetaGcm, MALLOC_CAP_SPIRAM));
  if (psram == nullptr) {
    Serial.println("[captura] autodiagnóstico GCM: no hay PSRAM para la prueba");
    ok = false;
  } else {
    for (size_t i = 0; i < kLargoPsram; ++i) psram[i] = static_cast<uint8_t>(i * 7 + 3);
    largoAad = logica_camara::construirAadRespuesta(aad, sizeof aad, vectores_gcm::kTs1, vectores_gcm::kNonce1);
    bool psramBien = largoAad != 0 &&
                     cifrarEnSitio(k, vectores_gcm::kIv, aad, largoAad, psram, kLargoPsram) == 0;
    bool distinto = false;
    for (size_t i = 0; i < kLargoPsram; ++i) {
      if (psram[i] != static_cast<uint8_t>(i * 7 + 3)) distinto = true;
    }
    psramBien = psramBien && distinto &&
                descifrarEnSitio(k, vectores_gcm::kIv, aad, largoAad, psram, kLargoPsram) == 0;
    for (size_t i = 0; psramBien && i < kLargoPsram; ++i) {
      if (psram[i] != static_cast<uint8_t>(i * 7 + 3)) psramBien = false;
    }
    mbedtls_platform_zeroize(psram, kLargoPsram + logica_camara::kLongitudEtiquetaGcm);
    heap_caps_free(psram);
    if (!psramBien) {
      Serial.println("[captura] autodiagnóstico GCM: el bloque en PSRAM no hizo ida y vuelta");
      ok = false;
    }
  }

  mbedtls_platform_zeroize(k, sizeof k);  // la K del vector no se queda en la pila
  Serial.printf("[captura] autodiagnóstico GCM: %s (%lu ms)\n", ok ? "OK" : "FALLO",
                (unsigned long)(millis() - inicio));
  return ok;
}

// Imprime una línea del registro con límite de una por segundo, y dice cuántas
// se callaron desde la última. Para lo que cualquiera puede provocar sin firma.
void registrarLimitado(logica_proteccion::LimitadorMensajes& limitador, const char* texto) {
  uint32_t omitidos = 0;
  if (!limitador.intentar(millis(), &omitidos)) return;
  if (omitidos > 0) {
    Serial.printf("[captura] %s (%lu omitidos desde la anterior)\n", texto, (unsigned long)omitidos);
  } else {
    Serial.printf("[captura] %s\n", texto);
  }
}

// Escribe una respuesta corta con su código y su motivo, con Connection: close
// (y pide cerrar la sesión: un error no debe dejar la conexión abierta, ni
// para un cliente que no firma), y la registra. `limitarRegistro`: las líneas
// de 401/403 salen con límite de ritmo. `reintentoMs` > 0 añade Retry-After.
void responderError(httpd_req_t* req, int codigo, const char* motivo, bool limitarRegistro = false,
                    uint32_t reintentoMs = 0) {
  char texto[96];
  snprintf(texto, sizeof texto, "%d %s", codigo, motivo);
  char reintento[12];
  if (reintentoMs > 0) {
    snprintf(reintento, sizeof reintento, "%lu", (unsigned long)((reintentoMs + 999) / 1000));
    httpd_resp_set_hdr(req, "Retry-After", reintento);
  }
  httpd_resp_set_status(req, texto);
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_set_hdr(req, "Connection", "close");
  httpd_resp_sendstr(req, texto);
  httpd_sess_trigger_close(g_servidor, httpd_req_to_sockfd(req));
  if (limitarRegistro) {
    registrarLimitado(g_logAutenticacion, texto);
  } else {
    Serial.printf("[captura] %s\n", texto);
  }
}

// Lee el valor de una cabecera. Devuelve false si la cabecera no está o si su
// valor no cabe en el búfer.
bool leerCabecera(httpd_req_t* req, const char* nombre, char* destino, size_t capacidad) {
  const size_t largo = httpd_req_get_hdr_value_len(req, nombre);
  if (largo == 0 || largo + 1 > capacidad) {
    return false;
  }
  return httpd_req_get_hdr_value_str(req, nombre, destino, capacidad) == ESP_OK;
}

// El framesize del driver de cada resolución de la lista blanca. No hay
// ningún caso por encima de XGA: es lo que impide producir algo mayor.
framesize_t framesizeDe(Resolucion resolucion) {
  switch (resolucion) {
    case Resolucion::Hvga:
      return FRAMESIZE_HVGA;  // 480x320
    case Resolucion::Vga:
      return FRAMESIZE_VGA;   // 640x480
    case Resolucion::Svga:
      return FRAMESIZE_SVGA;  // 800x600
    case Resolucion::Xga:
      return FRAMESIZE_XGA;   // 1024x768
  }
  return FRAMESIZE_SVGA;
}

// Anota en la RAM del RTC lo que debe sobrevivir a un reinicio: lo que falta del
// enfriamiento y si hay corte térmico. Con g_candado tomado (o en iniciar()).
void guardarEstadoRtc(uint32_t ahora) {
  g_estadoRtc.enfriamiento_restante_ms = g_presupuesto.esperaEnfriamientoMs(ahora);
  g_estadoRtc.corte_termico = g_termica.estado() == logica_proteccion::EstadoTermico::Corte ? 1 : 0;
  g_estadoRtc.encendido_usado_ms = g_presupuesto.encendidoEnVentanaMs(ahora);
  logica_proteccion::sellarEstadoRtc(g_estadoRtc);
}

// Apaga la cámara si estaba encendida. Con g_candado tomado.
void apagarCamara(const char* motivo) {
  if (!g_camaraLista.load()) return;
  esp_camera_deinit();
  g_camaraLista.store(false);
  g_presupuesto.apagada(millis());
  Serial.printf("[captura] cámara apagada: %s\n", motivo);
}

// La cámara falló (no arrancó, o no da cuadros): se apaga por si acaso, aunque
// el driver ya limpie sus fallos de init (esp_camera_deinit es inocuo si no hay
// nada que apagar), y durante kReposoTrasFalloMs las peticiones dan 503 "camara
// no lista" sin tocarla. Con g_candado tomado.
void fallarCamara(const char* motivo) {
  esp_camera_deinit();
  g_camaraLista.store(false);
  // Siempre se cierra el tramo de encendido: encenderCamara() lo abre ANTES del
  // init, así que el tiempo de un init fallido también gasta presupuesto.
  g_presupuesto.apagada(millis());
  g_reposo.fallo(millis());
  Serial.printf("[captura] fallo de la cámara (%s): reposo de %lu s\n", motivo,
                (unsigned long)(logica_proteccion::kReposoTrasFalloMs / 1000));
}

// Enciende la cámara al máximo permitido (XGA, para dimensionar los búferes) y
// con un solo búfer en modo bajo demanda. Con g_candado tomado. No arranca
// ningún flujo continuo: con fb_count = 1 y GRAB_WHEN_EMPTY el driver solo llena
// su único búfer; el sensor en sí transmite mientras tenga reloj, por eso se
// apaga por inactividad y por presupuesto.
bool encenderCamara(const Perfil& perfilInicial) {
  camera_config_t config;
  memset(&config, 0, sizeof config);
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = kPinD0;
  config.pin_d1 = kPinD1;
  config.pin_d2 = kPinD2;
  config.pin_d3 = kPinD3;
  config.pin_d4 = kPinD4;
  config.pin_d5 = kPinD5;
  config.pin_d6 = kPinD6;
  config.pin_d7 = kPinD7;
  config.pin_xclk = kPinXclk;
  config.pin_pclk = kPinPclk;
  config.pin_vsync = kPinVsync;
  config.pin_href = kPinHref;
  config.pin_sccb_sda = kPinSccbSda;   // SCCB de la cámara: GPIO40/39
  config.pin_sccb_scl = kPinSccbScl;
  config.pin_pwdn = kPinPwdn;
  config.pin_reset = kPinReset;
  config.xclk_freq_hz = 20000000;  // 20 MHz, el reloj que quiere el OV3660
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = framesizeDe(logica_proteccion::kResolucionMaxima);  // XGA: el techo
  config.jpeg_quality = perfilInicial.calidad_jpeg;
  config.jpeg_buffer_size = logica_proteccion::kBufferJpegBytes;
  config.fb_count = 1;                         // un solo búfer: sin captura continua
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;   // llena el búfer solo cuando está vacío
  config.fb_location = CAMERA_FB_IN_PSRAM;     // la foto no cabe en la RAM interna

  // El presupuesto cuenta desde ANTES del init: un init que falla (o se cuelga
  // un rato) calienta el módulo igual. El que llama cierra el tramo con
  // fallarCamara() si esto devuelve false.
  g_presupuesto.encendida(millis());
  const esp_err_t error = esp_camera_init(&config);
  if (error != ESP_OK) {
    Serial.printf("[captura] la cámara no arrancó: 0x%x\n", (int)error);
    return false;
  }
  g_camaraLista.store(true);
  g_resolucionActual = logica_proteccion::kResolucionMaxima;

  // El sensor: el OV3660 de la placa y el OV5640 de las más nuevas. El driver
  // los detecta por su identificador, así que el mismo firmware vale para los
  // dos. El autofocus del OV5640 (el VCM de los módulos con lente motorizada)
  // no se activa aquí: el esp32-camera del core viene sin
  // CONFIG_CAMERA_AF_SUPPORT, así que solo haría falta la biblioteca aparte
  // que controla el VCM.
  const sensor_t* sensor = esp_camera_sensor_get();
  if (sensor != nullptr) {
    Serial.printf("[captura] sensor PID=0x%04x\n", (int)sensor->id.PID);
  }
  return true;
}

// Lee la temperatura del chip, la pasa por la guarda térmica y reacciona: avisa,
// al cruzar el corte apaga la cámara, y una lectura no válida (NaN: en el
// core 3.3.12 temperatureRead() devuelve NaN si falla el sensor) también la
// apaga. Con g_candado tomado (o antes de que arranque el servidor).
void leerTemperatura(bool primera = false, bool corteRtc = false) {
  const float t = temperatureRead();
  g_temperatura.store(t);
  g_ultimaLecturaMs.store(millis());
  switch (primera ? g_termica.primeraLectura(t, corteRtc) : g_termica.actualizar(t)) {
    case Transicion::EntraAviso:
      Serial.printf("[captura] AVISO: %.1f C (aviso desde %.0f C)\n", (double)t,
                    (double)logica_proteccion::kTempAvisoC);
      break;
    case Transicion::SaleAviso:
      Serial.printf("[captura] temperatura normal otra vez: %.1f C\n", (double)t);
      break;
    case Transicion::EntraCorte:
      Serial.printf("[captura] CORTE: %.1f C (corte desde %.0f C): capturas suspendidas hasta bajar de %.0f C\n",
                    (double)t, (double)logica_proteccion::kTempCorteC,
                    (double)logica_proteccion::kTempAvisoC);
      apagarCamara("temperatura de corte");
      break;
    case Transicion::SaleCorte:
      Serial.printf("[captura] %.1f C: por debajo de %.0f C, capturas habilitadas\n", (double)t,
                    (double)logica_proteccion::kTempAvisoC);
      break;
    case Transicion::Ninguna:
      break;
  }
  if (!g_termica.lecturaValida()) {
    Serial.printf("[captura] lectura de temperatura no válida (%.1f): no se captura\n", (double)t);
    apagarCamara("lectura de temperatura no válida");
  }
  g_corteTermico.store(!g_termica.capturaPermitida());
  guardarEstadoRtc(millis());  // el corte (o su fin) sobrevive a un reinicio
}

// Mantenimiento periódico, con g_candado tomado y la hora de ahora: temperatura
// cada kIntervaloTemperaturaMs, presupuesto de encendido, e inactividad.
void mantener(uint32_t ahora) {
  if (logica_proteccion::intervaloCumplido(ahora, g_ultimaLecturaMs.load(),
                                           logica_proteccion::kIntervaloTemperaturaMs)) {
    leerTemperatura();
  }
  if (g_presupuesto.excedido(ahora)) {
    if (g_camaraLista.load()) {
      apagarCamara("presupuesto de encendido agotado");
      g_presupuesto.iniciarEnfriamiento(ahora);
      Serial.printf("[captura] presupuesto de encendido agotado: enfriamiento de %lu s\n",
                    (unsigned long)(logica_proteccion::kEnfriamientoMs / 1000));
    }
  } else if (logica_proteccion::debeApagarPorInactividad(g_camaraLista.load(), ahora,
                                                         g_ultimaActividadMs.load())) {
    apagarCamara("sin capturas durante el plazo de inactividad");
  }
  g_enfriando.store(g_presupuesto.enEnfriamiento(ahora));
  guardarEstadoRtc(ahora);
}

// La tarea de mantenimiento: corre cada segundo, sin depender de loop() (que
// se bloquea hasta unos 10 s en cada reconexión del MQTT). Toma el candado sin
// esperar mucho: si hay una captura con la cámara, lo hace en la siguiente.
void tareaMantenimiento(void*) {
  while (!g_pararTarea.load()) {
    vTaskDelay(pdMS_TO_TICKS(kPeriodoMantenimientoMs));
    Candado candado(pdMS_TO_TICKS(50));
    if (candado) {
      mantener(millis());
    }
  }
  g_tarea = nullptr;
  vTaskDelete(nullptr);
}

// Saca un cuadro y lo devuelve al driver sin usarlo. Con un solo búfer, el
// cuadro que espera lleno desde la captura anterior es viejo (y el de después
// de un cambio de resolución puede venir a medias); tirándolo, el siguiente es
// de ahora. false si el driver no dio cuadro.
bool descartarCuadro() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (fb == nullptr) return false;
  esp_camera_fb_return(fb);
  return true;
}

// Por qué no hubo foto (o Ninguno si la hay).
enum class Fallo { Ninguno, Temperatura, Enfriamiento, NoLista, Limite, SinCuadro, SinMemoria };

struct ResultadoCaptura {
  Fallo fallo = Fallo::Ninguno;
  uint32_t espera_ms = 0;          // para Retry-After (Limite, Enfriamiento)
  const Perfil* perfil = nullptr;
  bool arranqueEnFrio = false;
  uint32_t borrado_us = 0;         // lo que costó borrar el cuadro del driver
};

// Todo lo que toca la cámara y el estado de la protección, con g_candado
// tomado: temperatura, presupuesto, límite, arranque, resolución, cuadros
// descartados y la copia de la foto en `copia`. La cámara queda libre al
// volver (el cuadro ya se devolvió al driver): el envío va fuera del candado.
ResultadoCaptura capturarBajoCandado(httpd_req_t* req, uint32_t inicioMs, CopiaFoto& copia) {
  ResultadoCaptura r;
  // La hora con el candado ya tomado (no la de inicioMs, anterior a la espera
  // del candado): las cuentas de tiempo de la protección no deben ir hacia atrás.
  const uint32_t ahora = millis();

  // 3. Temperatura: se lee antes de CADA captura. En corte no se toca la cámara.
  leerTemperatura();
  if (!g_termica.capturaPermitida()) {
    r.fallo = Fallo::Temperatura;
    return r;
  }

  // 4. Presupuesto de encendido: dentro del enfriamiento, o agotado ahora mismo
  // (con la cámara apagada también cuenta lo que se usó en la ventana).
  if (g_presupuesto.enEnfriamiento(ahora)) {
    r.fallo = Fallo::Enfriamiento;
    r.espera_ms = g_presupuesto.esperaEnfriamientoMs(ahora);
    return r;
  }
  if (g_presupuesto.excedido(ahora)) {
    apagarCamara("presupuesto de encendido agotado");
    g_presupuesto.iniciarEnfriamiento(ahora);
    g_enfriando.store(true);
    guardarEstadoRtc(ahora);
    Serial.printf("[captura] presupuesto de encendido agotado: enfriamiento de %lu s\n",
                  (unsigned long)(logica_proteccion::kEnfriamientoMs / 1000));
    r.fallo = Fallo::Enfriamiento;
    r.espera_ms = g_presupuesto.esperaEnfriamientoMs(ahora);
    return r;
  }

  // 5. Reposo tras un fallo de la cámara: no se toca.
  if (g_reposo.enReposo(ahora)) {
    r.fallo = Fallo::NoLista;
    return r;
  }

  // 6. Límite de capturas: solo cuentan las peticiones autenticadas que llegan
  // hasta aquí, y solo las que de verdad van a usar la cámara.
  const logica_proteccion::DecisionLimite limite = g_limitador.intentar(ahora);
  if (!limite.permitido) {
    r.fallo = Fallo::Limite;
    r.espera_ms = limite.espera_ms;
    return r;
  }

  // 7. La resolución pedida: la query, o el valor por defecto si falta o no
  // está en la lista blanca. La query se lee con un búfer corto: lo que no
  // quepa no puede ser una resolución válida.
  char query[64];
  const char* queryPtr = nullptr;
  if (httpd_req_get_url_query_len(req) < sizeof query &&
      httpd_req_get_url_query_str(req, query, sizeof query) == ESP_OK) {
    queryPtr = query;
  }
  const Perfil& perfil = logica_proteccion::perfil(logica_proteccion::resolucionDeQuery(queryPtr));
  r.perfil = &perfil;

  // La cámara: se enciende si hace falta y se deja a la resolución pedida.
  r.arranqueEnFrio = !g_camaraLista.load();
  if (r.arranqueEnFrio) {
    const uint32_t inicioInit = millis();
    if (!encenderCamara(perfil)) {
      fallarCamara("init");
      r.fallo = Fallo::NoLista;
      return r;
    }
    Serial.printf("[captura] init de la cámara: %lu ms\n", (unsigned long)(millis() - inicioInit));
  }

  sensor_t* sensor = esp_camera_sensor_get();
  if (sensor == nullptr) {
    fallarCamara("sin sensor");
    r.fallo = Fallo::NoLista;
    return r;
  }
  const bool cambioResolucion = perfil.resolucion != g_resolucionActual;
  if (cambioResolucion) {
    if (sensor->set_framesize(sensor, framesizeDe(perfil.resolucion)) != 0) {
      fallarCamara("no se pudo fijar la resolución");
      r.fallo = Fallo::NoLista;
      return r;
    }
    g_resolucionActual = perfil.resolucion;
  }
  // La calidad va con la resolución. Es un registro del sensor: barato, así que se
  // fija siempre que se arranca o se cambia de resolución.
  if (r.arranqueEnFrio || cambioResolucion) {
    const int errorCalidad = sensor->set_quality(sensor, perfil.calidad_jpeg);
    if (errorCalidad != 0) {
      Serial.printf("[captura] set_quality(%d) devolvió %d: la calidad puede no ser la pedida\n",
                    (int)perfil.calidad_jpeg, errorCalidad);
    }
  }

  // Los cuadros a tirar: del arranque en frío, del cambio de resolución o el
  // viejo de siempre (logica_proteccion::cuadrosADescartar).
  const uint32_t inicioCalentamiento = millis();
  for (int i = logica_proteccion::cuadrosADescartar(r.arranqueEnFrio, cambioResolucion); i > 0; --i) {
    if (!descartarCuadro()) {
      fallarCamara("el driver no dio cuadro");
      r.fallo = Fallo::SinCuadro;
      return r;
    }
  }
  if (r.arranqueEnFrio) {
    // La latencia de la primera captura tras un apagado: init + calentamiento.
    // Se mide aquí para verla en la placa. La marca de pila sirve para decidir
    // si el stack_size de httpd sobra o falta (el init corre en esta tarea).
    Serial.printf(
        "[captura] arranque en frío: calentamiento %lu ms, total desde la petición %lu ms, "
        "pila libre mínima %u B, PSRAM libre %u B\n",
        (unsigned long)(millis() - inicioCalentamiento), (unsigned long)(millis() - inicioMs),
        (unsigned)uxTaskGetStackHighWaterMark(nullptr), (unsigned)ESP.getFreePsram());
  }

  // La foto: se toma el cuadro y se COPIA a PSRAM; el búfer del driver vuelve
  // enseguida y la cámara queda libre mientras se envía la copia.
  camera_fb_t* fb = esp_camera_fb_get();
  if (fb == nullptr) {
    fallarCamara("el driver no dio cuadro");
    r.fallo = Fallo::SinCuadro;
    return r;
  }
  const bool copiada = copia.copiar(fb->buf, fb->len);
  // El cuadro del driver queda con la foto en claro en la PSRAM hasta que otro
  // lo pise: se borra antes de devolverlo. Se mide lo que cuesta.
  const int64_t inicioBorrado = esp_timer_get_time();
  mbedtls_platform_zeroize(fb->buf, fb->len);
  r.borrado_us = static_cast<uint32_t>(esp_timer_get_time() - inicioBorrado);
  esp_camera_fb_return(fb);
  // El plazo de inactividad cuenta desde que termina la captura.
  g_ultimaActividadMs.store(millis());
  if (!copiada) {
    r.fallo = Fallo::SinMemoria;
  }
  return r;
}

// Envía la foto en trozos, con un plazo total. Si el cliente no la lee a
// tiempo se cierra la sesión (no se espera más). Devuelve ESP_OK aunque se
// corte (el cierre ya está pedido) y ESP_FAIL si el envío falló.
esp_err_t enviarConPlazo(httpd_req_t* req, const uint8_t* datos, size_t largo, bool* completo) {
  *completo = false;
  const uint32_t inicio = millis();
  size_t enviado = 0;
  while (enviado < largo) {
    if (logica_proteccion::intervaloCumplido(millis(), inicio, kPlazoEnvioMs)) {
      Serial.printf("[captura] el cliente no leyó la foto a tiempo (%lu de %lu B en %lu ms): se cierra la sesión\n",
                    (unsigned long)enviado, (unsigned long)largo, (unsigned long)kPlazoEnvioMs);
      httpd_sess_trigger_close(g_servidor, httpd_req_to_sockfd(req));
      return ESP_OK;
    }
    const size_t trozo = (largo - enviado) < kTrozoEnvioBytes ? (largo - enviado) : kTrozoEnvioBytes;
    const esp_err_t e = httpd_resp_send_chunk(req, reinterpret_cast<const char*>(datos + enviado), trozo);
    if (e != ESP_OK) {
      Serial.printf("[captura] no se pudo servir la foto: %d\n", (int)e);
      return ESP_FAIL;
    }
    enviado += trozo;
  }
  const esp_err_t fin = httpd_resp_send_chunk(req, nullptr, 0);  // cierra el cuerpo troceado
  if (fin != ESP_OK) {
    Serial.printf("[captura] no se pudo cerrar la respuesta: %d\n", (int)fin);
    return ESP_FAIL;
  }
  *completo = true;
  return ESP_OK;
}

// Abre el NVS (o lo reintenta, como mucho una vez por minuto) y, si hay un ts
// guardado, sube el piso del validador a pisoDeArranque(guardado). Solo la
// tarea de httpd lo llama (y iniciar() antes de que arranque).
void abrirNvs(bool borrarPiso) {
  if (g_prefsAbierto) return;
  const uint32_t ahora = millis();
  if (g_reposoNvs.enReposo(ahora)) return;
  g_prefsAbierto = g_prefs.begin(kNvsEspacio, false);
  if (!g_prefsAbierto) {
    g_reposoNvs.fallo(ahora);
    g_nvsBien.store(false);
    registrarLimitado(g_logNvs, "no se pudo abrir el NVS: sin protección de reenvío tras un reinicio");
    return;
  }
  g_nvsBien.store(true);
  if (borrarPiso) {
    g_prefs.remove(kNvsClaveTs);
    g_persistencia.arrancar(false, 0, static_cast<uint64_t>(esp_timer_get_time() / 1000));
    Serial.println("[captura] *** PISO DE TS BORRADO de la flash (BOOT o CAPTURA_BORRAR_PISO_TS) ***");
    return;
  }
  if (g_prefs.isKey(kNvsClaveTs)) {
    const int64_t guardado = g_prefs.getLong64(kNvsClaveTs, 0);
    g_persistencia.arrancar(true, guardado, static_cast<uint64_t>(esp_timer_get_time() / 1000));
    if (g_validador != nullptr) {
      g_validador->establecerPiso(logica_camara::pisoDeArranque(guardado));
    }
    Serial.printf("[captura] ts guardado en la flash: %lld (piso: %lld)\n", (long long)guardado,
                  (long long)logica_camara::pisoDeArranque(guardado));
  } else {
    g_persistencia.arrancar(false, 0, static_cast<uint64_t>(esp_timer_get_time() / 1000));
    Serial.println("[captura] sin ts en la flash (primer arranque o piso borrado)");
  }
}

// Guarda el ts aceptado en la flash según PersistenciaTs: como mucho 1 escritura
// por minuto de tiempo de la placa, solo si avanzó 60 s o más sobre lo guardado, y
// con el tope por arranque (24 h más el tiempo encendida desde el arranque), de
// modo que un ts absurdo no pueda dejar el piso fuera de alcance. Tras un fallo
// de escritura no se reintenta hasta pasado un minuto. Va antes de tocar la
// cámara: así la flash no se escribe con el sensor en marcha.
void guardarPisoTs(int64_t tsAceptado, uint64_t ahoraMs) {
  if (!g_prefsAbierto) {
    abrirNvs(false);  // como mucho 1 intento por minuto
    if (!g_prefsAbierto) return;
  }
  if (!g_persistencia.debeEscribir(ahoraMs, tsAceptado)) return;
  if (g_reposoNvs.enReposo(millis())) return;
  const int64_t valor = g_persistencia.valor(ahoraMs, tsAceptado);
  if (g_prefs.putLong64(kNvsClaveTs, valor) == sizeof(int64_t)) {
    g_persistencia.registrarEscritura(ahoraMs, valor);
    g_nvsBien.store(true);
  } else {
    g_reposoNvs.fallo(millis());
    g_nvsBien.store(false);
    registrarLimitado(g_logNvs, "no se pudo guardar el piso de ts en la flash");
  }
}

// El manejador de GET /capture. Ver el orden de las comprobaciones arriba.
//
// httpd da el uri sin la query y la query aparte: la firma se calcula sobre
// el camino sin la query (ver logica_camara). La resolución se lee de la
// query, pero solo de la lista blanca (logica_proteccion).
esp_err_t manejadorCaptura(httpd_req_t* req) {
  if (g_validador == nullptr || g_candado == nullptr) {
    responderError(req, 503, "camara no lista");
    return ESP_OK;
  }

  // 1. Firma y anti-replay: no toca la cámara ni el límite.
  char ts[64];
  char nonce[64];
  char firma[128];
  const bool hayTs = leerCabecera(req, logica_camara::kCabeceraTs, ts, sizeof ts);
  const bool hayNonce = leerCabecera(req, logica_camara::kCabeceraNonce, nonce, sizeof nonce);
  const bool hayFirma = leerCabecera(req, logica_camara::kCabeceraHmac, firma, sizeof firma);

  logica_camara::Peticion peticion;
  peticion.ts = hayTs ? ts : nullptr;
  peticion.nonce = hayNonce ? nonce : nullptr;
  peticion.hmac = hayFirma ? firma : nullptr;

  int64_t tsAceptado = 0;
  uint64_t nonceAceptado = 0;
  // El tiempo de la placa en un contador de 64 bits (no da la vuelta en 49 días
  // como millis()): el validador rechaza un ts que se adelante más de lo que
  // permite el tiempo transcurrido desde el último aceptado.
  const uint64_t ahoraMs = static_cast<uint64_t>(esp_timer_get_time() / 1000);
  const logica_camara::Resultado resultado =
      g_validador->validar(peticion, ahoraMs, &tsAceptado, &nonceAceptado);
  if (resultado != logica_camara::Resultado::Aceptada) {
    const int codigo = logica_camara::codigoHttp(resultado);
    char motivo[96];
    snprintf(motivo, sizeof motivo, "%s", logica_camara::aTexto(resultado));
    responderError(req, codigo, motivo, true);
    return ESP_OK;
  }

  guardarPisoTs(tsAceptado, ahoraMs);

  const uint32_t inicioMs = millis();
  CopiaFoto copia;
  ResultadoCaptura captura;
  {
    // 2. Un candado para la cámara (y para no pisar el apagado de la tarea de
    // mantenimiento). Se suelta antes de enviar la foto.
    Candado candado(kEsperaCandado);
    if (!candado) {
      responderError(req, 503, "ocupada");
      return ESP_OK;
    }
    captura = capturarBajoCandado(req, inicioMs, copia);
  }

  switch (captura.fallo) {
    case Fallo::Ninguno:
      break;
    case Fallo::Temperatura:
      responderError(req, 503, "temperatura");
      return ESP_OK;
    case Fallo::Enfriamiento:
      responderError(req, 503, "enfriamiento", false, captura.espera_ms);
      return ESP_OK;
    case Fallo::NoLista:
      responderError(req, 503, "camara no lista");
      return ESP_OK;
    case Fallo::Limite:
      responderError(req, 429, "demasiadas capturas", false, captura.espera_ms);
      return ESP_OK;
    case Fallo::SinCuadro:
      responderError(req, 503, "no hay cuadro");
      return ESP_OK;
    case Fallo::SinMemoria:
      responderError(req, 503, "sin memoria");
      return ESP_OK;
  }

  // 8. Cifrado de la respuesta: AES-256-GCM en sitio sobre la copia (la foto no
  // viaja en claro), con un IV nuevo en cada respuesta y el AAD "RESP\n<ts>\n<nonce>".
  // Se hace fuera del candado y se mide el tiempo para verlo en la placa.
  char ivHex[logica_camara::kLongitudIvHex + 1];
  const uint32_t inicioCifrado = millis();
  if (!cifrarFoto(copia, tsAceptado, nonceAceptado, ivHex)) {
    responderError(req, 503, "no se pudo cifrar");
    return ESP_OK;
  }
  const uint32_t msCifrado = millis() - inicioCifrado;

  httpd_resp_set_type(req, "application/octet-stream");
  httpd_resp_set_hdr(req, logica_camara::kCabeceraIv, ivHex);
  const uint32_t inicioEnvio = millis();
  bool completo = false;
  const esp_err_t enviado = enviarConPlazo(req, copia.datos(), copia.largoConEtiqueta(), &completo);
  if (completo) {
    Serial.printf(
        "[captura] 200 %s %u B (+16 de etiqueta) en %lu ms (%s; cifrado %lu ms, envío %lu ms, "
        "borrado del cuadro %lu us)\n",
        captura.perfil->nombre, (unsigned)copia.largo(), (unsigned long)(millis() - inicioMs),
        captura.arranqueEnFrio ? "arranque en frío" : "cámara encendida", (unsigned long)msCifrado,
        (unsigned long)(millis() - inicioEnvio), (unsigned long)captura.borrado_us);
  }
  return enviado;
}

// open_fn de httpd: se llama al aceptar cada conexión, antes de leer nada. Con
// ipPermitida puesta, solo deja pasar a ese origen. Es una barrera para quien
// llega de otra IP (un invitado de la LAN), no una autenticación: quien
// suplante la IP del origen (ARP spoofing en la misma red) la salta. Sube el
// listón, no cierra el agujero; lo que autentica es la firma. Un cliente IPv4 en
// un socket de doble pila llega como IPv6 mapeado (::ffff:a.b.c.d).
esp_err_t alAbrirSocket(httpd_handle_t, int sockfd) {
  if (!g_hayOrigen) return ESP_OK;
  sockaddr_storage origen;
  socklen_t largo = sizeof origen;
  uint8_t ip[4] = {0, 0, 0, 0};
  bool hayIp = false;
  if (getpeername(sockfd, reinterpret_cast<sockaddr*>(&origen), &largo) == 0) {
    if (origen.ss_family == AF_INET) {
      memcpy(ip, &reinterpret_cast<sockaddr_in*>(&origen)->sin_addr, 4);
      hayIp = true;
    } else if (origen.ss_family == AF_INET6) {
      hayIp = logica_proteccion::ipv4DeMapeada(
          reinterpret_cast<sockaddr_in6*>(&origen)->sin6_addr.un.u8_addr, ip);
    }
  }
  if (hayIp && memcmp(ip, g_origen, 4) == 0) return ESP_OK;
  registrarLimitado(g_logOrigen, "conexión rechazada: origen no permitido");
  return ESP_FAIL;  // httpd cierra el socket
}

// Lo que iniciar() deshace si algo falla después de arrancar el servidor.
void abortarInicio() {
  if (g_servidor != nullptr) {
    httpd_stop(g_servidor);
    g_servidor = nullptr;
  }
  delete g_validador;
  g_validador = nullptr;
  if (g_prefsAbierto) {
    g_prefs.end();
    g_prefsAbierto = false;
  }
  g_nvsBien.store(false);
  mbedtls_platform_zeroize(g_claveRespuesta, sizeof g_claveRespuesta);
}

}  // namespace

bool iniciar(const char* secreto, const char* ipPermitida, bool borrarPisoTs) {
  if (g_servidor != nullptr) {
    return true;  // ya está en marcha
  }
  // La fuerza del secreto: la misma regla que el borde. El valor no se imprime.
  const logica_proteccion::MotivoSecreto motivoSecreto = logica_proteccion::evaluarSecreto(secreto);
  if (motivoSecreto != logica_proteccion::MotivoSecreto::Valido) {
    Serial.printf("[captura] %s\n", logica_proteccion::aTexto(motivoSecreto));
    return false;
  }
  g_secreto = secreto;

  // El origen permitido es OBLIGATORIO: sin él, cualquiera en la LAN podría
  // abrir conexiones al puerto 80. Una IP vacía o mal escrita impide el arranque.
  g_hayOrigen = false;
  if (ipPermitida == nullptr || !logica_proteccion::parsearIpv4(ipPermitida, g_origen)) {
    Serial.println("[captura] RELAY_IP de secrets.h falta o está mal escrita: la cámara no arranca");
    return false;
  }
  g_hayOrigen = true;
  Serial.printf("[captura] /capture solo acepta conexiones de %u.%u.%u.%u\n", (unsigned)g_origen[0],
                (unsigned)g_origen[1], (unsigned)g_origen[2], (unsigned)g_origen[3]);

  // El cifrado de la respuesta: K se deriva una vez (vive solo en RAM) y el
  // autodiagnóstico comprueba mbedTLS y el hardware de AES contra los vectores.
  // Si falla, no se sirve nada: una foto sin cifrar bien no sale de la placa.
  logica_camara::derivarClaveRespuesta(g_secreto, strlen(g_secreto), g_claveRespuesta);
  if (!autodiagnosticoGcm()) {
    mbedtls_platform_zeroize(g_claveRespuesta, sizeof g_claveRespuesta);
    Serial.println("[captura] el cifrado no pasó el autodiagnóstico: la cámara no arranca");
    return false;
  }

  if (g_candado == nullptr) {
    g_candado = xSemaphoreCreateMutex();
    if (g_candado == nullptr) {
      Serial.println("[captura] no se pudo crear el candado de la cámara");
      mbedtls_platform_zeroize(g_claveRespuesta, sizeof g_claveRespuesta);
      return false;
    }
  }

  // La cámara NO se enciende aquí: la enciende la primera petición firmada
  // (manejadorCaptura) y la apaga la tarea de mantenimiento.
  g_camaraLista.store(false);
  g_limitador.reiniciar();
  g_presupuesto.reiniciar();
  g_reposo.reiniciar();
  g_reposoNvs.reiniciar();
  g_termica = logica_proteccion::GuardaTermica();

  // Lo que sobrevivió a un reinicio por software (pánico, WDT, esp_restart): el
  // enfriamiento que faltaba y el corte térmico. Tras quitar la corriente la RAM
  // del RTC tiene basura y no valida: se arranca limpio.
  bool corteRtc = false;
  const uint32_t ahoraInicio = millis();
  if (logica_proteccion::estadoRtcValido(g_estadoRtc)) {
    corteRtc = g_estadoRtc.corte_termico != 0;
    g_presupuesto.restaurarEnfriamiento(ahoraInicio, g_estadoRtc.enfriamiento_restante_ms);
    // El tiempo encendida ya gastado en la ventana, como un tramo cerrado que
    // termina ahora: un pánico no pone la cuenta del presupuesto a cero.
    g_presupuesto.restaurarEncendido(ahoraInicio, g_estadoRtc.encendido_usado_ms);
    if (corteRtc || g_estadoRtc.enfriamiento_restante_ms > 0 || g_estadoRtc.encendido_usado_ms > 0) {
      Serial.printf(
          "[captura] estado de antes del reinicio: corte térmico %s, enfriamiento %lu s por cumplir, "
          "%lu s encendida en la ventana del presupuesto\n",
          corteRtc ? "SÍ" : "no", (unsigned long)(g_estadoRtc.enfriamiento_restante_ms / 1000),
          (unsigned long)(g_estadoRtc.encendido_usado_ms / 1000));
    }
  } else {
    g_estadoRtc.enfriamiento_restante_ms = 0;
    g_estadoRtc.corte_termico = 0;
    g_estadoRtc.encendido_usado_ms = 0;
    logica_proteccion::sellarEstadoRtc(g_estadoRtc);
  }
  g_enfriando.store(g_presupuesto.enEnfriamiento(ahoraInicio));

  // Primera lectura de temperatura, antes de que haya servidor: así hay dato
  // para el latido de salud desde el arranque. Si el chip ya está a 65 °C o más
  // (o había corte anotado antes del reinicio), se arranca en Corte.
  leerTemperatura(true, corteRtc);

  // El validador de la firma: el secreto vive en secrets.h, que es estático.
  g_validador = new logica_camara::Validador(g_secreto, strlen(g_secreto));

  // El piso de ts guardado en la flash: tras un reinicio la RAM olvidó los
  // nonces, así que toda petición con ts menor o igual al guardado MÁS 60 s se
  // rechaza (el guardado va hasta 60 s por detrás del último aceptado: ver
  // pisoDeArranque). Con eso el reenvío de una petición vieja tras un reinicio
  // queda cerrado; el costo es un 403 para las legítimas de ese minuto. Sin NVS
  // el firmware sigue pero sin esa defensa: se dice en el registro y en el
  // latido ("nvs":false).
  g_persistencia.arrancar(false, 0, static_cast<uint64_t>(esp_timer_get_time() / 1000));
  g_prefsAbierto = false;
  abrirNvs(borrarPisoTs);

  // El servidor HTTP. httpd_start crea su propia tarea de FreeRTOS: el
  // manejador de /capture se ejecuta ahí, no en el lazo de Arduino, así que el
  // MQTT sigue respondiendo mientras se saca y se sirve la foto.
  httpd_config_t configHttp = HTTPD_DEFAULT_CONFIG();
  configHttp.server_port = kPuertoHttp;
  configHttp.max_uri_handlers = 4;
  // El manejador arranca la cámara (esp_camera_init: SCCB, LEDC, DMA) y tiene
  // los locals de la petición: 12 KB con margen. El mínimo real se imprime en
  // el registro del arranque en frío ("pila libre mínima").
  configHttp.stack_size = 12288;
  // Contra conexiones que se quedan colgadas (slowloris): pocas conexiones
  // abiertas, 1 s de espera al leer y al escribir, y si se llena, httpd cierra
  // la que lleva más tiempo sin actividad. Con RELAY_IP, además, solo llega el
  // origen permitido (alAbrirSocket).
  configHttp.max_open_sockets = 3;
  configHttp.recv_wait_timeout = 1;
  configHttp.send_wait_timeout = 1;
  configHttp.lru_purge_enable = true;
  configHttp.open_fn = alAbrirSocket;

  const esp_err_t errorServidor = httpd_start(&g_servidor, &configHttp);
  if (errorServidor != ESP_OK) {
    Serial.printf("[captura] el servidor HTTP no arrancó: 0x%x\n", (int)errorServidor);
    g_servidor = nullptr;
    abortarInicio();
    return false;
  }

  httpd_uri_t uri;
  memset(&uri, 0, sizeof uri);
  uri.uri = "/capture";
  uri.method = HTTP_GET;
  uri.handler = manejadorCaptura;
  uri.user_ctx = nullptr;

  const esp_err_t errorUri = httpd_register_uri_handler(g_servidor, &uri);
  if (errorUri != ESP_OK) {
    Serial.printf("[captura] no se pudo registrar /capture: 0x%x\n", (int)errorUri);
    abortarInicio();
    return false;
  }

  // La tarea de mantenimiento, aparte de loop(): el apagado por inactividad,
  // por presupuesto y por calor no puede esperar a una reconexión del MQTT.
  g_ultimaActividadMs.store(millis());
  g_pararTarea.store(false);
  if (xTaskCreate(tareaMantenimiento, "captura_mant", 5120, nullptr, 1, &g_tarea) != pdPASS) {
    Serial.println("[captura] no se pudo crear la tarea de mantenimiento");
    g_tarea = nullptr;
    abortarInicio();
    return false;
  }

  Serial.println("[captura] listo: GET /capture (cámara apagada hasta la primera petición)");
  return true;
}

float temperatura() {
  return g_temperatura.load();
}

bool camaraEncendida() {
  return g_camaraLista.load();
}

bool enCorte() {
  return g_corteTermico.load() || g_enfriando.load();
}

bool nvsBien() {
  return g_nvsBien.load();
}

void detener() {
  // Primero el servidor: deja de aceptar peticiones y el manejador no vuelve a
  // tocar la cámara. Luego la tarea de mantenimiento.
  if (g_servidor != nullptr) {
    httpd_stop(g_servidor);
    g_servidor = nullptr;
  }
  if (g_tarea != nullptr) {
    g_pararTarea.store(true);
    for (int i = 0; i < 30 && g_tarea != nullptr; ++i) {
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
  if (g_candado != nullptr) {
    Candado candado(portMAX_DELAY);
    apagarCamara("detener()");
  }
  delete g_validador;
  g_validador = nullptr;
  g_secreto = nullptr;
  if (g_prefsAbierto) {
    g_prefs.end();
    g_prefsAbierto = false;
  }
  g_nvsBien.store(false);
  mbedtls_platform_zeroize(g_claveRespuesta, sizeof g_claveRespuesta);  // la clave no sobrevive al apagado
}

bool activa() {
  return g_servidor != nullptr;
}

}  // namespace captura
