// Pruebas de HMAC-SHA256: los cinco vectores compartidos con el firmware de la XIAO
// (firmware/xiao/pruebas/test_hmac_sha256.cpp) y la comparacion en tiempo constante.
// Los vectores NO se pueden cambiar: la placa tiene que dar exactamente los mismos
// valores. Si uno cambia, hay que cambiar el otro lado.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "minitest.hpp"
#include "vision/hmac_sha256.hpp"

namespace {

using namespace vision::hmac_sha256;

// El HMAC de una clave y un mensaje, en hex minuscula.
std::string hmac_hex(const std::string& clave, const std::string& mensaje) {
  std::uint8_t firma[kLongitudHash];
  hmacSha256(reinterpret_cast<const std::uint8_t*>(clave.data()), clave.size(),
             reinterpret_cast<const std::uint8_t*>(mensaje.data()), mensaje.size(), firma);
  char hex[kLongitudHex];
  aHex(firma, sizeof firma, hex);
  return std::string(hex);
}

std::string sha_hex(const std::string& mensaje) {
  std::uint8_t digest[kLongitudHash];
  sha256(reinterpret_cast<const std::uint8_t*>(mensaje.data()), mensaje.size(), digest);
  char hex[kLongitudHex];
  aHex(digest, sizeof digest, hex);
  return std::string(hex);
}

}  // namespace

// Los dos vectores del protocolo de captura: la cadena canonica con el secreto compartido.
TEST(hmac_vectores_del_protocolo_de_captura) {
  CHECK_EQ(hmac_hex("clave-de-prueba", "GET\n/capture\n1762344000\n42"),
           std::string("e61d70e6e5c5548dc0690515917c34dbba4f66c3c6dfec255767da6b9f7f9a04"));
  CHECK_EQ(hmac_hex("clave-de-prueba", "GET\n/capture\n1762344000\n12345678901234567890"),
           std::string("ee1edce4dd7152da5e421677226666128bef57ef537db6dc886ff76cdac81a9a"));
}

// RFC 4231, caso 1: clave de 20 bytes de 0x0b.
TEST(hmac_rfc4231_caso_1) {
  CHECK_EQ(hmac_hex(std::string(20, '\x0b'), "Hi There"),
           std::string("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"));
}

// RFC 4231, caso 2: clave "Jefe".
TEST(hmac_rfc4231_caso_2) {
  CHECK_EQ(hmac_hex("Jefe", "quien dijo que solo se usa para auditoria?"),
           std::string("4c1a70251663c6ca7dec37361e952bb869d610fe48774ed3ce0089820727f702"));
}

// Clave de 128 bytes de 0xaa: mas larga que el bloque (64), asi que se sustituye por su
// SHA-256 (RFC 2104). Es el vector del protocolo compartido con el firmware.
TEST(hmac_clave_mas_larga_que_el_bloque) {
  CHECK_EQ(hmac_hex(std::string(128, '\xaa'),
                    "Test Using Larger Than Block-Size Key - Hash Key First"),
           std::string("f4c628398866742a99f3e2550d7f6ca135a8995a3940a190d75636a4fe27d788"));
}

TEST(sha256_y_hex_en_minusculas) {
  CHECK_EQ(sha_hex(""),
           std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
  CHECK_EQ(sha_hex("abc"),
           std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
  const std::uint8_t bytes[4] = {0x00, 0x0f, 0xa0, 0xff};
  char hex[kLongitudHex];
  aHex(bytes, sizeof bytes, hex);
  CHECK_EQ(std::string(hex), std::string("000fa0ff"));
}

// La comparacion recorre todos los bytes: una diferencia al final tambien se detecta.
TEST(la_comparacion_constante_detecta_la_diferencia_en_cualquier_posicion) {
  const std::uint8_t a[kLongitudHash] = {0x01, 0x02, 0x03, 0x04};
  const std::uint8_t b[kLongitudHash] = {0x01, 0x02, 0x03, 0x04};
  const std::uint8_t c[kLongitudHash] = {0x01, 0x02, 0x03, 0x05};
  std::uint8_t al_final[kLongitudHash] = {0x01, 0x02, 0x03, 0x04};
  al_final[kLongitudHash - 1] = 0xff;
  CHECK(compararConstante(a, b, sizeof a));
  CHECK(!compararConstante(a, c, sizeof a));
  CHECK(!compararConstante(a, al_final, sizeof a));
}

TEST(la_conversion_desde_hex_acepta_mayusculas_y_rechaza_lo_malformado) {
  std::uint8_t bytes[4];
  CHECK(desdeHex("00ff10ab", bytes, 4));
  CHECK_EQ(static_cast<int>(bytes[0]), 0x00);
  CHECK_EQ(static_cast<int>(bytes[1]), 0xff);
  CHECK_EQ(static_cast<int>(bytes[2]), 0x10);
  CHECK_EQ(static_cast<int>(bytes[3]), 0xab);
  std::uint8_t otros[4];
  CHECK(desdeHex("00FF10AB", otros, 4));
  CHECK(std::memcmp(bytes, otros, 4) == 0);
  CHECK(!desdeHex("00ff10ag", bytes, 4));
  CHECK(!desdeHex("00ff10a", bytes, 4));
  CHECK(!desdeHex("00ff10ab0", bytes, 4));
  CHECK(!desdeHex("", bytes, 4));
  CHECK(!desdeHex(nullptr, bytes, 4));
}
