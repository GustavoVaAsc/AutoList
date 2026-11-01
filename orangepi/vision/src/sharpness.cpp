#include "vision/sharpness.hpp"

#include <stdexcept>

#include <opencv2/imgproc.hpp>

namespace vision {

double laplacian_variance(const cv::Mat& image) {
  if (image.empty()) {
    throw std::invalid_argument("laplacian_variance: imagen vacia");
  }

  cv::Mat gray;
  if (image.channels() == 1) {
    gray = image;
  } else if (image.channels() == 3) {
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
  } else {
    throw std::invalid_argument("laplacian_variance: se esperan 1 o 3 canales");
  }

  cv::Mat laplacian;
  cv::Laplacian(gray, laplacian, CV_64F);
  cv::Scalar mean, stddev;
  cv::meanStdDev(laplacian, mean, stddev);
  return stddev[0] * stddev[0];
}

std::optional<SharpestFrame> pick_sharpest(std::span<const cv::Mat> frames) {
  std::optional<SharpestFrame> best;
  for (std::size_t i = 0; i < frames.size(); ++i) {
    const double score = laplacian_variance(frames[i]);
    if (!best || score > best->score) {
      best = SharpestFrame{i, score};
    }
  }
  return best;
}

}  // namespace vision
