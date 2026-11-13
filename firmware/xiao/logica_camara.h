// Lógica pura del endpoint /capture de la XIAO: cabeceras, firma HMAC,
// frescura y anti-replay.
// C++ puro, sin dependencias de Arduino: así se prueba en el host con g++
// (pruebas/test_logica_camara.cpp). El módulo de Arduino (captura.h) hace la
// E/S: lee las cabeceras, saca la foto y responde.
//
// Protocolo (tiene que coincidir byte a byte con el del servicio de visión,
// orangepi/vision/src/hmac_sha256.cpp):
//   GET /capture?res=UXGA
//   X-Captura-TS:    epoch en segundos del reloj del BORDE (decimal)
//   X-Captura-Nonce: entero aleatorio de 64 bits (decimal)
//   X-Captura-HMAC:  64 caracteres hex
// La cadena canónica firmada es "GET\n/capture\n<ts>\n<nonce>" (UTF-8, sin
// salto de línea final): se firma el camino SIN la query. La query solo pide
// la resolución, así que no cambia el contenido de la foto y no necesita
// firmarse.
//
// La RESPUESTA (solo el 200) va CIFRADA y autenticada con AES-256-GCM:
//   K   = HMAC-SHA256(clave = secreto, mensaje = "AUTOLIST-CAPTURA-RESP-AES256GCM-V1")
//   IV  = 12 bytes aleatorios nuevos en cada respuesta, en X-Captura-IV (24 hex)
//   AAD = "RESP\n<ts>\n<nonce>" (ts y nonce de la petición en decimal canónico,
//         sin salto final): ata la foto a esa petición
//   cuerpo = texto cifrado (mide lo mismo que la JPEG) seguido de la etiqueta de
//         16 bytes; Content-Type: application/octet-stream
// Así la foto no viaja en claro por la LAN y el borde sabe que la mandó la XIAO
// para ese nonce. Aquí solo está la parte pura (K, AAD, IV en hex); el cifrado
// lo hace mbedTLS en captura.cpp.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace logica_camara {

// Nombres de las tres cabeceras (identificadores técnicos: van tal cual).
constexpr const char* kCabeceraTs = "X-Captura-TS";
constexpr const char* kCabeceraNonce = "X-Captura-Nonce";
constexpr const char* kCabeceraHmac = "X-Captura-HMAC";
// La cabecera de la respuesta con el IV del cifrado (24 caracteres hex).
constexpr const char* kCabeceraIv = "X-Captura-IV";

// El método y el camino que se firman. El camino va sin la query.
constexpr const char* kMetodo = "GET";
constexpr const char* kCamino = "/capture";

// Longitudes máximas de los valores de las cabeceras. El ts es un epoch en
// segundos con signo (int64, 19 dígitos más el posible "-"); el nonce son 64
// bits sin signo (20 dígitos).
constexpr size_t kLongitudTsMax = 20;
constexpr size_t kLongitudNonceMax = 20;
// La firma viaja como texto hex: 2 caracteres por byte de SHA-256.
constexpr size_t kLongitudHmacHex = 64;
// Ventana de frescura: un ts más antiguo que (último ts aceptado - 300 s) se
// rechaza. Cubre el desfase normal del reloj del borde. No hay límite
// superior: entre dos capturas puede pasar más de una hora, así que un ts
// "futuro" no significa que el reloj haya saltado (el salto hacia atrás lo
// detecta el borde, antes de firmar).
constexpr uint64_t kVentanaFrescuraS = 300;
// Cuántos nonces aceptados se recuerdan para detectar repeticiones. La ráfaga
// de una captura son 4 fotos (vision.conf), así que con 64 entran unas 16
// capturas antes de que el más antiguo se salga de la memoria. Se guardan
// pares (ts, nonce): cuando uno se sale, su ts pasa a ser un piso (ver
// Validador) y un reenvío suyo ya no sirve aunque el nonce se haya olvidado.
constexpr size_t kNoncesRecordados = 64;
// El piso de ts se guarda en la flash (NVS) solo cuando avanza al menos esto
// respecto del guardado: limita el desgaste (como mucho 1 escritura por minuto
// de ts, unas 1440 al día; el NVS reparte el desgaste entre sus páginas).
constexpr int64_t kPasoPersistenciaTsS = 60;
// "RESP\n" + ts + "\n" + nonce + terminador nulo (el AAD del cifrado).
constexpr size_t kLongitudAadRespuestaMax = 5 + kLongitudTsMax + 1 + kLongitudNonceMax + 1;
// El cifrado de la respuesta: clave de 32 bytes (AES-256), IV de 12 bytes (el
// tamaño de GCM, aleatorio y nuevo en cada respuesta) y etiqueta de 16 bytes.
constexpr size_t kLongitudClaveRespuesta = 32;
constexpr size_t kLongitudIv = 12;
constexpr size_t kLongitudIvHex = 2 * kLongitudIv;  // sin el terminador nulo
constexpr size_t kLongitudEtiquetaGcm = 16;
// El mensaje con el que se deriva K del secreto (versionado: si el esquema
// cambia, cambia el sufijo).
constexpr const char* kContextoClaveRespuesta = "AUTOLIST-CAPTURA-RESP-AES256GCM-V1";
// Cuánto puede adelantarse el ts de una petición al último aceptado, además del
// tiempo que pasó en la placa (ver Validador::validar).
constexpr uint64_t kAdelantoMaxS = 600;
// Cuánto puede subir el piso de ts guardado en la flash POR ARRANQUE: como mucho
// esto más el tiempo que lleve encendida la placa desde ese arranque (ver
// PersistenciaTs). Un ts absurdo (años en el futuro), por muchas peticiones que
// lo sigan, no puede dejar el piso a más de un día (más su propio tiempo
// encendida) de donde empezó el arranque; una pausa larga legítima converge en
// unos cuantos arranques, uno por día de pausa.
constexpr int64_t kAvanceMaxPersistenciaS = 86400;
// Entre dos escrituras del piso en la flash pasa como mínimo esto, en tiempo de
// la placa: como mucho 1 por minuto (1440 al día), que limita el desgaste.
constexpr uint64_t kIntervaloEscrituraTsMs = 60000;
// "GET\n/capture\n" + ts + "\n" + nonce + terminador nulo.
constexpr size_t kLongitudCadenaCanonicaMax = 4 + 9 + kLongitudTsMax + 1 + kLongitudNonceMax + 1;

// Qué decide el validador sobre una petición.
enum class Resultado {
  Aceptada,           // la firma cuadra y la petición es fresca: se puede capturar
  CabecerasFaltantes, // falta alguna de las tres cabeceras
  FirmaInvalida,      // la firma no cuadra, o el ts o el nonce no son un decimal válido
  NonceRepetido,      // el nonce ya se había usado: intento de reenvío
  TimestampAntiguo,   // el ts es más antiguo que la ventana de frescura o el piso
  TimestampFuturo     // el ts se adelanta más de lo que cabe desde el último aceptado
};

// Texto corto para el registro serie (nunca se manda al cliente).
const char* aTexto(Resultado resultado);

// Código HTTP que corresponde a cada resultado.
int codigoHttp(Resultado resultado);

// Las tres cabeceras de la petición. Un puntero nulo o un texto vacío cuentan
// como cabecera ausente. Los valores pueden traer espacios o tabuladores
// alrededor: el validador los recorta antes de usarlos.
struct Peticion {
  const char* ts = nullptr;
  const char* nonce = nullptr;
  const char* hmac = nullptr;
};

// Estado del endpoint /capture: comprueba la firma, recuerda los nonces ya
// usados y el último ts aceptado.
//
// El validador no usa el reloj de la XIAO (no es confiable: el RTC está en la
// Orange Pi). La frescura se mide contra el último ts aceptado, no contra la
// hora local.
class Validador {
 public:
  // `secreto` es el secreto compartido con el servicio de visión, en bytes
  // UTF-8. Debe seguir vivo mientras el validador exista (en el sketch viene
  // de secrets.h, que es estático).
  Validador(const char* secreto, size_t longitudSecreto);

  // Comprueba la petición y, si la acepta, recuerda su nonce y actualiza el
  // último ts aceptado. Si la acepta y se pasan `ts_aceptado` y
  // `nonce_aceptado`, reciben los valores ya convertidos (para cifrar la
  // respuesta con ellos). Esta forma no comprueba el adelanto del ts (no tiene
  // reloj): es la de las pruebas.
  Resultado validar(const Peticion& peticion, int64_t* ts_aceptado = nullptr,
                    uint64_t* nonce_aceptado = nullptr);

  // Lo mismo, con el tiempo de la placa en milisegundos (un contador de 64 bits
  // que no da la vuelta, como esp_timer_get_time()/1000, NO millis()). Además
  // rechaza con TimestampFuturo (403) todo ts mayor que
  //   último ts aceptado + (tiempo desde que se aceptó)/1 s + kAdelantoMaxS.
  // Un ts no puede adelantarse al reloj de la placa más de eso: así un ts
  // absurdo no fija un "último ts" imposible de alcanzar. La primera petición
  // tras un arranque no tiene referencia y no se limita (PersistenciaTs acota
  // lo que de ella llega a la flash).
  Resultado validar(const Peticion& peticion, uint64_t ahora_ms, int64_t* ts_aceptado = nullptr,
                    uint64_t* nonce_aceptado = nullptr);

  // El piso de ts: toda petición con ts menor o igual se rechaza como
  // TimestampAntiguo (403). Sube de dos maneras: (a) al arrancar, con el ts
  // guardado en la flash (cierra el reenvío tras un reinicio, cuando la RAM
  // olvidó los nonces); (b) cuando el anillo de 64 nonces expulsa uno, con el ts
  // de ese (cierra el reenvío de una petición vieja cuyo nonce ya se olvidó).
  // Nunca baja. Un ts a la altura del piso solo lo manda el borde si su reloj
  // retrocede o se repite el segundo 64 peticiones después, y el límite de
  // capturas lo impide.
  void establecerPiso(int64_t ts);
  bool hayPiso() const { return hay_piso_; }
  int64_t piso() const { return piso_ts_; }

  // Olvida los nonces y el último ts (solo para las pruebas).
  void reiniciar();

  // Si ya se aceptó alguna petición, y cuál fue su ts.
  bool hayUltimoTs() const { return hay_ultimo_ts_; }
  int64_t ultimoTs() const { return ultimo_ts_; }

 private:
  const char* secreto_;
  size_t longitud_secreto_;
  struct EntradaNonce {
    int64_t ts;
    uint64_t nonce;
  };
  EntradaNonce nonces_[kNoncesRecordados] = {};
  size_t total_nonces_ = 0;
  size_t siguiente_nonce_ = 0;
  int64_t ultimo_ts_ = 0;
  bool hay_ultimo_ts_ = false;
  uint64_t ultimo_ts_ms_ = 0;  // el tiempo de la placa cuando se aceptó ultimo_ts_
  int64_t piso_ts_ = 0;
  bool hay_piso_ = false;

  Resultado validarInterno(const Peticion& peticion, bool hay_reloj, uint64_t ahora_ms,
                           int64_t* ts_aceptado, uint64_t* nonce_aceptado);

  bool nonceVisto(uint64_t nonce) const;
  void recordarNonce(int64_t ts, uint64_t nonce);
};

// Qué ts se guarda en la flash y cuándo. Es el piso contra el reenvío de una
// petición vieja tras un reinicio (ver pisoDeArranque), pero una escritura
// equivocada puede dejar la cámara bloqueada (un ts a años vista aceptado una vez
// subiría el piso fuera de alcance de las peticiones legítimas), así que el
// ritmo al que sube está acotado:
//   - como mucho 1 escritura cada kIntervaloEscrituraTsMs de tiempo de la placa
//     (el desgaste de la flash, unas 1440 al día);
//   - solo si el ts aceptado supera al guardado en kPasoPersistenciaTsS o más;
//   - la primera escritura de un arranque sube el guardado como mucho
//     kAvanceMaxPersistenciaS (24 h), y de ahí en adelante, ningún valor pasa de
//       guardado_al_arrancar + kAvanceMaxPersistenciaS + (segundos encendida
//       desde el arranque).
//     Así UN arranque solo puede mover el piso 24 h más su propio tiempo
//     encendida por delante de donde empezó, sin importar cuántas peticiones
//     lleguen. Sin nada guardado (primer arranque de la placa) no hay referencia:
//     la primera escritura es el ts tal cual, y a partir de ahí rige el tope.
//
// Costo aceptado: tras más de 24 h de pausa (placa apagada), el guardado va por
// detrás del tiempo real durante ese arranque, y el piso de arranque
// (guardado + 60 s) queda por debajo de lo que se aceptó antes de apagar: el
// reenvío tras el reinicio queda abierto SOLO en ese arranque, hasta que el
// guardado alcance al ts real (uno por día de pausa). El impacto es bajo: la
// respuesta va cifrada, así que quien reenvíe solo dispara una captura que no
// puede leer y gasta presupuesto del límite de capturas.
//
// El tiempo es el de un contador de 64 bits de la placa (esp_timer_get_time()/1000,
// que no da la vuelta como millis()).
class PersistenciaTs {
 public:
  // Al arrancar (y al reabrir el NVS): lo que había en la flash, y el tiempo de
  // la placa en ese momento. `hay_guardado` = false si no había nada.
  void arrancar(bool hay_guardado, int64_t guardado, uint64_t ahora_ms);

  // ¿Hay que escribir ahora, para un ts aceptado `actual`?
  bool debeEscribir(uint64_t ahora_ms, int64_t actual) const;
  // Qué valor escribir (el ts, recortado al tope del arranque).
  int64_t valor(uint64_t ahora_ms, int64_t actual) const;
  // La escritura salió bien.
  void registrarEscritura(uint64_t ahora_ms, int64_t valor);

  bool hayGuardado() const { return hay_guardado_; }
  int64_t guardado() const { return guardado_; }

 private:
  bool hay_guardado_ = false;
  int64_t guardado_ = 0;       // lo que hay ahora en la flash
  bool hay_base_ = false;
  int64_t base_ = 0;           // el guardado al arrancar (o la primera escritura sin referencia)
  uint64_t base_ms_ = 0;       // el tiempo de la placa en ese momento
  bool hubo_escritura_ = false;
  uint64_t ultima_escritura_ms_ = 0;
};

// El piso con el que arranca el validador: el ts guardado más
// kPasoPersistenciaTsS. El guardado va hasta 60 s por detrás del último ts
// aceptado (solo se escribe al avanzar 60 s), así que todo lo aceptado antes
// del reinicio queda por debajo: el reenvío de una petición vieja tras un
// reinicio ya no sirve. Costo: las peticiones legítimas con ts hasta ese piso
// (menos de un minuto de ts) reciben 403 justo tras el reinicio.
int64_t pisoDeArranque(int64_t guardado);

// Construye la cadena canónica "<metodo>\n<camino>\n<ts>\n<nonce>" en
// `destino`, en UTF-8 y sin salto de línea final.
//
// El camino se recorta en el primer "?" o "#": la query no se firma. El ts y el
// nonce se escriben en decimal, como los serializa el servicio de visión.
//
// Devuelve la longitud (sin el terminador nulo), o 0 si falta algún dato, si
// alguno es demasiado largo o si no cabe en `capacidad`.
size_t construirCadenaCanonica(char* destino, size_t capacidad, const char* metodo,
                                const char* camino, int64_t ts, uint64_t nonce);

// K, la clave AES-256 del cifrado de la respuesta:
// HMAC-SHA256(secreto, kContextoClaveRespuesta). `clave` recibe 32 bytes. Se
// deriva una vez al arrancar y vive solo en RAM.
void derivarClaveRespuesta(const char* secreto, size_t longitud_secreto, uint8_t* clave);

// El AAD del cifrado, "RESP\n<ts>\n<nonce>" (decimal canónico, sin salto
// final). Devuelve la longitud (sin el terminador), o 0 si no cabe.
size_t construirAadRespuesta(char* destino, size_t capacidad, int64_t ts, uint64_t nonce);

// El IV (12 bytes) en hex minúscula para X-Captura-IV. `destino` necesita
// kLongitudIvHex + 1 bytes.
void ivAHex(const uint8_t* iv, char* destino);

}  // namespace logica_camara
