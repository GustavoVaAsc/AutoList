#pragma once

#include <vector>

#include <opencv2/core.hpp>

namespace vision {

// Recorte alineado de 112x112 BGR -> embedding. La dimension la fija el modelo
// en tiempo de ejecucion (no se asume 128 ni 512); no hace falta que venga normalizado.
class FaceEmbedder {
 public:
  virtual ~FaceEmbedder() = default;
  virtual std::vector<float> embed(const cv::Mat& aligned_bgr_112) = 0;
};

}  // namespace vision
