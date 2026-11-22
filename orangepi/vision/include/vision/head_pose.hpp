#pragma once

#include <array>
#include <optional>
#include <string_view>

#include <opencv2/core.hpp>

#include "vision/enrollment_limits.hpp"

namespace vision {

// Giro de la cabeza estimado con los 5 landmarks de RetinaFace (ver estimate_head_pose).
// Son grados APROXIMADOS: salen de geometria simple (proyeccion ortografica y una
// profundidad fija de la punta de la nariz), no de un modelo, asi que sirven para decidir
// "mas o menos de frente" y no para medir un angulo. Los umbrales de PoseLimits se calibran
// con los valores que el servicio registra por stderr.
struct HeadPose {
  // Giro horizontal. Positivo = la cabeza gira hacia la IZQUIERDA DEL ALUMNO (con la imagen
  // sin espejo, la nariz se corre hacia la derecha de la imagen). PoseLimits::invert_turn
  // lo invierte si la camara entrega la imagen en espejo.
  double yaw_deg = 0.0;
  // Inclinacion. Positiva = la cara se levanta (la nariz sube hacia los ojos). Es absoluta
  // respecto a una cara neutra de ArcFace: un alumno de frente puede salir con unos grados
  // por la altura de la camara, por eso las poses posteriores a "frente" se miden contra el
  // pitch que "frente" dio (ver verify_pose).
  double pitch_deg = 0.0;
};

// Estima el giro de la cabeza. Los landmarks van en el orden de FaceDetection::landmarks
// (ojo izquierdo = el de menor x, ojo derecho, nariz, comisura izquierda, comisura derecha).
//
//   Se toma el eje de los ojos como horizontal de la cara (e_x) y su perpendicular como
//   vertical (e_y), asi que la inclinacion lateral de la cabeza (roll) no influye.
//   d = distancia entre los ojos, H = distancia vertical (sobre e_y) de los ojos a la boca.
//
//   yaw   = atan(r / k),  r = (nariz - punto medio entre ojos y boca) . e_x / d
//   pitch = atan((t0 - t) / kp),  t = (nariz - centro de los ojos) . e_y / H
//
//   La punta de la nariz sobresale de la cara una fraccion k = 0.5 de d. Con un giro de
//   yaw esa punta se corre r = k tan(yaw) respecto del plano de ojos y boca; con un
//   cabeceo, t baja de t0 (la posicion de la nariz en una cara de frente, tomada de
//   kArcFaceReference) en (k d / H) tan(pitch) = kp tan(pitch).
//
// Devuelve nullopt (fallo cerrado) si algun landmark no es finito, los ojos estan
// invertidos o juntos, o la boca no esta claramente debajo de los ojos.
//
// `invert_turn` cambia el signo del yaw (camara en espejo).
std::optional<HeadPose> estimate_head_pose(const std::array<cv::Point2f, 5>& landmarks,
                                           bool invert_turn);

// Limites de la verificacion de pose del registro. Todos los valores por omision son
// PROVISIONALES: hay que calibrarlos con la ESP32 (XIAO ESP32S3 Sense) a partir de los
// yaw y pitch que el servicio escribe por stderr en cada pose. Los grados son los de
// HeadPose (aproximados); el ruido esperado (una ESTIMACION, sin datos de la ESP32) es de
// unos 4 grados en yaw y unos 7 en pitch por foto, asi que los valores son holgados.
struct PoseLimits {
  // frente, frente_2 y arriba exigen |yaw| <= max_frontal_yaw_deg ("razonablemente de
  // frente"). Fuera de eso la pose sale rechazada con pose_incorrecta.
  float max_frontal_yaw_deg = enrollment_limits::kMaxFrontalYawDeg;
  // izquierda y derecha exigen un yaw >= min_lateral_yaw_deg en el sentido correcto
  // (izquierda = positivo; derecha = negativo). Menos es pose_incorrecta.
  float min_lateral_yaw_deg = enrollment_limits::kMinLateralYawDeg;
  // Con |yaw| > max_yaw_deg (cualquier pose) la pose sale con giro_excesivo. Un detector
  // RetinaFace casi no ve rostros por encima de 60 grados.
  float max_yaw_deg = enrollment_limits::kMaxYawDeg;
  // "frente" (la primera pose) exige |pitch| <= max_first_pitch_deg, ABSOLUTO: no hay
  // referencia previa, asi que "frente" NO compensa la altura de la camara (una camara alta
  // o baja hace que un alumno de frente salga con un pitch distinto de 0). Por eso el limite
  // es holgado.
  float max_first_pitch_deg = enrollment_limits::kMaxFirstPitchDeg;
  // frente_2 exige |pitch relativo| <= max_frontal_pitch_deg. El pitch relativo es el pitch
  // menos el que dio "frente" aceptada. Mas estricto que el de "frente": es la misma pose.
  float max_frontal_pitch_deg = enrollment_limits::kMaxFrontalPitchDeg;
  // arriba exige un pitch relativo >= min_up_pitch_deg (la cara se levanta). La referencia
  // sale de UNA foto de "frente" con ruido de unos 7 grados, y la resta de dos fotos suma
  // ruido (unos 10): 8 solo filtra "no levanto la cara"; "un poco hacia arriba" son 15 o mas.
  float min_up_pitch_deg = enrollment_limits::kMinUpPitchDeg;
  // El pitch de "frente" solo es una referencia fiable para "arriba" hasta este valor
  // (absoluto). Con un "frente" mas inclinado (la camara muy alta o baja, o un alumno que miraba
  // hacia abajo) un rostro neutro pasaria como "arriba" frente a una referencia de -25 grados;
  // en ese caso "arriba" NO se puede validar y sale pose_incorrecta (falla cerrado), y el
  // servicio avisa por stderr al aceptar "frente". Se corrige poniendo la camara a la altura de
  // la cara. frente y frente_2 no se ven afectados.
  float max_reference_pitch_deg = enrollment_limits::kMaxReferencePitchDeg;
  // Con |pitch relativo| > max_pitch_deg (cualquier pose) la pose sale con giro_excesivo.
  float max_pitch_deg = enrollment_limits::kMaxPitchDeg;
  // false: "izquierda" es la izquierda del alumno con la imagen sin espejo. true: se
  // invierte el sentido (la camara entrega la imagen en espejo, o "izquierda" se entiende
  // como la izquierda de la imagen). No se sabe cual es el caso de la ESP32: se calibra en
  // la prueba de extremo a extremo.
  bool invert_turn = false;
};

// Un limite incoherente. `key` es la clave de vision.conf (bloque "umbrales") que se culpa;
// `other_key` es la otra clave del par (vacia si el problema es de una sola): quien lee el
// archivo culpa a la que el usuario escribio. `message` explica la regla.
struct PoseLimitProblem {
  const char* key;
  const char* other_key;
  const char* message;
};

// nullopt si los limites son coherentes: finitos y positivos, los maximos de giro e
// inclinacion estrictamente por debajo de 90 (atan nunca llega) y por encima de los limites
// que gobiernan, las poses laterales mas alla de la frontal (min_lateral > max_frontal) y un
// minimo de "arriba" que no se confunda con el ruido (>= 5). Un valor no finito no es
// valido (fallo cerrado).
std::optional<PoseLimitProblem> check_pose_limits(const PoseLimits& limits);

enum class PoseVerdict {
  Ok,         // la cabeza esta en la pose pedida
  Incorrect,  // pose_incorrecta
  Excessive,  // giro_excesivo
};

// Compara el giro medido con la pose pedida (uno de kEnrollmentPoses). `reference_pitch_deg`
// es el pitch que dio la pose "frente" aceptada (0 para "frente" mismo). Una pose
// desconocida o un valor no finito es Incorrect (fallo cerrado). `limits.invert_turn` NO
// se aplica aqui: ya lo aplica estimate_head_pose al yaw.
PoseVerdict verify_pose(std::string_view pose, const HeadPose& measured,
                        double reference_pitch_deg, const PoseLimits& limits);

}  // namespace vision
