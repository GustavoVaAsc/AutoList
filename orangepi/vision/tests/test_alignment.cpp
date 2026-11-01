#include <complex>
#include <limits>

#include <opencv2/imgproc.hpp>

#include "minitest.hpp"
#include "vision/alignment.hpp"

namespace {

using namespace vision;

// Imagen negra con un punto blanco grande en cada landmark, para ver donde caen tras alinear.
cv::Mat image_with_dots(cv::Size size, const std::array<cv::Point2f, 5>& landmarks, int radius) {
  cv::Mat image = cv::Mat::zeros(size, CV_8UC3);
  for (const cv::Point2f& p : landmarks) {
    cv::circle(image, p, radius, cv::Scalar::all(255), cv::FILLED);
  }
  return image;
}

// Landmarks = referencia rotada `degrees`, escalada por `scale` y trasladada.
std::array<cv::Point2f, 5> similarity_of_reference(double scale, double degrees, cv::Point2f shift) {
  const cv::Mat m = cv::getRotationMatrix2D(cv::Point2f(56.0f, 56.0f), degrees, scale);
  std::array<cv::Point2f, 5> out;
  for (std::size_t i = 0; i < out.size(); ++i) {
    const cv::Point2f& r = kArcFaceReference[i];
    out[i] = {static_cast<float>(m.at<double>(0, 0) * r.x + m.at<double>(0, 1) * r.y + m.at<double>(0, 2)) + shift.x,
              static_cast<float>(m.at<double>(1, 0) * r.x + m.at<double>(1, 1) * r.y + m.at<double>(1, 2)) + shift.y};
  }
  return out;
}

// El centroide de lo brillante alrededor de `expected` debe estar a menos de 1 px.
void check_dot_at(const cv::Mat& aligned, const cv::Point2f& expected) {
  cv::Mat gray;
  cv::cvtColor(aligned, gray, cv::COLOR_BGR2GRAY);
  const cv::Rect window = cv::Rect(cv::Point(cvRound(expected.x) - 5, cvRound(expected.y) - 5), cv::Size(11, 11)) &
                          cv::Rect(0, 0, gray.cols, gray.rows);
  const cv::Moments m = cv::moments(gray(window) > 127, true);
  CHECK(m.m00 > 0.0);
  CHECK_NEAR(window.x + m.m10 / m.m00, expected.x, 1.0);
  CHECK_NEAR(window.y + m.m01 / m.m00, expected.y, 1.0);
}

}  // namespace

TEST(align_output_is_112x112_bgr) {
  const auto landmarks = similarity_of_reference(2.0, 0.0, {100.0f, 80.0f});
  const cv::Mat aligned = align_face(image_with_dots({400, 300}, landmarks, 3), landmarks);
  CHECK_EQ(aligned.cols, 112);
  CHECK_EQ(aligned.rows, 112);
  CHECK_EQ(aligned.type(), CV_8UC3);
}

TEST(align_maps_landmarks_onto_arcface_reference) {
  const auto landmarks = similarity_of_reference(2.0, 0.0, {100.0f, 80.0f});
  const cv::Mat aligned = align_face(image_with_dots({400, 300}, landmarks, 6), landmarks);
  for (const cv::Point2f& expected : kArcFaceReference) check_dot_at(aligned, expected);
}

TEST(align_undoes_rotation_and_scale) {
  const auto landmarks = similarity_of_reference(1.6, 20.0, {150.0f, 120.0f});
  const cv::Mat aligned = align_face(image_with_dots({480, 360}, landmarks, 5), landmarks);
  for (const cv::Point2f& expected : kArcFaceReference) check_dot_at(aligned, expected);
}

TEST(align_rejects_empty_image) {
  CHECK_THROWS(align_face(cv::Mat(), kArcFaceReference), std::invalid_argument);
}

TEST(align_fails_on_degenerate_landmarks) {
  std::array<cv::Point2f, 5> same_point;
  same_point.fill({50.0f, 50.0f});
  CHECK_THROWS(align_face(cv::Mat::zeros(100, 100, CV_8UC3), same_point), std::runtime_error);
}

// Fija los valores publicados de ArcFace; los demas tests usan la constante misma.
TEST(arcface_reference_points_are_the_standard_ones) {
  const float expected[5][2] = {{30.2946f, 51.6963f}, {65.5318f, 51.5014f}, {48.0252f, 71.7366f},
                                {33.5493f, 92.3655f}, {62.7299f, 92.2041f}};
  for (std::size_t i = 0; i < 5; ++i) {
    CHECK_NEAR(kArcFaceReference[i].x, expected[i][0], 1e-4);
    CHECK_NEAR(kArcFaceReference[i].y, expected[i][1], 1e-4);
  }
}

// --- Ajuste por minimos cuadrados -------------------------------------------------
// Con landmarks exactos, un ajuste con solo 2 puntos da lo mismo que uno con 5; con ruido
// no, asi que estas pruebas separan el ajuste de verdad del de dos puntos.

namespace {

// Landmarks de una similitud exacta, con un error fijo (hasta ~2 px) en cada punto.
std::array<cv::Point2f, 5> noisy_landmarks() {
  std::array<cv::Point2f, 5> out = similarity_of_reference(1.7, 12.0, {120.0f, 90.0f});
  const cv::Point2f jitter[5] = {{1.8f, -1.2f}, {-2.1f, 0.9f}, {0.7f, 2.0f}, {-1.5f, -1.9f}, {2.2f, 1.1f}};
  for (std::size_t i = 0; i < out.size(); ++i) out[i] += jitter[i];
  return out;
}

// Parametros (a, b, tx, ty) de x' = a x - b y + tx, y' = b x + a y + ty.
struct Params {
  double a, b, tx, ty;
};

Params from_matrix(const cv::Mat& m) {
  CHECK_EQ(m.rows, 2);
  CHECK_EQ(m.cols, 3);
  CHECK_EQ(m.type(), CV_64F);
  CHECK_NEAR(m.at<double>(0, 0), m.at<double>(1, 1), 1e-12);  // es una similitud
  CHECK_NEAR(m.at<double>(0, 1), -m.at<double>(1, 0), 1e-12);
  return {m.at<double>(0, 0), m.at<double>(1, 0), m.at<double>(0, 2), m.at<double>(1, 2)};
}

// Minimos cuadrados general (SVD de OpenCV) sobre las 10 ecuaciones, sin usar la formula cerrada.
Params least_squares_reference(const std::array<cv::Point2f, 5>& src) {
  cv::Mat A(10, 4, CV_64F), rhs(10, 1, CV_64F);
  for (int i = 0; i < 5; ++i) {
    const double x = src[i].x, y = src[i].y;
    A.at<double>(2 * i, 0) = x;      A.at<double>(2 * i, 1) = -y;
    A.at<double>(2 * i, 2) = 1;      A.at<double>(2 * i, 3) = 0;
    A.at<double>(2 * i + 1, 0) = y;  A.at<double>(2 * i + 1, 1) = x;
    A.at<double>(2 * i + 1, 2) = 0;  A.at<double>(2 * i + 1, 3) = 1;
    rhs.at<double>(2 * i) = kArcFaceReference[i].x;
    rhs.at<double>(2 * i + 1) = kArcFaceReference[i].y;
  }
  cv::Mat solution;
  cv::solve(A, rhs, solution, cv::DECOMP_SVD);
  return {solution.at<double>(0), solution.at<double>(1), solution.at<double>(2), solution.at<double>(3)};
}

double squared_error(const Params& p, const std::array<cv::Point2f, 5>& src) {
  double total = 0.0;
  for (int i = 0; i < 5; ++i) {
    const double x = p.a * src[i].x - p.b * src[i].y + p.tx - kArcFaceReference[i].x;
    const double y = p.b * src[i].x + p.a * src[i].y + p.ty - kArcFaceReference[i].y;
    total += x * x + y * y;
  }
  return total;
}

// Ajuste exacto por los dos primeros puntos (lo que haria un ajuste de solo 2 puntos).
Params two_point_fit(const std::array<cv::Point2f, 5>& src) {
  const std::complex<double> s0(src[0].x, src[0].y), s1(src[1].x, src[1].y);
  const std::complex<double> d0(kArcFaceReference[0].x, kArcFaceReference[0].y);
  const std::complex<double> d1(kArcFaceReference[1].x, kArcFaceReference[1].y);
  const std::complex<double> k = (d1 - d0) / (s1 - s0);
  const std::complex<double> t = d0 - k * s0;
  return {k.real(), k.imag(), t.real(), t.imag()};
}

}  // namespace

TEST(similarity_with_noisy_landmarks_equals_independent_least_squares) {
  const auto landmarks = noisy_landmarks();
  const Params ours = from_matrix(similarity_transform(landmarks, kArcFaceReference));
  const Params expected = least_squares_reference(landmarks);
  CHECK_NEAR(ours.a, expected.a, 1e-7);
  CHECK_NEAR(ours.b, expected.b, 1e-7);
  CHECK_NEAR(ours.tx, expected.tx, 1e-4);
  CHECK_NEAR(ours.ty, expected.ty, 1e-4);

  // El ruido es suficiente para distinguirlo de un ajuste con 2 puntos y de un vecino.
  const Params two_points = two_point_fit(landmarks);
  CHECK(std::fabs(two_points.a - expected.a) > 1e-3);
  CHECK(squared_error(ours, landmarks) < squared_error(two_points, landmarks) - 1.0);
  Params nudged = ours;
  nudged.a += 1e-3;
  CHECK(squared_error(ours, landmarks) < squared_error(nudged, landmarks));
}

TEST(similarity_is_deterministic) {
  const auto landmarks = noisy_landmarks();
  const cv::Mat first = similarity_transform(landmarks, kArcFaceReference);
  for (int i = 0; i < 5; ++i) {
    CHECK_EQ(cv::norm(first, similarity_transform(landmarks, kArcFaceReference), cv::NORM_INF), 0.0);
  }
}

TEST(similarity_rejects_non_finite_landmarks) {
  auto landmarks = similarity_of_reference(2.0, 0.0, {100.0f, 80.0f});
  landmarks[3].x = std::numeric_limits<float>::quiet_NaN();
  CHECK_THROWS(similarity_transform(landmarks, kArcFaceReference), std::invalid_argument);
  CHECK_THROWS(align_face(cv::Mat::zeros(300, 400, CV_8UC3), landmarks), std::invalid_argument);

  landmarks = similarity_of_reference(2.0, 0.0, {100.0f, 80.0f});
  landmarks[0].y = std::numeric_limits<float>::infinity();
  CHECK_THROWS(similarity_transform(landmarks, kArcFaceReference), std::invalid_argument);
}
