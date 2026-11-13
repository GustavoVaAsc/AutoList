#pragma once

// La firma de la peticion de captura: lo que el servicio manda a la placa (XIAO) en
// cada GET /capture y lo que la placa verifica (firmware/xiao/logica_camara.h). Los dos
// lados tienen que coincidir byte a byte.
//
//   X-Captura-TS:    epoch en segundos del reloj del BORDE (decimal)
//   X-Captura-Nonce: entero aleatorio de 64 bits sin signo (decimal)
//   X-Captura-HMAC:  64 caracteres hex en minuscula
//
// La cadena firmada es "GET\n/capture\n<ts>\n<nonce>" (UTF-8, sin salto de linea final).
//
// EL CAMINO FIRMADO ES FIJO: siempre "/capture", sin la query. La placa firma ese
// camino sea cual sea el de la peticion (kCamino en logica_camara.h), asi que el
// servicio no lo deriva de captura.url: la URL solo dice a donde se envia la peticion
// (puede apuntar a un puente que reexpone la placa en otro camino) y puede llevar
// "?res=SVGA". Derivarlo de la URL romperia la firma en cuanto el camino de la URL
// fuera otro; con el camino fijo, lo unico que importa es el que ve la placa.
//
// LA RESPUESTA VA CIFRADA Y AUTENTICADA (AES-256-GCM). La foto que contesta la placa a una
// peticion firmada viaja como application/octet-stream cifrada con una clave derivada del
// secreto compartido:
//   K    = HMAC-SHA256(clave = bytes del secreto, mensaje = kEtiquetaClaveRespuesta), 32 bytes
//          (se deriva una vez, al arrancar);
//   IV   = cabecera X-Captura-IV, 24 caracteres hex en minuscula (12 bytes aleatorios de la
//          placa);
//   AAD  = "RESP\n<ts>\n<nonce>" (decimal canonico, sin salto final) con el ts y el nonce que
//          el borde mando en la PETICION;
//   cuerpo = texto cifrado seguido de la etiqueta de 16 bytes.
// Asi nadie en la LAN puede leer la foto, ni suplantar la IP de la placa para contestar con
// otra (fraude de asistencia), ni reenviar la respuesta de otra peticion (el AAD la ata a
// esta). Cuando el servicio firmo la peticion, exige un IV valido y un cuerpo de al menos 16
// bytes, descifra y verifica; si algo falla, descarta la foto antes de decodificarla.
//
// Esta pieza no depende de OpenCV ni de la red: se prueba en cualquier maquina.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vision {

// Los nombres de las tres cabeceras (identificadores tecnicos: van tal cual).
inline constexpr const char* kHeaderCapturaTs = "X-Captura-TS";
inline constexpr const char* kHeaderCapturaNonce = "X-Captura-Nonce";
inline constexpr const char* kHeaderCapturaHmac = "X-Captura-HMAC";
// La cabecera de la RESPUESTA (un 200) con el IV de AES-GCM.
inline constexpr const char* kHeaderCapturaIv = "X-Captura-IV";

// El metodo y el camino que se firman (los de la placa).
inline constexpr const char* kMetodoCaptura = "GET";
inline constexpr const char* kCaminoCaptura = "/capture";

// Constantes de la respuesta cifrada.
inline constexpr const char* kEtiquetaClaveRespuesta = "AUTOLIST-CAPTURA-RESP-AES256GCM-V1";
inline constexpr std::size_t kLongitudClave = 32;
inline constexpr std::size_t kLongitudIv = 12;
inline constexpr std::size_t kLongitudIvHex = 2 * kLongitudIv;
inline constexpr std::size_t kLongitudEtiqueta = 16;

// Primer epoch (2026-01-01T00:00:00Z) que se acepta como hora real. Una placa sin NTP
// ni RTC arranca en 1970: firmar con esa hora hace que la placa la acepte y despues
// rechace como antigua toda peticion con la hora buena. El suelo es reciente a
// proposito: cualquier hora anterior a este proyecto es un reloj roto.
inline constexpr std::int64_t kMinEpochPlausible = 1767225600;
// Cuanto mas puede avanzar el reloj de pared que el monotonico entre dos peticiones firmadas
// antes de considerarlo un salto hacia adelante: se deja un margen de 10 minutos. Un ts
// firmado en el futuro dejaria a la placa rechazando con 403 todo lo que venga despues.
inline constexpr std::int64_t kSaltoRelojMaxS = 600;
// Techo duro (2100-01-01T00:00:00Z): un ts mayor no es una hora real. Firmarlo dejaria a la
// placa con un "ultimo ts aceptado" imposible y rechazaria con 403 todas las capturas buenas.
inline constexpr std::int64_t kMaxEpochPlausible = 4102444800;

// La cadena canonica "<metodo>\n<camino>\n<ts>\n<nonce>". El camino se corta en el
// primer "?" o "#": la query no se firma (igual que en la placa).
std::string cadena_canonica(std::string_view metodo, std::string_view camino, std::int64_t ts,
                            std::uint64_t nonce);

// La firma (HMAC-SHA256 en hex minuscula) de la peticion de captura. El secreto va en
// bytes, tal cual (sin derivar ni normalizar).
std::string firma_captura(std::string_view secreto, std::int64_t ts, std::uint64_t nonce);

// Las tres cabeceras, listas para el cliente HTTP. El valor va PEGADO a los dos puntos
// ("Nombre:valor"), sin el espacio habitual: asi el cliente no tiene que recortarlo.
std::vector<std::string> cabeceras_captura(std::string_view secreto, std::int64_t ts,
                                           std::uint64_t nonce);

// La clave de la respuesta. Se borra (OPENSSL_cleanse) al destruirse.
class ClaveRespuesta {
 public:
  ClaveRespuesta() = default;
  ClaveRespuesta(const ClaveRespuesta&) = default;
  ClaveRespuesta& operator=(const ClaveRespuesta&) = default;
  ~ClaveRespuesta();
  [[nodiscard]] const std::uint8_t* data() const { return bytes_.data(); }
  [[nodiscard]] std::array<std::uint8_t, kLongitudClave>& bytes() { return bytes_; }
  [[nodiscard]] const std::array<std::uint8_t, kLongitudClave>& bytes() const { return bytes_; }

 private:
  std::array<std::uint8_t, kLongitudClave> bytes_{};
};

// K = HMAC-SHA256(secreto, kEtiquetaClaveRespuesta).
ClaveRespuesta derivar_clave_respuesta(std::string_view secreto);

// El AAD de la respuesta: "RESP\n<ts>\n<nonce>" (los de la peticion).
std::string aad_respuesta(std::int64_t ts, std::uint64_t nonce);

// Por que una respuesta no es autentica (para el log por rafaga).
enum class ResultadoDescifrado {
  Ok,
  IvAusente,    // sin cabecera X-Captura-IV
  IvInvalido,   // no son 24 caracteres hex en minuscula
  CuerpoCorto,  // menos de 16 bytes: ni cabe la etiqueta
  NoAutentico   // la etiqueta no cuadra: clave, AAD (ts o nonce), IV o cuerpo distintos
};

// Descifra y verifica `cuerpo` (texto cifrado || etiqueta). Con Ok, `plano` tiene la foto;
// con cualquier otro resultado queda vacio (y lo que se haya descifrado a medias se borra).
// No lanza.
ResultadoDescifrado descifrar_respuesta(const ClaveRespuesta& clave, std::string_view iv_hex,
                                        std::int64_t ts, std::uint64_t nonce,
                                        const std::uint8_t* cuerpo, std::size_t tamano,
                                        std::vector<std::uint8_t>& plano);

// Borra el contenido (y lo vacia) sin que el compilador pueda omitirlo: la foto descifrada y
// otros datos que no deben quedarse en memoria.
void borrar_bytes(std::vector<std::uint8_t>& bytes);

// Un nonce de 64 bits sacado del generador criptografico del kernel (getrandom).
// std::nullopt si el kernel no puede dar bytes ahora: el llamador no firma. No hay
// reserva con un generador pseudoaleatorio: un nonce predecible anula el anti-replay.
std::optional<std::uint64_t> nonce_aleatorio();

}  // namespace vision
