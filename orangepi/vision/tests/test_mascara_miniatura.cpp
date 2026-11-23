// Pruebas de la mascara de la miniatura que usa mqtt_probe al imprimir un mensaje: la foto
// de un rostro nunca sale a la terminal, solo su tamano.

#include <string>

#include "minitest.hpp"
#include "vision/mascara_miniatura.hpp"

namespace {

using vision::enmascarar_miniatura;

TEST(un_valor_normal_se_reemplaza_por_su_tamano) {
  CHECK_EQ(enmascarar_miniatura("{\"cuenta\":\"1\",\"miniatura\":\"/9j/AAAA\",\"paso\":2}"),
           std::string("{\"cuenta\":\"1\",\"miniatura\":\"<8 caracteres>\",\"paso\":2}"));
}

TEST(sin_miniatura_el_mensaje_queda_igual) {
  const std::string sin = "{\"ts\":\"2026-11-04T09:12:33.120-06:00\",\"modo\":\"pase\"}";
  CHECK_EQ(enmascarar_miniatura(sin), sin);
}

TEST(un_json_vacio_o_una_cadena_vacia_quedan_igual) {
  CHECK_EQ(enmascarar_miniatura(""), std::string(""));
  CHECK_EQ(enmascarar_miniatura("{}"), std::string("{}"));
}

TEST(null_o_un_numero_no_son_una_foto_y_se_dejan) {
  CHECK_EQ(enmascarar_miniatura("{\"miniatura\":null,\"x\":1}"),
           std::string("{\"miniatura\":null,\"x\":1}"));
  CHECK_EQ(enmascarar_miniatura("{\"miniatura\":7,\"x\":1}"), std::string("{\"miniatura\":7,\"x\":1}"));
}

TEST(los_blancos_alrededor_de_los_dos_puntos_no_la_dejan_pasar) {
  CHECK_EQ(enmascarar_miniatura("{\"miniatura\" : \"ABCD\"}"),
           std::string("{\"miniatura\" : \"<4 caracteres>\"}"));
  CHECK_EQ(enmascarar_miniatura("{\"miniatura\"\n:\n\"ABCD\"}"),
           std::string("{\"miniatura\"\n:\n\"<4 caracteres>\"}"));
  CHECK_EQ(enmascarar_miniatura("{\"miniatura\"\r\n:\r\n\t\"ABCD\"}"),
           std::string("{\"miniatura\"\r\n:\r\n\t\"<4 caracteres>\"}"));
}

TEST(una_comilla_escapada_no_cierra_el_valor) {
  CHECK_EQ(enmascarar_miniatura("{\"miniatura\":\"ab\\\"c\",\"x\":1}"),
           std::string("{\"miniatura\":\"<5 caracteres>\",\"x\":1}"));
}

TEST(un_string_sin_cerrar_se_enmascara_hasta_el_final) {
  CHECK_EQ(enmascarar_miniatura("{\"miniatura\":\"QUJD"), std::string("{\"miniatura\":\"<4 caracteres>"));
}

TEST(una_barra_final_no_se_sale_del_mensaje) {
  CHECK_EQ(enmascarar_miniatura("{\"miniatura\":\"abc\\"), std::string("{\"miniatura\":\"<4 caracteres>"));
  CHECK_EQ(enmascarar_miniatura("{\"miniatura\":\"\\"), std::string("{\"miniatura\":\"<1 caracteres>"));
}

TEST(dos_apariciones_se_enmascaran_las_dos) {
  CHECK_EQ(enmascarar_miniatura("{\"miniatura\":\"A\",\"x\":{\"miniatura\":\"BB\"}}"),
           std::string("{\"miniatura\":\"<1 caracteres>\",\"x\":{\"miniatura\":\"<2 caracteres>\"}}"));
}

TEST(la_palabra_miniatura_como_valor_de_otra_clave_no_se_toca) {
  const std::string otra = "{\"campo\":\"miniatura\",\"otro\":\"x\"}";
  CHECK_EQ(enmascarar_miniatura(otra), otra);
  // Ni la clave sola o truncada, ni escapada dentro de un texto.
  CHECK_EQ(enmascarar_miniatura("{\"miniatura\""), std::string("{\"miniatura\""));
  CHECK_EQ(enmascarar_miniatura("\"miniatura\""), std::string("\"miniatura\""));
  CHECK_EQ(enmascarar_miniatura("{\"x\":\"\\\"miniatura\\\":\\\"A\\\"\"}"),
           std::string("{\"x\":\"\\\"miniatura\\\":\\\"A\\\"\"}"));
  // Y una vez enmascarada la foto, la palabra como valor que sigue tampoco.
  CHECK_EQ(enmascarar_miniatura("{\"miniatura\":\"AB\",\"campo\":\"miniatura\"}"),
           std::string("{\"miniatura\":\"<2 caracteres>\",\"campo\":\"miniatura\"}"));
}

TEST(un_valor_largo_solo_deja_su_tamano_y_ni_un_caracter_de_la_foto) {
  const std::string foto = "/9j/" + std::string(10920, 'Q');
  const std::string salida = enmascarar_miniatura("{\"miniatura\":\"" + foto + "\"}");
  CHECK_EQ(salida, std::string("{\"miniatura\":\"<10924 caracteres>\"}"));
  CHECK(salida.find("QQQ") == std::string::npos);
}

}  // namespace
