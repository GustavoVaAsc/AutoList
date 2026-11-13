// La firma de probar_captura: la cadena canonica, los cinco vectores compartidos con el
// firmware (firmware/xiao/pruebas/test_hmac_sha256.cpp, con los mismos valores esperados) y
// la interoperabilidad con el validador REAL de la placa (firmware/xiao/logica_camara.cpp).
#include <cctype>
#include <cstring>
#include <set>

#include <hmac_sha256.h>

#include "logica_camara.h"
#include "minitest.hpp"
#include "puente_aula/firma_captura.hpp"

using namespace puente_aula;

TEST(firma_cadena_canonica) {
  CHECK_EQ(cadena_canonica("GET", "/capture", 1762344000, 42),
           std::string("GET\n/capture\n1762344000\n42"));
  // La query no se firma.
  CHECK_EQ(cadena_canonica("GET", "/capture?res=SVGA", 1762344000, 42),
           std::string("GET\n/capture\n1762344000\n42"));
  CHECK_EQ(cadena_canonica("GET", "/capture#x", 1, 18446744073709551615ULL),
           std::string("GET\n/capture\n1\n18446744073709551615"));
}

TEST(firma_cadena_canonica_igual_a_la_de_la_placa) {
  const std::int64_t tss[] = {0, 1, 1762344000, 4102444800LL};
  const std::uint64_t nonces[] = {0, 42, 12345678901234567890ULL};
  for (const std::int64_t ts : tss) {
    for (const std::uint64_t nonce : nonces) {
      char placa[logica_camara::kLongitudCadenaCanonicaMax];
      const std::size_t largo = logica_camara::construirCadenaCanonica(
          placa, sizeof placa, "GET", "/capture", ts, nonce);
      CHECK_EQ(std::string(placa, largo), cadena_canonica("GET", "/capture", ts, nonce));
    }
  }
}

// Vectores del protocolo (mismos valores que el firmware y que el servicio de vision).
TEST(firma_vectores_del_protocolo) {
  CHECK_EQ(firma_captura("clave-de-prueba", 1762344000, 42),
           std::string("e61d70e6e5c5548dc0690515917c34dbba4f66c3c6dfec255767da6b9f7f9a04"));
  CHECK_EQ(firma_captura("clave-de-prueba", 1762344000, 12345678901234567890ULL),
           std::string("ee1edce4dd7152da5e421677226666128bef57ef537db6dc886ff76cdac81a9a"));
}

// Vectores de referencia de HMAC-SHA256 (RFC 4231), los otros tres de los cinco.
TEST(firma_vectores_rfc4231) {
  // Caso 1: clave de 20 bytes 0x0b.
  CHECK_EQ(hmac_hex(std::string(20, '\x0b'), "Hi There"),
           std::string("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"));
  // Caso 2: clave "Jefe".
  CHECK_EQ(hmac_hex("Jefe", "quien dijo que solo se usa para auditoria?"),
           std::string("4c1a70251663c6ca7dec37361e952bb869d610fe48774ed3ce0089820727f702"));
  // Clave de 128 bytes 0xaa (mas larga que el bloque: se sustituye por su SHA-256).
  CHECK_EQ(hmac_hex(std::string(128, '\xaa'),
                    "Test Using Larger Than Block-Size Key - Hash Key First"),
           std::string("f4c628398866742a99f3e2550d7f6ca135a8995a3940a190d75636a4fe27d788"));
}

TEST(firma_cabeceras) {
  const Cabeceras cabeceras = cabeceras_captura("clave-de-prueba", 1762344000, 42);
  CHECK_EQ(cabeceras.size(), static_cast<std::size_t>(3));
  CHECK_EQ(cabeceras[0].first, std::string("X-Captura-TS"));
  CHECK_EQ(cabeceras[0].second, std::string("1762344000"));
  CHECK_EQ(cabeceras[1].first, std::string("X-Captura-Nonce"));
  CHECK_EQ(cabeceras[1].second, std::string("42"));
  CHECK_EQ(cabeceras[2].first, std::string("X-Captura-HMAC"));
  CHECK_EQ(cabeceras[2].second.size(), static_cast<std::size_t>(64));
  for (const char c : cabeceras[2].second) CHECK(std::isxdigit(static_cast<unsigned char>(c)) && !std::isupper(static_cast<unsigned char>(c)));

  // --sin-firma: ts y nonce, sin la firma.
  const Cabeceras sin = cabeceras_captura("clave-de-prueba", 1762344000, 42, false);
  CHECK_EQ(sin.size(), static_cast<std::size_t>(2));
  for (const auto& [nombre, valor] : sin) CHECK(nombre != "X-Captura-HMAC");
}

TEST(firma_la_placa_acepta_lo_que_firma_probar_captura) {
  const std::string secreto = "secreto-de-prueba-con-mas-de-32-caracteres";
  logica_camara::Validador placa(secreto.c_str(), secreto.size());

  const auto peticion = [&](std::int64_t ts, std::uint64_t nonce, bool con_firma) {
    const Cabeceras cabeceras = cabeceras_captura(secreto, ts, nonce, con_firma);
    std::string valores[3];
    logica_camara::Peticion datos;
    for (const auto& [nombre, valor] : cabeceras) {
      if (nombre == "X-Captura-TS") { valores[0] = valor; datos.ts = valores[0].c_str(); }
      if (nombre == "X-Captura-Nonce") { valores[1] = valor; datos.nonce = valores[1].c_str(); }
      if (nombre == "X-Captura-HMAC") { valores[2] = valor; datos.hmac = valores[2].c_str(); }
    }
    return placa.validar(datos);
  };

  using R = logica_camara::Resultado;
  CHECK(peticion(1762344000, 7, true) == R::Aceptada);
  // Mismo nonce: la placa responde 403 (lo que prueba --reusar-nonce).
  CHECK(peticion(1762344001, 7, true) == R::NonceRepetido);
  CHECK_EQ(logica_camara::codigoHttp(R::NonceRepetido), 403);
  // Sin firma: 401 (lo que prueba --sin-firma).
  CHECK(peticion(1762344002, 8, false) == R::CabecerasFaltantes);
  CHECK_EQ(logica_camara::codigoHttp(R::CabecerasFaltantes), 401);
  // Otro secreto: firma invalida (401).
  logica_camara::Validador otra("otro-secreto-distinto-de-32-caracteres!", 39);
  const Cabeceras ajena = cabeceras_captura(secreto, 1762344003, 9);
  const std::string ts = ajena[0].second, nonce = ajena[1].second, firma = ajena[2].second;
  logica_camara::Peticion datos;
  datos.ts = ts.c_str();
  datos.nonce = nonce.c_str();
  datos.hmac = firma.c_str();
  CHECK(otra.validar(datos) == R::FirmaInvalida);
  // Una captura nueva con nonce nuevo vuelve a pasar.
  CHECK(peticion(1762344004, 10, true) == R::Aceptada);
}

TEST(firma_nonce_aleatorio_cambia) {
  std::set<std::uint64_t> vistos;
  for (int i = 0; i < 32; ++i) {
    const auto nonce = nonce_aleatorio();
    CHECK(nonce.has_value());
    vistos.insert(*nonce);
  }
  CHECK(vistos.size() > 30);
}
