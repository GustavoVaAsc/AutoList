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

// --- La ventana de los nonces ------------------------------------------------
// Se recuerdan los últimos 64: el nonce número 65 borra al primero.
static void ventana_de_nonces() {
  Validador validador(kSecreto, strlen(kSecreto));

  char firma[hmac_sha256::kLongitudHex];
  uint64_t ts = 1762344000;
  const uint64_t total = 65;
  uint64_t nonces[65];
  for (uint64_t i = 0; i < total; ++i) {
    nonces[i] = 100 + i;
    firmaEsperada(ts, nonces[i], firma);
    char ts_texto[24];
    sprintf(ts_texto, "%llu", (unsigned long long)ts);
    char nonce_texto[24];
    sprintf(nonce_texto, "%llu", (unsigned long long)nonces[i]);
    Peticion peticion = {ts_texto, nonce_texto, firma};
    VERIFICAR(validador.validar(peticion) == Resultado::Aceptada);
  }

  // El primer nonce (100) ya se salió de la ventana de 64: se acepta otra vez.
  firmaEsperada(ts, nonces[0], firma);
  char ts_texto[24];
  sprintf(ts_texto, "%llu", (unsigned long long)ts);
  char nonce_texto[24];
  sprintf(nonce_texto, "%llu", (unsigned long long)nonces[0]);
  Peticion repetido = {ts_texto, nonce_texto, firma};
  VERIFICAR(validador.validar(repetido) == Resultado::Aceptada);

  // Pero el último (164) sigue dentro de la ventana: se rechaza.
  firmaEsperada(ts, nonces[total - 1], firma);
  sprintf(nonce_texto, "%llu", (unsigned long long)nonces[total - 1]);
  Peticion dentro = {ts_texto, nonce_texto, firma};
  VERIFICAR(validador.validar(dentro) == Resultado::NonceRepetido);
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
