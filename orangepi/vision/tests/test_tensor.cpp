#include "minitest.hpp"
#include "vision/tensor.hpp"

namespace {

using namespace vision;

TensorInfo info(DataFormat format, QuantFormat quant) {
  TensorInfo t;
  t.format = format;
  t.quant = quant;
  return t;
}

}  // namespace

TEST(dequantize_uint8_affine) {
  TensorInfo t = info(DataFormat::Uint8, QuantFormat::Affine);
  t.scale = 0.5f;
  t.zero_point = 128;
  const std::vector<std::uint8_t> raw = {128, 130, 120};
  const std::vector<float> out = dequantize(t, raw);
  CHECK_EQ(out.size(), std::size_t{3});
  CHECK_NEAR(out[0], 0.0, 1e-6);
  CHECK_NEAR(out[1], 1.0, 1e-6);
  CHECK_NEAR(out[2], -4.0, 1e-6);
}

TEST(dequantize_int8_affine_with_negative_zero_point) {
  TensorInfo t = info(DataFormat::Int8, QuantFormat::Affine);
  t.scale = 0.25f;
  t.zero_point = -10;
  const std::vector<std::uint8_t> raw = {0xF6 /* -10 */, 0x00, 0x7F};
  const std::vector<float> out = dequantize(t, raw);
  CHECK_NEAR(out[0], 0.0, 1e-6);
  CHECK_NEAR(out[1], 2.5, 1e-6);
  CHECK_NEAR(out[2], (127 + 10) * 0.25, 1e-6);
}

TEST(dequantize_int16_dynamic_fixed_point) {
  TensorInfo t = info(DataFormat::Int16, QuantFormat::DynamicFixedPoint);
  t.fixed_point_pos = 8;
  const std::int16_t values[] = {256, -128};
  const auto* bytes = reinterpret_cast<const std::uint8_t*>(values);
  const std::vector<float> out = dequantize(t, std::span<const std::uint8_t>(bytes, sizeof(values)));
  CHECK_EQ(out.size(), std::size_t{2});
  CHECK_NEAR(out[0], 1.0, 1e-6);
  CHECK_NEAR(out[1], -0.5, 1e-6);
}

TEST(dequantize_fp32_is_a_copy) {
  const float values[] = {1.5f, -2.0f};
  const auto* bytes = reinterpret_cast<const std::uint8_t*>(values);
  const std::vector<float> out = dequantize(info(DataFormat::Fp32, QuantFormat::None),
                                            std::span<const std::uint8_t>(bytes, sizeof(values)));
  CHECK_EQ(out.size(), std::size_t{2});
  CHECK_NEAR(out[0], 1.5, 1e-9);
  CHECK_NEAR(out[1], -2.0, 1e-9);
}

TEST(dequantize_rejects_unsupported_format_and_ragged_size) {
  const std::vector<std::uint8_t> raw = {1, 2, 3};
  CHECK_THROWS(dequantize(info(DataFormat::Fp16, QuantFormat::None), raw), std::invalid_argument);
  CHECK_THROWS(dequantize(info(DataFormat::Other, QuantFormat::None), raw), std::invalid_argument);
  CHECK_THROWS(dequantize(info(DataFormat::Int16, QuantFormat::None), raw), std::invalid_argument);
}

TEST(dequantize_keeps_only_the_logical_elements_of_a_padded_buffer) {
  TensorInfo t = info(DataFormat::Uint8, QuantFormat::Affine);
  t.scale = 1.0f;
  t.zero_point = 0;
  t.element_count = 3;
  t.size_bytes = 8;  // el driver reservo mas de lo logico
  const std::vector<std::uint8_t> raw = {1, 2, 3, 99, 99, 99, 99, 99};
  const std::vector<float> out = dequantize(t, raw);
  CHECK_EQ(out.size(), std::size_t{3});
  CHECK_NEAR(out[2], 3.0, 1e-6);

  TensorInfo wide = info(DataFormat::Int16, QuantFormat::None);
  wide.element_count = 2;
  const std::vector<std::uint8_t> padded = {1, 0, 2, 0, 7};  // 5 bytes: 2 elementos y un resto
  CHECK_EQ(dequantize(wide, padded).size(), std::size_t{2});
}

TEST(dequantize_rejects_a_buffer_shorter_than_the_logical_size) {
  TensorInfo t = info(DataFormat::Fp32, QuantFormat::None);
  t.element_count = 3;
  CHECK_THROWS(dequantize(t, std::vector<std::uint8_t>(8)), std::invalid_argument);
}

TEST(element_size_covers_every_format) {
  CHECK_EQ(element_size(DataFormat::Fp32), std::size_t{4});
  CHECK_EQ(element_size(DataFormat::Fp16), std::size_t{2});
  CHECK_EQ(element_size(DataFormat::Int16), std::size_t{2});
  CHECK_EQ(element_size(DataFormat::Uint8), std::size_t{1});
  CHECK_EQ(element_size(DataFormat::Other), std::size_t{0});
}
