// Pruebas de la firma de la peticion de captura: la cadena canonica, las tres cabeceras
// y el nonce. El protocolo es el que verifica la placa (firmware/xiao/logica_camara.h).

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "gcm_apoyo.hpp"
#include "minitest.hpp"
#include "vision/captura_firma.hpp"

namespace {

using namespace vision;

// Los vectores del protocolo (los mismos de test_hmac_sha256.cpp y del firmware).
const char* kClave = "clave-de-prueba";
const char* kFirmaNonceCorto = "e61d70e6e5c5548dc0690515917c34dbba4f66c3c6dfec255767da6b9f7f9a04";
const char* kFirmaNonceLargo = "ee1edce4dd7152da5e421677226666128bef57ef537db6dc886ff76cdac81a9a";

}  // namespace

TEST(la_cadena_canonica_es_metodo_camino_ts_y_nonce_sin_salto_final) {
  const std::string canonica = cadena_canonica("GET", "/capture", 1762344000, 42);
  CHECK_EQ(canonica, std::string("GET\n/capture\n1762344000\n42"));
  CHECK(canonica.back() != '\n');
}

// El nonce es un uint64 en decimal: el mas grande no se desborda ni sale con signo.
TEST(la_cadena_canonica_escribe_el_nonce_de_64_bits_sin_signo) {
  CHECK_EQ(cadena_canonica("GET", "/capture", 1762344000, 12345678901234567890ULL),
           std::string("GET\n/capture\n1762344000\n12345678901234567890"));
  CHECK_EQ(cadena_canonica("GET", "/capture", 1762344000, UINT64_MAX),
           std::string("GET\n/capture\n1762344000\n18446744073709551615"));
}

// La query (y el fragmento) no se firman: igual que en la placa.
TEST(la_cadena_canonica_no_firma_la_query) {
  const std::string sin_query = cadena_canonica("GET", "/capture", 1762344000, 42);
  CHECK_EQ(cadena_canonica("GET", "/capture?res=SVGA", 1762344000, 42), sin_query);
  CHECK_EQ(cadena_canonica("GET", "/capture#x", 1762344000, 42), sin_query);
}

TEST(la_firma_de_la_captura_coincide_con_los_vectores_del_protocolo) {
  CHECK_EQ(firma_captura(kClave, 1762344000, 42), std::string(kFirmaNonceCorto));
  CHECK_EQ(firma_captura(kClave, 1762344000, 12345678901234567890ULL),
           std::string(kFirmaNonceLargo));
}

TEST(las_cabeceras_llevan_ts_nonce_y_firma_con_el_valor_pegado_a_los_dos_puntos) {
  const std::vector<std::string> cabeceras = cabeceras_captura(kClave, 1762344000, 42);
  CHECK_EQ(cabeceras.size(), std::size_t{3});
  CHECK_EQ(cabeceras[0], std::string("X-Captura-TS:1762344000"));
  CHECK_EQ(cabeceras[1], std::string("X-Captura-Nonce:42"));
  CHECK_EQ(cabeceras[2], std::string("X-Captura-HMAC:") + kFirmaNonceCorto);
}

// Una firma de 64 caracteres hex en minuscula, como exige la placa.
TEST(la_firma_son_64_caracteres_hex_en_minuscula) {
  const std::string firma = firma_captura("otro-secreto-cualquiera", 1793805153, 7);
  CHECK_EQ(firma.size(), std::size_t{64});
  CHECK(firma.find_first_not_of("0123456789abcdef") == std::string::npos);
}

TEST(otro_secreto_otro_ts_u_otro_nonce_dan_otra_firma) {
  const std::string base = firma_captura(kClave, 1762344000, 42);
  CHECK(base != firma_captura("otra-clave", 1762344000, 42));
  CHECK(base != firma_captura(kClave, 1762344001, 42));
  CHECK(base != firma_captura(kClave, 1762344000, 43));
}

TEST(el_camino_firmado_es_el_de_la_placa) {
  CHECK_EQ(std::string(kMetodoCaptura), std::string("GET"));
  CHECK_EQ(std::string(kCaminoCaptura), std::string("/capture"));
}

TEST(el_nonce_aleatorio_da_valores_distintos) {
  std::set<std::uint64_t> vistos;
  for (int i = 0; i < 64; ++i) {
    const std::optional<std::uint64_t> nonce = nonce_aleatorio();
    CHECK(nonce.has_value());
    vistos.insert(*nonce);
  }
  // 64 nonces de 64 bits: una repeticion no ocurre por azar.
  CHECK_EQ(vistos.size(), std::size_t{64});
}

// --- La respuesta cifrada (AES-256-GCM) ---------------------------------------------
// K = HMAC-SHA256(secreto, "AUTOLIST-CAPTURA-RESP-AES256GCM-V1"); IV de la cabecera
// X-Captura-IV; AAD = "RESP\n<ts>\n<nonce>"; cuerpo = cifrado || etiqueta de 16 bytes. Los
// vectores se calcularon con la biblioteca `cryptography` de Python, no con este codigo.

namespace {

using gcm_apoyo::bytes_de_hex;
using gcm_apoyo::hex_de;

const char* kSecretoGcm = "clave-de-prueba-con-mas-de-32-caracteres-xyz";
const char* kClaveEsperada = "9f79c9353b05eccdf86e720af1f5bd403d45da456ccf464458a838e3403e9dd1";
const char* kIvHex = "000102030405060708090a0b";
const char* kPlanoHex =
    "ffd8ffe0004a46494600010100000100010000ffdb0043000102030405060708090a0b0c0d0e0f00ffd9";
const char* kCifradoHex =
    "d1cdba7cb162a6416a74dd69a25d979e2ba84bc00d5a4afc8af016181dddee67af3d7b72d386a0fd77d6d2"
    "31784099b6a9d684ad0088480e62c2";
const char* kCifradoVacioHex = "75345f53bdf60b25a8eb4d5cb466dc7b";

ResultadoDescifrado descifra(const std::string& iv, std::int64_t ts, std::uint64_t nonce,
                             const std::vector<std::uint8_t>& cuerpo,
                             std::vector<std::uint8_t>& plano,
                             const char* secreto = kSecretoGcm) {
  return descifrar_respuesta(derivar_clave_respuesta(secreto), iv, ts, nonce, cuerpo.data(),
                             cuerpo.size(), plano);
}

}  // namespace

TEST(la_clave_de_la_respuesta_sale_del_hmac_de_la_etiqueta) {
  const ClaveRespuesta clave = derivar_clave_respuesta(kSecretoGcm);
  CHECK_EQ(hex_de(std::vector<std::uint8_t>(clave.bytes().begin(), clave.bytes().end())),
           std::string(kClaveEsperada));
  CHECK_EQ(std::string(kEtiquetaClaveRespuesta),
           std::string("AUTOLIST-CAPTURA-RESP-AES256GCM-V1"));
  // Otro secreto, otra clave.
  const ClaveRespuesta otra = derivar_clave_respuesta("otro-secreto-con-mas-de-32-caracteres!!");
  CHECK(otra.bytes() != clave.bytes());
}

TEST(el_aad_de_la_respuesta_es_resp_ts_nonce_sin_salto_final) {
  CHECK_EQ(aad_respuesta(1767225600, 42), std::string("RESP\n1767225600\n42"));
  CHECK_EQ(aad_respuesta(0, 0), std::string("RESP\n0\n0"));
  CHECK_EQ(aad_respuesta(1767225600, UINT64_MAX),
           std::string("RESP\n1767225600\n18446744073709551615"));
}

TEST(se_descifran_los_vectores_de_la_respuesta) {
  std::vector<std::uint8_t> plano;
  CHECK(descifra(kIvHex, 1767225600, 42, bytes_de_hex(kCifradoHex), plano) ==
        ResultadoDescifrado::Ok);
  CHECK_EQ(hex_de(plano), std::string(kPlanoHex));

  // Texto vacio: solo la etiqueta (16 bytes); el AAD es RESP\n0\n0.
  CHECK(descifra(kIvHex, 0, 0, bytes_de_hex(kCifradoVacioHex), plano) ==
        ResultadoDescifrado::Ok);
  CHECK(plano.empty());
}

// Lo que cifra la placa (aqui, el apoyo de pruebas) coincide con los vectores.
TEST(el_apoyo_de_cifrado_reproduce_los_vectores) {
  const ClaveRespuesta clave = derivar_clave_respuesta(kSecretoGcm);
  CHECK_EQ(hex_de(gcm_apoyo::cifrar(clave, bytes_de_hex(kIvHex), "RESP\n1767225600\n42",
                                    bytes_de_hex(kPlanoHex))),
           std::string(kCifradoHex));
  CHECK_EQ(hex_de(gcm_apoyo::cifrar(clave, bytes_de_hex(kIvHex), "RESP\n0\n0", {})),
           std::string(kCifradoVacioHex));
}

TEST(una_etiqueta_alterada_no_autentica_y_no_entrega_nada) {
  for (const std::size_t desde_el_final : {std::size_t{1}, std::size_t{8}, std::size_t{16}}) {
    std::vector<std::uint8_t> cuerpo = bytes_de_hex(kCifradoHex);
    cuerpo[cuerpo.size() - desde_el_final] ^= 0x01;
    std::vector<std::uint8_t> plano{1, 2, 3};
    CHECK(descifra(kIvHex, 1767225600, 42, cuerpo, plano) == ResultadoDescifrado::NoAutentico);
    CHECK(plano.empty());
  }
}

TEST(un_texto_cifrado_alterado_en_un_byte_no_autentica) {
  std::vector<std::uint8_t> cuerpo = bytes_de_hex(kCifradoHex);
  cuerpo[10] ^= 0x80;
  std::vector<std::uint8_t> plano;
  CHECK(descifra(kIvHex, 1767225600, 42, cuerpo, plano) == ResultadoDescifrado::NoAutentico);
  CHECK(plano.empty());
}

// El AAD ata la respuesta a LA peticion: otro nonce o ts (la respuesta de otra peticion).
TEST(un_aad_distinto_no_autentica) {
  std::vector<std::uint8_t> plano;
  const std::vector<std::uint8_t> cuerpo = bytes_de_hex(kCifradoHex);
  CHECK(descifra(kIvHex, 1767225600, 43, cuerpo, plano) == ResultadoDescifrado::NoAutentico);
  CHECK(descifra(kIvHex, 1767225601, 42, cuerpo, plano) == ResultadoDescifrado::NoAutentico);
  CHECK(plano.empty());
}

TEST(otro_iv_u_otro_secreto_no_autentican) {
  std::vector<std::uint8_t> plano;
  const std::vector<std::uint8_t> cuerpo = bytes_de_hex(kCifradoHex);
  CHECK(descifra("000102030405060708090a0c", 1767225600, 42, cuerpo, plano) ==
        ResultadoDescifrado::NoAutentico);
  CHECK(descifra(kIvHex, 1767225600, 42, cuerpo, plano, "otro-secreto-con-mas-de-32-caracteres!!") ==
        ResultadoDescifrado::NoAutentico);
}

TEST(un_iv_ausente_o_mal_formado_se_distingue) {
  std::vector<std::uint8_t> plano;
  const std::vector<std::uint8_t> cuerpo = bytes_de_hex(kCifradoHex);
  CHECK(descifra("", 1767225600, 42, cuerpo, plano) == ResultadoDescifrado::IvAusente);
  const char* malos[] = {"0001020304050607",                  // corto
                         "000102030405060708090a0b0c",        // largo
                         "000102030405060708090a0g",          // no es hex
                         "000102030405060708090A0B",          // mayusculas
                         "00 102030405060708090a0b"};         // espacio
  for (const char* iv : malos) {
    CHECK(descifra(iv, 1767225600, 42, cuerpo, plano) == ResultadoDescifrado::IvInvalido);
    CHECK(plano.empty());
  }
}

// Menos de 16 bytes no puede ni llevar la etiqueta.
TEST(un_cuerpo_de_menos_de_16_bytes_es_corto) {
  std::vector<std::uint8_t> plano;
  CHECK(descifra(kIvHex, 1767225600, 42, {}, plano) == ResultadoDescifrado::CuerpoCorto);
  CHECK(descifra(kIvHex, 1767225600, 42, std::vector<std::uint8_t>(15, 0), plano) ==
        ResultadoDescifrado::CuerpoCorto);
  // 16 bytes de ceros: tiene forma de etiqueta, pero no verifica.
  CHECK(descifra(kIvHex, 1767225600, 42, std::vector<std::uint8_t>(16, 0), plano) ==
        ResultadoDescifrado::NoAutentico);
}

TEST(una_foto_grande_cifrada_por_la_placa_se_recupera_entera) {
  std::vector<std::uint8_t> foto(300000);
  for (std::size_t i = 0; i < foto.size(); ++i) foto[i] = static_cast<std::uint8_t>(i * 31 + 7);
  const ClaveRespuesta clave = derivar_clave_respuesta(kSecretoGcm);
  const std::vector<std::uint8_t> cifrada =
      gcm_apoyo::cifrar(clave, bytes_de_hex("a1a2a3a4a5a6a7a8a9aaabac"),
                        aad_respuesta(1793805153, 987654321012345678ULL), foto);
  CHECK_EQ(cifrada.size(), foto.size() + 16);
  std::vector<std::uint8_t> plano;
  CHECK(descifrar_respuesta(clave, "a1a2a3a4a5a6a7a8a9aaabac", 1793805153, 987654321012345678ULL,
                            cifrada.data(), cifrada.size(), plano) == ResultadoDescifrado::Ok);
  CHECK(plano == foto);
  borrar_bytes(plano);
  CHECK(plano.empty());
}

TEST(los_limites_del_reloj_plausible) {
  CHECK_EQ(kMinEpochPlausible, std::int64_t{1767225600});    // 2026-01-01
  CHECK_EQ(kMaxEpochPlausible, std::int64_t{4102444800});    // 2100-01-01
  CHECK_EQ(kSaltoRelojMaxS, std::int64_t{600});
}
