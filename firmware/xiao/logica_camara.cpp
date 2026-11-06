// Lógica pura del endpoint /capture (validación de cabeceras, firma, frescura
// y anti-replay). C++ puro, sin Arduino: se prueba en el host con g++.
#include "logica_camara.h"

#include <string.h>

#include "hmac_sha256.h"

namespace logica_camara {
namespace {

// Convierte un texto decimal en un entero de 64 bits con signo, como el ts del
// borde (int64). Solo se aceptan dígitos y, delante, un "-" opcional. El borde
// manda el ts ya en decimal canónico, así que cualquier otra cosa es una
// petición manipulada.
//
// Devuelve false si el texto no es un decimal válido o si no cabe en int64.
bool parseTs(const char* texto, size_t longitudMax, int64_t& valor) {
  if (texto == nullptr) {
    return false;
  }
  size_t longitud = 0;
  while (texto[longitud] != '\0') {
    ++longitud;
  }
  if (longitud == 0 || longitud > longitudMax) {
    return false;
  }

  bool negativo = false;
  size_t inicio = 0;
  if (texto[0] == '-') {
    negativo = true;
    inicio = 1;
    if (longitud == 1) {
      return false;  // un "-" solo no es un número
    }
  }

  const uint64_t limite = negativo ? (uint64_t(1) << 63) : (uint64_t(1) << 63) - 1;
  uint64_t acumulado = 0;
  for (size_t i = inicio; i < longitud; ++i) {
    const char c = texto[i];
    if (c < '0' || c > '9') {
      return false;
    }
    const uint64_t digito = uint64_t(c - '0');
    if (acumulado > (limite - digito) / 10) {
      return false;  // desbordaría el rango
    }
    acumulado = acumulado * 10 + digito;
  }

  valor = negativo ? -static_cast<int64_t>(acumulado) : static_cast<int64_t>(acumulado);
  return true;
}

// Convierte un texto decimal en un entero de 64 bits sin signo, como el nonce
// del borde (uint64). Solo se aceptan dígitos.
bool parseNonce(const char* texto, size_t longitudMax, uint64_t& valor) {
  if (texto == nullptr) {
    return false;
  }
  size_t longitud = 0;
  while (texto[longitud] != '\0') {
    ++longitud;
  }
  if (longitud == 0 || longitud > longitudMax) {
    return false;
  }

  uint64_t acumulado = 0;
  for (size_t i = 0; i < longitud; ++i) {
    const char c = texto[i];
    if (c < '0' || c > '9') {
      return false;
    }
    const uint64_t digito = uint64_t(c - '0');
    if (acumulado > (UINT64_MAX - digito) / 10) {
      return false;  // desbordaría el rango
    }
    acumulado = acumulado * 10 + digito;
  }
  valor = acumulado;
  return true;
}

// Escribe un entero de 64 bits en decimal, como lo serializa el borde
// (std::to_string). Devuelve la longitud escrita, sin el terminador, o 0 si no
// cabe.
size_t escribirDecimal(uint64_t magnitud, bool negativo, char* destino, size_t capacidad) {
  char digitos[24];
  size_t n = 0;
  do {
    digitos[n++] = char('0' + (magnitud % 10));
    magnitud /= 10;
  } while (magnitud != 0 && n < sizeof digitos);

  const size_t largo = n + (negativo ? 1 : 0);
  if (largo + 1 > capacidad) {
    return 0;
  }
  size_t i = 0;
  if (negativo) {
    destino[i++] = '-';
  }
  for (size_t k = n; k > 0; --k) {
    destino[i++] = digitos[k - 1];
  }
  destino[i] = '\0';
  return i;
}

// Recorta de `entrada` los espacios y tabuladores de los extremos y lo copia a
// `destino`. Devuelve la longitud copiada, o 0 si la entrada está vacía o no
// cabe.
size_t recortar(const char* entrada, char* destino, size_t capacidad) {
  if (entrada == nullptr || destino == nullptr || capacidad == 0) {
    return 0;
  }
  size_t inicio = 0;
  while (entrada[inicio] == ' ' || entrada[inicio] == '\t') {
    ++inicio;
  }
  size_t fin = inicio;
  while (entrada[fin] != '\0') {
    ++fin;
  }
  while (fin > inicio && (entrada[fin - 1] == ' ' || entrada[fin - 1] == '\t')) {
    --fin;
  }
  const size_t largo = fin - inicio;
  if (largo == 0 || largo + 1 > capacidad) {
    return 0;
  }
  memcpy(destino, entrada + inicio, largo);
  destino[largo] = '\0';
  return largo;
}

}  // namespace

const char* aTexto(Resultado resultado) {
  switch (resultado) {
    case Resultado::Aceptada:
      return "aceptada";
    case Resultado::CabecerasFaltantes:
      return "cabeceras faltantes";
    case Resultado::FirmaInvalida:
      return "firma invalida";
    case Resultado::NonceRepetido:
      return "nonce repetido";
    case Resultado::TimestampAntiguo:
      return "timestamp antiguo";
  }
  return "desconocido";
}

int codigoHttp(Resultado resultado) {
  switch (resultado) {
    case Resultado::Aceptada:
      return 200;
    case Resultado::CabecerasFaltantes:
    case Resultado::FirmaInvalida:
      return 401;
    case Resultado::NonceRepetido:
    case Resultado::TimestampAntiguo:
      return 403;
  }
  return 401;
}

Validador::Validador(const char* secreto, size_t longitudSecreto)
    : secreto_(secreto), longitud_secreto_(longitudSecreto) {}

bool Validador::nonceVisto(uint64_t nonce) const {
  for (size_t i = 0; i < total_nonces_; ++i) {
    if (nonces_[i] == nonce) {
      return true;
    }
  }
  return false;
}

void Validador::recordarNonce(uint64_t nonce) {
  nonces_[siguiente_nonce_] = nonce;
  siguiente_nonce_ = (siguiente_nonce_ + 1) % kNoncesRecordados;
  if (total_nonces_ < kNoncesRecordados) {
    ++total_nonces_;
  }
}

void Validador::reiniciar() {
  total_nonces_ = 0;
  siguiente_nonce_ = 0;
  ultimo_ts_ = 0;
  hay_ultimo_ts_ = false;
}

Resultado Validador::validar(const Peticion& peticion) {
  // 1. Cabeceras ausentes: sin firma que comprobar, no se responde 403.
  if (peticion.ts == nullptr || peticion.nonce == nullptr || peticion.hmac == nullptr) {
    return Resultado::CabecerasFaltantes;
  }

  // Los valores pueden venir con espacios o tabuladores alrededor (el cliente
  // HTTP deja un espacio tras los dos puntos): se recortan antes de usarlos.
  char ts[kLongitudTsMax + 8];
  char nonce[kLongitudNonceMax + 8];
  char hmac[kLongitudHmacHex + 8];
  if (recortar(peticion.ts, ts, sizeof ts) == 0) {
    return Resultado::CabecerasFaltantes;
  }
  if (recortar(peticion.nonce, nonce, sizeof nonce) == 0) {
    return Resultado::CabecerasFaltantes;
  }
  if (recortar(peticion.hmac, hmac, sizeof hmac) == 0) {
    return Resultado::CabecerasFaltantes;
  }

  // 2. Firma: se recalcula sobre la cadena canónica y se compara en tiempo
  //    constante. Un ts o un nonce con un formato que no es decimal se
  //    tratan como firma inválida: el borde nunca los envía así.
  int64_t ts_valor = 0;
  uint64_t nonce_valor = 0;
  if (!parseTs(ts, kLongitudTsMax, ts_valor)) {
    return Resultado::FirmaInvalida;
  }
  if (!parseNonce(nonce, kLongitudNonceMax, nonce_valor)) {
    return Resultado::FirmaInvalida;
  }

  uint8_t firmaRecibida[hmac_sha256::kLongitudHash];
  if (!hmac_sha256::desdeHex(hmac, firmaRecibida, sizeof firmaRecibida)) {
    return Resultado::FirmaInvalida;
  }

  char canonica[kLongitudCadenaCanonicaMax];
  const size_t longitudCanonica =
      construirCadenaCanonica(canonica, sizeof canonica, kMetodo, kCamino, ts_valor, nonce_valor);
  if (longitudCanonica == 0) {
    return Resultado::FirmaInvalida;
  }

  uint8_t firmaCalculada[hmac_sha256::kLongitudHash];
  hmac_sha256::hmacSha256(reinterpret_cast<const uint8_t*>(secreto_), longitud_secreto_,
                          reinterpret_cast<const uint8_t*>(canonica), longitudCanonica,
                          firmaCalculada);
  if (!hmac_sha256::compararConstante(firmaCalculada, firmaRecibida, sizeof firmaCalculada)) {
    return Resultado::FirmaInvalida;
  }

  // 3. Anti-replay: el nonce no se repite y el ts es fresco respecto del
  //    último ts aceptado (no del reloj de la XIAO, que no es confiable).
  if (nonceVisto(nonce_valor)) {
    return Resultado::NonceRepetido;
  }
  if (hay_ultimo_ts_ && ts_valor < ultimo_ts_) {
    // La diferencia se calcula sin signo: con los dos valores dentro de int64
    // la resta da el hueco real sin desbordarse.
    const uint64_t diferencia =
        static_cast<uint64_t>(ultimo_ts_) - static_cast<uint64_t>(ts_valor);
    if (diferencia > kVentanaFrescuraS) {
      return Resultado::TimestampAntiguo;
    }
  }
  // OJO: no hay límite superior para el ts. El ts lo pone el reloj del borde y
  // entre dos capturas puede pasar más de una hora (una sesión sin alumunos),
  // así que un ts "en el futuro" respecto del último no significa que el reloj
  // haya saltado. Lo que sí puede pasar es que el reloj del borde salte hacia
  // atrás: entonces todas las peticiones quedan "antiguas" y la captura se
  // queda muerta hasta que el reloj vuelva a pasar por el valor anterior. Por
  // eso el borde comprueba que su reloj sea plausible ANTES de firmar
  // (vision/src/main.cpp, is_plausible_timestamp) y avisa por log: el fallo se
  // ve en el borde, no como un 403 sin explicación en la placa.

  recordarNonce(nonce_valor);
  // La marca de frescura no baja nunca: con el reloj del borde avanzando es el
  // último ts aceptado; si el reloj retrocede, la ventana no se afloja.
  if (!hay_ultimo_ts_ || ts_valor > ultimo_ts_) {
    ultimo_ts_ = ts_valor;
    hay_ultimo_ts_ = true;
  }
  return Resultado::Aceptada;
}

size_t construirCadenaCanonica(char* destino, size_t capacidad, const char* metodo,
                                const char* camino, int64_t ts, uint64_t nonce) {
  if (destino == nullptr || capacidad == 0 || metodo == nullptr || camino == nullptr) {
    return 0;
  }

  // La query no se firma: el camino se corta en el primer "?" o "#".
  size_t largoCamino = 0;
  while (camino[largoCamino] != '\0' && camino[largoCamino] != '?' && camino[largoCamino] != '#') {
    ++largoCamino;
  }
  if (largoCamino == 0) {
    return 0;
  }

  const size_t largoMetodo = strlen(metodo);
  if (largoMetodo == 0) {
    return 0;
  }

  // El ts se serializa como int64 (con "-" si es negativo) y el nonce como
  // uint64: igual que hace el borde con std::to_string.
  const uint64_t magnitudTs = ts < 0 ? (~static_cast<uint64_t>(ts) + 1) : static_cast<uint64_t>(ts);
  char ts_texto[24];
  char nonce_texto[24];
  const size_t largoTs = escribirDecimal(magnitudTs, ts < 0, ts_texto, sizeof ts_texto);
  const size_t largoNonce = escribirDecimal(nonce, false, nonce_texto, sizeof nonce_texto);
  if (largoTs == 0 || largoNonce == 0) {
    return 0;
  }

  const size_t largo = largoMetodo + 1 + largoCamino + 1 + largoTs + 1 + largoNonce;
  if (largo + 1 > capacidad) {
    return 0;
  }

  size_t i = 0;
  memcpy(destino + i, metodo, largoMetodo);
  i += largoMetodo;
  destino[i++] = '\n';
  memcpy(destino + i, camino, largoCamino);
  i += largoCamino;
  destino[i++] = '\n';
  memcpy(destino + i, ts_texto, largoTs);
  i += largoTs;
  destino[i++] = '\n';
  memcpy(destino + i, nonce_texto, largoNonce);
  i += largoNonce;
  destino[i] = '\0';
  return largo;
}

}  // namespace logica_camara
