#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include <opencv2/core.hpp>

namespace vision {

enum class ImageFormat { Jpeg, Png };

struct ImageHeader {
  ImageFormat format;
  cv::Size size;
};

// Formato y dimensiones declaradas en la cabecera de un JPEG o PNG (IHDR), sin
// decodificar. nullopt si el formato es otro, la cabecera esta truncada o es invalida.
// Sirve para rechazar imagenes enormes antes de que cv::imdecode reserve su memoria.
//
// Del JPEG solo se aceptan los modos baseline (SOF0) y extendido secuencial (SOF1), de 8
// bits y 1 o 3 componentes, con un unico SOF y un primer scan entrelazado que lista todos
// los componentes (Ns == Nf, Ss=0, Se=63, Ah/Al=0). Los progresivos, aritmeticos, sin
// perdida, jerarquicos y los de varios scans dan nullopt: se pueden construir para gastar
// mucha CPU al decodificarlos. La cabecera debe llegar completa hasta el SOS.
std::optional<ImageHeader> peek_image_header(std::span<const std::uint8_t> bytes);

// Solo las dimensiones de peek_image_header.
std::optional<cv::Size> peek_image_size(std::span<const std::uint8_t> bytes);

}  // namespace vision
