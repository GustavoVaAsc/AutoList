#pragma once

// La firma de la peticion de captura que verifica la placa (firmware/xiao/logica_camara.*) y
// que firma el servicio de vision (orangepi/vision/src/captura_firma.cpp). Aqui la usa
// probar_captura para pedir fotos a la placa sin el servicio de vision.
//
// El HMAC-SHA256 es el de la placa: CMake compila firmware/xiao/hmac_sha256.cpp directamente,
// sin copiarlo.
//
//   X-Captura-TS:    epoch en segundos (decimal)
//   X-Captura-Nonce: entero de 64 bits sin signo (decimal)
//   X-Captura-HMAC:  64 caracteres hex en minuscula
//
// La cadena firmada es "GET\n/capture\n<ts>\n<nonce>" (sin salto final). El camino va sin la
// query y es siempre "/capture".
//
// La RESPUESTA 200 de la placa va cifrada y autenticada con AES-256-GCM, no firmada con HMAC:
// ver respuesta_cifrada.hpp.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "puente_aula/cliente_http.hpp"

namespace puente_aula {

inline constexpr const char* kCabeceraTs = "X-Captura-TS";
inline constexpr const char* kCabeceraNonce = "X-Captura-Nonce";
inline constexpr const char* kCabeceraHmac = "X-Captura-HMAC";
inline constexpr const char* kMetodoCaptura = "GET";
inline constexpr const char* kCaminoFirmado = "/capture";

// "<metodo>\n<camino>\n<ts>\n<nonce>"; el camino se corta en el primer '?' o '#'.
std::string cadena_canonica(std::string_view metodo, std::string_view camino, std::int64_t ts,
                            std::uint64_t nonce);

// HMAC-SHA256 en hex minuscula de `mensaje` con `clave` (bytes, tal cual).
std::string hmac_hex(std::string_view clave, std::string_view mensaje);

// La firma de una captura (hex minuscula).
std::string firma_captura(std::string_view secreto, std::int64_t ts, std::uint64_t nonce);

// Las cabeceras de la peticion. Con `con_firma` falso se manda ts y nonce sin la firma (la
// placa debe contestar 401).
Cabeceras cabeceras_captura(std::string_view secreto, std::int64_t ts, std::uint64_t nonce,
                            bool con_firma = true);

// Un nonce de 64 bits del generador criptografico del kernel; std::nullopt si no puede.
std::optional<std::uint64_t> nonce_aleatorio();

}  // namespace puente_aula
