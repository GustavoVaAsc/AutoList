#include "puente_aula/jpeg.hpp"

namespace puente_aula {

std::optional<DimensionesJpeg> dimensiones_jpeg(const std::uint8_t* datos, std::size_t longitud) {
  if (datos == nullptr || longitud < 4 || datos[0] != 0xFF || datos[1] != 0xD8) return std::nullopt;
  std::size_t pos = 2;
  while (pos + 1 < longitud) {
    if (datos[pos] != 0xFF) return std::nullopt;
    // Los 0xFF de relleno antes de un marcador.
    while (pos < longitud && datos[pos] == 0xFF) ++pos;
    if (pos >= longitud) return std::nullopt;
    const std::uint8_t marcador = datos[pos++];
    // Marcadores sin longitud: TEM, RSTn, SOI. EOI o SOS: ya no hay SOF por delante.
    if (marcador == 0x01 || (marcador >= 0xD0 && marcador <= 0xD8)) continue;
    if (marcador == 0xD9 || marcador == 0xDA) return std::nullopt;
    if (pos + 2 > longitud) return std::nullopt;
    const std::size_t segmento = (static_cast<std::size_t>(datos[pos]) << 8) | datos[pos + 1];
    if (segmento < 2) return std::nullopt;
    // SOFn son C0-CF salvo C4 (DHT), C8 (JPG) y CC (DAC).
    const bool es_sof = marcador >= 0xC0 && marcador <= 0xCF && marcador != 0xC4 &&
                        marcador != 0xC8 && marcador != 0xCC;
    if (es_sof) {
      // Longitud(2) precision(1) alto(2) ancho(2)
      if (segmento < 7 || pos + 7 > longitud) return std::nullopt;
      DimensionesJpeg dimensiones;
      dimensiones.alto = (datos[pos + 3] << 8) | datos[pos + 4];
      dimensiones.ancho = (datos[pos + 5] << 8) | datos[pos + 6];
      if (dimensiones.alto == 0 || dimensiones.ancho == 0) return std::nullopt;
      return dimensiones;
    }
    pos += segmento;
  }
  return std::nullopt;
}

}  // namespace puente_aula
