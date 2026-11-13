// La respuesta cifrada de la placa (AES-256-GCM). Los vectores se calcularon aparte, con Python
// `cryptography`, y son los mismos que usan el firmware y el servicio de vision.
#include <cstring>

#include "minitest.hpp"
#include "puente_aula/respuesta_cifrada.hpp"

using namespace puente_aula;

namespace {

const char* kSecreto = "clave-de-prueba-con-mas-de-32-caracteres-xyz";
const char* kJpegHex = "ffd8ffe0004a46494600010100000100010000ffdb0043000102030405060708090a0b0c0d0e0f00ffd9";
const char* kCifradoHex =
    "d1cdba7cb162a6416a74dd69a25d979e2ba84bc00d5a4afc8af016181dddee67af3d7b72d386a0fd77d6d231784099b6"
    "a9d684ad0088480e62c2";

std::string de_hex(const std::string& hex) {
  std::string bytes;
  for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
    bytes += static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16));
  }
  return bytes;
}

std::string a_hex(const std::uint8_t* datos, std::size_t n) {
  static const char* cifras = "0123456789abcdef";
  std::string hex;
  for (std::size_t i = 0; i < n; ++i) {
    hex += cifras[datos[i] >> 4];
    hex += cifras[datos[i] & 15];
  }
  return hex;
}

IvRespuesta iv_de_prueba() {
  IvRespuesta iv{};
  for (std::size_t i = 0; i < iv.size(); ++i) iv[i] = static_cast<std::uint8_t>(i);
  return iv;
}

}  // namespace

TEST(cifrado_derivacion_de_la_clave) {
  const ClaveRespuesta clave = derivar_clave_respuesta(kSecreto);
  CHECK_EQ(a_hex(clave.data(), clave.size()),
           std::string("9f79c9353b05eccdf86e720af1f5bd403d45da456ccf464458a838e3403e9dd1"));
  // Otro secreto, otra clave.
  CHECK(derivar_clave_respuesta("otro-secreto") != clave);
}

TEST(cifrado_aad_es_el_texto_canonico_sin_salto_final) {
  CHECK_EQ(aad_respuesta(1767225600, 42), std::string("RESP\n1767225600\n42"));
  CHECK_EQ(aad_respuesta(0, 0), std::string("RESP\n0\n0"));
  CHECK_EQ(aad_respuesta(1, 18446744073709551615ULL), std::string("RESP\n1\n18446744073709551615"));
}

TEST(cifrado_vectores_de_cifrado) {
  const ClaveRespuesta clave = derivar_clave_respuesta(kSecreto);
  const std::string jpeg = de_hex(kJpegHex);
  CHECK_EQ(jpeg.size(), static_cast<std::size_t>(42));
  const std::string cifrado = cifrar_gcm(clave, iv_de_prueba(), aad_respuesta(1767225600, 42), jpeg);
  CHECK_EQ(cifrado.size(), jpeg.size() + kLongitudEtiqueta);
  CHECK_EQ(a_hex(reinterpret_cast<const std::uint8_t*>(cifrado.data()), cifrado.size()),
           std::string(kCifradoHex));
  // Texto vacio: queda solo la etiqueta.
  const std::string vacio = cifrar_gcm(clave, iv_de_prueba(), aad_respuesta(0, 0), "");
  CHECK_EQ(a_hex(reinterpret_cast<const std::uint8_t*>(vacio.data()), vacio.size()),
           std::string("75345f53bdf60b25a8eb4d5cb466dc7b"));
}

TEST(cifrado_vectores_de_descifrado) {
  const ClaveRespuesta clave = derivar_clave_respuesta(kSecreto);
  std::string plano;
  CHECK(descifrar_gcm(clave, iv_de_prueba(), aad_respuesta(1767225600, 42), de_hex(kCifradoHex), plano));
  CHECK_EQ(a_hex(reinterpret_cast<const std::uint8_t*>(plano.data()), plano.size()),
           std::string(kJpegHex));
  std::string vacio = "sobrante";
  CHECK(descifrar_gcm(clave, iv_de_prueba(), aad_respuesta(0, 0),
                      de_hex("75345f53bdf60b25a8eb4d5cb466dc7b"), vacio));
  CHECK(vacio.empty());
}

TEST(cifrado_rechaza_etiqueta_aad_iv_clave_y_cuerpo_alterados) {
  const ClaveRespuesta clave = derivar_clave_respuesta(kSecreto);
  const std::string bueno = de_hex(kCifradoHex);
  const std::string aad = aad_respuesta(1767225600, 42);
  std::string plano = "intacto";

  // Etiqueta alterada (un bit del ultimo byte)
  std::string etiqueta = bueno;
  etiqueta.back() ^= 1;
  CHECK(!descifrar_gcm(clave, iv_de_prueba(), aad, etiqueta, plano));
  // Cuerpo alterado
  std::string cuerpo = bueno;
  cuerpo[5] ^= 0x80;
  CHECK(!descifrar_gcm(clave, iv_de_prueba(), aad, cuerpo, plano));
  // AAD alterado: otro nonce, otro ts
  CHECK(!descifrar_gcm(clave, iv_de_prueba(), aad_respuesta(1767225600, 43), bueno, plano));
  CHECK(!descifrar_gcm(clave, iv_de_prueba(), aad_respuesta(1767225601, 42), bueno, plano));
  // IV equivocado
  IvRespuesta otro = iv_de_prueba();
  otro[0] ^= 1;
  CHECK(!descifrar_gcm(clave, otro, aad, bueno, plano));
  // Clave equivocada
  CHECK(!descifrar_gcm(derivar_clave_respuesta("otro-secreto"), iv_de_prueba(), aad, bueno, plano));
  // Truncado: sin etiqueta completa, o recortado
  CHECK(!descifrar_gcm(clave, iv_de_prueba(), aad, bueno.substr(0, 15), plano));
  CHECK(!descifrar_gcm(clave, iv_de_prueba(), aad, bueno.substr(0, bueno.size() - 1), plano));
  CHECK(!descifrar_gcm(clave, iv_de_prueba(), aad, "", plano));
  // En ningun fallo se entrega texto: queda lo que habia.
  CHECK_EQ(plano, std::string("intacto"));
  // Y lo bueno sigue funcionando.
  CHECK(descifrar_gcm(clave, iv_de_prueba(), aad, bueno, plano));
}

TEST(cifrado_ida_y_vuelta_con_cuerpo_grande) {
  const ClaveRespuesta clave = derivar_clave_respuesta(kSecreto);
  std::string foto(70000, '\0');
  for (std::size_t i = 0; i < foto.size(); ++i) foto[i] = static_cast<char>(i * 17 + 3);
  const std::string cifrado = cifrar_gcm(clave, iv_de_prueba(), aad_respuesta(5, 6), foto);
  std::string plano;
  CHECK(descifrar_gcm(clave, iv_de_prueba(), aad_respuesta(5, 6), cifrado, plano));
  CHECK(plano == foto);
  CHECK(cifrado.substr(0, foto.size()) != foto);  // de verdad esta cifrado
}

TEST(cifrado_descifrar_respuesta_distingue_ok_ausente_e_invalida) {
  const std::int64_t ts = 1767225600;
  const std::uint64_t nonce = 42;
  RespuestaCaptura respuesta;
  respuesta.estado = 200;
  respuesta.cuerpo = de_hex(kCifradoHex);

  // Sin X-Captura-IV: ausente
  CHECK(descifrar_respuesta(kSecreto, ts, nonce, respuesta).estado == EstadoRespuesta::Ausente);

  respuesta.cabeceras.emplace_back("X-Captura-IV", "000102030405060708090a0b");
  RespuestaDescifrada ok = descifrar_respuesta(kSecreto, ts, nonce, respuesta);
  CHECK(ok.estado == EstadoRespuesta::Ok);
  CHECK_EQ(a_hex(reinterpret_cast<const std::uint8_t*>(ok.texto_plano.data()), ok.texto_plano.size()),
           std::string(kJpegHex));

  // La misma respuesta con otro nonce, otro ts, otro secreto o un cuerpo alterado: invalida, y sin foto.
  for (const RespuestaDescifrada& mala :
       {descifrar_respuesta(kSecreto, ts, 43, respuesta), descifrar_respuesta(kSecreto, ts + 1, nonce, respuesta),
        descifrar_respuesta("otro-secreto", ts, nonce, respuesta)}) {
    CHECK(mala.estado == EstadoRespuesta::Invalida);
    CHECK(mala.texto_plano.empty());
  }
  RespuestaCaptura alterada = respuesta;
  alterada.cuerpo[3] ^= 1;
  CHECK(descifrar_respuesta(kSecreto, ts, nonce, alterada).estado == EstadoRespuesta::Invalida);
  // Un IV distinto (bien formado) tampoco vale.
  RespuestaCaptura otro_iv = respuesta;
  otro_iv.cabeceras[0].second = "ff0102030405060708090a0b";
  CHECK(descifrar_respuesta(kSecreto, ts, nonce, otro_iv).estado == EstadoRespuesta::Invalida);

  // IV que llego mal formado (y se descarto): invalida, no ausente.
  RespuestaCaptura malformado;
  malformado.cuerpo = respuesta.cuerpo;
  malformado.iv_malformado = true;
  CHECK(descifrar_respuesta(kSecreto, ts, nonce, malformado).estado == EstadoRespuesta::Invalida);
}
