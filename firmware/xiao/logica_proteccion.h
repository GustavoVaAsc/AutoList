// Lógica pura de la protección de la cámara de la XIAO: resolución permitida,
// límite de capturas, guarda térmica, apagado por inactividad y latido de salud.
// C++ puro, sin dependencias de Arduino: así se prueba en el host con g++
// (pruebas/test_logica_proteccion.cpp). captura.cpp y xiao.ino hacen la E/S: leen
// el sensor de temperatura, encienden y apagan la cámara y publican.
//
// Por qué existe: la placa anterior se quemó con captura continua a alta
// resolución. La cámara solo está encendida mientras se usa, a una resolución
// acotada, con un límite de ritmo y con corte por temperatura.
//
// Todos los tiempos son los de millis() (uint32_t, con desbordamiento cada ~49
// días): las comparaciones son restas sin signo, igual que en logica_luz y
// logica_pir. Un instante que lleve más de ~49 días sin consultarse puede
// confundirse con uno reciente; aquí solo costaría un 429 de unos segundos.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace logica_proteccion {

// --- Resolución ---------------------------------------------------------------
// Lista blanca: res= acepta exactamente estos cuatro nombres (mayúsculas, sin
// espacios). Nada por encima de XGA se puede producir: no hay forma de llegar
// a otro valor desde la petición.
enum class Resolucion : uint8_t { Hvga, Vga, Svga, Xga };

constexpr Resolucion kResolucionPorDefecto = Resolucion::Svga;
// La mayor permitida: con ella se dimensiona el búfer del driver (captura.h).
constexpr Resolucion kResolucionMaxima = Resolucion::Xga;

struct Perfil {
  Resolucion resolucion;
  const char* nombre;     // el valor de res= que la pide
  uint16_t ancho;
  uint16_t alto;
  uint8_t calidad_jpeg;   // 0 = máxima calidad, 63 = mínima (escala del driver)
};

// El perfil de una resolución. Calidades:
//   HVGA 20: 480x320 son solo 150 mil píxeles; con calidad 20 la foto pesa
//            unos 10 KB y el sensor trabaja menos. Es la vista de bajo
//            costo: no sirve para reconocer, sí para saber que hay alguien.
//   VGA 12 y SVGA 12: el término medio habitual del driver (12 en el ejemplo
//            CameraWebServer). SVGA (800x600) es el valor por defecto.
//   XGA 12: 1024x768 con la misma calidad: unos 60-150 KB típicos, muy por
//            debajo del búfer de 512 KB (captura.h) y del corte de 2 MB de la
//            visión. No se sube la calidad (número menor) porque a esta
//            resolución el codificador del sensor es el que más trabaja y la
//            foto se hace más pesada sin más detalle útil para el recorte
//            que hace la NPU.
const Perfil& perfil(Resolucion resolucion);

// El valor de res= ya extraído ("XGA"). Coincidencia exacta; cualquier otra
// cosa (nulo, vacío, minúsculas, "UXGA", "QXGA", basura) cae en
// kResolucionPorDefecto.
Resolucion parsearResolucion(const char* valor);

// Busca res= en la query de la petición ("res=XGA&x=1", sin el "?"). Gana la
// primera aparición del parámetro; el valor termina en "&" o "#". Una query
// nula, sin res o con un valor desconocido da kResolucionPorDefecto. No decodifica
// porcentajes: "res=%58GA" cae en el valor por defecto.
Resolucion resolucionDeQuery(const char* query);

// --- Límite de capturas ----------------------------------------------------------
// Como mucho 4 capturas en cualquier ventana deslizante de 2 s (una ráfaga de
// la visión son 4 fotos). Tras llenar la ventana, además, una pausa de 1 s
// después de que la ventana se vacíe: el siguiente grupo de 4 no puede empezar
// antes de 3 s del primero de la ráfaga anterior, es decir, el ritmo
// sostenido baja de 2 capturas/s a unas 1.3/s y el sensor descansa entre
// ráfagas. 1 s es lo bastante corto para que la visión no note la pausa entre
// dos pases de lista y lo bastante largo para que el límite no sea solo la
// ventana.
constexpr size_t kCapturasPorVentana = 4;
constexpr uint32_t kVentanaCapturasMs = 2000;
constexpr uint32_t kPausaTrasRafagaMs = 1000;

struct DecisionLimite {
  bool permitido;
  uint32_t espera_ms;  // si no se permite: cuánto falta para la próxima (0 si se permite)
};

// Registra y decide. Solo las capturas permitidas se anotan: un 429 no
// alarga la espera, así que insistir no sirve de nada. El llamador solo debe
// consultarlo con peticiones que ya pasaron la firma (captura.cpp).
class LimitadorCapturas {
 public:
  explicit LimitadorCapturas(uint32_t ventana_ms = kVentanaCapturasMs,
                             uint32_t pausa_ms = kPausaTrasRafagaMs)
      : ventana_ms_(ventana_ms), pausa_ms_(pausa_ms) {}

  // ¿Se puede capturar ahora? Si sí, anota la captura.
  DecisionLimite intentar(uint32_t ahora_ms);

  // Olvida todo (solo para las pruebas).
  void reiniciar();

 private:
  uint32_t ventana_ms_;
  uint32_t pausa_ms_;
  uint32_t instantes_[kCapturasPorVentana] = {0};  // los más viejos primero
  size_t cuenta_ = 0;
  bool bloqueado_ = false;
  uint32_t bloqueo_inicio_ = 0;
  uint32_t bloqueo_duracion_ = 0;
};

// --- Guarda térmica ----------------------------------------------------------------
// Sensor interno del ESP32-S3 (temperatureRead()): mide el chip, no el aire.
// Una lectura fuera de este rango (o NaN) no es creíble y se trata como no válida.
constexpr float kTempMinimaValidaC = -40.0f;
constexpr float kTempMaximaValidaC = 150.0f;
constexpr float kTempAvisoC = 65.0f;  // desde aquí, aviso por el serie
constexpr float kTempCorteC = 75.0f;  // desde aquí, se corta la cámara
// Cada cuánto se lee aunque nadie pida una foto (además de antes de cada una).
constexpr uint32_t kIntervaloTemperaturaMs = 30000;

enum class EstadoTermico : uint8_t { Normal, Aviso, Corte };

// Qué pasó con la última lectura (para el registro y para apagar la cámara).
enum class Transicion : uint8_t { Ninguna, EntraAviso, SaleAviso, EntraCorte, SaleCorte };

// Máquina de estados con histéresis: se corta a >= 75 °C y no se vuelve a
// permitir hasta una lectura ESTRICTAMENTE por debajo de 65 °C. Entre 65 y 75
// estando en corte se sigue cortado.
//
// Falla cerrado: una lectura no válida (NaN o fuera de rango) no cambia
// el estado, pero deja capturaPermitida() en false hasta que haya una válida.
// Antes de la primera lectura tampoco se permite capturar.
class GuardaTermica {
 public:
  Transicion actualizar(float temperatura_c);

  // La primera lectura tras un arranque. Entra en Corte (y lo registra) si el
  // chip ya está a kTempAvisoC o más: tras un reinicio no hay historia que
  // diga que el calor viene bajando. `corte_previo` es el corte que quedó
  // anotado antes del reinicio (RAM del RTC, ver EstadoSeguridadRtc): se parte
  // de Corte y solo una lectura por debajo de kTempAvisoC lo levanta.
  Transicion primeraLectura(float temperatura_c, bool corte_previo);

  EstadoTermico estado() const { return estado_; }
  bool lecturaValida() const { return lectura_valida_; }
  bool capturaPermitida() const { return lectura_valida_ && estado_ != EstadoTermico::Corte; }

 private:
  EstadoTermico estado_ = EstadoTermico::Normal;
  bool lectura_valida_ = false;
};

// --- Planificación -------------------------------------------------------------------
// Ya pasaron `intervalo_ms` desde `ultimo_ms` (resta sin signo, correcta con el
// desbordamiento de millis()). Sirve para la lectura térmica y para el latido.
bool intervaloCumplido(uint32_t ahora_ms, uint32_t ultimo_ms, uint32_t intervalo_ms);

// --- Cámara apagada en reposo -----------------------------------------------------------
// Sin capturas durante este tiempo, la cámara se apaga (esp_camera_deinit). La
// siguiente petición firmada la vuelve a arrancar (cuesta el arranque más los
// cuadros de calentamiento; se mide en la placa: captura.cpp lo registra). 15 s
// cubren las cuatro fotos de una ráfaga y pases de lista seguidos sin reiniciar
// la cámara, y la dejan apagada el resto del tiempo. Se puede cambiar aquí.
constexpr uint32_t kInactividadCamaraMs = 15000;

// ¿Hay que apagar la cámara? Solo si está encendida y la última actividad (el
// arranque o el fin de la última captura) fue hace `inactividad_ms` o más.
bool debeApagarPorInactividad(bool camara_encendida, uint32_t ahora_ms, uint32_t ultima_actividad_ms,
                              uint32_t inactividad_ms = kInactividadCamaraMs);

// Cuántos cuadros hay que tirar antes de servir la foto:
//   - arranque en frío: la exposición y el balance de blanco aún no se asientan;
//   - cambio de resolución: el sensor reconfigura y el primer cuadro puede salir
//     a medias o con la resolución anterior;
//   - en caliente: con un solo búfer (fb_count = 1) el driver deja un cuadro
//     lleno esperando desde el fin de la captura anterior; ese cuadro es viejo
//     (puede tener hasta kInactividadCamaraMs de antigüedad) y no se sirve.
constexpr int kCuadrosArranqueEnFrio = 3;
constexpr int kCuadrosCambioResolucion = 2;
constexpr int kCuadrosEnCaliente = 1;
int cuadrosADescartar(bool arranque_en_frio, bool cambio_resolucion);

// --- Presupuesto de encendido de la cámara --------------------------------------------------
// Como mucho kPresupuestoEncendidoMs (300 s) con la cámara encendida en
// cualquier ventana deslizante de kVentanaPresupuestoMs (10 min): un 50 % de
// ciclo de trabajo. Pasado eso, la cámara se apaga, las capturas dan 503
// "enfriamiento" y se rechazan durante kEnfriamientoMs (2 min fijos, desde el
// momento en que se detecta el exceso).
//
// Por qué un presupuesto además de la guarda térmica: con XCLK en marcha el
// sensor produce cuadros, y calor, lo lea alguien o no, y la guarda térmica
// mide el die del ESP32-S3, no el módulo de la cámara. El tiempo encendida es
// lo único que se puede acotar con seguridad sin un sensor en el módulo.
// Los tres valores se pueden cambiar aquí (o por el constructor, en las pruebas).
constexpr uint32_t kPresupuestoEncendidoMs = 300000;
constexpr uint32_t kVentanaPresupuestoMs = 600000;
constexpr uint32_t kEnfriamientoMs = 120000;
// Intervalos encendida/apagada que se recuerdan. Un ciclo dura como mínimo el
// plazo de inactividad (15 s), así que en 10 min caben unos 40: 64 sobra. Si
// se llenara, se olvida el más viejo (infravalora el exceso, pero solo con más
// de 64 ciclos en la ventana, que el plazo de inactividad impide).
constexpr size_t kIntervalosPresupuesto = 64;

class PresupuestoCamara {
 public:
  explicit PresupuestoCamara(uint32_t presupuesto_ms = kPresupuestoEncendidoMs,
                             uint32_t ventana_ms = kVentanaPresupuestoMs,
                             uint32_t enfriamiento_ms = kEnfriamientoMs)
      : presupuesto_ms_(presupuesto_ms), ventana_ms_(ventana_ms), enfriamiento_ms_(enfriamiento_ms) {}

  // La cámara acaba de encenderse / de apagarse (por el motivo que sea). Si ya
  // estaba en ese estado, no hace nada.
  void encendida(uint32_t ahora_ms);
  void apagada(uint32_t ahora_ms);

  // Cuánto estuvo encendida en la ventana que termina en `ahora_ms`, contando
  // el tramo que siga abierto. Olvida de paso lo que ya salió de la ventana.
  uint32_t encendidoEnVentanaMs(uint32_t ahora_ms);

  // ¿Se agotó el presupuesto? (encendido en la ventana >= presupuesto)
  bool excedido(uint32_t ahora_ms);

  // El enfriamiento fijo: se inicia al detectar el exceso.
  void iniciarEnfriamiento(uint32_t ahora_ms);
  // Retoma un enfriamiento que quedó a medias antes de un reinicio: faltan
  // `restante_ms` (como mucho kEnfriamientoMs). 0 no hace nada.
  void restaurarEnfriamiento(uint32_t ahora_ms, uint32_t restante_ms);
  // Retoma el tiempo encendida que ya había gastado antes de un reinicio, como
  // un tramo cerrado de `usado_ms` (como mucho la ventana) que termina ahora.
  void restaurarEncendido(uint32_t ahora_ms, uint32_t usado_ms);
  bool enEnfriamiento(uint32_t ahora_ms) const;
  uint32_t esperaEnfriamientoMs(uint32_t ahora_ms) const;

  void reiniciar();

 private:
  struct Intervalo {
    uint32_t inicio_ms;
    uint32_t fin_ms;
  };
  uint32_t presupuesto_ms_;
  uint32_t ventana_ms_;
  uint32_t enfriamiento_ms_;
  Intervalo cerrados_[kIntervalosPresupuesto] = {};  // en orden, el más viejo primero
  size_t cuenta_ = 0;
  bool encendida_ = false;
  uint32_t inicio_actual_ms_ = 0;
  bool enfriando_ = false;
  uint32_t enfriamiento_inicio_ms_ = 0;
  uint32_t enfriamiento_duracion_ms_ = 0;  // la de este enfriamiento (menor si se restauró)

  void podar(uint32_t ahora_ms);
  void agregarCerrado(uint32_t inicio_ms, uint32_t fin_ms);
};

// --- Estado de seguridad que sobrevive a un reinicio ---------------------------------------------
// El enfriamiento que falta y el corte térmico se anotan en variables de la RAM
// del RTC sin inicializar (RTC_NOINIT_ATTR en captura.cpp): sobreviven a un
// pánico, al perro guardián (WDT) y a esp_restart(), así que un reinicio no
// sirve para saltarse el enfriamiento ni el corte por calor. NO sobreviven a
// quitar la corriente (la RAM del RTC queda con basura): por eso lleva una
// palabra mágica y una verificación, y lo que no valida se ignora. Con el
// tiempo encendida ya gastado del presupuesto: tras un reinicio se reaplica
// como un tramo cerrado que termina en el arranque (conservador: envejece más
// despacio que el real), así que un pánico no reinicia la cuenta a cero.
constexpr uint32_t kMagiaEstadoRtc = 0x53454731;  // "SEG1"
struct EstadoSeguridadRtc {
  uint32_t magia;
  uint32_t enfriamiento_restante_ms;
  uint32_t corte_termico;  // 0 o 1
  uint32_t encendido_usado_ms;  // tiempo encendida dentro de la ventana del presupuesto
  uint32_t verificacion;
};
// Pone la magia y la verificación sobre los campos actuales.
void sellarEstadoRtc(EstadoSeguridadRtc& estado);
// ¿Es un estado escrito por sellarEstadoRtc (y no basura)?
bool estadoRtcValido(const EstadoSeguridadRtc& estado);

// --- Reposo tras un fallo de la cámara -----------------------------------------------------
// Tras un esp_camera_init fallido o un "no hay cuadro", durante este tiempo las
// peticiones reciben 503 "camara no lista" sin tocar la cámara: un reintento en
// bucle de un hardware que falla es justo lo que no se quiere.
constexpr uint32_t kReposoTrasFalloMs = 10000;

class ReposoTrasFallo {
 public:
  explicit ReposoTrasFallo(uint32_t reposo_ms = kReposoTrasFalloMs) : reposo_ms_(reposo_ms) {}
  void fallo(uint32_t ahora_ms) {
    activo_ = true;
    inicio_ms_ = ahora_ms;
  }
  bool enReposo(uint32_t ahora_ms) const {
    return activo_ && static_cast<uint32_t>(ahora_ms - inicio_ms_) < reposo_ms_;
  }
  void reiniciar() { activo_ = false; }

 private:
  uint32_t reposo_ms_;
  bool activo_ = false;
  uint32_t inicio_ms_ = 0;
};

// --- Límite de mensajes del registro -----------------------------------------------------------
// Las líneas del serie por cada 401/403 (o conexión rechazada) se limitan a una
// por segundo: sin firma, cualquiera en la LAN puede generar miles por minuto.
constexpr uint32_t kIntervaloLogMs = 1000;

class LimitadorMensajes {
 public:
  explicit LimitadorMensajes(uint32_t intervalo_ms = kIntervaloLogMs) : intervalo_ms_(intervalo_ms) {}
  // true si este mensaje se puede imprimir. En ese caso, `suprimidos` recibe
  // cuántos se callaron desde el último impreso (y se pone a cero).
  bool intentar(uint32_t ahora_ms, uint32_t* suprimidos);

 private:
  uint32_t intervalo_ms_;
  bool hubo_ = false;
  uint32_t ultimo_ms_ = 0;
  uint32_t suprimidos_ = 0;
};

// --- Fuerza del secreto de la firma ----------------------------------------------------------------
// La misma regla que el borde (vision/src/process_config.cpp) y que
// GALERIA_TOKEN: al menos 32 caracteres, al menos 8 distintos y no puede ser
// el valor de ejemplo de la plantilla ("cambiar_"). Si no cumple, la cámara no
// arranca; el valor nunca se imprime.
constexpr size_t kSecretoLongitudMin = 32;
constexpr size_t kSecretoDistintosMin = 8;
enum class MotivoSecreto : uint8_t { Valido, Vacio, Corto, PocaVariedad, Plantilla };
MotivoSecreto evaluarSecreto(const char* secreto);
const char* aTexto(MotivoSecreto motivo);

// --- Origen permitido (lista de un solo IP) ------------------------------------------------------
// IPv4 en cuatro octetos decimales sin ceros a la izquierda ("192.168.1.7").
// Devuelve false con cualquier otra cosa (vacío, octal, 5 octetos, > 255).
bool parsearIpv4(const char* texto, uint8_t salida[4]);
// Un IPv6 mapeado a IPv4 (::ffff:a.b.c.d, así se ve un cliente IPv4 en un
// socket de doble pila): extrae los 4 octetos. false si no es de ese tipo.
bool ipv4DeMapeada(const uint8_t v6[16], uint8_t salida[4]);

// --- Tamaño del búfer de la foto -------------------------------------------------------------------
// Tamaño del búfer del cuadro JPEG que se le da al driver (jpeg_buffer_size).
// Sin él, el driver reserva ancho*alto/5 (unos 157 KB a 1024x768); se reservan
// 512 KB para que un cuadro con mucho detalle (una escena ruidosa) no se quede
// corto y se pierda la foto. Con fb_count = 1 es un solo búfer, y solo existe
// mientras la cámara está encendida. Tiene que cubrir la mayor resolución
// permitida (XGA): la prueba comprueba que ancho*alto/2 cabe.
constexpr size_t kBufferJpegBytes = 512 * 1024;

// --- Latido de salud (contrato MQTT, sección 3: aula/camara/estado) -----------------------
// {"en_linea":true,"rssi":-58,"heap":120000,"temp":48.5,"camara":false,"corte":false,"nvs":true}
// rssi y heap son enteros, temp es float (un decimal); si temp no es una lectura
// válida (ver kTempMinimaValidaC), el campo se omite (en el contrato es
// opcional). "camara" dice si la cámara está encendida y "corte" si las
// capturas están suspendidas por temperatura o por el enfriamiento del
// presupuesto de encendido, y "nvs" si la flash donde se guarda el piso de ts
// funciona (si es false, no hay defensa contra el reenvío tras un reinicio).
// Devuelve la longitud sin el terminador nulo, o 0
// si no cabe en `capacidad`.
constexpr uint32_t kIntervaloLatidoMs = 30000;
// El peor caso del payload (rssi y heap en su valor extremo, temp de -40.0,
// camara, corte y nvs en false) son 108 B; con el terminador caben en 112.
// Con el tópico (18 B) y la cabecera MQTT (7 B) quedan muy por debajo de los
// 256 B del búfer de PubSubClient, que no hace falta ampliar.
constexpr size_t kLatidoBytesMax = 112;
size_t construirLatido(char* destino, size_t capacidad, int rssi, uint32_t heap, float temperatura_c,
                       bool camara_encendida, bool en_corte, bool nvs_ok);

}  // namespace logica_proteccion
