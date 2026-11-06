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
  // Orden de VIPLite: de la dimension mas rapida a la mas lenta. La entrada puede ser
  // planar [w, h, c, n] o intercalada [c, w, h, n] (ver vision::input_shape).
  std::vector<std::uint32_t> dims;
  DataFormat format = DataFormat::Other;
  QuantFormat quant = QuantFormat::None;
  int fixed_point_pos = 0;   // solo DynamicFixedPoint: real = q * 2^-pos
  float scale = 1.0f;        // solo Affine: real = (q - zero_point) * scale
  std::int32_t zero_point = 0;
  std::size_t element_count = 0;  // producto de dims (tamano logico); 0 = desconocido
  std::size_t size_bytes = 0;     // bytes que reserva el driver; pueden ser mas que los logicos
};

// Tolerancia con la que se acepta que el rango real de un tensor no cubra del todo el
// dominio de un modo de escala: es el margen del redondeo de las cuantificaciones
// habituales (un uint8 con escala 1/127,5 y zero_point 128 representa [-1,0039;
// 0,9961] y su extremo superior se pierde por redondeo). La MISMA tolerancia se usa en
// la comprobacion estatica (al arrancar) y en la de saturacion (por fotograma), para
// que las dos no se contradigan.
inline constexpr float kToleranciaRango = 0.01f;

std::string_view to_string(DataFormat format);
std::string_view to_string(QuantFormat quant);

// Bytes de un elemento; 0 si el formato es desconocido.
std::size_t element_size(DataFormat format);

// true si dequantize sabe leer un tensor con ese formato. El fp16 NO se soporta:
// una salida del modelo en fp16 hay que ampliarla antes de usarla.
bool formato_desquantizable(DataFormat format);

// Exige que la salida de un modelo se pueda desquantizar, con un mensaje que diga que
// hacer si no se puede (por ejemplo, "la salida del modelo es fp16: hay que ampliar
// dequantize"). `quien` es el nombre del adaptador, para el mensaje.
//
// Lanza std::invalid_argument si dequantize no sabe leer el formato del tensor.
void exigir_salida_descuantizable(const TensorInfo& info, const char* quien);

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
// es inevitable: un valor fuera de rango no es representable. PERO si se recorta mas del
// 1% de los valores, la entrada satura: eso no es un dato de entrada, es un error de
// configuracion (el modo de escala no corresponde al dominio del tensor) y se lanza
// std::invalid_argument en vez de dejar un tensor lleno de valores recortados.
//
// Lanza std::invalid_argument si el formato no se soporta (solo fp32, fp16, uint8 e
// int8), si info.element_count no es 0 y el numero de valores no es exactamente ese, si
// una cuantizacion afín trae scale <= 0, si un valor no es finito o si la entrada
// satura (mas del 1% de los valores se recortan).
std::vector<std::uint8_t> quantize(const TensorInfo& info, std::span<const float> values);

}  // namespace vision
