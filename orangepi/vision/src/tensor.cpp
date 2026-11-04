#include "vision/tensor.hpp"

#include <cmath>
#include <cstring>
#include <stdexcept>

namespace vision {

namespace {

std::size_t dequantizable_element_size(DataFormat format) {
  switch (format) {
    case DataFormat::Fp32: return 4;
    case DataFormat::Uint8:
    case DataFormat::Int8: return 1;
    case DataFormat::Uint16:
    case DataFormat::Int16: return 2;
    case DataFormat::Fp16:
    case DataFormat::Other: break;
  }
  throw std::invalid_argument("dequantize: formato no soportado: " + std::string(to_string(format)));
}

template <typename T>
T load(const std::uint8_t* p) {
  T value;
  std::memcpy(&value, p, sizeof(T));
  return value;
}

float read_integer(DataFormat format, const std::uint8_t* p) {
  switch (format) {
    case DataFormat::Uint8: return static_cast<float>(load<std::uint8_t>(p));
    case DataFormat::Int8: return static_cast<float>(load<std::int8_t>(p));
    case DataFormat::Uint16: return static_cast<float>(load<std::uint16_t>(p));
    default: return static_cast<float>(load<std::int16_t>(p));
  }
}

// Convierte un float a media precision (IEEE 754 binary16) con redondeo al mas cercano
// y al par. Se hace a mano para no depender de OpenCV en un archivo de tensores.
std::uint16_t float_to_half(float value) {
  std::uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  const std::uint32_t sign = (bits >> 16) & 0x8000u;
  const std::uint32_t exponent_bits = (bits >> 23) & 0xffu;
  const std::uint32_t mantissa = bits & 0x7fffffu;
  if (exponent_bits == 0xffu) {
    // Infinito o NaN: se conserva el NaN (con un bit de mantija).
    return static_cast<std::uint16_t>(sign | 0x7c00u | (mantissa != 0 ? 0x200u : 0u));
  }
  const std::int32_t exponent = static_cast<std::int32_t>(exponent_bits) - 127 + 15;
  if (exponent >= 0x1f) {
    // Desbordamiento: se satura al infinito.
    return static_cast<std::uint16_t>(sign | 0x7c00u);
  }
  if (exponent <= 0) {
    // Denormal: se normaliza la mantissa y se desplaza.
    if (exponent < -10) return static_cast<std::uint16_t>(sign);  // por debajo del rango
    const std::uint32_t mantissa_normalizada = mantissa | 0x800000u;
    const std::uint32_t desplazamiento = static_cast<std::uint32_t>(14 - exponent);
    const std::uint32_t entero = mantissa_normalizada >> desplazamiento;
    const std::uint32_t resto = mantissa_normalizada & ((1u << desplazamiento) - 1u);
    const std::uint32_t medio = 1u << (desplazamiento - 1);
    std::uint32_t resultado = entero;
    if (resto > medio || (resto == medio && (entero & 1u) != 0)) ++resultado;
    return static_cast<std::uint16_t>(sign | resultado);
  }
  std::uint32_t resultado = (static_cast<std::uint32_t>(exponent) << 10) | (mantissa >> 13);
  const std::uint32_t resto = mantissa & 0x1fffu;
  if (resto > 0x1000u || (resto == 0x1000u && (resultado & 1u) != 0)) ++resultado;
  return static_cast<std::uint16_t>(sign | resultado);
}

// Redondea al entero mas cercano (mitad hacia arriba) y recorta al rango del tipo.
// `recortado` dice si el valor estaba fuera del rango (lo que cuenta como saturacion).
//
// Un valor no finito (NaN o infinito) no se puede convertir a entero: en C++ eso es
// comportamiento indefinido, asi que se lanza antes de intentarlo.
int round_and_clip(float value, int minimo, int maximo, bool& recortado) {
  recortado = false;
  if (!std::isfinite(value)) {
    throw std::invalid_argument(
        "quantize: un valor no finito (NaN o infinito) no se puede cuantizar");
  }
  const float redondeado = std::round(value);
  if (redondeado < static_cast<float>(minimo)) {
    recortado = true;
    return minimo;
  }
  if (redondeado > static_cast<float>(maximo)) {
    recortado = true;
    return maximo;
  }
  return static_cast<int>(redondeado);
}

}  // namespace

std::size_t element_size(DataFormat format) {
  switch (format) {
    case DataFormat::Fp32: return 4;
    case DataFormat::Fp16:
    case DataFormat::Uint16:
    case DataFormat::Int16: return 2;
    case DataFormat::Uint8:
    case DataFormat::Int8: return 1;
    case DataFormat::Other: break;
  }
  return 0;
}

bool formato_desquantizable(DataFormat format) {
  switch (format) {
    case DataFormat::Fp32:
    case DataFormat::Uint8:
    case DataFormat::Int8:
    case DataFormat::Uint16:
    case DataFormat::Int16: return true;
    case DataFormat::Fp16:
    case DataFormat::Other: break;
  }
  return false;
}

void exigir_salida_descuantizable(const TensorInfo& info, const char* quien) {
  if (formato_desquantizable(info.format)) return;
  if (info.format == DataFormat::Fp16) {
    throw std::invalid_argument(std::string(quien) +
                                ": la salida del modelo es fp16: hay que ampliar "
                                "dequantize antes de usar este modelo");
  }
  throw std::invalid_argument(std::string(quien) + ": la salida del modelo tiene un formato "
                                "que dequantize no sabe leer (" +
                                std::string(to_string(info.format)) + ")");
}

std::string_view to_string(DataFormat format) {
  switch (format) {
    case DataFormat::Fp32: return "fp32";
    case DataFormat::Fp16: return "fp16";
    case DataFormat::Uint8: return "uint8";
    case DataFormat::Int8: return "int8";
    case DataFormat::Uint16: return "uint16";
    case DataFormat::Int16: return "int16";
    case DataFormat::Other: break;
  }
  return "other";
}

std::string_view to_string(QuantFormat quant) {
  switch (quant) {
    case QuantFormat::None: return "none";
    case QuantFormat::DynamicFixedPoint: return "dynamic_fixed_point";
    case QuantFormat::Affine: return "affine";
  }
  return "none";
}

std::vector<float> dequantize(const TensorInfo& info, std::span<const std::uint8_t> raw) {
  const std::size_t step = dequantizable_element_size(info.format);
  std::size_t count = raw.size() / step;
  if (info.element_count != 0) {
    if (info.element_count > count) {
      throw std::invalid_argument("dequantize: " + std::to_string(raw.size()) + " bytes no alcanzan para " +
                                  std::to_string(info.element_count) + " elementos de " +
                                  std::to_string(step));
    }
    count = info.element_count;
  } else if (raw.size() % step != 0) {
    throw std::invalid_argument("dequantize: " + std::to_string(raw.size()) +
                                " bytes no es multiplo del elemento de " + std::to_string(step));
  }

  std::vector<float> out(count);
  for (std::size_t i = 0; i < out.size(); ++i) {
    const std::uint8_t* p = raw.data() + i * step;
    if (info.format == DataFormat::Fp32) {
      out[i] = load<float>(p);
      continue;
    }
    const float q = read_integer(info.format, p);
    switch (info.quant) {
      case QuantFormat::Affine:
        out[i] = (q - static_cast<float>(info.zero_point)) * info.scale;
        break;
      case QuantFormat::DynamicFixedPoint:
        out[i] = std::ldexp(q, -info.fixed_point_pos);
        break;
      case QuantFormat::None:
        out[i] = q;
        break;
    }
  }
  return out;
}

std::vector<std::uint8_t> quantize(const TensorInfo& info, std::span<const float> values) {
  if (info.element_count != 0 && values.size() != info.element_count) {
    throw std::invalid_argument("quantize: el tensor declara " +
                                std::to_string(info.element_count) + " elementos y llegaron " +
                                std::to_string(values.size()));
  }
  if (info.quant == QuantFormat::Affine && !(info.scale > 0.0f)) {
    throw std::invalid_argument("quantize: la cuantizacion afín necesita scale > 0");
  }

  switch (info.format) {
    case DataFormat::Fp32: {
      std::vector<std::uint8_t> out(values.size() * sizeof(float));
      for (std::size_t i = 0; i < values.size(); ++i) {
        const float value = values[i];
        std::memcpy(out.data() + i * sizeof(float), &value, sizeof(float));
      }
      return out;
    }
    case DataFormat::Fp16: {
      std::vector<std::uint8_t> out(values.size() * sizeof(std::uint16_t));
      for (std::size_t i = 0; i < values.size(); ++i) {
        const std::uint16_t media = float_to_half(values[i]);
        std::memcpy(out.data() + i * sizeof(std::uint16_t), &media, sizeof(std::uint16_t));
      }
      return out;
    }
    case DataFormat::Uint8:
    case DataFormat::Int8: {
      const bool con_signo = info.format == DataFormat::Int8;
      const int minimo = con_signo ? -128 : 0;
      const int maximo = con_signo ? 127 : 255;
      std::vector<std::uint8_t> out(values.size());
      std::size_t recortados = 0;
      for (std::size_t i = 0; i < values.size(); ++i) {
        float cuantizado = 0.0f;
        switch (info.quant) {
          case QuantFormat::Affine:
            // q = round(real / scale) + zero_point
            cuantizado = std::round(values[i] / info.scale) +
                         static_cast<float>(info.zero_point);
            break;
          case QuantFormat::DynamicFixedPoint:
            // q = round(real * 2^pos)
            cuantizado = std::round(values[i] * std::ldexp(1.0f, info.fixed_point_pos));
            break;
          case QuantFormat::None:
            // q = round(real)
            cuantizado = std::round(values[i]);
            break;
        }
        bool recortado = false;
        const int valor = round_and_clip(cuantizado, minimo, maximo, recortado);
        if (recortado) ++recortados;
        out[i] = con_signo ? static_cast<std::uint8_t>(static_cast<std::int8_t>(valor))
                            : static_cast<std::uint8_t>(valor);
      }
      // Saturacion: si se recorta mas del 1% de los valores, el dominio real no
      // corresponde al tensor (por ejemplo, el byte de la imagen sobre un tensor
      // cuantificado con escala 1/255). Eso es un error de configuracion, no un dato.
      if (recortados * 100 > values.size()) {
        throw std::invalid_argument(
            "quantize: la entrada satura: " + std::to_string(recortados) + " de " +
            std::to_string(values.size()) +
            " valores se recortaron al rango del tensor; el modo de escala no corresponde "
            "al dominio del tensor");
      }
      return out;
    }
    case DataFormat::Uint16:
    case DataFormat::Int16:
    case DataFormat::Other:
      break;
  }
  throw std::invalid_argument("quantize: formato no soportado: " +
                              std::string(to_string(info.format)));
}

}  // namespace vision
