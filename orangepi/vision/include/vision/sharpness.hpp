#pragma once

#include <cstddef>
#include <optional>
#include <span>

#include <opencv2/core.hpp>

namespace vision {

// Varianza del Laplaciano (en escala de grises): mayor = mas nitida. Lanza
// std::invalid_argument con una imagen vacia.
double laplacian_variance(const cv::Mat& image);

struct SharpestFrame {
  std::size_t index = 0;
  double score = 0.0;
};

// La foto mas nitida de una rafaga; nullopt si la rafaga esta vacia.
std::optional<SharpestFrame> pick_sharpest(std::span<const cv::Mat> frames);

}  // namespace vision
