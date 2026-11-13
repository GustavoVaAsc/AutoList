#include "puente_aula/firma_captura.hpp"

#include <sys/random.h>

#include <cerrno>
#include <cstring>

#include <hmac_sha256.h>  // firmware/xiao: el mismo codigo con el que verifica la placa

namespace puente_aula {

std::string cadena_canonica(std::string_view metodo, std::string_view camino, std::int64_t ts,
                            std::uint64_t nonce) {
  const std::size_t fin_camino = camino.find_first_of("?#");
  if (fin_camino != std::string_view::npos) camino = camino.substr(0, fin_camino);
  std::string canonica(metodo);
  canonica += '\n';
  canonica += camino;
  canonica += '\n';
  canonica += std::to_string(ts);
  canonica += '\n';
  canonica += std::to_string(nonce);
  return canonica;
}

std::string hmac_hex(std::string_view clave, std::string_view mensaje) {
  std::uint8_t firma[hmac_sha256::kLongitudHash];
  hmac_sha256::hmacSha256(reinterpret_cast<const std::uint8_t*>(clave.data()), clave.size(),
                          reinterpret_cast<const std::uint8_t*>(mensaje.data()), mensaje.size(),
                          firma);
  char hex[hmac_sha256::kLongitudHex];
  hmac_sha256::aHex(firma, sizeof firma, hex);
  std::string salida = hex;
  // No dejar firmas ni intermedios en la pila (los del propio hmac_sha256 son de la placa y no
  // se tocan aqui).
  explicit_bzero(firma, sizeof firma);
  explicit_bzero(hex, sizeof hex);
  return salida;
}

std::string firma_captura(std::string_view secreto, std::int64_t ts, std::uint64_t nonce) {
  return hmac_hex(secreto, cadena_canonica(kMetodoCaptura, kCaminoFirmado, ts, nonce));
}

Cabeceras cabeceras_captura(std::string_view secreto, std::int64_t ts, std::uint64_t nonce,
                            bool con_firma) {
  Cabeceras cabeceras{{kCabeceraTs, std::to_string(ts)}, {kCabeceraNonce, std::to_string(nonce)}};
  if (con_firma) cabeceras.emplace_back(kCabeceraHmac, firma_captura(secreto, ts, nonce));
  return cabeceras;
}

std::optional<std::uint64_t> nonce_aleatorio() {
  std::uint64_t nonce = 0;
  for (;;) {
    const ssize_t leidos = getrandom(&nonce, sizeof nonce, 0);
    if (leidos == static_cast<ssize_t>(sizeof nonce)) return nonce;
    if (leidos < 0 && errno == EINTR) continue;
    return std::nullopt;
  }
}

}  // namespace puente_aula
