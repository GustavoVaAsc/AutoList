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
  // El modo de escala se deriva del tensor de entrada (Auto) salvo que se elija a mano.
  ScaleMode scale_mode = ScaleMode::Auto;
  // Disposicion de la salida del modelo (PROVISIONAL: ver DetectorLayout).
  DetectorLayout layout{};
  // Tensor de salida que trae las cajas. Por omision el 0. Si el .nb expone el score y
  // los landmarks en tensores aparte, se apuntan en el layout.
  std::size_t output_index = 0;
};

// Red de deteccion sobre la NPU.
//
// Lanza std::invalid_argument si las opciones no cuadran con el .nb (tamano de entrada
// distinto del del tensor, indice de salida fuera de rango, salida en un formato que
// dequantize no sabe leer, o numero de elementos de la salida que no cuadra con el
// layout) y npu::NpuError si el modelo no se puede cargar o preparar.
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
  // Las anclas dependen solo del tamano de entrada (que es fijo): se generan una vez
  // y se reutilizan en cada fotograma.
  std::vector<Anchor> anchors_;
};

}  // namespace vision
