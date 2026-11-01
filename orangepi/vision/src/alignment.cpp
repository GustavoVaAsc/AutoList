#include "vision/alignment.hpp"

#include <cmath>
#include <stdexcept>

#include <opencv2/imgproc.hpp>

namespace vision {

// Con los puntos centrados en sus centroides, la similitud optima s*R(theta) cumple, para
// a = sum(u . v) y b = sum(u x v) sobre los pares (u de src, v de dst) y d = sum(|u|^2):
//   s*cos(theta) = a / d,  s*sin(theta) = b / d.
// Es la solucion de Umeyama restringida a rotaciones propias (en 2D no hace falta SVD).
cv::Mat similarity_transform(const std::array<cv::Point2f, 5>& src,
                             const std::array<cv::Point2f, 5>& dst) {
  const auto finite = [](const cv::Point2f& p) { return std::isfinite(p.x) && std::isfinite(p.y); };
  for (std::size_t i = 0; i < src.size(); ++i) {
    if (!finite(src[i]) || !finite(dst[i])) {
      throw std::invalid_argument("similarity_transform: punto no finito en el indice " +
                                  std::to_string(i));
    }
  }

  cv::Point2d src_mean, dst_mean;
  for (std::size_t i = 0; i < src.size(); ++i) {
    src_mean += cv::Point2d(src[i]);
    dst_mean += cv::Point2d(dst[i]);
  }
  src_mean *= 1.0 / static_cast<double>(src.size());
  dst_mean *= 1.0 / static_cast<double>(src.size());

  double dot = 0.0, cross = 0.0, src_energy = 0.0;
  for (std::size_t i = 0; i < src.size(); ++i) {
    const cv::Point2d u = cv::Point2d(src[i]) - src_mean;
    const cv::Point2d v = cv::Point2d(dst[i]) - dst_mean;
    dot += u.x * v.x + u.y * v.y;
    cross += u.x * v.y - u.y * v.x;
    src_energy += u.x * u.x + u.y * u.y;
  }
  const double a = dot / src_energy;    // s*cos(theta)
  const double b = cross / src_energy;  // s*sin(theta)
  if (!(src_energy > 1e-9) || !std::isfinite(a) || !std::isfinite(b) || (a == 0.0 && b == 0.0)) {
    throw std::runtime_error("similarity_transform: no se pudo estimar la transformacion de similitud");
  }

  cv::Mat transform(2, 3, CV_64F);
  transform.at<double>(0, 0) = a;
  transform.at<double>(0, 1) = -b;
  transform.at<double>(1, 0) = b;
  transform.at<double>(1, 1) = a;
  transform.at<double>(0, 2) = dst_mean.x - (a * src_mean.x - b * src_mean.y);
  transform.at<double>(1, 2) = dst_mean.y - (b * src_mean.x + a * src_mean.y);
  return transform;
}

cv::Mat align_face(const cv::Mat& image, const std::array<cv::Point2f, 5>& landmarks) {
  if (image.empty()) {
    throw std::invalid_argument("align_face: imagen vacia");
  }
  const cv::Mat transform = similarity_transform(landmarks, kArcFaceReference);

  cv::Mat aligned;
  cv::warpAffine(image, aligned, transform, cv::Size(kAlignedFaceSize, kAlignedFaceSize),
                 cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar());
  return aligned;
}

}  // namespace vision
