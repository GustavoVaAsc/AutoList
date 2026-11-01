#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vision {

enum class DataFormat { Fp32, Fp16, Uint8, Int8, Uint16, Int16, Other };
enum class QuantFormat { None, DynamicFixedPoint, Affine };

// Descripcion de un tensor de entrada o salida de una red .nb.
struct TensorInfo {
  std::string name;
  std::vector<std::uint32_t> dims;  // orden de VIPLite: [w, h, c, n]
  DataFormat format = DataFormat::Other;
  QuantFormat quant = QuantFormat::None;
  int fixed_point_pos = 0;   // solo DynamicFixedPoint: real = q * 2^-pos
  float scale = 1.0f;        // solo Affine: real = (q - zero_point) * scale
  std::int32_t zero_point = 0;
  std::size_t element_count = 0;  // producto de dims (tamano logico); 0 = desconocido
  std::size_t size_bytes = 0;     // bytes que reserva el driver; pueden ser mas que los logicos
};

std::string_view to_string(DataFormat format);
std::string_view to_string(QuantFormat quant);

// Bytes de un elemento; 0 si el formato es desconocido.
std::size_t element_size(DataFormat format);

// Convierte los bytes crudos de un tensor a float aplicando su cuantizacion. Si
// info.element_count no es 0, devuelve exactamente esos elementos y descarta el relleno
// que el driver pueda haber agregado al final de `raw`.
// Lanza std::invalid_argument si el formato no se soporta (solo fp32, uint8, int8, uint16
// e int16), si `raw` es mas corto que element_count elementos o, con element_count en 0,
// si su tamano no es multiplo del elemento.
std::vector<float> dequantize(const TensorInfo& info, std::span<const std::uint8_t> raw);

}  // namespace vision
