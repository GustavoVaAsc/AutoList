#include "vision/head_pose.hpp"

#include <cmath>

#include "vision/alignment.hpp"

namespace vision {

namespace {

using enrollment_limits::kAngleCeilingDeg;
using enrollment_limits::kMinUpPitchFloorDeg;

constexpr double kPi = 3.14159265358979323846;

// Cuanto sobresale la punta de la nariz del plano de los ojos y la boca, en distancias
// entre los ojos (un rostro adulto: unos 30 mm de punta de nariz contra unos 63 mm entre
// pupilas, es decir, cerca de 0.5).
constexpr double kNoseDepth = 0.5;

// Ojos casi juntos o boca casi a la altura de los ojos: los landmarks no son de una cara.
constexpr double kMinEyeDistancePx = 1.0;
constexpr double kMinMouthDropRatio = 0.25;  // H / d minimo (en una cara de frente, 1.15)

struct NeutralFace {
  double nose_ratio;   // t0: posicion de la nariz entre los ojos (0) y la boca (1), de frente
  double pitch_depth;  // kp = k d / H
};

// La cara de frente de referencia es la de ArcFace (kArcFaceReference): de ahi salen t0 y
// la proporcion H / d, para no copiar numeros magicos.
NeutralFace neutral_face() {
  static const NeutralFace face = [] {
    const auto& ref = kArcFaceReference;
    const double eyes_x = static_cast<double>(ref[1].x) - ref[0].x;
    const double eyes_y = static_cast<double>(ref[1].y) - ref[0].y;
    const double d = std::hypot(eyes_x, eyes_y);
    const double eye_center_y = (static_cast<double>(ref[0].y) + ref[1].y) / 2.0;
    const double mouth_center_y = (static_cast<double>(ref[3].y) + ref[4].y) / 2.0;
    const double h = mouth_center_y - eye_center_y;
    return NeutralFace{(static_cast<double>(ref[2].y) - eye_center_y) / h, kNoseDepth * d / h};
  }();
  return face;
}

bool finite_point(const cv::Point2f& p) { return std::isfinite(p.x) && std::isfinite(p.y); }

double to_degrees(double radians) { return radians * 180.0 / kPi; }

}  // namespace

std::optional<HeadPose> estimate_head_pose(const std::array<cv::Point2f, 5>& landmarks,
                                           bool invert_turn) {
  for (const cv::Point2f& p : landmarks) {
    if (!finite_point(p)) return std::nullopt;
  }
  const cv::Point2d eye_l = landmarks[0];
  const cv::Point2d eye_r = landmarks[1];
  const cv::Point2d nose = landmarks[2];
  const cv::Point2d mouth_l = landmarks[3];
  const cv::Point2d mouth_r = landmarks[4];

  const cv::Point2d eyes = eye_r - eye_l;
  const double d = std::hypot(eyes.x, eyes.y);
  // El ojo izquierdo es el de menor x (face_detector.hpp): con los ojos al reves la imagen
  // esta girada o el detector fallo, y el signo del yaw no se podria interpretar.
  if (!(d > kMinEyeDistancePx) || !(eyes.x > 0.0)) return std::nullopt;

  // Ejes de la cara: e_x a lo largo de los ojos, e_y perpendicular (hacia abajo en la imagen).
  const cv::Point2d ex = eyes * (1.0 / d);
  const cv::Point2d ey(-ex.y, ex.x);
  const cv::Point2d eye_center = (eye_l + eye_r) * 0.5;
  const cv::Point2d mouth_center = (mouth_l + mouth_r) * 0.5;

  const double h = (mouth_center - eye_center).dot(ey);
  if (!(h > kMinMouthDropRatio * d)) return std::nullopt;

  const cv::Point2d midline = (eye_center + mouth_center) * 0.5;
  const double turn_ratio = (nose - midline).dot(ex) / d;
  const double nose_ratio = (nose - eye_center).dot(ey) / h;

  const NeutralFace neutral = neutral_face();
  HeadPose pose;
  pose.yaw_deg = to_degrees(std::atan(turn_ratio / kNoseDepth));
  pose.pitch_deg = to_degrees(std::atan((neutral.nose_ratio - nose_ratio) / neutral.pitch_depth));
  if (invert_turn) pose.yaw_deg = -pose.yaw_deg;
  if (!std::isfinite(pose.yaw_deg) || !std::isfinite(pose.pitch_deg)) return std::nullopt;
  return pose;
}

std::optional<PoseLimitProblem> check_pose_limits(const PoseLimits& limits) {
  const auto positive = [](float x) { return std::isfinite(x) && x > 0.0f; };
  constexpr const char* kPositive = "debe ser un numero finito mayor que 0";
  if (!positive(limits.max_frontal_yaw_deg)) return PoseLimitProblem{"max_yaw_frontal_grados", "", kPositive};
  if (!positive(limits.min_lateral_yaw_deg)) return PoseLimitProblem{"min_yaw_lateral_grados", "", kPositive};
  if (!positive(limits.max_yaw_deg)) return PoseLimitProblem{"max_yaw_grados", "", kPositive};
  if (!positive(limits.max_first_pitch_deg)) return PoseLimitProblem{"max_pitch_frente_grados", "", kPositive};
  if (!positive(limits.max_frontal_pitch_deg)) return PoseLimitProblem{"max_pitch_frontal_grados", "", kPositive};
  if (!positive(limits.min_up_pitch_deg)) return PoseLimitProblem{"min_pitch_arriba_grados", "", kPositive};
  if (!positive(limits.max_pitch_deg)) return PoseLimitProblem{"max_pitch_grados", "", kPositive};
  if (!positive(limits.max_reference_pitch_deg)) {
    return PoseLimitProblem{"max_pitch_referencia_grados", "", kPositive};
  }

  // atan nunca llega a 90 grados: un maximo de 90 (o mas) no excluiria nada.
  if (!(limits.max_yaw_deg < kAngleCeilingDeg)) return PoseLimitProblem{"max_yaw_grados", "", "debe ser menor que 90"};
  if (!(limits.max_pitch_deg < kAngleCeilingDeg)) return PoseLimitProblem{"max_pitch_grados", "", "debe ser menor que 90"};
  if (!(limits.max_reference_pitch_deg < kAngleCeilingDeg)) {
    return PoseLimitProblem{"max_pitch_referencia_grados", "", "debe ser menor que 90"};
  }

  // Una pose lateral correcta no puede ser excesiva, ni una frontal estar permitida a la vez
  // que una lateral.
  if (!(limits.max_yaw_deg > limits.max_frontal_yaw_deg)) {
    return PoseLimitProblem{"max_yaw_grados", "max_yaw_frontal_grados",
                            "max_yaw_grados debe ser mayor que max_yaw_frontal_grados"};
  }
  if (!(limits.max_yaw_deg > limits.min_lateral_yaw_deg)) {
    return PoseLimitProblem{"max_yaw_grados", "min_yaw_lateral_grados",
                            "max_yaw_grados debe ser mayor que min_yaw_lateral_grados"};
  }
  if (!(limits.min_lateral_yaw_deg > limits.max_frontal_yaw_deg)) {
    return PoseLimitProblem{"min_yaw_lateral_grados", "max_yaw_frontal_grados",
                            "min_yaw_lateral_grados debe ser mayor que max_yaw_frontal_grados "
                            "(si no, un mismo giro valdria para una pose frontal y para una lateral)"};
  }
  // "arriba" se mide con la resta de dos fotos con ruido de unos 7 grados cada una.
  if (!(limits.min_up_pitch_deg >= kMinUpPitchFloorDeg)) {
    return PoseLimitProblem{"min_pitch_arriba_grados", "",
                            "debe ser al menos 5 (con menos, el ruido de la estimacion lo cumple solo)"};
  }
  if (!(limits.max_first_pitch_deg >= limits.max_frontal_pitch_deg)) {
    return PoseLimitProblem{"max_pitch_frente_grados", "max_pitch_frontal_grados",
                            "max_pitch_frente_grados no puede ser menor que max_pitch_frontal_grados "
                            "(frente no tiene referencia y es el mas holgado)"};
  }
  if (!(limits.max_pitch_deg > limits.max_first_pitch_deg)) {
    return PoseLimitProblem{"max_pitch_grados", "max_pitch_frente_grados",
                            "max_pitch_grados debe ser mayor que max_pitch_frente_grados"};
  }
  if (!(limits.max_pitch_deg > limits.min_up_pitch_deg)) {
    return PoseLimitProblem{"max_pitch_grados", "min_pitch_arriba_grados",
                            "max_pitch_grados debe ser mayor que min_pitch_arriba_grados"};
  }
  return std::nullopt;
}

PoseVerdict verify_pose(std::string_view pose, const HeadPose& measured,
                        double reference_pitch_deg, const PoseLimits& limits) {
  const double yaw = measured.yaw_deg;
  const double pitch = measured.pitch_deg - reference_pitch_deg;
  if (!std::isfinite(yaw) || !std::isfinite(pitch)) return PoseVerdict::Incorrect;

  // El giro exagerado se avisa aparte (el alumno gira mas de la cuenta): vale para toda pose.
  if (std::fabs(yaw) > limits.max_yaw_deg || std::fabs(pitch) > limits.max_pitch_deg) {
    return PoseVerdict::Excessive;
  }

  if (pose == "frente" || pose == "frente_2") {
    // "frente" no tiene referencia de pitch (es absoluto) y tiene un limite mas holgado.
    const double max_pitch = pose == "frente" ? limits.max_first_pitch_deg : limits.max_frontal_pitch_deg;
    const bool facing = std::fabs(yaw) <= limits.max_frontal_yaw_deg && std::fabs(pitch) <= max_pitch;
    return facing ? PoseVerdict::Ok : PoseVerdict::Incorrect;
  }
  if (pose == "izquierda") {
    return yaw >= limits.min_lateral_yaw_deg ? PoseVerdict::Ok : PoseVerdict::Incorrect;
  }
  if (pose == "derecha") {
    return yaw <= -limits.min_lateral_yaw_deg ? PoseVerdict::Ok : PoseVerdict::Incorrect;
  }
  if (pose == "arriba") {
    // Una referencia de "frente" demasiado inclinada no sirve para medir "arriba": con -25 grados
    // de referencia, una cara neutra (0) pareceria 25 grados hacia arriba.
    if (std::fabs(reference_pitch_deg) > limits.max_reference_pitch_deg) return PoseVerdict::Incorrect;
    const bool up = pitch >= limits.min_up_pitch_deg && std::fabs(yaw) <= limits.max_frontal_yaw_deg;
    return up ? PoseVerdict::Ok : PoseVerdict::Incorrect;
  }
  return PoseVerdict::Incorrect;  // una pose que no es del registro no se acepta
}

}  // namespace vision
