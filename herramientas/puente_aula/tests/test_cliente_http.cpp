#include "minitest.hpp"
#include "puente_aula/cliente_http.hpp"

using namespace puente_aula;

TEST(cliente_gramatica_de_retry_after) {
  CHECK(cabecera_respuesta_valida("Retry-After", "0"));
  CHECK(cabecera_respuesta_valida("Retry-After", "7"));
  CHECK(cabecera_respuesta_valida("Retry-After", "123456"));
  CHECK(!cabecera_respuesta_valida("Retry-After", ""));
  CHECK(!cabecera_respuesta_valida("Retry-After", "1234567"));
  CHECK(!cabecera_respuesta_valida("Retry-After", "-1"));
  CHECK(!cabecera_respuesta_valida("Retry-After", "1.0"));
  CHECK(!cabecera_respuesta_valida("Retry-After", "Wed, 21 Oct 2026 07:28:00 GMT"));
  CHECK(!cabecera_respuesta_valida("Retry-After", "1\r\nX: y"));
}

TEST(cliente_gramatica_del_iv) {
  CHECK(cabecera_respuesta_valida("X-Captura-IV", "000102030405060708090a0b"));
  CHECK(cabecera_respuesta_valida("X-Captura-IV", std::string(24, 'f')));
  CHECK(!cabecera_respuesta_valida("X-Captura-IV", std::string(23, 'a')));
  CHECK(!cabecera_respuesta_valida("X-Captura-IV", std::string(25, 'a')));
  CHECK(!cabecera_respuesta_valida("X-Captura-IV", std::string(24, 'A')));  // solo minuscula
  CHECK(!cabecera_respuesta_valida("X-Captura-IV", std::string(23, 'a') + "g"));
  CHECK(!cabecera_respuesta_valida("X-Captura-IV", ""));
  // La cabecera que se quito del protocolo ya no se conserva.
  CHECK(!cabecera_respuesta_valida("X-Captura-Resp-HMAC", std::string(64, 'a')));
}

TEST(cliente_otras_cabeceras_nunca_son_validas_de_conservar) {
  CHECK(!cabecera_respuesta_valida("X-Otro", "1"));
  CHECK(!cabecera_respuesta_valida("Set-Cookie", "1"));
  CHECK(!cabecera_respuesta_valida("retry-after", "1"));  // el nombre se compara tal cual
}

TEST(cliente_tipo_de_contenido_permitido) {
  CHECK_EQ(tipo_contenido_permitido("image/jpeg"), std::string("image/jpeg"));
  CHECK_EQ(tipo_contenido_permitido("Image/JPEG"), std::string("image/jpeg"));
  CHECK_EQ(tipo_contenido_permitido("text/plain"), std::string("text/plain"));
  CHECK_EQ(tipo_contenido_permitido("text/plain; charset=utf-8"), std::string("text/plain"));
  CHECK_EQ(tipo_contenido_permitido(" text/plain ;x=1"), std::string("text/plain"));
  CHECK_EQ(tipo_contenido_permitido("application/octet-stream"), std::string("application/octet-stream"));
  CHECK_EQ(tipo_contenido_permitido("text/html"), std::string("application/octet-stream"));
  CHECK_EQ(tipo_contenido_permitido("image/jpegx"), std::string("application/octet-stream"));
  CHECK_EQ(tipo_contenido_permitido("image/png"), std::string("application/octet-stream"));
  CHECK_EQ(tipo_contenido_permitido(""), std::string("application/octet-stream"));
}
