#include <vector>

#include "minitest.hpp"
#include "puente_aula/jpeg.hpp"

using namespace puente_aula;

namespace {

using Bytes = std::vector<std::uint8_t>;

void agregar(Bytes& destino, std::initializer_list<std::uint8_t> bytes) {
  for (const std::uint8_t byte : bytes) destino.push_back(byte);
}

// SOI, un APP0 de relleno, un DHT (C4: no es SOF) y el SOF indicado.
Bytes jpeg_con_sof(std::uint8_t marcador_sof, int ancho, int alto) {
  Bytes datos;
  agregar(datos, {0xFF, 0xD8});
  agregar(datos, {0xFF, 0xE0, 0x00, 0x04, 0x00, 0x00});
  agregar(datos, {0xFF, 0xC4, 0x00, 0x03, 0x00});
  agregar(datos, {0xFF, 0xFF, marcador_sof, 0x00, 0x0B, 0x08,
                  static_cast<std::uint8_t>(alto >> 8), static_cast<std::uint8_t>(alto & 0xFF),
                  static_cast<std::uint8_t>(ancho >> 8), static_cast<std::uint8_t>(ancho & 0xFF),
                  0x01, 0x01, 0x11, 0x00});
  agregar(datos, {0xFF, 0xDA, 0x00, 0x02});
  return datos;
}

}  // namespace

TEST(jpeg_lee_ancho_y_alto_del_sof0) {
  const Bytes datos = jpeg_con_sof(0xC0, 800, 600);
  const auto dim = dimensiones_jpeg(datos.data(), datos.size());
  CHECK(dim.has_value());
  CHECK_EQ(dim->ancho, 800);
  CHECK_EQ(dim->alto, 600);
}

TEST(jpeg_lee_el_sof2_progresivo_y_dimensiones_grandes) {
  const Bytes datos = jpeg_con_sof(0xC2, 2048, 1536);
  const auto dim = dimensiones_jpeg(datos.data(), datos.size());
  CHECK(dim.has_value());
  CHECK_EQ(dim->ancho, 2048);
  CHECK_EQ(dim->alto, 1536);
}

TEST(jpeg_rechaza_lo_que_no_es_o_esta_truncado) {
  const Bytes texto = {'h', 'o', 'l', 'a', '!', '!'};
  CHECK(!dimensiones_jpeg(texto.data(), texto.size()).has_value());
  CHECK(!dimensiones_jpeg(nullptr, 0).has_value());
  const Bytes completo = jpeg_con_sof(0xC0, 800, 600);
  // Cortado en cualquier punto antes del final del SOF: nunca lee fuera ni inventa medidas.
  for (std::size_t corte = 0; corte < 21; ++corte) {
    CHECK(!dimensiones_jpeg(completo.data(), corte).has_value());
  }
}

TEST(jpeg_sin_sof_antes_de_los_datos_de_imagen) {
  Bytes datos;
  agregar(datos, {0xFF, 0xD8, 0xFF, 0xDA, 0x00, 0x02, 0x00, 0x00});
  CHECK(!dimensiones_jpeg(datos.data(), datos.size()).has_value());
}

TEST(jpeg_longitud_de_segmento_absurda_no_se_sale) {
  Bytes datos;
  agregar(datos, {0xFF, 0xD8, 0xFF, 0xE0, 0xFF, 0xFF, 0x00});
  CHECK(!dimensiones_jpeg(datos.data(), datos.size()).has_value());
  Bytes cero;
  agregar(cero, {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x00, 0x00, 0x00});
  CHECK(!dimensiones_jpeg(cero.data(), cero.size()).has_value());
}
