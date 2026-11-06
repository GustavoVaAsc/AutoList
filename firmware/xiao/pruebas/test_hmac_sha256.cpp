// Pruebas de host de hmac_sha256 (sin dependencias). Compilar y correr desde
// firmware/xiao/ con los comandos del README, sección "Pruebas de host".
//
// Los vectores del protocolo son los mismos que usa el servicio de visión
// (orangepi/vision/tests/test_hmac_sha256.cpp): si uno cambia, hay que cambiar
// el otro lado.
//
// Este archivo y pruebas/test_logica_camara.cpp se pueden compilar por
// separado (uno por módulo) o juntos en un solo binario, como hace el README.
// Para que valgan las dos formas, los contadores y el main son "débiles"
// (weak): con varios archivos, el enlazador conserva una sola definición de
// cada uno, y el main que sobrevive llama también a la suite del otro archivo si
// existe.
#include <stdio.h>
#include <string.h>

#include "../hmac_sha256.h"

using namespace hmac_sha256;

// Contadores compartidos con el otro archivo de pruebas (ver la nota de
// arriba): el enlazador los une cuando los dos archivos se compilan juntos.
int g_verificaciones __attribute__((weak));
int g_fallos __attribute__((weak));

// La suite del otro archivo, si este se compila con el.
extern void suiteLogicaCamara() __attribute__((weak));

#define VERIFICAR(cond)                                                   \
  do {                                                                    \
    ++g_verificaciones;                                                   \
    if (!(cond)) {                                                        \
      ++g_fallos;                                                         \
      printf("FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
    }                                                                     \
  } while (0)

// El HMAC de una clave y un mensaje, en hex minúsculas.
static void hmacHex(const char* clave, const char* mensaje, char* salida) {
  uint8_t firma[kLongitudHash];
  hmacSha256(reinterpret_cast<const uint8_t*>(clave), strlen(clave),
             reinterpret_cast<const uint8_t*>(mensaje), strlen(mensaje), firma);
  aHex(firma, sizeof firma, salida);
}

// El SHA-256 de un mensaje, en hex minúsculas.
static void shaHex(const char* mensaje, char* salida) {
  uint8_t digest[kLongitudHash];
  sha256(reinterpret_cast<const uint8_t*>(mensaje), strlen(mensaje), digest);
  aHex(digest, sizeof digest, salida);
}

// --- Vectores del protocolo de captura ---------------------------------------
// Son los de la especificación: la cadena canónica es
// "GET\n/capture\n<ts>\n<nonce>" con el secreto compartido.
static void vectores_del_protocolo() {
  char firma[kLongitudHex];

  hmacHex("clave-de-prueba", "GET\n/capture\n1762344000\n42", firma);
  VERIFICAR(strcmp(firma, "e61d70e6e5c5548dc0690515917c34dbba4f66c3c6dfec255767da6b9f7f9a04") == 0);

  hmacHex("clave-de-prueba", "GET\n/capture\n1762344000\n12345678901234567890", firma);
  VERIFICAR(strcmp(firma, "ee1edce4dd7152da5e421677226666128bef57ef537db6dc886ff76cdac81a9a") == 0);
}

// --- Vectores de referencia de HMAC-SHA256 (RFC 4231) -------------------------
static void vectores_de_referencia() {
  char firma[kLongitudHex];

  // Caso 1: clave de 20 bytes, todo 0x0b.
  uint8_t clave1[20];
  memset(clave1, 0x0b, sizeof clave1);
  uint8_t digest1[kLongitudHash];
  hmacSha256(clave1, sizeof clave1, reinterpret_cast<const uint8_t*>("Hi There"), 8, digest1);
  aHex(digest1, sizeof digest1, firma);
  VERIFICAR(strcmp(firma, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7") == 0);

  // Caso 2: clave "Jefe".
  hmacHex("Jefe", "quien dijo que solo se usa para auditoria?", firma);
  VERIFICAR(strcmp(firma, "4c1a70251663c6ca7dec37361e952bb869d610fe48774ed3ce0089820727f702") == 0);

  // Clave de 128 bytes, todo 0xaa: la clave es más larga que el bloque (64
  // bytes), así que se sustituye por su SHA-256 (RFC 2104).
  uint8_t claveLarga[128];
  memset(claveLarga, 0xaa, sizeof claveLarga);
  const char* mensajeLargo = "Test Using Larger Than Block-Size Key - Hash Key First";
  uint8_t digestLargo[kLongitudHash];
  hmacSha256(claveLarga, sizeof claveLarga,
             reinterpret_cast<const uint8_t*>(mensajeLargo), strlen(mensajeLargo), digestLargo);
  aHex(digestLargo, sizeof digestLargo, firma);
  VERIFICAR(strcmp(firma, "f4c628398866742a99f3e2550d7f6ca135a8995a3940a190d75636a4fe27d788") == 0);
}

// --- SHA-256 y el hex --------------------------------------------------------
static void sha256_y_hex() {
  char hex[kLongitudHex];

  shaHex("", hex);
  VERIFICAR(strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);

  shaHex("abc", hex);
  VERIFICAR(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);

  // El hex es en minúsculas y siempre de 64 caracteres.
  const uint8_t bytes[4] = {0x00, 0x0f, 0xa0, 0xff};
  aHex(bytes, sizeof bytes, hex);
  VERIFICAR(strlen(hex) == 8);
  VERIFICAR(strcmp(hex, "000fa0ff") == 0);
}

// --- La comparación en tiempo constante --------------------------------------
static void comparacion_en_tiempo_constante() {
  const uint8_t a[kLongitudHash] = {0x01, 0x02, 0x03, 0x04};
  const uint8_t b[kLongitudHash] = {0x01, 0x02, 0x03, 0x04};
  const uint8_t c[kLongitudHash] = {0x01, 0x02, 0x03, 0x05};  // un byte distinto
  const uint8_t cAlFinal[kLongitudHash] = {0x00, 0x00, 0x00, 0x04};

  VERIFICAR(compararConstante(a, b, sizeof a));
  VERIFICAR(!compararConstante(a, c, sizeof a));
  // La diferencia al final también se detecta: la comparación no sale antes de
  // tiempo por el contenido.
  VERIFICAR(!compararConstante(a, cAlFinal, sizeof a));
}

// --- La conversión desde hex -------------------------------------------------
static void conversion_desde_hex() {
  uint8_t bytes[kLongitudHash];

  // 64 caracteres hex válidos.
  VERIFICAR(desdeHex("00ff10ab", bytes, 4));
  VERIFICAR(bytes[0] == 0x00);
  VERIFICAR(bytes[1] == 0xff);
  VERIFICAR(bytes[2] == 0x10);
  VERIFICAR(bytes[3] == 0xab);

  // Mayúsculas y minúsculas dan el mismo resultado.
  uint8_t bytes2[4];
  VERIFICAR(desdeHex("00FF10AB", bytes2, 4));
  VERIFICAR(memcmp(bytes, bytes2, 4) == 0);

  // Caracteres que no son hex, y longitudes que no cuadran: se rechazan.
  VERIFICAR(!desdeHex("00ff10ag", bytes, 4));
  VERIFICAR(!desdeHex("00ff10a", bytes, 4));
  VERIFICAR(!desdeHex("00ff10ab0", bytes, 4));
  VERIFICAR(!desdeHex("", bytes, 4));
  VERIFICAR(!desdeHex(nullptr, bytes, 4));
}

void suiteHmacSha256() {
  vectores_del_protocolo();
  vectores_de_referencia();
  sha256_y_hex();
  comparacion_en_tiempo_constante();
  conversion_desde_hex();
}

int main() __attribute__((weak));
int main() {
  suiteHmacSha256();
  if (suiteLogicaCamara) {
    suiteLogicaCamara();
  }
  printf("%d verificaciones, %d fallos\n", g_verificaciones, g_fallos);
  return g_fallos == 0 ? 0 : 1;
}
