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

// Simetrico de dequantize: convierte valores float a los bytes crudos que espera un
// tensor de entrada, aplicando su cuantizacion.
//
// Formato (el real es el valor que el modelo espera; el byte es lo que se escribe):
//  - fp32/fp16: los bytes del float en el endianness nativo (la placa es little-endian,
//    que es el que espera VIPLite). Para fp16 el float se convierte a media precision.
//  - uint8/int8 con cuantizacion afin: q = round(real / scale) + zero_point, recortado al
//    rango del tipo. Es el inverso de real = (q - zero_point) * scale.
//  - uint8/int8 sin cuantizacion (quant == None): q = round(real), recortado al rango del
//    tipo. El valor real es el byte de la imagen (0..255) cuando la normalizacion va
//    DENTRO del modelo cuantizado.
//  - uint8/int8 con punto fijo dinamico: q = round(real * 2^fixed_point_pos), recortado
//    al rango del tipo. Es el inverso de real = q * 2^-fixed_point_pos.
//
// El redondeo es al entero mas cercano (mitad hacia arriba). El recorte al rango del tipo
// es inevitable: un valor fuera de rango no es representable.
//
// Lanza std::invalid_argument si el formato no se soporta (solo fp32, fp16, uint8 e
// int8), si info.element_count no es 0 y el numero de valores no es exactamente ese, o
// si una cuantizacion afín trae scale <= 0.
std::vector<std::uint8_t> quantize(const TensorInfo& info, std::span<const float> values);

}  // namespace vision
