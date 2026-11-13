#include "puente_aula/respuesta_cifrada.hpp"

#include <openssl/evp.h>

#include <cstring>
#include <memory>
#include <stdexcept>

#include <hmac_sha256.h>  // firmware/xiao: el mismo HMAC con el que la placa deriva K

namespace puente_aula {

namespace {

constexpr std::string_view kEtiquetaDerivacion = "AUTOLIST-CAPTURA-RESP-AES256GCM-V1";

struct LiberarContexto {
  void operator()(EVP_CIPHER_CTX* contexto) const { EVP_CIPHER_CTX_free(contexto); }
};
using Contexto = std::unique_ptr<EVP_CIPHER_CTX, LiberarContexto>;

const std::uint8_t* bytes(std::string_view texto) {
  return reinterpret_cast<const std::uint8_t*>(texto.data());
}

}  // namespace

ClaveRespuesta derivar_clave_respuesta(std::string_view secreto) {
  ClaveRespuesta clave{};
  hmac_sha256::hmacSha256(bytes(secreto), secreto.size(), bytes(kEtiquetaDerivacion),
                          kEtiquetaDerivacion.size(), clave.data());
  return clave;
}

std::string aad_respuesta(std::int64_t ts, std::uint64_t nonce) {
  return "RESP\n" + std::to_string(ts) + "\n" + std::to_string(nonce);
}

std::string cifrar_gcm(const ClaveRespuesta& clave, const IvRespuesta& iv, std::string_view aad,
                       std::string_view texto_plano) {
  Contexto contexto(EVP_CIPHER_CTX_new());
  int largo = 0;
  if (!contexto || EVP_EncryptInit_ex(contexto.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
      EVP_CIPHER_CTX_ctrl(contexto.get(), EVP_CTRL_GCM_SET_IVLEN, kLongitudIv, nullptr) != 1 ||
      EVP_EncryptInit_ex(contexto.get(), nullptr, nullptr, clave.data(), iv.data()) != 1) {
    throw std::runtime_error("AES-GCM: no se pudo iniciar el cifrado");
  }
  if (!aad.empty() &&
      EVP_EncryptUpdate(contexto.get(), nullptr, &largo, bytes(aad), static_cast<int>(aad.size())) != 1) {
    throw std::runtime_error("AES-GCM: AAD");
  }
  std::string salida(texto_plano.size() + kLongitudEtiqueta, '\0');
  auto* destino = reinterpret_cast<std::uint8_t*>(salida.data());
  int total = 0;
  if (!texto_plano.empty()) {
    if (EVP_EncryptUpdate(contexto.get(), destino, &largo, bytes(texto_plano),
                          static_cast<int>(texto_plano.size())) != 1) {
      throw std::runtime_error("AES-GCM: cifrado");
    }
    total = largo;
  }
  if (EVP_EncryptFinal_ex(contexto.get(), destino + total, &largo) != 1) {
    throw std::runtime_error("AES-GCM: final");
  }
  total += largo;
  if (EVP_CIPHER_CTX_ctrl(contexto.get(), EVP_CTRL_GCM_GET_TAG, kLongitudEtiqueta,
                          destino + total) != 1) {
    throw std::runtime_error("AES-GCM: etiqueta");
  }
  return salida;
}

bool descifrar_gcm(const ClaveRespuesta& clave, const IvRespuesta& iv, std::string_view aad,
                   std::string_view cifrado_y_etiqueta, std::string& texto_plano) {
  if (cifrado_y_etiqueta.size() < kLongitudEtiqueta) return false;
  const std::size_t largo_cifrado = cifrado_y_etiqueta.size() - kLongitudEtiqueta;
  Contexto contexto(EVP_CIPHER_CTX_new());
  int largo = 0;
  if (!contexto || EVP_DecryptInit_ex(contexto.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
      EVP_CIPHER_CTX_ctrl(contexto.get(), EVP_CTRL_GCM_SET_IVLEN, kLongitudIv, nullptr) != 1 ||
      EVP_DecryptInit_ex(contexto.get(), nullptr, nullptr, clave.data(), iv.data()) != 1) {
    return false;
  }
  if (!aad.empty() &&
      EVP_DecryptUpdate(contexto.get(), nullptr, &largo, bytes(aad), static_cast<int>(aad.size())) != 1) {
    return false;
  }
  std::string plano(largo_cifrado, '\0');
  auto* destino = reinterpret_cast<std::uint8_t*>(plano.data());
  int total = 0;
  if (largo_cifrado > 0) {
    if (EVP_DecryptUpdate(contexto.get(), destino, &largo, bytes(cifrado_y_etiqueta),
                          static_cast<int>(largo_cifrado)) != 1) {
      explicit_bzero(plano.data(), plano.size());
      return false;
    }
    total = largo;
  }
  // La etiqueta se pasa tal cual: OpenSSL la compara en tiempo constante en el Final.
  std::uint8_t etiqueta[kLongitudEtiqueta];
  std::memcpy(etiqueta, cifrado_y_etiqueta.data() + largo_cifrado, kLongitudEtiqueta);
  const bool bien =
      EVP_CIPHER_CTX_ctrl(contexto.get(), EVP_CTRL_GCM_SET_TAG, kLongitudEtiqueta, etiqueta) == 1 &&
      EVP_DecryptFinal_ex(contexto.get(), destino + total, &largo) == 1;
  if (!bien) {
    // Un descifrado sin etiqueta valida no se entrega: ni un byte.
    explicit_bzero(plano.data(), plano.size());
    return false;
  }
  // Lo que el llamador tuviera de una llamada anterior (otra foto) se borra antes de soltarlo.
  explicit_bzero(texto_plano.data(), texto_plano.size());
  texto_plano = std::move(plano);
  return true;
}

RespuestaDescifrada descifrar_respuesta(std::string_view secreto, std::int64_t ts,
                                        std::uint64_t nonce, const RespuestaCaptura& respuesta) {
  RespuestaDescifrada salida;
  const std::string* iv_hex = nullptr;
  for (const auto& [nombre, valor] : respuesta.cabeceras) {
    if (nombre == "X-Captura-IV") iv_hex = &valor;
  }
  if (iv_hex == nullptr) {
    salida.estado = respuesta.iv_malformado ? EstadoRespuesta::Invalida : EstadoRespuesta::Ausente;
    return salida;
  }
  IvRespuesta iv{};
  if (!hmac_sha256::desdeHex(iv_hex->c_str(), iv.data(), iv.size())) {
    salida.estado = EstadoRespuesta::Invalida;
    return salida;
  }
  ClaveRespuesta clave = derivar_clave_respuesta(secreto);
  const bool bien = descifrar_gcm(clave, iv, aad_respuesta(ts, nonce), respuesta.cuerpo,
                                  salida.texto_plano);
  explicit_bzero(clave.data(), clave.size());
  salida.estado = bien ? EstadoRespuesta::Ok : EstadoRespuesta::Invalida;
  return salida;
}

}  // namespace puente_aula
