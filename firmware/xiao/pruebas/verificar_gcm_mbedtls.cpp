// Verifica el cifrado de la respuesta (AES-256-GCM de mbedTLS, la misma API que
// usa captura.cpp) contra los vectores de vectores_gcm.h, en el host y SIN el
// hardware del ESP32. Es opcional porque necesita las fuentes de mbedTLS (las
// del ESP-IDF, mbedtls 3.6.x, sirven): la ruta va en M. Desde firmware/xiao/:
//
//   M=<ruta>/esp-idf/components/mbedtls/mbedtls
//   d=$(mktemp -d)
//   for f in gcm aes cipher cipher_wrap platform_util constant_time platform aesni aesce; do gcc -c -O1 -DMBEDTLS_CONFIG_FILE='"gcm_mbedtls_cfg.h"' -Ipruebas -I$M/include -I$M/library $M/library/$f.c -o $d/$f.o; done
//   g++ -std=c++17 -Wall -Wextra -Werror -DMBEDTLS_CONFIG_FILE='"gcm_mbedtls_cfg.h"' -I. -Ipruebas -I$M/include pruebas/verificar_gcm_mbedtls.cpp logica_camara.cpp hmac_sha256.cpp $d/*.o -o $d/verificar_gcm
//   $d/verificar_gcm   # termina con "N verificaciones, 0 fallos" y código 0
//
// Prueba la misma secuencia que captura.cpp: K derivada con logica_camara, AAD
// construido con logica_camara, cifrado EN SITIO (entrada = salida) con la
// etiqueta escrita detrás del texto cifrado, y el descifrado con verificación.
#include <stdio.h>
#include <string.h>

#include "mbedtls/gcm.h"

#include "../hmac_sha256.h"
#include "../logica_camara.h"
#include "../vectores_gcm.h"

using namespace logica_camara;

static int fallos = 0;
static int total = 0;

#define VERIFICAR(cond)                                                   \
  do {                                                                    \
    ++total;                                                              \
    if (!(cond)) {                                                        \
      ++fallos;                                                           \
      printf("FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
    }                                                                     \
  } while (0)

// Lo que hace captura.cpp: cifra en sitio `buf` (texto de `largo` bytes, con
// sitio para la etiqueta detrás) con K derivada del secreto.
static int cifrarEnSitio(const char* secreto, int64_t ts, uint64_t nonce, const uint8_t* iv, uint8_t* buf,
                         size_t largo) {
  uint8_t k[kLongitudClaveRespuesta];
  derivarClaveRespuesta(secreto, strlen(secreto), k);
  char aad[kLongitudAadRespuestaMax];
  const size_t largoAad = construirAadRespuesta(aad, sizeof aad, ts, nonce);
  mbedtls_gcm_context ctx;
  mbedtls_gcm_init(&ctx);
  int r = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, k, 256);
  if (r == 0) {
    r = mbedtls_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT, largo, iv, kLongitudIv,
                                  reinterpret_cast<const unsigned char*>(aad), largoAad, buf, buf,
                                  kLongitudEtiquetaGcm, buf + largo);
  }
  mbedtls_gcm_free(&ctx);
  return r;
}

static int descifrar(const char* secreto, int64_t ts, uint64_t nonce, const uint8_t* iv, const uint8_t* salida,
                     size_t largo, uint8_t* texto) {
  uint8_t k[kLongitudClaveRespuesta];
  derivarClaveRespuesta(secreto, strlen(secreto), k);
  char aad[kLongitudAadRespuestaMax];
  const size_t largoAad = construirAadRespuesta(aad, sizeof aad, ts, nonce);
  mbedtls_gcm_context ctx;
  mbedtls_gcm_init(&ctx);
  int r = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, k, 256);
  if (r == 0) {
    r = mbedtls_gcm_auth_decrypt(&ctx, largo, iv, kLongitudIv, reinterpret_cast<const unsigned char*>(aad),
                                 largoAad, salida + largo, kLongitudEtiquetaGcm, salida, texto);
  }
  mbedtls_gcm_free(&ctx);
  return r;
}

static void vector_1() {
  uint8_t buf[sizeof vectores_gcm::kTexto1 + kLongitudEtiquetaGcm];
  memcpy(buf, vectores_gcm::kTexto1, sizeof vectores_gcm::kTexto1);
  VERIFICAR(cifrarEnSitio(vectores_gcm::kSecreto, vectores_gcm::kTs1, vectores_gcm::kNonce1, vectores_gcm::kIv,
                          buf, sizeof vectores_gcm::kTexto1) == 0);
  VERIFICAR(memcmp(buf, vectores_gcm::kSalida1, sizeof buf) == 0);
  // El descifrado devuelve el texto original.
  uint8_t texto[sizeof vectores_gcm::kTexto1];
  VERIFICAR(descifrar(vectores_gcm::kSecreto, vectores_gcm::kTs1, vectores_gcm::kNonce1, vectores_gcm::kIv, buf,
                      sizeof texto, texto) == 0);
  VERIFICAR(memcmp(texto, vectores_gcm::kTexto1, sizeof texto) == 0);
  // Un byte del texto cifrado cambiado, otra petición (AAD) o otra clave: no autentica.
  uint8_t malo[sizeof buf];
  memcpy(malo, buf, sizeof buf);
  malo[5] ^= 1;
  VERIFICAR(descifrar(vectores_gcm::kSecreto, vectores_gcm::kTs1, vectores_gcm::kNonce1, vectores_gcm::kIv, malo,
                      sizeof texto, texto) != 0);
  VERIFICAR(descifrar(vectores_gcm::kSecreto, vectores_gcm::kTs1, vectores_gcm::kNonce1 + 1, vectores_gcm::kIv, buf,
                      sizeof texto, texto) != 0);
  VERIFICAR(descifrar(vectores_gcm::kSecreto, vectores_gcm::kTs1 + 1, vectores_gcm::kNonce1, vectores_gcm::kIv, buf,
                      sizeof texto, texto) != 0);
  VERIFICAR(descifrar("otro-secreto-distinto-de-32-caracteres!", vectores_gcm::kTs1, vectores_gcm::kNonce1,
                      vectores_gcm::kIv, buf, sizeof texto, texto) != 0);
  // La etiqueta cambiada.
  memcpy(malo, buf, sizeof buf);
  malo[sizeof buf - 1] ^= 0x80;
  VERIFICAR(descifrar(vectores_gcm::kSecreto, vectores_gcm::kTs1, vectores_gcm::kNonce1, vectores_gcm::kIv, malo,
                      sizeof texto, texto) != 0);
}

static void vector_2_texto_vacio() {
  uint8_t buf[kLongitudEtiquetaGcm];
  VERIFICAR(cifrarEnSitio(vectores_gcm::kSecreto, vectores_gcm::kTs2, vectores_gcm::kNonce2, vectores_gcm::kIv, buf,
                          0) == 0);
  VERIFICAR(memcmp(buf, vectores_gcm::kSalida2, sizeof buf) == 0);
}

static void texto_grande_viaja_de_ida_y_vuelta() {
  // Un tamaño que no es múltiplo de 16 (como una JPEG real) y mayor que un bloque de DMA.
  const size_t largo = 100003;
  uint8_t* original = new uint8_t[largo];
  uint8_t* buf = new uint8_t[largo + kLongitudEtiquetaGcm];
  uint8_t* texto = new uint8_t[largo];
  for (size_t i = 0; i < largo; ++i) original[i] = static_cast<uint8_t>(i * 131 + (i >> 8));
  memcpy(buf, original, largo);
  VERIFICAR(cifrarEnSitio(vectores_gcm::kSecreto, 1767225600, 7, vectores_gcm::kIv, buf, largo) == 0);
  VERIFICAR(memcmp(buf, original, largo) != 0);
  VERIFICAR(descifrar(vectores_gcm::kSecreto, 1767225600, 7, vectores_gcm::kIv, buf, largo, texto) == 0);
  VERIFICAR(memcmp(texto, original, largo) == 0);
  delete[] original;
  delete[] buf;
  delete[] texto;
}

int main() {
  vector_1();
  vector_2_texto_vacio();
  texto_grande_viaja_de_ida_y_vuelta();
  printf("%d verificaciones, %d fallos\n", total, fallos);
  return fallos == 0 ? 0 : 1;
}
