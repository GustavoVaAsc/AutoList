#pragma once

#include <array>
#include <vector>

#include <opencv2/core.hpp>

namespace vision {

struct FaceDetection {
  cv::Rect2f bbox;  // en pixeles de la imagen de entrada
  float score = 0.0f;
  // Orden: ojo izquierdo, ojo derecho, nariz, boca izquierda, boca derecha,
  // vistos desde la imagen (el ojo izquierdo es el de menor x), en pixeles.
  std::array<cv::Point2f, 5> landmarks;
};

// Imagen BGR -> rostros detectados. SCRFD/RetinaFace y las variantes de YOLO-face
// producen exactamente esto; el decodificador concreto depende del modelo elegido.
class FaceDetector {
 public:
  virtual ~FaceDetector() = default;
  virtual std::vector<FaceDetection> detect(const cv::Mat& bgr) = 0;
};

}  // namespace vision
