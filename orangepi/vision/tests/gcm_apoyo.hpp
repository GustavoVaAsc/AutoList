#pragma once

// Apoyo de las pruebas: lo que hace la placa con la foto (cifrarla con AES-256-GCM), para
// armar respuestas en las pruebas. El servicio solo descifra; esto vive aqui, no en la
// biblioteca.

#include <openssl/evp.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "vision/captura_firma.hpp"

namespace gcm_apoyo {

inline std::vector<std::uint8_t> bytes_de_hex(std::string_view hex) {
  std::vector<std::uint8_t> bytes;
  for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
    bytes.push_back(static_cast<std::uint8_t>(std::stoi(std::string(hex.substr(i, 2)), nullptr, 16)));
  }
  return bytes;
}

inline std::string hex_de(const std::vector<std::uint8_t>& bytes) {
  static const char digitos[] = "0123456789abcdef";
  std::string hex;
  for (const std::uint8_t b : bytes) {
    hex.push_back(digitos[b >> 4]);
    hex.push_back(digitos[b & 0x0f]);
  }
  return hex;
}

// texto cifrado || etiqueta de 16 bytes, con el IV (12 bytes) y el AAD dados.
inline std::vector<std::uint8_t> cifrar(const vision::ClaveRespuesta& clave,
                                        const std::vector<std::uint8_t>& iv,
                                        const std::string& aad,
                                        const std::vector<std::uint8_t>& plano) {
  struct Libera {
    void operator()(EVP_CIPHER_CTX* c) const { EVP_CIPHER_CTX_free(c); }
  };
  std::unique_ptr<EVP_CIPHER_CTX, Libera> contexto(EVP_CIPHER_CTX_new());
  int n = 0;
  std::vector<std::uint8_t> salida(plano.size() + 16);
  if (!contexto || iv.size() != 12 ||
      EVP_EncryptInit_ex(contexto.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
      EVP_CIPHER_CTX_ctrl(contexto.get(), EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) != 1 ||
      EVP_EncryptInit_ex(contexto.get(), nullptr, nullptr, clave.data(), iv.data()) != 1 ||
      EVP_EncryptUpdate(contexto.get(), nullptr, &n,
                        reinterpret_cast<const unsigned char*>(aad.data()),
                        static_cast<int>(aad.size())) != 1) {
    throw std::runtime_error("gcm_apoyo: no se pudo preparar el cifrado");
  }
  int total = 0;
  if (!plano.empty()) {
    if (EVP_EncryptUpdate(contexto.get(), salida.data(), &n, plano.data(),
                          static_cast<int>(plano.size())) != 1) {
      throw std::runtime_error("gcm_apoyo: fallo el cifrado");
    }
    total = n;
  }
  if (EVP_EncryptFinal_ex(contexto.get(), salida.data() + total, &n) != 1 ||
      EVP_CIPHER_CTX_ctrl(contexto.get(), EVP_CTRL_GCM_GET_TAG, 16, salida.data() + plano.size()) !=
          1) {
    throw std::runtime_error("gcm_apoyo: fallo la etiqueta");
  }
  return salida;
}

}  // namespace gcm_apoyo
