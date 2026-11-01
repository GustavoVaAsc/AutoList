#include <opencv2/imgproc.hpp>

#include "minitest.hpp"
#include "vision/sharpness.hpp"

namespace {

using namespace vision;

cv::Mat noise(int seed) {
  cv::Mat image(120, 160, CV_8UC3);
  cv::RNG rng(seed);
  rng.fill(image, cv::RNG::UNIFORM, 0, 256);
  return image;
}

cv::Mat blurred(const cv::Mat& image, double sigma) {
  cv::Mat out;
  cv::GaussianBlur(image, out, cv::Size(0, 0), sigma);
  return out;
}

}  // namespace

TEST(sharpness_is_zero_for_flat_image) {
  CHECK_NEAR(laplacian_variance(cv::Mat(50, 50, CV_8UC3, cv::Scalar::all(120))), 0.0, 1e-9);
}

TEST(sharpness_drops_with_blur) {
  const cv::Mat sharp = noise(1);
  const double s0 = laplacian_variance(sharp);
  const double s1 = laplacian_variance(blurred(sharp, 1.0));
  const double s2 = laplacian_variance(blurred(sharp, 3.0));
  CHECK(s0 > s1);
  CHECK(s1 > s2);
  CHECK(s2 > 0.0);
}

TEST(sharpness_accepts_gray_and_color) {
  const cv::Mat color = noise(2);
  cv::Mat gray;
  cv::cvtColor(color, gray, cv::COLOR_BGR2GRAY);
  CHECK_NEAR(laplacian_variance(color), laplacian_variance(gray), 1e-6);
}

TEST(sharpness_rejects_empty_image) {
  CHECK_THROWS(laplacian_variance(cv::Mat()), std::invalid_argument);
}

TEST(pick_sharpest_returns_best_frame_of_burst) {
  const cv::Mat base = noise(3);
  const std::vector<cv::Mat> burst = {blurred(base, 3.0), base, blurred(base, 1.0)};
  const auto best = pick_sharpest(burst);
  CHECK(best.has_value());
  CHECK_EQ(best->index, std::size_t{1});
  CHECK_NEAR(best->score, laplacian_variance(base), 1e-9);
}

TEST(pick_sharpest_of_empty_burst_is_nullopt) {
  CHECK(!pick_sharpest(std::vector<cv::Mat>{}).has_value());
}
