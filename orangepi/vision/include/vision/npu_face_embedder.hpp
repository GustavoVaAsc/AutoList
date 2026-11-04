#pragma once

// Adaptador del embedder sobre la NPU: recorte alineado de 112x112 -> embedding.
//
// El recorte alineado se preprocesa (resize, orden de color, escala y cuantizacion) y
// se pasa a la red .nb; la salida cruda se desquantiza con vision::dequantize y se
// devuelve tal cual: el servicio normaliza al final (l2_normalized), asi que aqui NO se
// normaliza.
//
// El embedder entrega lo que da el modelo: la dimension la fija el .nb en tiempo de
// ejecucion (no se asume 128 ni 512).
//
// LIMITACION CONOCIDA: la desquantizacion (vision::dequantize) no todavia sabe leer una
// salida en fp16 (solo fp32 y enteros). Si el .nb devuelve su salida en fp16, el
// constructor lo dice (std::invalid_argument) en vez de fallar en cada inferencia,
// con el alumno posando y la luz encendida.

#include <cstddef>
#include <filesystem>
#include <vector>

#include <opencv2/core.hpp>

#include "vision/face_embedder.hpp"
#include "vision/npu.hpp"
#include "vision/npu_preprocess.hpp"

namespace vision {

struct NpuEmbedderOptions {
  // Tamano de la entrada del modelo. Por omision 112x112 (el recorte de ArcFace).
  // Si se deja en 0 (o en negativo), se toma el del tensor del .nb.
  int input_width = 112;
  int input_height = 112;
  // Orden de color del tensor de entrada (archivo de configuracion, bloque "npu",
  // clave "color"). Por omision RGB.
  ColorOrder color_order = ColorOrder::Rgb;
  // El modo de escala se deriva del tensor de entrada (Auto) salvo que se elija a mano
  // (archivo de configuracion, bloque "npu", clave "escala").
  ScaleMode scale_mode = ScaleMode::Auto;
  // Tensor de salida que se lee. Por omision el 0.
  std::size_t output_index = 0;
};

// Red de reconocimiento sobre la NPU.
//
// NO es segura entre hilos: la usa el hilo de trabajo del servicio, el mismo que
// llama a poll(). Lanza std::invalid_argument si las opciones no cuadran con el .nb
// (tamano de entrada distinto del del tensor, indice de salida fuera de rango, salida
// en un formato que dequantize no sabe leer, una salida sin elementos, o un tensor de
// entrada con un formato, unas dimensiones o un rango que no valen) y npu::NpuError
// si el modelo no se puede cargar o preparar.
class NpuFaceEmbedder final : public FaceEmbedder {
 public:
  NpuFaceEmbedder(const npu::Driver& driver, const std::filesystem::path& nb_file,
                  const NpuEmbedderOptions& options = {});
  ~NpuFaceEmbedder() override = default;
  NpuFaceEmbedder(const NpuFaceEmbedder&) = delete;
  NpuFaceEmbedder& operator=(const NpuFaceEmbedder&) = delete;

  std::vector<float> embed(const cv::Mat& aligned_bgr_112) override;

 private:
  npu::Network network_;
  NpuEmbedderOptions options_;
  TensorInfo input_info_;
};

}  // namespace vision
