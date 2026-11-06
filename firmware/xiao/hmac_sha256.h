// SHA-256 y HMAC-SHA256 en C++ puro, sin Arduino y sin dependencias.
// Se usa para el endpoint /capture de la XIAO: la cabecera X-Captura-HMAC es
// el HMAC-SHA256 del secreto compartido con el servicio de visión sobre la
// cadena canónica de la petición.
//
// Sin asignación dinámica: todos los búferes son de tamaño fijo y el camino
// caliente (hashear una petición) no llama a new ni a malloc.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace hmac_sha256 {

// Longitud de un hash SHA-256 en bytes.
constexpr size_t kLongitudHash = 32;
// Longitud del texto hex en minúscula (2 caracteres por byte) más el
// terminador nulo.
constexpr size_t kLongitudHex = 2 * 32 + 1;
// Tamaño de bloque de SHA-256.
constexpr size_t kLongitudBloque = 64;

// SHA-256 de `datos`. `salida` recibe 32 bytes.
void sha256(const uint8_t* datos, size_t longitud, uint8_t salida[kLongitudHash]);

// HMAC-SHA256 (RFC 2104) de `datos` con la clave `clave`. La clave va en bytes
// UTF-8, tal cual (sin derivar ni normalizar). `salida` recibe 32 bytes.
void hmacSha256(const uint8_t* clave, size_t longitudClave, const uint8_t* datos,
                size_t longitudDatos, uint8_t salida[kLongitudHash]);

// Escribe en `salida` el texto hex en minúscula de `datos`, con terminador
// nulo. `salida` necesita 2 * longitud + 1 bytes.
void aHex(const uint8_t* datos, size_t longitud, char* salida);

// Compara dos buffers de la misma longitud en tiempo constante: recorre
// siempre todos los bytes y no sale antes de tiempo por el contenido (evita
// que un atacante que mida el tiempo deduzca la firma byte a byte).
bool compararConstante(const uint8_t* a, const uint8_t* b, size_t longitud);

// Convierte un texto hex en bytes. Acepta minúsculas y mayúsculas. Devuelve
// false si el texto no es hex válido o si no tiene la longitud esperada.
bool desdeHex(const char* hex, uint8_t* salida, size_t longitudEsperada);

}  // namespace hmac_sha256
