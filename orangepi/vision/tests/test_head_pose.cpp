#include <functional>
#include <limits>
#include <string>

#include "minitest.hpp"
#include "vision/fakes.hpp"
#include "vision/head_pose.hpp"

namespace {

using namespace vision;
using vision::fakes::synthetic_landmarks;

constexpr bool kNoInvertir = false;

HeadPose medir(double yaw, double pitch, bool invertir = false) {
  const auto pose = estimate_head_pose(synthetic_landmarks(yaw, pitch), invertir);
  if (!pose) throw minitest::Failure{"estimate_head_pose devolvio nullopt"};
  return *pose;
}

}  // namespace

// --- estimate_head_pose -----------------------------------------------------------------

TEST(la_cara_de_frente_de_arcface_da_giro_y_cabeceo_cero) {
  const auto pose = estimate_head_pose(kArcFaceReference, kNoInvertir);
  CHECK(pose.has_value());
  CHECK_NEAR(pose->yaw_deg, 0.0, 0.5);
  CHECK_NEAR(pose->pitch_deg, 0.0, 1e-3);
}

TEST(el_giro_a_la_izquierda_del_alumno_es_yaw_positivo) {
  for (const double yaw : {10.0, 25.0, 40.0, 55.0}) {
    CHECK_NEAR(medir(yaw, 0.0).yaw_deg, yaw, 0.6);
    CHECK_NEAR(medir(-yaw, 0.0).yaw_deg, -yaw, 0.6);
  }
}

TEST(invert_turn_cambia_el_signo_del_yaw_y_no_toca_el_cabeceo) {
  const HeadPose normal = medir(30.0, 10.0, false);
  const HeadPose espejo = medir(30.0, 10.0, true);
  CHECK_NEAR(espejo.yaw_deg, -normal.yaw_deg, 1e-9);
  CHECK_NEAR(espejo.pitch_deg, normal.pitch_deg, 1e-9);
}

TEST(levantar_la_cara_es_pitch_positivo) {
  for (const double pitch : {10.0, 20.0, 35.0}) {
    CHECK_NEAR(medir(0.0, pitch).pitch_deg, pitch, 0.6);
    CHECK_NEAR(medir(0.0, -pitch).pitch_deg, -pitch, 0.6);
  }
  CHECK_NEAR(medir(0.0, 20.0).yaw_deg, 0.0, 0.6);  // el cabeceo no se cuela en el giro
}

TEST(la_estimacion_no_depende_del_tamano_ni_de_la_posicion_de_la_cara) {
  const HeadPose base = medir(30.0, 15.0);
  const auto lejos = estimate_head_pose(synthetic_landmarks(30.0, 15.0, 0.4, {500.0f, 300.0f}), false);
  CHECK(lejos.has_value());
  CHECK_NEAR(lejos->yaw_deg, base.yaw_deg, 1e-3);
  CHECK_NEAR(lejos->pitch_deg, base.pitch_deg, 1e-3);
}

TEST(la_inclinacion_lateral_de_la_cabeza_no_cambia_el_yaw_ni_el_pitch) {
  const HeadPose base = medir(30.0, 15.0);
  for (const double roll : {-25.0, 15.0, 30.0}) {
    const auto torcida = estimate_head_pose(synthetic_landmarks(30.0, 15.0, 1.5, {80.0f, 40.0f}, roll), false);
    CHECK(torcida.has_value());
    CHECK_NEAR(torcida->yaw_deg, base.yaw_deg, 1e-3);
    CHECK_NEAR(torcida->pitch_deg, base.pitch_deg, 1e-3);
  }
}

TEST(landmarks_no_finitos_o_degenerados_no_se_estiman) {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();

  auto con_nan = synthetic_landmarks(0.0, 0.0);
  con_nan[2].x = nan;
  CHECK(!estimate_head_pose(con_nan, false).has_value());

  auto con_inf = synthetic_landmarks(0.0, 0.0);
  con_inf[4].y = inf;
  CHECK(!estimate_head_pose(con_inf, false).has_value());

  auto ojos_al_reves = synthetic_landmarks(0.0, 0.0);
  std::swap(ojos_al_reves[0], ojos_al_reves[1]);
  CHECK(!estimate_head_pose(ojos_al_reves, false).has_value());

  auto ojos_juntos = synthetic_landmarks(0.0, 0.0);
  ojos_juntos[1] = ojos_juntos[0];
  CHECK(!estimate_head_pose(ojos_juntos, false).has_value());

  auto boca_arriba_de_los_ojos = synthetic_landmarks(0.0, 0.0);
  boca_arriba_de_los_ojos[3].y = 10.0f;
  boca_arriba_de_los_ojos[4].y = 10.0f;
  CHECK(!estimate_head_pose(boca_arriba_de_los_ojos, false).has_value());
}

// --- verify_pose ------------------------------------------------------------------------

TEST(las_poses_frontales_exigen_estar_de_frente) {
  const PoseLimits limits;
  for (const char* pose : {"frente", "frente_2"}) {
    // "frente" (sin referencia, pitch absoluto) es mas holgado que "frente_2" (relativo).
    const double max_pitch = std::string(pose) == "frente" ? limits.max_first_pitch_deg
                                                            : limits.max_frontal_pitch_deg;
    CHECK(verify_pose(pose, {0.0, 0.0}, 0.0, limits) == PoseVerdict::Ok);
    CHECK(verify_pose(pose, {limits.max_frontal_yaw_deg - 1.0, 0.0}, 0.0, limits) == PoseVerdict::Ok);
    CHECK(verify_pose(pose, {-(limits.max_frontal_yaw_deg + 1.0), 0.0}, 0.0, limits) ==
          PoseVerdict::Incorrect);
    CHECK(verify_pose(pose, {0.0, max_pitch - 1.0}, 0.0, limits) == PoseVerdict::Ok);
    CHECK(verify_pose(pose, {0.0, max_pitch + 1.0}, 0.0, limits) == PoseVerdict::Incorrect);
    CHECK(verify_pose(pose, {0.0, -(max_pitch + 1.0)}, 0.0, limits) == PoseVerdict::Incorrect);
  }
}

TEST(frente_no_compensa_la_altura_de_la_camara_pero_su_limite_es_holgado) {
  const PoseLimits limits;
  // Una camara alta hace que un alumno de frente salga a -25 grados: frente lo acepta (limite
  // 30) y frente_2, medido contra el de frente (-25), tambien. Con el limite de frente_2 (20)
  // en vez del de frente, el alumno nunca podria registrarse.
  CHECK(verify_pose("frente", {0.0, -25.0}, 0.0, limits) == PoseVerdict::Ok);
  CHECK(verify_pose("frente_2", {0.0, -25.0}, -25.0, limits) == PoseVerdict::Ok);
  CHECK(verify_pose("frente", {0.0, -35.0}, 0.0, limits) == PoseVerdict::Incorrect);
}

TEST(izquierda_y_derecha_exigen_el_sentido_correcto_por_encima_del_minimo) {
  const PoseLimits limits;
  const double bastante = limits.min_lateral_yaw_deg + 5.0;
  CHECK(verify_pose("izquierda", {bastante, 0.0}, 0.0, limits) == PoseVerdict::Ok);
  CHECK(verify_pose("derecha", {-bastante, 0.0}, 0.0, limits) == PoseVerdict::Ok);
  // El sentido contrario y los giros insuficientes son pose_incorrecta.
  CHECK(verify_pose("izquierda", {-bastante, 0.0}, 0.0, limits) == PoseVerdict::Incorrect);
  CHECK(verify_pose("derecha", {bastante, 0.0}, 0.0, limits) == PoseVerdict::Incorrect);
  CHECK(verify_pose("izquierda", {limits.min_lateral_yaw_deg - 1.0, 0.0}, 0.0, limits) ==
        PoseVerdict::Incorrect);
  CHECK(verify_pose("derecha", {0.0, 0.0}, 0.0, limits) == PoseVerdict::Incorrect);
}

TEST(arriba_exige_cabeceo_hacia_arriba_y_estar_de_frente_en_el_giro) {
  const PoseLimits limits;
  CHECK(verify_pose("arriba", {0.0, limits.min_up_pitch_deg + 3.0}, 0.0, limits) == PoseVerdict::Ok);
  CHECK(verify_pose("arriba", {0.0, 0.0}, 0.0, limits) == PoseVerdict::Incorrect);
  CHECK(verify_pose("arriba", {0.0, -20.0}, 0.0, limits) == PoseVerdict::Incorrect);  // hacia abajo
  CHECK(verify_pose("arriba", {limits.max_frontal_yaw_deg + 5.0, limits.min_up_pitch_deg + 3.0}, 0.0,
                    limits) == PoseVerdict::Incorrect);
}

TEST(el_cabeceo_se_mide_contra_el_de_la_pose_frente) {
  const PoseLimits limits;
  // La camara esta alta: "frente" dio -15 grados. Mirar al frente otra vez (-15) es de frente,
  // y levantar la cara 15 grados (a 0 absoluto) es "arriba".
  CHECK(verify_pose("frente_2", {0.0, -15.0}, -15.0, limits) == PoseVerdict::Ok);
  CHECK(verify_pose("arriba", {0.0, 0.0}, -15.0, limits) == PoseVerdict::Ok);
  CHECK(verify_pose("arriba", {0.0, -15.0}, -15.0, limits) == PoseVerdict::Incorrect);
}

TEST(arriba_no_se_valida_con_una_referencia_de_frente_demasiado_inclinada) {
  const PoseLimits limits;
  // Con -15 (el limite) la referencia sirve: una cara neutra son 15 grados hacia arriba.
  CHECK(verify_pose("arriba", {0.0, 0.0}, -limits.max_reference_pitch_deg, limits) == PoseVerdict::Ok);
  // Con -25, no: una cara neutra pareceria 25 grados hacia arriba. Falla cerrado, tambien al
  // levantar mucho la cara, y tambien con la referencia hacia el otro lado.
  CHECK(verify_pose("arriba", {0.0, 0.0}, -25.0, limits) == PoseVerdict::Incorrect);
  CHECK(verify_pose("arriba", {0.0, 15.0}, -25.0, limits) == PoseVerdict::Incorrect);
  CHECK(verify_pose("arriba", {0.0, 40.0}, 25.0, limits) == PoseVerdict::Incorrect);
  // frente_2 (la misma pose) y las laterales no se ven afectadas.
  CHECK(verify_pose("frente_2", {0.0, -25.0}, -25.0, limits) == PoseVerdict::Ok);
  CHECK(verify_pose("izquierda", {30.0, -25.0}, -25.0, limits) == PoseVerdict::Ok);
}

TEST(un_giro_exagerado_es_giro_excesivo_en_cualquier_pose) {
  const PoseLimits limits;
  for (const char* pose : {"frente", "izquierda", "derecha", "arriba", "frente_2"}) {
    CHECK(verify_pose(pose, {limits.max_yaw_deg + 1.0, 0.0}, 0.0, limits) == PoseVerdict::Excessive);
    CHECK(verify_pose(pose, {-(limits.max_yaw_deg + 1.0), 0.0}, 0.0, limits) == PoseVerdict::Excessive);
    CHECK(verify_pose(pose, {0.0, limits.max_pitch_deg + 1.0}, 0.0, limits) == PoseVerdict::Excessive);
    CHECK(verify_pose(pose, {0.0, -(limits.max_pitch_deg + 1.0)}, 0.0, limits) == PoseVerdict::Excessive);
  }
  // Justo por debajo del limite, una pose lateral sigue siendo valida.
  CHECK(verify_pose("izquierda", {limits.max_yaw_deg - 1.0, 0.0}, 0.0, limits) == PoseVerdict::Ok);
}

TEST(una_pose_desconocida_o_un_valor_no_finito_no_se_aceptan) {
  const PoseLimits limits;
  const double nan = std::numeric_limits<double>::quiet_NaN();
  CHECK(verify_pose("atras", {0.0, 0.0}, 0.0, limits) == PoseVerdict::Incorrect);
  CHECK(verify_pose("", {0.0, 0.0}, 0.0, limits) == PoseVerdict::Incorrect);
  CHECK(verify_pose("frente", {nan, 0.0}, 0.0, limits) == PoseVerdict::Incorrect);
  CHECK(verify_pose("frente", {0.0, nan}, 0.0, limits) == PoseVerdict::Incorrect);
  CHECK(verify_pose("frente", {0.0, 0.0}, nan, limits) == PoseVerdict::Incorrect);
  CHECK(verify_pose("izquierda", {std::numeric_limits<double>::infinity(), 0.0}, 0.0, limits) ==
        PoseVerdict::Incorrect);
}

TEST(los_limites_por_omision_son_coherentes) {
  CHECK(!check_pose_limits(PoseLimits{}).has_value());
  // Los valores por omision respetan las reglas que el comprobador exige.
  const PoseLimits limits;
  CHECK(limits.min_lateral_yaw_deg > limits.max_frontal_yaw_deg);
  CHECK(limits.min_up_pitch_deg >= 5.0f);
}

// Cada regla rota culpa a la clave correcta (la de vision.conf) y nombra la otra del par.
TEST(un_limite_incoherente_nombra_la_clave_culpable) {
  const auto culpable = [](const std::function<void(PoseLimits&)>& cambio) {
    PoseLimits limits;
    cambio(limits);
    const auto problem = check_pose_limits(limits);
    if (!problem) throw minitest::Failure{"el limite incoherente no se detecto"};
    return std::string(problem->key) + "/" + problem->other_key;
  };
  const float nan = std::numeric_limits<float>::quiet_NaN();
  CHECK_EQ(culpable([](PoseLimits& l) { l.max_frontal_yaw_deg = 0.0f; }), std::string("max_yaw_frontal_grados/"));
  CHECK_EQ(culpable([](PoseLimits& l) { l.min_lateral_yaw_deg = -1.0f; }), std::string("min_yaw_lateral_grados/"));
  CHECK_EQ(culpable([&](PoseLimits& l) { l.max_yaw_deg = nan; }), std::string("max_yaw_grados/"));
  CHECK_EQ(culpable([](PoseLimits& l) { l.max_yaw_deg = std::numeric_limits<float>::infinity(); }),
           std::string("max_yaw_grados/"));
  CHECK_EQ(culpable([&](PoseLimits& l) { l.min_up_pitch_deg = nan; }), std::string("min_pitch_arriba_grados/"));
  CHECK_EQ(culpable([](PoseLimits& l) { l.max_first_pitch_deg = 0.0f; }), std::string("max_pitch_frente_grados/"));
  CHECK_EQ(culpable([](PoseLimits& l) { l.max_reference_pitch_deg = 0.0f; }), std::string("max_pitch_referencia_grados/"));
  CHECK_EQ(culpable([](PoseLimits& l) { l.max_reference_pitch_deg = 90.0f; }), std::string("max_pitch_referencia_grados/"));
  // Menos de 90: atan nunca llega.
  CHECK_EQ(culpable([](PoseLimits& l) { l.max_yaw_deg = 90.0f; }), std::string("max_yaw_grados/"));
  CHECK_EQ(culpable([](PoseLimits& l) { l.max_pitch_deg = 91.0f; }), std::string("max_pitch_grados/"));
  // Pares.
  CHECK_EQ(culpable([](PoseLimits& l) { l.max_yaw_deg = l.min_lateral_yaw_deg; }),
           std::string("max_yaw_grados/min_yaw_lateral_grados"));
  CHECK_EQ(culpable([](PoseLimits& l) { l.max_yaw_deg = 10.0f; l.min_lateral_yaw_deg = 5.0f; l.max_frontal_yaw_deg = 10.0f; }),
           std::string("max_yaw_grados/max_yaw_frontal_grados"));
  CHECK_EQ(culpable([](PoseLimits& l) { l.min_lateral_yaw_deg = l.max_frontal_yaw_deg; }),
           std::string("min_yaw_lateral_grados/max_yaw_frontal_grados"));
  CHECK_EQ(culpable([](PoseLimits& l) { l.min_up_pitch_deg = 4.0f; }), std::string("min_pitch_arriba_grados/"));
  CHECK_EQ(culpable([](PoseLimits& l) { l.max_first_pitch_deg = l.max_frontal_pitch_deg - 1.0f; }),
           std::string("max_pitch_frente_grados/max_pitch_frontal_grados"));
  CHECK_EQ(culpable([](PoseLimits& l) { l.max_pitch_deg = l.max_first_pitch_deg; }),
           std::string("max_pitch_grados/max_pitch_frente_grados"));
  CHECK_EQ(culpable([](PoseLimits& l) {
             l.max_frontal_pitch_deg = 5.0f;
             l.max_first_pitch_deg = 10.0f;
             l.max_pitch_deg = 15.0f;
             l.min_up_pitch_deg = 15.0f;
           }),
           std::string("max_pitch_grados/min_pitch_arriba_grados"));
}
