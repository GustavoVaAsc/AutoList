// Requiere /dev/vipcore: solo corre en la placa.
//
// Prueba de punta a punta del cableado de los adaptadores sobre la NPU. Usa la red de
// ejemplo del fabricante (/opt/vpm_run/network_binary.nb: entrada 224x224x3 uint8 y una
// salida de 2 valores), que no es un modelo de ArcFace ni de RetinaFace: el adaptador
// tiene que ser generico en la dimension de la salida (no se exige 512).
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <vector>

#include <opencv2/core.hpp>

#include "minitest.hpp"
#include "vision/npu.hpp"
#include "vision/npu_face_detector.hpp"
#include "vision/npu_face_embedder.hpp"
#include "vision/npu_preprocess.hpp"

namespace {

const char* kModelo = "/opt/vpm_run/network_binary.nb";

bool hay_modelo() { return std::filesystem::exists(kModelo); }

}  // namespace

TEST(los_bytes_del_preproceso_miden_exactamente_lo_que_pide_el_tensor) {
  if (!hay_modelo()) return;
  vision::npu::Driver driver;
  vision::npu::Network red(driver, kModelo);
  const vision::TensorInfo& entrada = red.input_info(0);
  CHECK_EQ(entrada.element_count, std::size_t{224} * 224 * 3);
  CHECK_EQ(static_cast<int>(entrada.format), static_cast<int>(vision::DataFormat::Uint8));
  // El tensor de entrada del modelo del fabricante esta cuantificado de forma afín con
  // escala 1/255: su dominio real es [0,1], no el byte de la imagen.
  CHECK_EQ(static_cast<int>(entrada.quant), static_cast<int>(vision::QuantFormat::Affine));
  CHECK_NEAR(entrada.scale, 1.0f / 255.0f, 1e-6);

  const cv::Mat imagen(224, 224, CV_8UC3, cv::Scalar(10, 20, 30));
  // El modo de escala se deriva del tensor (por omision): con la escala 1/255 el
  // dominio real es [0,1] y el byte de la imagen se recupera tal cual.
  vision::PreprocessOptions opciones;
  const std::vector<std::uint8_t> bytes = vision::preprocess_to_bytes(imagen, entrada, opciones);
  CHECK_EQ(bytes.size(), entrada.size_bytes);
  // Con el orden RGB, el primer canal es el rojo (30) de todos los pixeles.
  CHECK_EQ(static_cast<int>(bytes[0]), 30);
}

// La entrada del modelo NO puede saturar: si se satura, la red ve una imagen casi
// blanca y la decision biometrica queda en fallo abierto.
TEST(la_entrada_del_modelo_del_fabricante_no_satura) {
  if (!hay_modelo()) return;
  vision::npu::Driver driver;
  vision::npu::Network red(driver, kModelo);
  const vision::TensorInfo& entrada = red.input_info(0);

  const cv::Mat imagen(224, 224, CV_8UC3, cv::Scalar(10, 20, 30));
  const std::vector<std::uint8_t> bytes =
      vision::preprocess_to_bytes(imagen, entrada, vision::PreprocessOptions{});
  std::size_t saturados = 0;
  for (const std::uint8_t byte : bytes) {
    if (byte == 255) ++saturados;
  }
  CHECK_EQ(saturados, std::size_t{0});
  // El pixel (0,0) es BGR (10,20,30): con RGB el primer byte es el 30.
  CHECK_EQ(static_cast<int>(bytes[0]), 30);
}

TEST(el_embedder_sobre_la_npu_corre_de_punta_a_punta) {
  if (!hay_modelo()) return;
  vision::npu::Driver driver;
  vision::npu::Network red(driver, kModelo);
  const std::size_t salidas = red.output_info(0).element_count;

  vision::NpuEmbedderOptions options;
  options.input_width = 224;
  options.input_height = 224;
  // El modelo de ejemplo del fabricante da 2 valores: no es un modelo de
  // reconocimiento, asi que el minimo del rango se baja para poder ejercitar el
  // cableado (el techo de produccion sigue en pie).
  options.min_output_elements = 1;
  vision::NpuFaceEmbedder embedder(driver, kModelo, options);

  const cv::Mat imagen(224, 224, CV_8UC3, cv::Scalar(10, 20, 30));
  const auto inicio = std::chrono::steady_clock::now();
  const std::vector<float> embedding = embedder.embed(imagen);
  const auto fin = std::chrono::steady_clock::now();
  const double ms = std::chrono::duration<double, std::milli>(fin - inicio).count();
  std::cout << "[npu] inferencia del embedder sobre la NPU: " << ms << " ms\n";
  // La dimension la fija el modelo en tiempo de ejecucion (aqui, 2 valores).
  CHECK_EQ(embedding.size(), salidas);
  for (float valor : embedding) CHECK(std::isfinite(valor));
}

TEST(el_embedder_rechaza_un_tamano_de_entrada_que_no_cuadra_con_el_modelo) {
  if (!hay_modelo()) return;
  vision::npu::Driver driver;
  // Por omision el embedder toma el tamano del .nb; un tamano pedido que no cuadra con
  // el tensor se rechaza (el modelo del fabricante es de 224x224).
  vision::NpuEmbedderOptions options;
  options.input_width = 112;
  options.input_height = 112;
  CHECK_THROWS(vision::NpuFaceEmbedder(driver, kModelo, options), std::invalid_argument);
}

TEST(el_detector_rechaza_un_tamano_de_entrada_que_no_cuadra_con_el_modelo) {
  if (!hay_modelo()) return;
  vision::npu::Driver driver;
  // Igual que el embedder: por omision se toma el tamano del .nb; un tamano pedido que
  // no cuadra se rechaza (el modelo del fabricante es de 224x224).
  vision::NpuDetectorOptions options;
  options.input_width = 640;
  options.input_height = 640;
  CHECK_THROWS(vision::NpuFaceDetector(driver, kModelo, options), std::invalid_argument);
}

TEST(el_embedder_rechaza_una_salida_que_no_puede_ser_un_embedding) {
  if (!hay_modelo()) return;
  vision::npu::Driver driver;
  // El modelo de ejemplo del fabricante da 2 valores: con el rango de produccion (64 a
  // 8192) se rechaza, porque un embedding de 2 valores no es un embedding.
  vision::NpuEmbedderOptions options;
  options.input_width = 224;
  options.input_height = 224;
  CHECK_THROWS(vision::NpuFaceEmbedder(driver, kModelo, options), std::invalid_argument);
}

// --- Tercera ronda: el tensor de entrada se valida AL ARRANCAR ------------------
//
// El modelo del fabricante (uint8 afin con escala 1/255 y zero_point 0) tiene que
// pasar la validacion de entrada y deducir el modo Unit: con el, la entrada no satura.
TEST(el_modelo_del_fabricante_pasa_la_validacion_de_entrada) {
  if (!hay_modelo()) return;
  vision::npu::Driver driver;
  vision::npu::Network red(driver, kModelo);
  const vision::TensorInfo& entrada = red.input_info(0);

  // La validacion de arranque no lanza con el tensor del fabricante.
  vision::validar_tensor_entrada(entrada, vision::PreprocessOptions{}, "NpuFaceEmbedder");
  // El modo deducido es Unit (dominio real [0,1]).
  CHECK_EQ(static_cast<int>(vision::modo_escala_efectivo(vision::ScaleMode::Auto, entrada)),
           static_cast<int>(vision::ScaleMode::Unit));
}

// Un modo de escala que no cuadra con el tensor tiene que hacer fallar el PROCESO al
// arrancar (al construir el adaptador), no en el primer fotograma.
TEST(el_embedder_rechaza_un_modo_de_escala_incompatible_al_arrancar) {
  if (!hay_modelo()) return;
  vision::npu::Driver driver;
  vision::NpuEmbedderOptions options;
  options.input_width = 224;
  options.input_height = 224;
  // El tensor del fabricante esta cuantificado con escala 1/255: su dominio es [0,1], no
  // [-1,1]. Con ArcFace, el adaptador tiene que.rejectarlo al construirlo.
  options.scale_mode = vision::ScaleMode::ArcFace;
  CHECK_THROWS(vision::NpuFaceEmbedder(driver, kModelo, options), std::invalid_argument);
}

// Con el tamano de entrada en 0, se toma el del .nb (224 en el modelo del fabricante).
TEST(el_embedder_toma_el_tamano_de_entrada_del_modelo_cuando_se_pide_cero) {
  if (!hay_modelo()) return;
  vision::npu::Driver driver;
  vision::NpuEmbedderOptions options;
  options.input_width = 0;
  options.input_height = 0;
  options.min_output_elements = 1;
  vision::NpuFaceEmbedder embedder(driver, kModelo, options);
  const cv::Mat imagen(224, 224, CV_8UC3, cv::Scalar(10, 20, 30));
  const std::vector<float> embedding = embedder.embed(imagen);
  CHECK(!embedding.empty());
}
