#pragma once

// Adaptador del detector sobre la NPU: imagen BGR -> rostros.
//
// La imagen se preprocesa (resize al tamano del modelo, orden de color, escala y
// cuantizacion), se pasa a la red .nb y la salida se decodifica en CPU con el
// decodificador de RetinaFace (retinaface_decode.hpp): cajas, landmarks y supresion de
// no maximos.
//
// Las detecciones se devuelven en pixeles de la imagen ORIGINAL que se paso: el
// reescalado del modelo a la original se aplica al final.
//
// NO es segura entre hilos: la usa el hilo de trabajo del servicio, el mismo que llama
// a poll().

#include <cstddef>
#include <filesystem>
#include <vector>

#include <opencv2/core.hpp>

#include "vision/face_detector.hpp"
#include "vision/npu.hpp"
#include "vision/npu_preprocess.hpp"
#include "vision/retinaface_decode.hpp"

namespace vision {

struct NpuDetectorOptions {
  // Tamano de la entrada del modelo. Por omision 640x640 (el RetinaFace clasico).
  // Si se deja en 0 (o en negativo), se toma el del tensor del .nb.
  int input_width = 640;
  int input_height = 640;
  // PROVISIONAL: depende del .nb, que todavia no existe (ver PreprocessOptions).
  ColorOrder color_order = ColorOrder::Rgb;
  // PROVISIONAL: por omision la normalizacion va DENTRO del modelo cuantizado.
  ScaleMode scale_mode = ScaleMode::None;
  // Disposicion de la salida del modelo (PROVISIONAL: ver DetectorLayout).
  DetectorLayout layout{};
  // Tensor de salida que trae las cajas. Por omision el 0. Si el .nb expone el score y
  // los landmarks en tensores aparte, se apuntan en el layout.
  std::size_t output_index = 0;
};

// Red de deteccion sobre la NPU.
//
// Lanza std::invalid_argument si las opciones no cuadran con el .nb (tamano de entrada
// distinto del del tensor) y npu::NpuError si el modelo no se puede cargar o preparar.
class NpuFaceDetector final : public FaceDetector {
 public:
  NpuFaceDetector(const npu::Driver& driver, const std::filesystem::path& nb_file,
                  const NpuDetectorOptions& options = {});
  ~NpuFaceDetector() override = default;
  NpuFaceDetector(const NpuFaceDetector&) = delete;
  NpuFaceDetector& operator=(const NpuFaceDetector&) = delete;

  std::vector<FaceDetection> detect(const cv::Mat& bgr) override;

 private:
  npu::Network network_;
  NpuDetectorOptions options_;
  TensorInfo input_info_;
};

}  // namespace vision
