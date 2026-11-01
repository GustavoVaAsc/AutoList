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

}  // namespace vision
