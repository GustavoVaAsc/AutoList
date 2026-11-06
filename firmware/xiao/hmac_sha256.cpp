// SHA-256 y HMAC-SHA256 en C++ puro (implementación propia, sin Arduino).
#include "hmac_sha256.h"

#include <string.h>

namespace hmac_sha256 {
namespace {

// Constantes de redondeo de SHA-256 (FIPS 180-4).
constexpr uint32_t kK[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

// Estado inicial de SHA-256.
constexpr uint32_t kEstadoInicial[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u,
                                        0xa54ff53au, 0x510e527fu, 0x9b05688cu,
                                        0x1f83d9abu, 0x5be0cd19u};

// Contexto de SHA-256 para hashear por partes sin reservar memoria.
struct Contexto {
  uint32_t estado[8];
  uint8_t bloque[kLongitudBloque];
  size_t enBloque = 0;
  uint64_t bytesTotales = 0;
};

inline uint32_t rotar(uint32_t valor, uint32_t bits) {
  return (valor >> bits) | (valor << (32 - bits));
}

void iniciar(Contexto& contexto) {
  memcpy(contexto.estado, kEstadoInicial, sizeof kEstadoInicial);
  contexto.enBloque = 0;
  contexto.bytesTotales = 0;
}

void comprimir(uint32_t estado[8], const uint8_t bloque[kLongitudBloque]) {
  uint32_t w[64];
  for (size_t i = 0; i < 16; ++i) {
    w[i] = (uint32_t(bloque[4 * i]) << 24) | (uint32_t(bloque[4 * i + 1]) << 16) |
           (uint32_t(bloque[4 * i + 2]) << 8) | uint32_t(bloque[4 * i + 3]);
  }
  for (size_t i = 16; i < 64; ++i) {
    const uint32_t s0 = rotar(w[i - 15], 7) ^ rotar(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const uint32_t s1 = rotar(w[i - 2], 17) ^ rotar(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }

  uint32_t a = estado[0];
  uint32_t b = estado[1];
  uint32_t c = estado[2];
  uint32_t d = estado[3];
  uint32_t e = estado[4];
  uint32_t f = estado[5];
  uint32_t g = estado[6];
  uint32_t h = estado[7];

  for (size_t i = 0; i < 64; ++i) {
    const uint32_t s1 = rotar(e, 6) ^ rotar(e, 11) ^ rotar(e, 25);
    const uint32_t choix = (e & f) ^ (~e & g);
    const uint32_t t1 = h + s1 + choix + kK[i] + w[i];
    const uint32_t s0 = rotar(a, 2) ^ rotar(a, 13) ^ rotar(a, 22);
    const uint32_t mayoria = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t t2 = s0 + mayoria;
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }

  estado[0] += a;
  estado[1] += b;
  estado[2] += c;
  estado[3] += d;
  estado[4] += e;
  estado[5] += f;
  estado[6] += g;
  estado[7] += h;
}

void actualizar(Contexto& contexto, const uint8_t* datos, size_t longitud) {
  contexto.bytesTotales += longitud;
  while (longitud > 0) {
    const size_t caben = kLongitudBloque - contexto.enBloque;
    const size_t copiar = longitud < caben ? longitud : caben;
    memcpy(contexto.bloque + contexto.enBloque, datos, copiar);
    contexto.enBloque += copiar;
    datos += copiar;
    longitud -= copiar;
    if (contexto.enBloque == kLongitudBloque) {
      comprimir(contexto.estado, contexto.bloque);
      contexto.enBloque = 0;
    }
  }
}

void finalizar(Contexto& contexto, uint8_t salida[kLongitudHash]) {
  const uint64_t bits = contexto.bytesTotales * 8;
  uint8_t uno = 0x80;
  actualizar(contexto, &uno, 1);
  const uint8_t cero = 0;
  while (contexto.enBloque != 56) {
    actualizar(contexto, &cero, 1);
  }
  uint8_t longitudBytes[8];
  for (size_t i = 0; i < 8; ++i) {
    longitudBytes[i] = uint8_t(bits >> (56 - 8 * i));
  }
  // Los 8 bytes de longitud entran justo en el bloque que ya está a medias:
  // se actualizan directamente para no alterar el contador de bytes.
  memcpy(contexto.bloque + contexto.enBloque, longitudBytes, 8);
  comprimir(contexto.estado, contexto.bloque);
  contexto.enBloque = 0;

  for (size_t i = 0; i < 8; ++i) {
    salida[4 * i] = uint8_t(contexto.estado[i] >> 24);
    salida[4 * i + 1] = uint8_t(contexto.estado[i] >> 16);
    salida[4 * i + 2] = uint8_t(contexto.estado[i] >> 8);
    salida[4 * i + 3] = uint8_t(contexto.estado[i]);
  }
}

}  // namespace

void sha256(const uint8_t* datos, size_t longitud, uint8_t salida[kLongitudHash]) {
  Contexto contexto;
  iniciar(contexto);
  actualizar(contexto, datos, longitud);
  finalizar(contexto, salida);
}

void hmacSha256(const uint8_t* clave, size_t longitudClave, const uint8_t* datos,
                size_t longitudDatos, uint8_t salida[kLongitudHash]) {
  uint8_t claveNormalizada[kLongitudBloque];
  memset(claveNormalizada, 0, sizeof claveNormalizada);
  if (longitudClave > kLongitudBloque) {
    // Clave más larga que el bloque: se hashea y se usa el resultado.
    sha256(clave, longitudClave, claveNormalizada);
  } else if (longitudClave > 0) {
    memcpy(claveNormalizada, clave, longitudClave);
  }

  uint8_t ipad[kLongitudBloque];
  uint8_t opad[kLongitudBloque];
  for (size_t i = 0; i < kLongitudBloque; ++i) {
    ipad[i] = uint8_t(claveNormalizada[i] ^ 0x36);
    opad[i] = uint8_t(claveNormalizada[i] ^ 0x5c);
  }

  uint8_t hashInterno[kLongitudHash];
  Contexto contexto;
  iniciar(contexto);
  actualizar(contexto, ipad, sizeof ipad);
  actualizar(contexto, datos, longitudDatos);
  finalizar(contexto, hashInterno);

  Contexto exterior;
  iniciar(exterior);
  actualizar(exterior, opad, sizeof opad);
  actualizar(exterior, hashInterno, sizeof hashInterno);
  finalizar(exterior, salida);
}

void aHex(const uint8_t* datos, size_t longitud, char* salida) {
  static const char kDigitos[] = "0123456789abcdef";
  for (size_t i = 0; i < longitud; ++i) {
    salida[2 * i] = kDigitos[datos[i] >> 4];
    salida[2 * i + 1] = kDigitos[datos[i] & 0x0f];
  }
  salida[2 * longitud] = '\0';
}

bool compararConstante(const uint8_t* a, const uint8_t* b, size_t longitud) {
  uint8_t diferencia = 0;
  for (size_t i = 0; i < longitud; ++i) {
    diferencia = uint8_t(diferencia | (a[i] ^ b[i]));
  }
  return diferencia == 0;
}

bool desdeHex(const char* hex, uint8_t* salida, size_t longitudEsperada) {
  if (hex == nullptr) {
    return false;
  }
  size_t largo = 0;
  while (hex[largo] != '\0') {
    ++largo;
  }
  if (largo != 2 * longitudEsperada) {
    return false;
  }
  for (size_t i = 0; i < longitudEsperada; ++i) {
    uint8_t alto = 0;
    uint8_t bajo = 0;
    const char cAlto = hex[2 * i];
    const char cBajo = hex[2 * i + 1];
    if (cAlto >= '0' && cAlto <= '9') {
      alto = uint8_t(cAlto - '0');
    } else if (cAlto >= 'a' && cAlto <= 'f') {
      alto = uint8_t(cAlto - 'a' + 10);
    } else if (cAlto >= 'A' && cAlto <= 'F') {
      alto = uint8_t(cAlto - 'A' + 10);
    } else {
      return false;
    }
    if (cBajo >= '0' && cBajo <= '9') {
      bajo = uint8_t(cBajo - '0');
    } else if (cBajo >= 'a' && cBajo <= 'f') {
      bajo = uint8_t(cBajo - 'a' + 10);
    } else if (cBajo >= 'A' && cBajo <= 'F') {
      bajo = uint8_t(cBajo - 'A' + 10);
    } else {
      return false;
    }
    salida[i] = uint8_t((alto << 4) | bajo);
  }
  return true;
}

}  // namespace hmac_sha256
