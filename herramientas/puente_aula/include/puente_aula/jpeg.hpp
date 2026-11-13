#pragma once

// Dimensiones de una JPEG leidas del marcador SOF (sin decodificarla).

#include <cstddef>
#include <cstdint>
#include <optional>

namespace puente_aula {

struct DimensionesJpeg {
  int ancho = 0;
  int alto = 0;
};

// std::nullopt si no empieza con SOI, si el SOF no aparece antes de los datos de imagen (SOS)
// o si el archivo esta truncado. Todo acceso se comprueba contra `longitud`.
std::optional<DimensionesJpeg> dimensiones_jpeg(const std::uint8_t* datos, std::size_t longitud);

}  // namespace puente_aula
