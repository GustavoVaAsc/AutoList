// Pruebas de la cuantizacion de la entrada y del preproceso de imagen: son funciones
// puras, asi que no necesitan NPU ni modelo.
#include <cstdint>
#include <cstring>
#include <vector>

#include <opencv2/core.hpp>

#include "minitest.hpp"
#include "vision/npu_preprocess.hpp"
#include "vision/tensor.hpp"

namespace {

using namespace vision;

TensorInfo tensor(DataFormat format, QuantFormat quant, std::vector<std::uint32_t> dims) {
  TensorInfo info;
  info.format = format;
  info.quant = quant;
  info.dims = std::move(dims);
  std::size_t elementos = 1;
  for (std::uint32_t d : info.dims) elementos *= d;
  info.element_count = elementos;
  info.size_bytes = elementos * element_size(format);
  return info;
}

// Imagen 2x2 con pixeles BGR consecutivos: (1,2,3), (4,5,6), (7,8,9), (10,11,12).
cv::Mat imagen_de_prueba() {
  cv::Mat imagen(2, 2, CV_8UC3);
  const std::uint8_t pixeles[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
  std::size_t i = 0;
  for (int y = 0; y < 2; ++y) {
    for (int x = 0; x < 2; ++x) {
      for (int c = 0; c < 3; ++c) imagen.at<cv::Vec3b>(y, x)[c] = pixeles[i++];
    }
  }
  return imagen;
}

}  // namespace

TEST(quantize_uint8_sin_cuantizacion_es_el_valor_redondeado) {
  const TensorInfo info = tensor(DataFormat::Uint8, QuantFormat::None, {3});
  const std::vector<float> valores = {0.0f, 127.5f, 255.0f};
  const std::vector<std::uint8_t> bytes = quantize(info, valores);
  CHECK_EQ(bytes.size(), std::size_t{3});
  CHECK_EQ(static_cast<int>(bytes[0]), 0);
  CHECK_EQ(static_cast<int>(bytes[1]), 128);
  CHECK_EQ(static_cast<int>(bytes[2]), 255);
}

TEST(quantize_uint8_afin_es_el_inverso_de_dequantize) {
  TensorInfo info = tensor(DataFormat::Uint8, QuantFormat::Affine, {3});
  info.scale = 0.5f;
  info.zero_point = 128;
  const std::vector<float> valores = {0.0f, 1.0f, -4.0f};
  const std::vector<std::uint8_t> bytes = quantize(info, valores);
  CHECK_EQ(static_cast<int>(bytes[0]), 128);
  CHECK_EQ(static_cast<int>(bytes[1]), 130);
  CHECK_EQ(static_cast<int>(bytes[2]), 120);

  const std::vector<float> vuelta = dequantize(info, bytes);
  CHECK_EQ(vuelta.size(), std::size_t{3});
  CHECK_NEAR(vuelta[0], 0.0, 1e-6);
  CHECK_NEAR(vuelta[1], 1.0, 1e-6);
  CHECK_NEAR(vuelta[2], -4.0, 1e-6);
}

TEST(quantize_int8_afin_acepta_el_zero_point_negativo) {
  TensorInfo info = tensor(DataFormat::Int8, QuantFormat::Affine, {2});
  info.scale = 0.25f;
  info.zero_point = -10;
  const std::vector<std::uint8_t> bytes = quantize(info, std::vector<float>{0.0f, 2.5f});
  CHECK_EQ(static_cast<int>(bytes[0]), 0xF6);  // -10 en complemento a dos
  CHECK_EQ(static_cast<int>(bytes[1]), 0x00);
}

TEST(quantize_recorta_al_rango_del_tipo) {
  const TensorInfo info = tensor(DataFormat::Uint8, QuantFormat::None, {3});
  const std::vector<std::uint8_t> bytes = quantize(info, std::vector<float>{-10.0f, 300.0f, 200.4f});
  CHECK_EQ(static_cast<int>(bytes[0]), 0);
  CHECK_EQ(static_cast<int>(bytes[1]), 255);
  CHECK_EQ(static_cast<int>(bytes[2]), 200);

  const TensorInfo con_signo = tensor(DataFormat::Int8, QuantFormat::None, {2});
  const std::vector<std::uint8_t> bytes_con_signo = quantize(con_signo, std::vector<float>{-200.0f, 200.0f});
  CHECK_EQ(static_cast<int>(bytes_con_signo[0]), 0x80);  // -128
  CHECK_EQ(static_cast<int>(bytes_con_signo[1]), 0x7F);  // 127
}

TEST(quantize_fp32_escribe_los_bytes_del_float) {
  const TensorInfo info = tensor(DataFormat::Fp32, QuantFormat::None, {2});
  const std::vector<float> valores = {1.5f, -2.0f};
  const std::vector<std::uint8_t> bytes = quantize(info, valores);
  CHECK_EQ(bytes.size(), std::size_t{8});
  float leida = 0.0f;
  std::memcpy(&leida, bytes.data(), sizeof(float));
  CHECK_NEAR(leida, 1.5, 1e-9);
  std::memcpy(&leida, bytes.data() + 4, sizeof(float));
  CHECK_NEAR(leida, -2.0, 1e-9);
}

TEST(quantize_fp16_convierte_a_media_precision) {
  const TensorInfo info = tensor(DataFormat::Fp16, QuantFormat::None, {3});
  const std::vector<std::uint8_t> bytes = quantize(info, std::vector<float>{1.0f, -2.0f, 0.5f});
  CHECK_EQ(bytes.size(), std::size_t{6});
  std::uint16_t media = 0;
  std::memcpy(&media, bytes.data(), sizeof(std::uint16_t));
  CHECK_EQ(static_cast<int>(media), 0x3C00);  // 1.0
  std::memcpy(&media, bytes.data() + 2, sizeof(std::uint16_t));
  CHECK_EQ(static_cast<int>(media), 0xC000);  // -2.0
  std::memcpy(&media, bytes.data() + 4, sizeof(std::uint16_t));
  CHECK_EQ(static_cast<int>(media), 0x3800);  // 0.5
}

TEST(quantize_rechaza_formatos_no_soportados_y_tamanos_que_no_cuadran) {
  const std::vector<float> valores = {1.0f, 2.0f};
  CHECK_THROWS(quantize(tensor(DataFormat::Uint16, QuantFormat::None, {2}), valores),
               std::invalid_argument);
  CHECK_THROWS(quantize(tensor(DataFormat::Int16, QuantFormat::None, {2}), valores),
               std::invalid_argument);
  CHECK_THROWS(quantize(tensor(DataFormat::Other, QuantFormat::None, {2}), valores),
               std::invalid_argument);
  CHECK_THROWS(quantize(tensor(DataFormat::Uint8, QuantFormat::None, {3}), valores),
               std::invalid_argument);
}

TEST(el_preproceso_escribe_canal_a_canal_en_el_orden_del_tensor) {
  const TensorInfo info = tensor(DataFormat::Uint8, QuantFormat::None, {2, 2, 3, 1});
  PreprocessOptions options;
  options.color_order = ColorOrder::Rgb;

  const std::vector<float> valores = preprocess_to_floats(imagen_de_prueba(), info, options);
  CHECK_EQ(valores.size(), std::size_t{12});
  // Canal 0 (rojo) de los cuatro pixeles, luego el verde y luego el azul.
  const std::vector<float> esperado = {3, 6, 9, 12, 2, 5, 8, 11, 1, 4, 7, 10};
  for (std::size_t i = 0; i < valores.size(); ++i) {
    CHECK_NEAR(valores[i], esperado[i], 1e-6);
  }
}

TEST(el_preproceso_con_bgr_invierte_los_canales) {
  const TensorInfo info = tensor(DataFormat::Uint8, QuantFormat::None, {2, 2, 3, 1});
  PreprocessOptions options;
  options.color_order = ColorOrder::Bgr;
  const std::vector<float> valores = preprocess_to_floats(imagen_de_prueba(), info, options);
  const std::vector<float> esperado = {1, 4, 7, 10, 2, 5, 8, 11, 3, 6, 9, 12};
  for (std::size_t i = 0; i < valores.size(); ++i) {
    CHECK_NEAR(valores[i], esperado[i], 1e-6);
  }
}

TEST(el_preproceso_arcface_normaliza_a_menos_uno_y_uno) {
  const TensorInfo info = tensor(DataFormat::Fp32, QuantFormat::None, {2, 2, 3, 1});
  PreprocessOptions options;
  options.scale_mode = ScaleMode::ArcFace;
  const std::vector<float> valores = preprocess_to_floats(imagen_de_prueba(), info, options);
  CHECK_NEAR(valores[0], (3.0f - 127.5f) / 127.5f, 1e-6);
  CHECK_NEAR(valores[1], (6.0f - 127.5f) / 127.5f, 1e-6);
}

TEST(el_preproceso_rehace_el_tamano_de_la_imagen_al_del_tensor) {
  const TensorInfo info = tensor(DataFormat::Uint8, QuantFormat::None, {2, 2, 3, 1});
  cv::Mat imagen(4, 4, CV_8UC3, cv::Scalar(7, 9, 11));
  const std::vector<float> valores = preprocess_to_floats(imagen, info, PreprocessOptions{});
  CHECK_EQ(valores.size(), std::size_t{12});
  for (std::size_t i = 0; i < valores.size(); ++i) {
    CHECK_NEAR(valores[i], i < 4 ? 11.0 : (i < 8 ? 9.0 : 7.0), 1e-6);
  }
}

TEST(el_preproceso_rechaza_una_imagen_que_no_cuadra_con_el_tensor) {
  const TensorInfo info = tensor(DataFormat::Uint8, QuantFormat::None, {2, 2, 3, 1});
  CHECK_THROWS(preprocess_to_floats(cv::Mat(), info, PreprocessOptions{}), std::invalid_argument);
  cv::Mat gris(2, 2, CV_8UC1, cv::Scalar(3));
  CHECK_THROWS(preprocess_to_floats(gris, info, PreprocessOptions{}), std::invalid_argument);
  // Un tensor de un solo canal no tiene sentido con una imagen BGR.
  const TensorInfo un_canal = tensor(DataFormat::Uint8, QuantFormat::None, {2, 2, 1, 1});
  CHECK_THROWS(preprocess_to_floats(imagen_de_prueba(), un_canal, PreprocessOptions{}),
               std::invalid_argument);
  // Un tensor sin las tres dimensiones minimas no se puede interpretar.
  const TensorInfo incompleto = tensor(DataFormat::Uint8, QuantFormat::None, {2, 2});
  CHECK_THROWS(preprocess_to_floats(imagen_de_prueba(), incompleto, PreprocessOptions{}),
               std::invalid_argument);
}

TEST(el_preproceso_rechaza_la_escala_arcface_sobre_un_tensor_de_ocho_bits) {
  const TensorInfo info = tensor(DataFormat::Uint8, QuantFormat::None, {2, 2, 3, 1});
  PreprocessOptions options;
  options.scale_mode = ScaleMode::ArcFace;
  CHECK_THROWS(preprocess_to_floats(imagen_de_prueba(), info, options), std::invalid_argument);
}

TEST(el_preproceso_unitario_codifica_el_dominio_real_con_la_cuantizacion_del_tensor) {
  // Un tensor de entrada cuantificado con escala 1/255 (como el de la red de ejemplo del
  // fabricante) espera un dominio real de [0,1]: el byte se recupera al codificar.
  TensorInfo info = tensor(DataFormat::Uint8, QuantFormat::Affine, {2, 2, 3, 1});
  info.scale = 1.0f / 255.0f;
  info.zero_point = 0;
  PreprocessOptions options;
  options.scale_mode = ScaleMode::Unit;
  const std::vector<std::uint8_t> bytes =
      preprocess_to_bytes(imagen_de_prueba(), info, options);
  CHECK_EQ(bytes.size(), info.size_bytes);
  CHECK_EQ(static_cast<int>(bytes[0]), 3);    // rojo del pixel (0,0)
  CHECK_EQ(static_cast<int>(bytes[11]), 10);  // azul del pixel (1,1)

  // El valor real que se cuantiza es el byte dividido entre 255.
  const std::vector<float> valores = preprocess_to_floats(imagen_de_prueba(), info, options);
  CHECK_NEAR(valores[0], 3.0f / 255.0f, 1e-9);
}

TEST(los_bytes_del_preproceso_cubren_exactamente_el_tamano_del_tensor) {
  TensorInfo info = tensor(DataFormat::Uint8, QuantFormat::None, {2, 2, 3, 1});
  const std::vector<std::uint8_t> bytes =
      preprocess_to_bytes(imagen_de_prueba(), info, PreprocessOptions{});
  CHECK_EQ(bytes.size(), info.size_bytes);
  // El pixel (0,0) es BGR (1,2,3): con RGB el primer byte es el 3.
  CHECK_EQ(static_cast<int>(bytes[0]), 3);
  CHECK_EQ(static_cast<int>(bytes[1]), 6);
  CHECK_EQ(static_cast<int>(bytes[2]), 9);
  CHECK_EQ(static_cast<int>(bytes[3]), 12);

  // Si el driver reservo mas bytes, el relleno va en ceros.
  info.size_bytes = 16;
  const std::vector<std::uint8_t> rellenados =
      preprocess_to_bytes(imagen_de_prueba(), info, PreprocessOptions{});
  CHECK_EQ(rellenados.size(), std::size_t{16});
  CHECK_EQ(static_cast<int>(rellenados[12]), 0);
}
