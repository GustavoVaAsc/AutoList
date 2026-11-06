#pragma once

// Adaptador del detector sobre la NPU: imagen BGR -> rostros.
//
// La imagen se preprocesa (letterbox al tamano del modelo con relleno 114, orden de
// color, escala y cuantizacion, como la demo del fabricante), se pasa a la red .nb y
// sus tres salidas (cajas, scores y landmarks) se decodifican en CPU con el
// decodificador de RetinaFace (retinaface_decode.hpp): cajas, landmarks y supresion de
// no maximos.
//
// Las detecciones se devuelven en pixeles de la imagen ORIGINAL que se paso: el
// letterbox se deshace al final y la caja se recorta a la imagen.
//
// NO es segura entre hilos: la usa el hilo de trabajo del servicio, el mismo que llama
// a poll().

#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

#include <opencv2/core.hpp>

#include "vision/face_detector.hpp"
#include "vision/npu.hpp"
#include "vision/npu_preprocess.hpp"
#include "vision/retinaface_decode.hpp"

namespace vision {

struct NpuDetectorOptions {
  // Tamano de la entrada del modelo. Si se deja en 0 (o en negativo, el valor por
  // omision), se toma el del tensor del .nb. Se puede fijar en el archivo de
  // configuracion (bloque "npu", claves "ancho" y "alto").
  int input_width = 0;
  int input_height = 0;
  // Orden de color del tensor de entrada (archivo de configuracion, bloque "npu", clave
  // "color", obligatoria). SIN valor por omision: no se deduce del modelo y equivocarlo
  // no falla, degrada la deteccion. Si no se fija, el constructor lanza. El RetinaFace
  // del fabricante espera BGR (REVERSE_CHANNEL = True en su config_yml.py).
  std::optional<ColorOrder> color_order;
  // El modo de escala se deriva del tensor de entrada (Auto) salvo que se elija a mano
  // (archivo de configuracion, bloque "npu", clave "escala").
  ScaleMode scale_mode = ScaleMode::Auto;
  // Disposicion de la salida del modelo, con los indices de sus tres tensores de salida
  // (ver DetectorLayout).
  DetectorLayout layout{};
};

// Red de deteccion sobre la NPU.
//
// Lanza std::invalid_argument si falta el orden de color, si las opciones no cuadran con
// el .nb (tamano de entrada
// distinto del del tensor, indice de salida fuera de rango, salida en un formato que
// dequantize no sabe leer, numero de elementos o forma de una salida que no cuadran con
// el layout, o un tensor de entrada con un formato, unas dimensiones o un rango que no
// valen) y npu::NpuError si el modelo no se puede cargar o preparar.
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
