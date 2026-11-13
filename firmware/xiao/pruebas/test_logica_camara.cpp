// Pruebas de host de logica_camara (sin dependencias). Compilar y correr desde
// firmware/xiao/ con los comandos del README, sección "Pruebas de host".
//
// La firma se verifica con los mismos vectores que usa el servicio de visión
// (orangepi/vision/tests/test_hmac_sha256.cpp): el HMAC de la cadena canónica
// "GET\n/capture\n<ts>\n<nonce>" con el secreto "clave-de-prueba".
//
// Este archivo y pruebas/test_hmac_sha256.cpp se pueden compilar por separado
// (uno por módulo) o juntos en un solo binario, como hace el README. Para que
// valgan las dos formas, los contadores y el main son "débiles" (weak): con
// varios archivos, el enlazador conserva una sola definición de cada uno, y el
// main que sobrevive llama también a la suite del otro archivo si existe.
#include <stdio.h>
#include <string.h>

#include "../hmac_sha256.h"
#include "../logica_camara.h"
#include "../vectores_gcm.h"

using namespace logica_camara;

// Contadores compartidos con el otro archivo de pruebas (ver la nota de
// arriba): el enlazador los une cuando los dos archivos se compilan juntos.
int g_verificaciones __attribute__((weak));
int g_fallos __attribute__((weak));

// La suite del otro archivo, si este se compila con el.
extern void suiteHmacSha256() __attribute__((weak));

#define VERIFICAR(cond)                                                   \
  do {                                                                    \
    ++g_verificaciones;                                                   \
    if (!(cond)) {                                                        \
      ++g_fallos;                                                         \
      printf("FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
    }                                                                     \
  } while (0)

static const char* kSecreto = "clave-de-prueba";

// La firma que el borde pondría en X-Captura-HMAC para un ts y un nonce dados.
static void firmaEsperada(uint64_t ts, uint64_t nonce, char* salida) {
  char canonica[128];
  const size_t largo = construirCadenaCanonica(canonica, sizeof canonica, kMetodo, kCamino, ts, nonce);
  uint8_t digest[hmac_sha256::kLongitudHash];
  hmac_sha256::hmacSha256(reinterpret_cast<const uint8_t*>(kSecreto), strlen(kSecreto),
                           reinterpret_cast<const uint8_t*>(canonica), largo, digest);
  hmac_sha256::aHex(digest, sizeof digest, salida);
}

// --- La cadena canónica ------------------------------------------------------
static void cadena_canonica() {
  char canonica[128];

  // Exactamente la cadena que firma el borde: método, camino sin query, ts y
  // nonce, separados por saltos de línea y sin salto final.
  size_t largo = construirCadenaCanonica(canonica, sizeof canonica, "GET", "/capture", 1762344000, 42);
  VERIFICAR(largo == strlen("GET\n/capture\n1762344000\n42"));
  VERIFICAR(strcmp(canonica, "GET\n/capture\n1762344000\n42") == 0);
  VERIFICAR(canonica[largo - 1] != '\n');

  // La query NO se firma: el camino se corta en el "?".
  char conQuery[128];
  construirCadenaCanonica(conQuery, sizeof conQuery, "GET", "/capture?res=UXGA", 1762344000, 42);
  VERIFICAR(strcmp(conQuery, canonica) == 0);

  // Un fragmento "#" tampoco se firma.
  char conFragmento[128];
  construirCadenaCanonica(conFragmento, sizeof conFragmento, "GET", "/capture#x", 1762344000, 42);
  VERIFICAR(strcmp(conFragmento, canonica) == 0);

  // El nonce se serializa en decimal sin ceros a la izquierda.
  char nonceLargo[128];
  construirCadenaCanonica(nonceLargo, sizeof nonceLargo, "GET", "/capture", 1762344000,
                           12345678901234567890ULL);
  VERIFICAR(strcmp(nonceLargo, "GET\n/capture\n1762344000\n12345678901234567890") == 0);

  // Un ts de 0 también es un decimal válido.
  char tsCero[128];
  construirCadenaCanonica(tsCero, sizeof tsCero, "GET", "/capture", 0, 0);
  VERIFICAR(strcmp(tsCero, "GET\n/capture\n0\n0") == 0);

  // Si no cabe, no se escribe nada.
  char pequeno[8];
  VERIFICAR(construirCadenaCanonica(pequeno, sizeof pequeno, "GET", "/capture", 1762344000, 42) == 0);
  VERIFICAR(construirCadenaCanonica(nullptr, 128, "GET", "/capture", 1762344000, 42) == 0);
}

// --- La validación de una petición bien firmada ------------------------------
static void peticion_valida() {
  Validador validador(kSecreto, strlen(kSecreto));

  char firma[hmac_sha256::kLongitudHex];
  firmaEsperada(1762344000, 42, firma);

  Peticion peticion = {"1762344000", "42", firma};
  VERIFICAR(validador.validar(peticion) == Resultado::Aceptada);
  VERIFICAR(codigoHttp(Resultado::Aceptada) == 200);
  VERIFICAR(validador.hayUltimoTs());
  VERIFICAR(validador.ultimoTs() == 1762344000);

  // Con un nonce de 64 bits.
  char firmaLarga[hmac_sha256::kLongitudHex];
  firmaEsperada(1762344000, 12345678901234567890ULL, firmaLarga);
  Peticion peticionLarga = {"1762344000", "12345678901234567890", firmaLarga};
  VERIFICAR(validador.validar(peticionLarga) == Resultado::Aceptada);
}

// --- Las cabeceras ausentes --------------------------------------------------
static void cabeceras_ausentes() {
  Validador validador(kSecreto, strlen(kSecreto));
  char firma[hmac_sha256::kLongitudHex];
  firmaEsperada(1762344000, 42, firma);

  // Falta cada una de las tres: 401, no 403.
  Peticion sinTs = {nullptr, "42", firma};
  VERIFICAR(validador.validar(sinTs) == Resultado::CabecerasFaltantes);
  VERIFICAR(codigoHttp(Resultado::CabecerasFaltantes) == 401);

  Peticion sinNonce = {"1762344000", nullptr, firma};
  VERIFICAR(validador.validar(sinNonce) == Resultado::CabecerasFaltantes);

  Peticion sinFirma = {"1762344000", "42", nullptr};
  VERIFICAR(validador.validar(sinFirma) == Resultado::CabecerasFaltantes);

  // Una cabecera presente pero vacía también cuenta como ausente.
  Peticion vacia = {"", "42", firma};
  VERIFICAR(validador.validar(vacia) == Resultado::CabecerasFaltantes);

  // Y una que solo tiene espacios.
  Peticion espacios = {"   ", "42", firma};
  VERIFICAR(validador.validar(espacios) == Resultado::CabecerasFaltantes);
}

// --- La firma inválida -------------------------------------------------------
static void firma_invalida() {
  Validador validador(kSecreto, strlen(kSecreto));
  char firma[hmac_sha256::kLongitudHex];
  firmaEsperada(1762344000, 42, firma);

  // Con otro secreto, la firma no cuadra.
  {
    Validador otro("otro-secreto", strlen("otro-secreto"));
    Peticion peticion = {"1762344000", "42", firma};
    VERIFICAR(otro.validar(peticion) == Resultado::FirmaInvalida);
    VERIFICAR(codigoHttp(Resultado::FirmaInvalida) == 401);
  }

  // Con el ts cambiado, la firma no cuadra.
  {
    Peticion peticion = {"1762344001", "42", firma};
    VERIFICAR(validador.validar(peticion) == Resultado::FirmaInvalida);
  }

  // Con el nonce cambiado, la firma no cuadra.
  {
    Peticion peticion = {"1762344000", "43", firma};
    VERIFICAR(validador.validar(peticion) == Resultado::FirmaInvalida);
  }

  // Una firma que no son 64 hexes.
  {
    char corrupta[16];
    strcpy(corrupta, "abcd");
    Peticion peticion = {"1762344000", "42", corrupta};
    VERIFICAR(validador.validar(peticion) == Resultado::FirmaInvalida);
  }

  // Un ts que no es un decimal.
  {
    char firmaTs[hmac_sha256::kLongitudHex];
    firmaEsperada(1762344000, 42, firmaTs);
    Peticion peticion = {"no-es-un-numero", "42", firmaTs};
    VERIFICAR(validador.validar(peticion) == Resultado::FirmaInvalida);
  }

  // Un nonce que no es un decimal.
  {
    char firmaNonce[hmac_sha256::kLongitudHex];
    firmaEsperada(1762344000, 42, firmaNonce);
    Peticion peticion = {"1762344000", "42x", firmaNonce};
    VERIFICAR(validador.validar(peticion) == Resultado::FirmaInvalida);
  }
}

// --- Los espacios alrededor del valor de la cabecera -------------------------
// El cliente HTTP deja un espacio tras los dos puntos: el valor llega con
// espacios y la comparación tiene que seguir cuadrando.
static void espacios_alrededor_del_valor() {
  Validador validador(kSecreto, strlen(kSecreto));

  // Cada caso usa un nonce distinto: si se repitiera, saltaría el
  // anti-replay y no se vería que el recorte funciona.
  char firma[hmac_sha256::kLongitudHex];

  // El ts y el nonce con espacios alrededor: se recortan y la firma cuadra.
  firmaEsperada(1762344000, 42, firma);
  Peticion peticion = {" 1762344000 ", " 42 ", firma};
  VERIFICAR(validador.validar(peticion) == Resultado::Aceptada);

  // Un tabulador también se recorta.
  firmaEsperada(1762344000, 43, firma);
  Peticion conTabulador = {"\t1762344000\t", "\t43\t", firma};
  VERIFICAR(validador.validar(conTabulador) == Resultado::Aceptada);

  // La firma con espacios alrededor también vale: se recorta antes de comparar.
  char firmaConEspacios[hmac_sha256::kLongitudHex + 8];
  firmaEsperada(1762344000, 44, firma);
  sprintf(firmaConEspacios, "  %s  ", firma);
  Peticion peticionFirma = {"1762344000", "44", firmaConEspacios};
  VERIFICAR(validador.validar(peticionFirma) == Resultado::Aceptada);
}

// --- El nonce repetido -------------------------------------------------------
static void nonce_repetido() {
  Validador validador(kSecreto, strlen(kSecreto));
  char firma[hmac_sha256::kLongitudHex];
  firmaEsperada(1762344000, 42, firma);

  Peticion peticion = {"1762344000", "42", firma};
  VERIFICAR(validador.validar(peticion) == Resultado::Aceptada);

  // La misma petición otra vez: el nonce ya se vio.
  VERIFICAR(validador.validar(peticion) == Resultado::NonceRepetido);
  VERIFICAR(codigoHttp(Resultado::NonceRepetido) == 403);

  // Con otro ts, el nonce sigue siendo el mismo: también se rechaza.
  char firma2[hmac_sha256::kLongitudHex];
  firmaEsperada(1762344001, 42, firma2);
  Peticion mismoNonce = {"1762344001", "42", firma2};
  VERIFICAR(validador.validar(mismoNonce) == Resultado::NonceRepetido);

  // Con otro nonce, sí pasa.
  char firma3[hmac_sha256::kLongitudHex];
  firmaEsperada(1762344002, 43, firma3);
  Peticion otroNonce = {"1762344002", "43", firma3};
  VERIFICAR(validador.validar(otroNonce) == Resultado::Aceptada);
}

// --- El ts más antiguo que la ventana de frescura ------------------------------
static void ts_antiguo() {
  Validador validador(kSecreto, strlen(kSecreto));

  char firma[hmac_sha256::kLongitudHex];
  firmaEsperada(1762344000, 42, firma);
  Peticion peticion = {"1762344000", "42", firma};
  VERIFICAR(validador.validar(peticion) == Resultado::Aceptada);

  // Un ts 299 s más antiguo está dentro de la ventana de 300 s: se acepta.
  char firmaVieja[hmac_sha256::kLongitudHex];
  firmaEsperada(1762344000 - 299, 43, firmaVieja);
  Peticion dentroDeVentana = {"1762343701", "43", firmaVieja};
  VERIFICAR(validador.validar(dentroDeVentana) == Resultado::Aceptada);

  // Un ts 301 s más antiguo está fuera: se rechaza con 403.
  char firmaMuyVieja[hmac_sha256::kLongitudHex];
  firmaEsperada(1762344000 - 301, 44, firmaMuyVieja);
  Peticion fueraDeVentana = {"1762343699", "44", firmaMuyVieja};
  VERIFICAR(validador.validar(fueraDeVentana) == Resultado::TimestampAntiguo);
  VERIFICAR(codigoHttp(Resultado::TimestampAntiguo) == 403);

  // Un ts más nuevo que el último se acepta y actualiza la marca.
  char firmaNueva[hmac_sha256::kLongitudHex];
  firmaEsperada(1762345000, 45, firmaNueva);
  Peticion masNuevo = {"1762345000", "45", firmaNueva};
  VERIFICAR(validador.validar(masNuevo) == Resultado::Aceptada);
  VERIFICAR(validador.ultimoTs() == 1762345000);

  // Y ahora un ts que es antiguo respecto de esa marca nueva: se rechaza.
  char firmaMuyVieja2[hmac_sha256::kLongitudHex];
  firmaEsperada(1762344000 - 400, 46, firmaMuyVieja2);
  Peticion antigua = {"1762343600", "46", firmaMuyVieja2};
  VERIFICAR(validador.validar(antigua) == Resultado::TimestampAntiguo);
}

// --- La ventana de los nonces y el piso de ts ------------------------------------
// Se recuerdan los últimos 64: el nonce número 65 expulsa al primero. Pero su
// ts pasa a ser un piso: reenviar la petición expulsada ya no sirve (antes se
// aceptaba, porque el nonce se había olvidado).
static Resultado enviar(Validador& v, uint64_t ts, uint64_t nonce) {
  char firma[hmac_sha256::kLongitudHex];
  firmaEsperada(ts, nonce, firma);
  char ts_texto[24];
  char nonce_texto[24];
  sprintf(ts_texto, "%llu", (unsigned long long)ts);
  sprintf(nonce_texto, "%llu", (unsigned long long)nonce);
  Peticion peticion = {ts_texto, nonce_texto, firma};
  return v.validar(peticion);
}

static void ventana_de_nonces() {
  Validador validador(kSecreto, strlen(kSecreto));
  const uint64_t ts = 1762344000;
  VERIFICAR(!validador.hayPiso());
  for (uint64_t i = 0; i < 64; ++i) {
    VERIFICAR(enviar(validador, ts + i, 100 + i) == Resultado::Aceptada);
  }
  VERIFICAR(!validador.hayPiso());  // el anillo aún no expulsó a nadie
  // La 65 expulsa a la primera (ts, 100): su ts es el piso.
  VERIFICAR(enviar(validador, ts + 64, 164) == Resultado::Aceptada);
  VERIFICAR(validador.hayPiso());
  VERIFICAR(validador.piso() == (int64_t)ts);

  // Reenviar la expulsada: el nonce se olvidó, pero el ts está en el piso.
  VERIFICAR(enviar(validador, ts, 100) == Resultado::TimestampAntiguo);
  // Otra petición con un ts viejo (por debajo del piso) y un nonce nuevo, tampoco.
  VERIFICAR(enviar(validador, ts - 5, 9999) == Resultado::TimestampAntiguo);
  // Lo que sigue dentro del anillo sigue siendo un nonce repetido.
  VERIFICAR(enviar(validador, ts + 63, 163) == Resultado::NonceRepetido);
  // Una petición nueva con ts por encima del piso pasa.
  VERIFICAR(enviar(validador, ts + 65, 165) == Resultado::Aceptada);
  // Y el piso sube con cada expulsión: ahora la segunda (ts+1).
  VERIFICAR(validador.piso() == (int64_t)ts + 1);
  VERIFICAR(enviar(validador, ts + 1, 101) == Resultado::TimestampAntiguo);
}

static void el_piso_del_arranque() {
  Validador validador(kSecreto, strlen(kSecreto));
  validador.establecerPiso(1762344000);  // el ts guardado en la flash
  VERIFICAR(validador.hayPiso());
  VERIFICAR(enviar(validador, 1762344000, 1) == Resultado::TimestampAntiguo);  // el piso mismo
  VERIFICAR(enviar(validador, 1762343000, 2) == Resultado::TimestampAntiguo);
  VERIFICAR(enviar(validador, 1762344001, 3) == Resultado::Aceptada);
  // El piso no baja nunca.
  validador.establecerPiso(10);
  VERIFICAR(validador.piso() == 1762344000);
  validador.establecerPiso(1762344500);
  VERIFICAR(validador.piso() == 1762344500);
  VERIFICAR(enviar(validador, 1762344400, 4) == Resultado::TimestampAntiguo);
  // Un reinicio de pruebas lo borra.
  validador.reiniciar();
  VERIFICAR(!validador.hayPiso());
  VERIFICAR(enviar(validador, 1762344000, 1) == Resultado::Aceptada);
}

static void el_ts_y_el_nonce_aceptados_salen_por_los_punteros() {
  Validador validador(kSecreto, strlen(kSecreto));
  char firma[hmac_sha256::kLongitudHex];
  firmaEsperada(1762344000, 77, firma);
  Peticion peticion = {"1762344000", "77", firma};
  int64_t ts = 0;
  uint64_t nonce = 0;
  VERIFICAR(validador.validar(peticion, &ts, &nonce) == Resultado::Aceptada);
  VERIFICAR(ts == 1762344000 && nonce == 77);
  // Si no se acepta, no se tocan.
  ts = -1;
  nonce = 5;
  VERIFICAR(validador.validar(peticion, &ts, &nonce) == Resultado::NonceRepetido);
  VERIFICAR(ts == -1 && nonce == 5);
}

// --- El cifrado de la respuesta: K, AAD e IV -----------------------------------------
// El cifrado en sí (AES-256-GCM de mbedTLS) no se puede enlazar aquí sin
// dependencias: lo cubre el autodiagnóstico de la placa (captura.cpp) y el
// programa pruebas/verificar_gcm_mbedtls.c. Aquí va todo lo puro: la derivación
// de K y el AAD, con los vectores de vectores_gcm.h (generados con Python).
static void la_clave_de_la_respuesta_sale_del_secreto() {
  uint8_t k[kLongitudClaveRespuesta];
  derivarClaveRespuesta(vectores_gcm::kSecreto, strlen(vectores_gcm::kSecreto), k);
  VERIFICAR(memcmp(k, vectores_gcm::kClave, sizeof k) == 0);
  char hex[hmac_sha256::kLongitudHex];
  hmac_sha256::aHex(k, sizeof k, hex);
  VERIFICAR(strcmp(hex, vectores_gcm::kClaveHex) == 0);
  VERIFICAR(strcmp(kContextoClaveRespuesta, "AUTOLIST-CAPTURA-RESP-AES256GCM-V1") == 0);

  // Otro secreto da otra clave; el mismo secreto, la misma.
  uint8_t otra[kLongitudClaveRespuesta];
  derivarClaveRespuesta("otro-secreto-distinto-de-32-caracteres!", strlen("otro-secreto-distinto-de-32-caracteres!"), otra);
  VERIFICAR(memcmp(k, otra, sizeof k) != 0);
  uint8_t igual[kLongitudClaveRespuesta];
  derivarClaveRespuesta(vectores_gcm::kSecreto, strlen(vectores_gcm::kSecreto), igual);
  VERIFICAR(memcmp(k, igual, sizeof k) == 0);
  // La clave de la respuesta no es la firma de la petición (otro mensaje).
  uint8_t peticion[hmac_sha256::kLongitudHash];
  hmac_sha256::hmacSha256(reinterpret_cast<const uint8_t*>(vectores_gcm::kSecreto), strlen(vectores_gcm::kSecreto),
                          reinterpret_cast<const uint8_t*>("GET\n/capture\n1\n1"), 16, peticion);
  VERIFICAR(memcmp(k, peticion, sizeof k) != 0);
}

static void el_aad_de_la_respuesta() {
  char aad[kLongitudAadRespuestaMax];
  size_t largo = construirAadRespuesta(aad, sizeof aad, vectores_gcm::kTs1, vectores_gcm::kNonce1);
  VERIFICAR(largo == strlen(vectores_gcm::kAad1));
  VERIFICAR(strcmp(aad, vectores_gcm::kAad1) == 0);
  VERIFICAR(strcmp(aad, "RESP\n1767225600\n42") == 0);
  VERIFICAR(aad[largo - 1] != '\n');  // sin salto final
  largo = construirAadRespuesta(aad, sizeof aad, vectores_gcm::kTs2, vectores_gcm::kNonce2);
  VERIFICAR(strcmp(aad, vectores_gcm::kAad2) == 0 && largo == 8);
  // Nonce de 64 bits en decimal sin ceros a la izquierda.
  construirAadRespuesta(aad, sizeof aad, 1762344000, 12345678901234567890ULL);
  VERIFICAR(strcmp(aad, "RESP\n1762344000\n12345678901234567890") == 0);
  // El peor caso cabe en el búfer; no cabe en uno corto.
  VERIFICAR(construirAadRespuesta(aad, sizeof aad, INT64_MIN, UINT64_MAX) > 0);
  VERIFICAR(construirAadRespuesta(aad, 10, 1762344000, 42) == 0);
  VERIFICAR(construirAadRespuesta(nullptr, 100, 1, 1) == 0);
  // Otro ts u otro nonce, otro AAD (ata la respuesta a la petición).
  char otro[kLongitudAadRespuestaMax];
  construirAadRespuesta(otro, sizeof otro, vectores_gcm::kTs1, vectores_gcm::kNonce1 + 1);
  VERIFICAR(strcmp(otro, vectores_gcm::kAad1) != 0);
  construirAadRespuesta(otro, sizeof otro, vectores_gcm::kTs1 + 1, vectores_gcm::kNonce1);
  VERIFICAR(strcmp(otro, vectores_gcm::kAad1) != 0);
}

static void el_iv_va_en_24_hex() {
  char hex[kLongitudIvHex + 1];
  ivAHex(vectores_gcm::kIv, hex);
  VERIFICAR(strlen(hex) == 24);
  VERIFICAR(strcmp(hex, vectores_gcm::kIvHex) == 0);
  const uint8_t otro[kLongitudIv] = {0xff, 0xfe, 0x00, 0x10, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89};
  ivAHex(otro, hex);
  VERIFICAR(strcmp(hex, "fffe0010abcdef0123456789") == 0);  // minúsculas
}

static void los_tamanos_del_cifrado() {
  VERIFICAR(kLongitudClaveRespuesta == 32);  // AES-256
  VERIFICAR(kLongitudIv == 12);
  VERIFICAR(kLongitudEtiquetaGcm == 16);
  VERIFICAR(strcmp(kCabeceraIv, "X-Captura-IV") == 0);
  // El texto cifrado mide lo mismo que el texto y la etiqueta va detrás.
  VERIFICAR(sizeof vectores_gcm::kSalida1 == sizeof vectores_gcm::kTexto1 + kLongitudEtiquetaGcm);
  VERIFICAR(sizeof vectores_gcm::kSalida2 == kLongitudEtiquetaGcm);
}

// --- El ts de una petición no puede adelantarse sin límite -----------------------------
static Resultado enviarConReloj(Validador& v, uint64_t ahora_ms, uint64_t ts, uint64_t nonce) {
  char firma[hmac_sha256::kLongitudHex];
  firmaEsperada(ts, nonce, firma);
  char ts_texto[24];
  char nonce_texto[24];
  sprintf(ts_texto, "%llu", (unsigned long long)ts);
  sprintf(nonce_texto, "%llu", (unsigned long long)nonce);
  Peticion peticion = {ts_texto, nonce_texto, firma};
  return v.validar(peticion, ahora_ms);
}

static void el_adelanto_del_ts_se_limita() {
  Validador v(kSecreto, strlen(kSecreto));
  const uint64_t ts = 1767225600;
  // La primera no tiene referencia: se acepta (a las 0 ms de la placa).
  VERIFICAR(enviarConReloj(v, 1000, ts, 1) == Resultado::Aceptada);
  // 600 s de adelanto con 0 s transcurridos: justo el margen.
  VERIFICAR(enviarConReloj(v, 1000, ts + 600, 2) == Resultado::Aceptada);
  // La referencia es ahora ts + 600 aceptado a los 1000 ms: 601 más ya no.
  VERIFICAR(enviarConReloj(v, 1000, ts + 600 + 601, 3) == Resultado::TimestampFuturo);
  VERIFICAR(codigoHttp(Resultado::TimestampFuturo) == 403);
  VERIFICAR(strcmp(aTexto(Resultado::TimestampFuturo), "timestamp futuro") == 0);
  // Un rechazo no mueve nada: ultimoTs sigue en ts + 600.
  VERIFICAR(v.ultimoTs() == (int64_t)ts + 600);
  // Con el tiempo que pasa en la placa el margen crece: 100 s después caben 700.
  VERIFICAR(enviarConReloj(v, 101000, ts + 600 + 700, 4) == Resultado::Aceptada);
  VERIFICAR(enviarConReloj(v, 101000, ts + 1300 + 701, 5) == Resultado::TimestampFuturo);
}

static void una_pausa_larga_legitima_no_se_rechaza() {
  Validador v(kSecreto, strlen(kSecreto));
  const uint64_t ts = 1767225600;
  VERIFICAR(enviarConReloj(v, 0, ts, 1) == Resultado::Aceptada);
  // 60 días después (más de 2^32 ms: el contador de 64 bits no da la vuelta) el
  // ts avanzó lo mismo que el tiempo real.
  const uint64_t dias60_s = 60ull * 86400;
  VERIFICAR(enviarConReloj(v, dias60_s * 1000, ts + dias60_s, 2) == Resultado::Aceptada);
  VERIFICAR(enviarConReloj(v, dias60_s * 1000, ts + dias60_s + 2000, 3) == Resultado::TimestampFuturo);
}

static void un_ts_absurdo_no_deja_la_camara_bloqueada() {
  Validador v(kSecreto, strlen(kSecreto));
  const uint64_t ts = 1767225600;
  VERIFICAR(enviarConReloj(v, 0, ts, 1) == Resultado::Aceptada);
  // Un ts de 10 años después, con la placa recién llegada de ese ts: rechazado.
  const uint64_t absurdo = ts + 10ull * 365 * 86400;
  VERIFICAR(enviarConReloj(v, 5000, absurdo, 2) == Resultado::TimestampFuturo);
  // Y como no se aceptó, las peticiones normales siguen pasando.
  VERIFICAR(enviarConReloj(v, 6000, ts + 5, 3) == Resultado::Aceptada);
}

static void sin_reloj_no_se_limita_el_adelanto() {
  Validador v(kSecreto, strlen(kSecreto));
  VERIFICAR(enviar(v, 1767225600, 1) == Resultado::Aceptada);
  VERIFICAR(enviar(v, 1767225600 + 10ull * 365 * 86400, 2) == Resultado::Aceptada);  // la forma sin reloj
}

static void el_reloj_no_rompe_el_resto() {
  Validador v(kSecreto, strlen(kSecreto));
  const uint64_t ts = 1767225600;
  VERIFICAR(enviarConReloj(v, 0, ts, 1) == Resultado::Aceptada);
  VERIFICAR(enviarConReloj(v, 10, ts, 1) == Resultado::NonceRepetido);
  VERIFICAR(enviarConReloj(v, 20, ts - 301, 2) == Resultado::TimestampAntiguo);
  VERIFICAR(enviarConReloj(v, 30, ts - 10, 3) == Resultado::Aceptada);  // dentro de la ventana
  int64_t t = 0;
  uint64_t n = 0;
  char firma[hmac_sha256::kLongitudHex];
  firmaEsperada(ts + 5, 9, firma);
  Peticion p = {"1767225605", "9", firma};
  VERIFICAR(v.validar(p, (uint64_t)40, &t, &n) == Resultado::Aceptada);
  VERIFICAR(t == (int64_t)ts + 5 && n == 9);
}

// --- El piso guardado en la flash --------------------------------------------------------
static const uint64_t kS = 1000;  // ms por segundo

static void el_ritmo_de_escritura_del_piso() {
  VERIFICAR(kPasoPersistenciaTsS == 60);
  VERIFICAR(kIntervaloEscrituraTsMs == 60000);
  VERIFICAR(kAvanceMaxPersistenciaS == 86400);
  PersistenciaTs p;
  const int64_t s0 = 1767225600;
  p.arrancar(true, s0, 10 * kS);
  VERIFICAR(p.hayGuardado() && p.guardado() == s0);
  VERIFICAR(!p.debeEscribir(20 * kS, s0));          // igual
  VERIFICAR(!p.debeEscribir(20 * kS, s0 - 5));      // menor: nunca
  VERIFICAR(!p.debeEscribir(20 * kS, s0 + 59));     // 59 s de avance: no
  VERIFICAR(p.debeEscribir(20 * kS, s0 + 60));      // 60 s: sí
  VERIFICAR(p.valor(20 * kS, s0 + 3600) == s0 + 3600);  // un avance normal pasa entero
  p.registrarEscritura(20 * kS, s0 + 60);
  VERIFICAR(p.guardado() == s0 + 60);
  // Ni dos escrituras con menos de 60 s de tiempo de placa entre ellas,
  // aunque el ts avance mucho.
  VERIFICAR(!p.debeEscribir(79 * kS, s0 + 5000));
  VERIFICAR(p.debeEscribir(80 * kS, s0 + 5000));
  // Sin nada guardado, se escribe la primera vez sin más.
  PersistenciaTs q;
  q.arrancar(false, 0, 0);
  VERIFICAR(!q.hayGuardado());
  VERIFICAR(q.debeEscribir(1000, s0));
  VERIFICAR(q.valor(1000, s0) == s0);
}

static void la_primera_escritura_de_un_arranque_sube_como_mucho_un_dia() {
  PersistenciaTs p;
  const int64_t s0 = 1767225600;
  p.arrancar(true, s0, 0);
  // Una petición a 10 años vista: lo guardado es stored0 + 24 h, no el ts.
  const int64_t absurdo = s0 + 10ll * 365 * 86400;
  VERIFICAR(p.debeEscribir(5 * kS, absurdo));
  VERIFICAR(p.valor(5 * kS, absurdo) == s0 + 86400);
  // Un avance de justo un día pasa entero; un segundo más se recorta.
  VERIFICAR(p.valor(5 * kS, s0 + 86400) == s0 + 86400);
  VERIFICAR(p.valor(5 * kS, s0 + 86401) == s0 + 86400);
}

// Lo que importa: por muchas peticiones aceptadas que lleguen en un arranque, lo
// guardado nunca pasa de stored0 + 86400 + los segundos que lleva encendida la placa.
static void un_arranque_no_mueve_el_piso_mas_de_un_dia_mas_su_tiempo() {
  PersistenciaTs p;
  const int64_t s0 = 1767225600;
  p.arrancar(true, s0, 0);
  const int64_t absurdo = s0 + 10ll * 365 * 86400;
  int escrituras = 0;
  uint64_t ultima = 0;
  // 3 horas de uptime con una petición aceptada cada 7 s, todas a años vista.
  for (uint64_t t = 5 * kS; t <= 3 * 3600 * kS; t += 7 * kS) {
    const int64_t ts = absurdo + static_cast<int64_t>(t / kS);
    if (p.debeEscribir(t, ts)) {
      const int64_t v = p.valor(t, ts);
      if (escrituras > 0) VERIFICAR(t - ultima >= kIntervaloEscrituraTsMs);  // 1 por minuto
      p.registrarEscritura(t, v);
      ultima = t;
      ++escrituras;
    }
    const int64_t tope = s0 + 86400 + static_cast<int64_t>(t / kS);
    VERIFICAR(p.guardado() <= tope);
    if (p.guardado() > tope) break;
  }
  VERIFICAR(escrituras > 100);            // sí avanza con el tiempo (una por minuto)
  VERIFICAR(escrituras <= 3 * 60 + 1);    // y no más de una por minuto
  VERIFICAR(p.guardado() > s0 + 86400);   // más de lo de la primera escritura
  VERIFICAR(p.guardado() <= s0 + 86400 + 3 * 3600);
}

static void sin_guardado_previo_el_tope_corre_desde_la_primera_escritura() {
  PersistenciaTs p;
  p.arrancar(false, 0, 0);
  const int64_t primero = 1767225600 + 10ll * 365 * 86400;  // confianza en el primer uso
  VERIFICAR(p.debeEscribir(1000, primero));
  p.registrarEscritura(1000, primero);
  // Las siguientes ya van acotadas desde ahí: 24 h + el tiempo desde la primera escritura.
  VERIFICAR(p.valor(100 * kS, primero + 10ll * 365 * 86400) == primero + 86400 + 99);
}

static void una_pausa_larga_legitima_converge_en_varios_arranques() {
  const int64_t s0 = 1767225600;
  const int64_t real0 = s0 + 5ll * 86400 + 123;  // la placa estuvo apagada 5 días
  int64_t guardado = s0;
  int arranques = 0;
  while (arranques < 10) {
    ++arranques;
    PersistenciaTs p;
    p.arrancar(true, guardado, 0);
    // Cada arranque: unas cuantas peticiones legítimas en sus primeros minutos.
    for (uint64_t t = 5 * kS; t <= 300 * kS; t += 10 * kS) {
      const int64_t ts = real0 + static_cast<int64_t>(t / kS);
      if (p.debeEscribir(t, ts)) p.registrarEscritura(t, p.valor(t, ts));
    }
    guardado = p.guardado();
    // Nunca pasa de lo real ni del tope del arranque.
    VERIFICAR(guardado <= real0 + 300);
    if (guardado >= real0 - 59) break;  // ya alcanzó (a menos de un paso de 60 s)
  }
  VERIFICAR(arranques == 5);  // unos 5 arranques: uno por día de pausa (el tiempo encendida de cada uno adelanta un poco)
  VERIFICAR(guardado >= real0 - 59);
  // Una pausa corta (menos de un día) se alcanza en el primer arranque.
  PersistenciaTs q;
  q.arrancar(true, s0, 0);
  const int64_t real1 = s0 + 7200;
  VERIFICAR(q.debeEscribir(5 * kS, real1));
  q.registrarEscritura(5 * kS, q.valor(5 * kS, real1));
  VERIFICAR(q.guardado() == real1);
}

static void el_piso_del_arranque_es_el_guardado_mas_60_s() {
  VERIFICAR(pisoDeArranque(1767225600) == 1767225660);
  VERIFICAR(pisoDeArranque(INT64_MAX - 5) == INT64_MAX);
  // Todo lo aceptado antes de un reinicio queda por debajo: el guardado va a
  // lo sumo 59 s por detrás del último aceptado (si no, se habría escrito).
  const int64_t guardado = 1767225600;
  const int64_t ultimoAceptado = guardado + 59;
  PersistenciaTs p;
  p.arrancar(true, guardado, 0);
  VERIFICAR(!p.debeEscribir(100 * kS, ultimoAceptado));
  Validador v(kSecreto, strlen(kSecreto));
  v.establecerPiso(pisoDeArranque(guardado));
  VERIFICAR(enviar(v, (uint64_t)ultimoAceptado, 1) == Resultado::TimestampAntiguo);  // el reenvío
  VERIFICAR(enviar(v, (uint64_t)guardado + 60, 2) == Resultado::TimestampAntiguo);   // el piso mismo
  VERIFICAR(enviar(v, (uint64_t)guardado + 61, 3) == Resultado::Aceptada);
}

// --- Reiniciar y los textos de resultado --------------------------------------
static void reinicio_y_textos() {
  Validador validador(kSecreto, strlen(kSecreto));
  char firma[hmac_sha256::kLongitudHex];
  firmaEsperada(1762344000, 42, firma);

  Peticion peticion = {"1762344000", "42", firma};
  VERIFICAR(validador.validar(peticion) == Resultado::Aceptada);
  VERIFICAR(validador.validar(peticion) == Resultado::NonceRepetido);

  // Tras reiniciar, el nonce se olvida y la marca de frescura también.
  validador.reiniciar();
  VERIFICAR(!validador.hayUltimoTs());
  VERIFICAR(validador.validar(peticion) == Resultado::Aceptada);

  // Cada resultado tiene su texto y su código HTTP.
  VERIFICAR(strcmp(aTexto(Resultado::Aceptada), "aceptada") == 0);
  VERIFICAR(strcmp(aTexto(Resultado::CabecerasFaltantes), "cabeceras faltantes") == 0);
  VERIFICAR(strcmp(aTexto(Resultado::FirmaInvalida), "firma invalida") == 0);
  VERIFICAR(strcmp(aTexto(Resultado::NonceRepetido), "nonce repetido") == 0);
  VERIFICAR(strcmp(aTexto(Resultado::TimestampAntiguo), "timestamp antiguo") == 0);
  VERIFICAR(strcmp(aTexto(Resultado::TimestampFuturo), "timestamp futuro") == 0);
  VERIFICAR(codigoHttp(Resultado::Aceptada) == 200);
  VERIFICAR(codigoHttp(Resultado::CabecerasFaltantes) == 401);
  VERIFICAR(codigoHttp(Resultado::FirmaInvalida) == 401);
  VERIFICAR(codigoHttp(Resultado::NonceRepetido) == 403);
  VERIFICAR(codigoHttp(Resultado::TimestampAntiguo) == 403);
}

void suiteLogicaCamara() {
  cadena_canonica();
  peticion_valida();
  cabeceras_ausentes();
  firma_invalida();
  espacios_alrededor_del_valor();
  nonce_repetido();
  ts_antiguo();
  ventana_de_nonces();
  el_piso_del_arranque();
  el_ts_y_el_nonce_aceptados_salen_por_los_punteros();
  la_clave_de_la_respuesta_sale_del_secreto();
  el_aad_de_la_respuesta();
  el_iv_va_en_24_hex();
  los_tamanos_del_cifrado();
  el_adelanto_del_ts_se_limita();
  una_pausa_larga_legitima_no_se_rechaza();
  un_ts_absurdo_no_deja_la_camara_bloqueada();
  sin_reloj_no_se_limita_el_adelanto();
  el_reloj_no_rompe_el_resto();
  el_ritmo_de_escritura_del_piso();
  la_primera_escritura_de_un_arranque_sube_como_mucho_un_dia();
  un_arranque_no_mueve_el_piso_mas_de_un_dia_mas_su_tiempo();
  sin_guardado_previo_el_tope_corre_desde_la_primera_escritura();
  una_pausa_larga_legitima_converge_en_varios_arranques();
  el_piso_del_arranque_es_el_guardado_mas_60_s();
  reinicio_y_textos();
}

int main() __attribute__((weak));
int main() {
  suiteLogicaCamara();
  if (suiteHmacSha256) {
    suiteHmacSha256();
  }
  printf("%d verificaciones, %d fallos\n", g_verificaciones, g_fallos);
  return g_fallos == 0 ? 0 : 1;
}
