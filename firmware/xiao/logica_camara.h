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
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace logica_camara {

// Nombres de las tres cabeceras (identificadores técnicos: van tal cual).
constexpr const char* kCabeceraTs = "X-Captura-TS";
constexpr const char* kCabeceraNonce = "X-Captura-Nonce";
constexpr const char* kCabeceraHmac = "X-Captura-HMAC";

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
// capturas antes de que el más antiguo se salga de la memoria.
constexpr size_t kNoncesRecordados = 64;
// "GET\n/capture\n" + ts + "\n" + nonce + terminador nulo.
constexpr size_t kLongitudCadenaCanonicaMax = 4 + 9 + kLongitudTsMax + 1 + kLongitudNonceMax + 1;

// Qué decide el validador sobre una petición.
enum class Resultado {
  Aceptada,           // la firma cuadra y la petición es fresca: se puede capturar
  CabecerasFaltantes, // falta alguna de las tres cabeceras
  FirmaInvalida,      // la firma no cuadra, o el ts o el nonce no son un decimal válido
  NonceRepetido,      // el nonce ya se había usado: intento de reenvío
  TimestampAntiguo    // el ts es más antiguo que la ventana de frescura
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
  // último ts aceptado.
  Resultado validar(const Peticion& peticion);

  // Olvida los nonces y el último ts (solo para las pruebas).
  void reiniciar();

  // Si ya se aceptó alguna petición, y cuál fue su ts.
  bool hayUltimoTs() const { return hay_ultimo_ts_; }
  int64_t ultimoTs() const { return ultimo_ts_; }

 private:
  const char* secreto_;
  size_t longitud_secreto_;
  uint64_t nonces_[kNoncesRecordados] = {0};
  size_t total_nonces_ = 0;
  size_t siguiente_nonce_ = 0;
  int64_t ultimo_ts_ = 0;
  bool hay_ultimo_ts_ = false;

  bool nonceVisto(uint64_t nonce) const;
  void recordarNonce(uint64_t nonce);
};

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

}  // namespace logica_camara
