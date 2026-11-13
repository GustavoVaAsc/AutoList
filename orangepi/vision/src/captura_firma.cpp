#include "vision/captura_firma.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <sys/random.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <memory>

#include "vision/hmac_sha256.hpp"

namespace vision {

namespace {

std::string hmac_hex(std::string_view secreto, std::string_view mensaje) {
  std::uint8_t firma[hmac_sha256::kLongitudHash];
  hmac_sha256::hmacSha256(reinterpret_cast<const std::uint8_t*>(secreto.data()), secreto.size(),
                          reinterpret_cast<const std::uint8_t*>(mensaje.data()),
                          mensaje.size(), firma);
  char hex[hmac_sha256::kLongitudHex];
  hmac_sha256::aHex(firma, sizeof firma, hex);
  return std::string(hex);
}

}  // namespace

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

std::string firma_captura(std::string_view secreto, std::int64_t ts, std::uint64_t nonce) {
  return hmac_hex(secreto, cadena_canonica(kMetodoCaptura, kCaminoCaptura, ts, nonce));
}

void borrar_bytes(std::vector<std::uint8_t>& bytes) {
  if (!bytes.empty()) OPENSSL_cleanse(bytes.data(), bytes.size());
  bytes.clear();
}

ClaveRespuesta::~ClaveRespuesta() { OPENSSL_cleanse(bytes_.data(), bytes_.size()); }

ClaveRespuesta derivar_clave_respuesta(std::string_view secreto) {
  ClaveRespuesta clave;
  const std::string_view etiqueta = kEtiquetaClaveRespuesta;
  hmac_sha256::hmacSha256(reinterpret_cast<const std::uint8_t*>(secreto.data()), secreto.size(),
                          reinterpret_cast<const std::uint8_t*>(etiqueta.data()),
                          etiqueta.size(), clave.bytes().data());
  return clave;
}

std::string aad_respuesta(std::int64_t ts, std::uint64_t nonce) {
  return "RESP\n" + std::to_string(ts) + "\n" + std::to_string(nonce);
}

namespace {

// 24 caracteres hex en minuscula -> 12 bytes.
bool iv_desde_hex(std::string_view hex, std::array<std::uint8_t, kLongitudIv>& iv) {
  if (hex.size() != kLongitudIvHex) return false;
  const auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;  // las mayusculas tampoco: el protocolo pide minusculas
  };
  for (std::size_t i = 0; i < kLongitudIv; ++i) {
    const int alto = nibble(hex[2 * i]);
    const int bajo = nibble(hex[2 * i + 1]);
    if (alto < 0 || bajo < 0) return false;
    iv[i] = static_cast<std::uint8_t>((alto << 4) | bajo);
  }
  return true;
}

struct LiberaContexto {
  void operator()(EVP_CIPHER_CTX* contexto) const { EVP_CIPHER_CTX_free(contexto); }
};

}  // namespace

ResultadoDescifrado descifrar_respuesta(const ClaveRespuesta& clave, std::string_view iv_hex,
                                        std::int64_t ts, std::uint64_t nonce,
                                        const std::uint8_t* cuerpo, std::size_t tamano,
                                        std::vector<std::uint8_t>& plano) {
  plano.clear();
  if (iv_hex.empty()) return ResultadoDescifrado::IvAusente;
  std::array<std::uint8_t, kLongitudIv> iv{};
  if (!iv_desde_hex(iv_hex, iv)) return ResultadoDescifrado::IvInvalido;
  if (cuerpo == nullptr || tamano < kLongitudEtiqueta) return ResultadoDescifrado::CuerpoCorto;

  const std::size_t largo_cifrado = tamano - kLongitudEtiqueta;
  const std::string aad = aad_respuesta(ts, nonce);
  std::unique_ptr<EVP_CIPHER_CTX, LiberaContexto> contexto(EVP_CIPHER_CTX_new());
  if (!contexto) return ResultadoDescifrado::NoAutentico;
  if (EVP_DecryptInit_ex(contexto.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
      EVP_CIPHER_CTX_ctrl(contexto.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(kLongitudIv),
                          nullptr) != 1 ||
      EVP_DecryptInit_ex(contexto.get(), nullptr, nullptr, clave.data(), iv.data()) != 1) {
    return ResultadoDescifrado::NoAutentico;
  }
  int escritos = 0;
  if (EVP_DecryptUpdate(contexto.get(), nullptr, &escritos,
                        reinterpret_cast<const unsigned char*>(aad.data()),
                        static_cast<int>(aad.size())) != 1) {
    return ResultadoDescifrado::NoAutentico;
  }
  plano.resize(largo_cifrado);
  int total = 0;
  if (largo_cifrado > 0) {
    if (EVP_DecryptUpdate(contexto.get(), plano.data(), &escritos, cuerpo,
                          static_cast<int>(largo_cifrado)) != 1) {
      OPENSSL_cleanse(plano.data(), plano.size());
      plano.clear();
      return ResultadoDescifrado::NoAutentico;
    }
    total = escritos;
  }
  // La etiqueta son los ultimos 16 bytes del cuerpo.
  std::array<std::uint8_t, kLongitudEtiqueta> etiqueta{};
  std::copy_n(cuerpo + largo_cifrado, kLongitudEtiqueta, etiqueta.begin());
  if (EVP_CIPHER_CTX_ctrl(contexto.get(), EVP_CTRL_GCM_SET_TAG,
                          static_cast<int>(kLongitudEtiqueta), etiqueta.data()) != 1 ||
      EVP_DecryptFinal_ex(contexto.get(), plano.data() + total, &escritos) <= 0) {
    // No es autentico: lo descifrado a medias no se entrega.
    if (!plano.empty()) OPENSSL_cleanse(plano.data(), plano.size());
    plano.clear();
    return ResultadoDescifrado::NoAutentico;
  }
  return ResultadoDescifrado::Ok;
}

std::vector<std::string> cabeceras_captura(std::string_view secreto, std::int64_t ts,
                                           std::uint64_t nonce) {
  return {std::string(kHeaderCapturaTs) + ":" + std::to_string(ts),
          std::string(kHeaderCapturaNonce) + ":" + std::to_string(nonce),
          std::string(kHeaderCapturaHmac) + ":" + firma_captura(secreto, ts, nonce)};
}

std::optional<std::uint64_t> nonce_aleatorio() {
  // getrandom y no /dev/urandom: no abre ningun descriptor (no falla por falta de
  // descriptores ni dentro de un entorno sin /dev) y con 8 bytes devuelve todo o nada.
  // GRND_NONBLOCK: si el generador del kernel aun no esta listo (arranque temprano), no
  // se queda esperando en el hilo de trabajo; esa captura falla y la siguiente
  // reintenta.
  std::uint64_t nonce = 0;
  for (;;) {
    const ssize_t leidos = getrandom(&nonce, sizeof nonce, GRND_NONBLOCK);
    if (leidos == static_cast<ssize_t>(sizeof nonce)) return nonce;
    if (leidos < 0 && errno == EINTR) continue;
    return std::nullopt;
  }
}

}  // namespace vision
