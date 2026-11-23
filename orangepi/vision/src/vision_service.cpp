#include "vision/vision_service.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <opencv2/imgcodecs.hpp>

#include "vision/alignment.hpp"
#include "vision/captura_firma.hpp"
#include "vision/image_header.hpp"
#include "vision/mqtt_contract.hpp"
#include "vision/sharpness.hpp"
#include "vision/thumbnail.hpp"

namespace vision {

namespace {

// Ejecuta `action` al salir del ambito, sea por retorno normal o por excepcion. La accion
// no puede lanzar: un destructor que lanza durante un unwinding termina el proceso.
template <typename F>
class ScopeExit {
  static_assert(std::is_nothrow_invocable_v<F&>, "la accion de ScopeExit debe ser noexcept");

 public:
  explicit ScopeExit(F action) : action_(std::move(action)) {}
  ScopeExit(const ScopeExit&) = delete;
  ScopeExit& operator=(const ScopeExit&) = delete;
  ~ScopeExit() { action_(); }

 private:
  F action_;
};

// Similitud del coseno entre dos vectores. Si alguno tiene norma cero no hay direccion
// que comparar: la similitud es 0 (la pose se descarta). Si el resultado no es finito
// (un valor NaN o infinito en algun embedding) tambien es 0: una similitud no finita no
// puede pasar un umbral, asi que la pose se descarta (fallo cerrado).
float cosine_similarity(const std::vector<float>& a, const std::vector<float>& b) {
  if (a.size() != b.size()) return 0.0f;
  double dot = 0.0;
  double norma_a = 0.0;
  double norma_b = 0.0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    dot += static_cast<double>(a[i]) * b[i];
    norma_a += static_cast<double>(a[i]) * a[i];
    norma_b += static_cast<double>(b[i]) * b[i];
  }
  if (norma_a <= 0.0 || norma_b <= 0.0) return 0.0f;
  const double similitud = dot / (std::sqrt(norma_a) * std::sqrt(norma_b));
  if (!std::isfinite(similitud)) return 0.0f;
  return static_cast<float>(similitud);
}

// Una pose es comparable si todos sus valores son finitos: si no, no tiene direccion que
// comparar y ademas contaminaria la media de las candidatas.
bool embedding_finito(const std::vector<float>& embedding) {
  for (const float x : embedding) {
    if (!std::isfinite(x)) return false;
  }
  return true;
}

// Nombre de la pose del registro (los del plan 5.4, en orden), para el aviso de descarte.
std::string nombre_pose(std::size_t indice) {
  if (indice < kEnrollmentPoses.size()) {
    return std::string(kEnrollmentPoses[indice]);
  }
  return "pose " + std::to_string(indice + 1);
}

// Aviso de una pose descartada (por stderr; el contrato MQTT no cambia). Dice QUE pose se
// descarto y por que, para que un registro rechazado se pueda diagnosticar. El motivo
// va como etiqueta propia: lo no finito suele ser el embedding, no la similitud.
void avisar_pose_descartada(std::size_t indice, const std::string& motivo) {
  std::cerr << "[vision] pose descartada: " << nombre_pose(indice) << " (motivo: " << motivo
            << ")\n";
}

// Las medidas de una pose para el diagnostico: lo que el usuario necesita para calibrar los
// umbrales (yaw y pitch estimados, coseno contra "frente"). Solo lleva lo que se midio.
std::string texto_medidas(const std::optional<HeadPose>& cabeza,
                          const std::optional<float>& coseno_frente) {
  std::ostringstream texto;
  texto << std::fixed << std::setprecision(1);
  // `+ 0.0` convierte un -0.0 (de redondear -0.04) en 0.0 para que el log no confunda.
  const auto un_decimal = [](double grados) { return std::round(grados * 10.0) / 10.0 + 0.0; };
  if (cabeza) {
    texto << "yaw " << un_decimal(cabeza->yaw_deg) << ", pitch " << un_decimal(cabeza->pitch_deg)
          << " grados";
  } else {
    texto << "landmarks no utilizables";
  }
  if (coseno_frente) texto << std::setprecision(2) << ", coseno con frente " << *coseno_frente;
  return texto.str();
}

// Aviso de una pose rechazada por la verificacion del registro (pose, varios rostros o
// identidad). Por stderr, como avisar_pose_descartada: el contrato MQTT solo lleva el motivo.
void avisar_pose_rechazada(std::size_t indice, std::string_view motivo,
                           const std::string& medidas) {
  std::cerr << "[vision] pose rechazada: " << nombre_pose(indice) << " (motivo: " << motivo << "; "
            << medidas << ")\n";
}

// Aviso de una pose aceptada, con las mismas medidas: sin ellas no se pueden calibrar los
// umbrales por el lado de lo que SI pasa.
void avisar_pose_aceptada(std::size_t indice, const std::string& medidas) {
  std::cerr << "[vision] pose aceptada: " << nombre_pose(indice) << " (" << medidas << ")\n";
}

bool recuadro_finito(const cv::Rect2f& box) {
  return std::isfinite(box.x) && std::isfinite(box.y) && std::isfinite(box.width) &&
         std::isfinite(box.height);
}

// Area de un recuadro de la deteccion; 0 si no es utilizable (no finito o sin tamano).
float area_de(const cv::Rect2f& box) {
  if (!recuadro_finito(box)) return 0.0f;
  return std::max(box.width, 0.0f) * std::max(box.height, 0.0f);
}

// El rostro principal del registro: el de mayor area (a igual area, el de mayor score) entre
// los de score >= `min_score`, para que un falso positivo grande y de score bajo no sea el
// principal; si ninguno llega al minimo, entre todos. Un score no finito vale -infinito. Se
// recorre con comparaciones estrictas (no con max_element y un comparador) para que un NaN
// no rompa el orden.
const FaceDetection& rostro_principal(const std::vector<FaceDetection>& faces, float min_score) {
  constexpr float kSinScore = -std::numeric_limits<float>::infinity();
  const auto score_de = [](const FaceDetection& f) { return std::isfinite(f.score) ? f.score : kSinScore; };
  const bool alguno_suficiente = std::any_of(
      faces.begin(), faces.end(), [&](const FaceDetection& f) { return score_de(f) >= min_score; });
  const FaceDetection* principal = nullptr;
  for (const FaceDetection& face : faces) {
    if (alguno_suficiente && !(score_de(face) >= min_score)) continue;
    if (principal == nullptr || area_de(face.bbox) > area_de(principal->bbox) ||
        (area_de(face.bbox) == area_de(principal->bbox) && score_de(face) > score_de(*principal))) {
      principal = &face;
    }
  }
  return *principal;  // `faces` no esta vacia: ya se comprobo
}

// Cuantos de los demas rostros cuentan como "otro rostro" frente al principal: los que no
// son claramente pequenos (area menor que `min_area_ratio` de la principal) ni de score bajo
// (menor que `min_score`). Un recuadro o un score no finito no se puede descartar como
// "chico" o "bajo": cuenta como otro rostro (fallo cerrado).
int contar_otros_rostros(const std::vector<FaceDetection>& faces, const FaceDetection& principal,
                         float min_area_ratio, float min_score) {
  const float area_principal = area_de(principal.bbox);
  int otros = 0;
  for (const FaceDetection& face : faces) {
    if (&face == &principal) continue;
    const bool score_bajo = std::isfinite(face.score) && face.score < min_score;
    const bool chico =
        recuadro_finito(face.bbox) && area_de(face.bbox) < min_area_ratio * area_principal;
    if (!score_bajo && !chico) ++otros;
  }
  return otros;
}

// El veredicto de la pose del registro como motivo del contrato (vacio si esta en la pose).
// Sin medida utilizable (landmarks degenerados) no hay pose que dar por buena.
std::string_view motivo_de_pose(std::size_t indice, const std::optional<HeadPose>& cabeza,
                                double pitch_de_frente, const PoseLimits& limites) {
  if (!cabeza) return enrollment_reasons::kWrongPose;
  switch (verify_pose(nombre_pose(indice), *cabeza, pitch_de_frente, limites)) {
    case PoseVerdict::Ok:
      return {};
    case PoseVerdict::Excessive:
      return enrollment_reasons::kExcessiveTurn;
    case PoseVerdict::Incorrect:
      break;
  }
  return enrollment_reasons::kWrongPose;
}

// Avisa por stderr, al arrancar, de los controles del registro que la configuracion deja
// apagados o muy debilitados: no son errores (se pueden querer en una prueba), pero no deben
// pasar desapercibidos en produccion.
void avisar_controles_debilitados(const ServiceConfig& config) {
  if (config.min_front_similarity <= 0.0f) {
    std::cerr << "[vision] AVISO: la identidad de izquierda, derecha y arriba contra frente esta "
                 "DESACTIVADA (min_similitud_frente <= 0): un cambio de persona entre poses solo "
                 "lo detecta el descarte final\n";
  }
  if (config.min_front_2_similarity <= 0.0f) {
    std::cerr << "[vision] AVISO: la identidad de frente_2 contra frente esta DESACTIVADA "
                 "(min_similitud_frente_2 <= 0)\n";
  }
  if (config.other_face_min_area_ratio > enrollment_limits::kOtherFaceWeakAreaRatio) {
    std::cerr << "[vision] AVISO: area_min_otro_rostro (" << config.other_face_min_area_ratio
              << ") ignora los rostros de hasta ese tamano relativo: varios_rostros casi no se "
                 "detecta\n";
  }
  if (config.other_face_min_score > enrollment_limits::kOtherFaceWeakScore) {
    std::cerr << "[vision] AVISO: score_min_otro_rostro (" << config.other_face_min_score
              << ") ignora casi todos los demas rostros: varios_rostros casi no se detecta\n";
  }
  if (config.max_identity_rejections > enrollment_limits::kHighIdentityRejections) {
    std::cerr << "[vision] AVISO: max_rechazos_identidad (" << config.max_identity_rejections
              << ") es alto: un cambio de persona puede intentarlo muchas veces\n";
  }
}

void log_error(const char* context, const std::exception& error) {
  std::cerr << "[vision] " << context << ": " << error.what() << '\n';
}

void log_unknown_error(const char* context) {
  std::cerr << "[vision] " << context << ": excepcion desconocida\n";
}

// Hora de reserva cuando el reloj de pared falla: el mensaje sale con una hora que no
// es la real, pero el servicio no deja de responder.
constexpr std::string_view kFallbackTimestamp = "1970-01-01T00:00:00.000+00:00";

}  // namespace

// Promedio de los embeddings indicados (ya normalizados y de la misma dimension: el
// servicio lo comprueba al aceptar cada pose). Lanza std::invalid_argument si la lista
// de indices esta vacia (una media sin muestras no es un embedding) o si los embeddings
// indicados no tienen la misma dimension (leer fuera de rango no es un fallo que se pueda
// diagnosticar despues).
std::vector<float> mean_embedding(const std::vector<std::vector<float>>& embeddings,
                                  const std::vector<std::size_t>& indices) {
  if (indices.empty()) {
    throw std::invalid_argument("mean_embedding: no hay embeddings que promediar");
  }
  std::vector<float> mean(embeddings.front().size(), 0.0f);
  for (const std::size_t indice : indices) {
    if (embeddings[indice].size() != mean.size()) {
      throw std::invalid_argument("mean_embedding: los embeddings no tienen la misma dimension");
    }
    for (std::size_t i = 0; i < mean.size(); ++i) mean[i] += embeddings[indice][i];
  }
  for (float& x : mean) x /= static_cast<float>(indices.size());
  return mean;
}

// Descarte de outliers del registro (plan 5.4, paso 5), regla iterativa:
//
//   1. se calcula la media de las poses candidatas (al principio, las cinco);
//   2. se compara cada candidata con esa media (coseno) y se descarta SOLO la peor
//      (la de menor coseno) si su coseno esta por debajo del umbral;
//   3. se recalcula la media con las candidatas que quedan y se repite;
//   4. se para cuando la peor ya supera el umbral o cuando quedan menos de tres
//      candidatas (el registro se cierra como inconsistente, sin embedding).
//
// El registro es valido si queda un grupo coherente de al menos tres poses. Por eso el
// descarte sigue mientras queden tres candidatas: al llegar a tres, si la peor sigue
// por debajo del umbral, es que no hay grupo coherente de tres y el registro se cierra
// como inconsistente (fallar cerrado). Parar antes, en tres, daria por bueno un grupo
// que no se ha comprobado.
//
// La regla es robusta frente a un grupo en conflicto: la media se recalcula con lo que
// queda en cada vuelta, asi que un outlier deja de arrastrar a las demas. Antes (comparar
// cada pose con la media de las otras cuatro, sin recalcular) un alumno legitimo con
// poses poco parecidas entre si (rho bajo) descartaba las cinco y el registro moria
// como inconsistente.
//
// Devuelve los indices de las poses que sobreviven, en el orden del registro. Cada
// descarte se avisa por stderr (con el nombre de la pose), sin tocar el contrato MQTT.
std::vector<std::size_t> keep_consistent_poses(const std::vector<std::vector<float>>& embeddings,
                                                float min_pose_similarity) {
  std::vector<std::size_t> candidatas;
  for (std::size_t i = 0; i < embeddings.size(); ++i) {
    // Una pose con algun valor no finito no se puede comparar: se descarta antes de que
    // contamine la media de las candidatas (si no, contaminaria la de todas).
    if (!embedding_finito(embeddings[i])) {
      avisar_pose_descartada(i, "embedding no finito");
      continue;
    }
    candidatas.push_back(i);
  }

  while (candidatas.size() >= 3) {
    const std::vector<float> media = mean_embedding(embeddings, candidatas);
    // La peor: la de menor similitud con la media. Una similitud no finita es la peor
    // de todas (se descarta).
    std::size_t peor = candidatas.front();
    float peor_similitud = 2.0f;  // mas que cualquier coseno
    for (const std::size_t i : candidatas) {
      float similitud = cosine_similarity(embeddings[i], media);
      if (!std::isfinite(similitud)) similitud = 0.0f;
      if (similitud < peor_similitud) {
        peor_similitud = similitud;
        peor = i;
      }
    }
    // Todas las candidatas superan el umbral: no hay nada que descartar.
    if (peor_similitud >= min_pose_similarity) break;

    std::ostringstream texto;
    texto << "similitud " << std::fixed << std::setprecision(2) << peor_similitud;
    avisar_pose_descartada(peor, texto.str());
    candidatas.erase(std::find(candidatas.begin(), candidatas.end(), peor));
  }
  // Con menos de tres candidatas el registro es inconsistente: lo decide quien llama
  // (mismo motivo "inconsistente" de antes, sin motivos nuevos).
  return candidatas;
}

VisionService::VisionService(ServiceDeps deps, ServiceConfig config)
    : deps_(deps), config_(std::move(config)), clock_guard_(deps.clock_anchors) {
  if (config_.capture_url.empty()) {
    throw std::invalid_argument("ServiceConfig.capture_url vacia");
  }
  if (config_.burst_size < 1 || config_.max_attempts_per_pose < 1) {
    throw std::invalid_argument("ServiceConfig: burst_size y max_attempts_per_pose deben ser >= 1");
  }
  if (config_.http_timeout <= Millis::zero() || config_.max_body_bytes == 0 ||
      config_.max_image_width < 1 || config_.max_image_height < 1) {
    throw std::invalid_argument(
        "ServiceConfig: http_timeout, max_body_bytes y las dimensiones maximas deben ser > 0");
  }
  if (config_.max_inbox_events < 4) {
    throw std::invalid_argument(
        "ServiceConfig: max_inbox_events debe ser >= 4 (un PIR, un probar y un enrolar caben siempre)");
  }
  if (config_.min_dudoso_score < 0.0f || config_.min_recognized_score > 1.0f ||
      config_.min_dudoso_score > config_.min_recognized_score) {
    throw std::invalid_argument(
        "ServiceConfig: los umbrales de decision no son validos (0 <= dudoso <= reconocido <= 1)");
  }
  if (config_.min_pose_similarity < 0.0f || config_.min_pose_similarity > 1.0f) {
    throw std::invalid_argument(
        "ServiceConfig: min_pose_similarity debe estar entre 0 y 1");
  }
  // Los umbrales del registro. Se comprueba la forma valida (y se niega) para que un NaN no pase.
  if (!(config_.min_front_similarity >= 0.0f && config_.min_front_similarity <= 1.0f) ||
      !(config_.min_front_2_similarity >= config_.min_front_similarity &&
        config_.min_front_2_similarity <= 1.0f)) {
    throw std::invalid_argument(
        "ServiceConfig: la similitud con frente debe cumplir 0 <= min_front_similarity <= "
        "min_front_2_similarity <= 1");
  }
  if (!(config_.other_face_min_area_ratio > 0.0f && config_.other_face_min_area_ratio <= 1.0f) ||
      !(config_.other_face_min_score >= 0.0f && config_.other_face_min_score <= 1.0f)) {
    throw std::invalid_argument(
        "ServiceConfig: other_face_min_area_ratio debe estar en (0, 1] y other_face_min_score "
        "entre 0 y 1");
  }
  // Un control casi apagado no es una configuracion: area_min y score_min muy altos ignoran
  // casi cualquier otro rostro.
  if (!(config_.other_face_min_area_ratio <= enrollment_limits::kOtherFaceMaxAreaRatio) ||
      !(config_.other_face_min_score <= enrollment_limits::kOtherFaceMaxScore)) {
    throw std::invalid_argument(
        "ServiceConfig: other_face_min_area_ratio no puede pasar de 0.8 ni other_face_min_score "
        "de 0.9 (ignorarian casi cualquier otro rostro)");
  }
  // El umbral de duplicado esta en (0, min_dudoso_score]: con 0 todo registro chocaria con
  // cualquier otra cuenta, y por encima de "dudoso" dejaria dos cuentas con el mismo rostro.
  if (!(config_.min_duplicate_similarity > 0.0f &&
        config_.min_duplicate_similarity <= config_.min_dudoso_score)) {
    throw std::invalid_argument(
        "ServiceConfig: min_duplicate_similarity debe estar en (0, min_dudoso_score]");
  }
  if (config_.max_identity_rejections < 1) {
    throw std::invalid_argument("ServiceConfig: max_identity_rejections debe ser >= 1");
  }
  if (const auto problem = check_pose_limits(config_.pose_limits)) {
    throw std::invalid_argument(std::string("ServiceConfig: limite de pose invalido (") +
                                problem->key + "): " + problem->message);
  }
  avisar_controles_debilitados(config_);
  if (!deps_.gallery_ready) {
    std::cerr << "[vision] AVISO: sin gallery_ready: el chequeo de que la galeria este cargada "
                 "queda ABIERTO (un registro no se rechaza con una galeria sin cargar). Es lo "
                 "esperado con --dry-run y en pruebas\n";
  }
  // La clave de la respuesta cifrada: se deriva una sola vez, al arrancar.
  if (!config_.capture_hmac_secret.empty()) {
    response_key_ = derivar_clave_respuesta(config_.capture_hmac_secret);
  }
  // Si la segunda suscripcion lanza, la primera (ya miembro) se da de baja al deshacerse
  // el constructor. Todo lo que usan los handlers esta construido antes de este punto.
  pir_subscription_ =
      deps_.bus.subscribe(topics::kCameraPir, [this](const Payload& p) { on_message(p); });
  command_subscription_ =
      deps_.bus.subscribe(topics::kVisionCmd, [this](const Payload& p) { on_message(p); });
}

// Corre en el hilo del bus (un callback de C en la libreria MQTT): no puede dejar escapar
// ninguna excepcion. Si algo falla despues de tomar una bandera de pendiente, se suelta.
void VisionService::on_message(const Payload& payload) noexcept {
  std::atomic<bool>* claimed = nullptr;
  try {
    enqueue(payload, claimed);
  } catch (...) {
    if (claimed != nullptr) *claimed = false;
    if (first_log(kLogEnqueueFailed)) {
      std::cerr << "[vision] no se pudo encolar un mensaje\n";  // sin el contenido del mensaje
    }
  }
}

// `claimed` queda apuntando a la bandera tomada hasta que el evento esta en la cola.
void VisionService::enqueue(const Payload& payload, std::atomic<bool>*& claimed) {
  Event event;
  bool is_cancel = false;
  if (const auto* pir = std::get_if<PirEvent>(&payload)) {
    // Un PIR con el servicio ocupado, o con otro ya en cola, no genera otro pase de lista.
    if (!pir->motion || state_ != ServiceState::Idle || pir_pending_.exchange(true)) return;
    claimed = &pir_pending_;
    event.payload = *pir;
  } else if (const auto* command = std::get_if<VisionCommand>(&payload)) {
    // Los descartes de aqui no responden: las ordenes vienen de un cliente que ya valido al
    // alumno, y un flujo de basura no debe ocupar la cola ni provocar respuestas.
    std::atomic<bool>* pending = nullptr;
    switch (command->action) {
      case VisionAction::Enroll:
        if (!is_valid_account(command->account)) {
          if (first_log(kLogInvalidEnroll)) {
            std::cerr << "[vision] enrolar con cuenta invalida descartado (sin respuesta)\n";
          }
          return;
        }
        break;
      case VisionAction::Test:
        pending = &test_pending_;
        break;
      case VisionAction::Cancel:
        if (!command->account.empty() && !is_valid_account(command->account)) {
          if (first_log(kLogInvalidCancel)) {
            std::cerr << "[vision] cancelar con cuenta invalida descartado (sin respuesta)\n";
          }
          return;
        }
        is_cancel = true;
        break;
    }
    // A lo sumo un probar en cola, como el PIR. El enrolar no se coalesce: el segundo recibe
    // su "ocupado" en start_enrollment.
    if (pending != nullptr) {
      if (pending->exchange(true)) {
        if (first_log(kLogCoalesced)) {
          std::cerr << "[vision] orden repetida descartada: ya hay un probar en cola\n";
        }
        return;
      }
      claimed = pending;
    }
    event.payload = *command;
  } else {
    return;
  }
  event.received = deps_.clock.now();

  // El cancelar no se coalesce (uno ajeno taparia al verdadero), asi que se acota: deja siempre
  // lugar para el PIR, el probar y el enrolar.
  const std::size_t limit = is_cancel ? config_.max_inbox_events - 3 : config_.max_inbox_events;
  {
    std::lock_guard lock(inbox_mutex_);
    if (inbox_.size() < limit) {
      inbox_.push_back(std::move(event));
      claimed = nullptr;  // ya en la cola: handle() soltara la bandera
      return;
    }
  }
  if (claimed != nullptr) *std::exchange(claimed, nullptr) = false;
  if (first_log(kLogOverflow)) {
    std::cerr << "[vision] cola de eventos llena (" << config_.max_inbox_events
              << "): se descartan los eventos hasta el proximo poll\n";
  }
}

void VisionService::poll() {
  std::deque<Event> events;
  {
    std::lock_guard lock(inbox_mutex_);
    events.swap(inbox_);
  }
  logged_ = 0;  // cada linea de log de la cola vuelve a salir una vez por poll
  // Un evento que lanza no descarta el resto del lote (p. ej. un cancelar en cola), ni el
  // avance del registro que viene despues.
  const auto isolated = [](const char* context, const auto& action) {
    try {
      action();
    } catch (const std::exception& error) {
      log_error(context, error);
    } catch (...) {
      log_unknown_error(context);
    }
  };
  for (const Event& event : events) {
    isolated("fallo al atender un evento", [&] { handle(event); });
  }
  if (state_ == ServiceState::Enrolling && deps_.clock.now() >= enrollment_->due) {
    isolated("fallo al avanzar el registro", [&] { advance_enrollment(); });
  }
}

void VisionService::handle(const Event& event) {
  std::visit([&](const auto& payload) { handle(payload, event.received); }, event.payload);
}

void VisionService::handle(const PirEvent&, TimePoint received) {
  const ScopeExit clear_pending([this]() noexcept { pir_pending_ = false; });
  if (state_ == ServiceState::Idle) {
    run_attendance(received, AttendanceMode::Attendance);
  }
}

void VisionService::handle(const VisionCommand& command, TimePoint received) {
  switch (command.action) {
    case VisionAction::Enroll: {
      start_enrollment(command.account, command.name);
      break;
    }
    case VisionAction::Cancel:
      cancel_enrollment(command.account);
      break;
    case VisionAction::Test: {
      const ScopeExit clear_pending([this]() noexcept { test_pending_ = false; });
      if (state_ == ServiceState::Idle) {
        run_attendance(received, AttendanceMode::Test);
      } else {
        publish_refusal(command.account, reasons::kBusy);
      }
      break;
    }
  }
}

// --- Pase de lista (plan 5.1) -----------------------------------------------------

// Pase lo que pase (incluso si el bus o el reloj lanzan), el servicio vuelve a Idle, se
// intenta apagar la luz y el pedido recibe un resultado (error_interno si fallo antes de
// poder evaluar); el resultado y el apagado se intentan por separado.
void VisionService::run_attendance(TimePoint received, AttendanceMode mode) {
  state_ = ServiceState::AttendanceInProgress;
  publish_status();
  const ScopeExit finish([this]() noexcept {
    state_ = ServiceState::Idle;
    publish_light(false);
    publish_status();
  });

  const auto internal_error = [&] {
    AttendanceResult failed(mode);
    failed.timestamp = stamp_now();
    failed.decision = Decision::Error;
    failed.reason = std::string(reasons::kInternalError);
    try {
      failed.lat_ms.total = elapsed_ms(received);
    } catch (...) {  // si lo que fallo es el reloj, igual se responde (sin total)
      failed.lat_ms.total = std::nullopt;
    }
    return failed;
  };
  AttendanceResult result(mode);
  try {
    publish_light(true);
    deps_.clock.sleep_for(config_.settle_time);
    result = evaluate_attendance(received, mode);
  } catch (const std::exception& error) {
    log_error("fallo el pase de lista antes de evaluarlo", error);
    result = internal_error();
  } catch (...) {
    log_unknown_error("fallo el pase de lista antes de evaluarlo");
    result = internal_error();
  }
  try_publish(topics::kVisionResult, result, "el resultado del pase de lista");
}

// Latencias: cap = rafaga + eleccion de la mas nitida; det = detector; emb = alineacion
// + embedder; total = desde que el PIR (o el comando) llego al servicio hasta tener el
// resultado, o sea con la espera en cola y la de la luz incluidas.
//
// Decision (contrato v0.3): el servicio es el dueno del modelo y de los umbrales, asi
// que es quien decide. "reconocido" por encima de min_recognized_score, "dudoso" (zona
// gris) entre los dos umbrales y "desconocido" por debajo. Con "dudoso" y "desconocido"
// se manda el mejor candidato, para que Node-RED pueda auditarlo.
AttendanceResult VisionService::evaluate_attendance(TimePoint start, AttendanceMode mode) {
  AttendanceResult result(mode);
  result.timestamp = stamp_now();
  try {
    const TimePoint cap_start = deps_.clock.now();
    const std::vector<cv::Mat> frames = capture_burst();
    const std::optional<SharpestFrame> sharpest = pick_sharpest(frames);
    result.lat_ms.cap = elapsed_ms(cap_start);

    if (!sharpest) {
      // No se pudo capturar: la imagen no sirve para decidir nada.
      result.decision = Decision::InsufficientImage;
      result.reason = std::string(reasons::kCaptureFailed);
    } else if (sharpest->score < config_.min_sharpness) {
      result.decision = Decision::InsufficientImage;
      result.reason = std::string(reasons::kInsufficientImage);
    } else {
      FaceOutcome face = analyze(frames[sharpest->index]);
      result.lat_ms.det = face.det_ms;
      result.lat_ms.emb = face.emb_ms;
      if (!face.reason.empty()) {
        // No hay rostro: no hay a quien comparar.
        result.decision = Decision::NoFace;
        result.reason = std::string(face.reason);
      } else if (const auto match = deps_.gallery.match(face.embedding)) {
        result.confidence = match->score;
        result.account = match->account;
        result.margin = match->margin;  // nullopt si la galeria tiene menos de dos
        if (match->score >= config_.min_recognized_score) {
          result.decision = Decision::Recognized;
          // Solo el reconocido del pase de lista lleva la miniatura del rostro (contrato
          // v0.17): la prueba del registro no marca asistencia y no la necesita.
          if (mode == AttendanceMode::Attendance) {
            result.thumbnail = attendance_thumbnail(frames[sharpest->index], face.bbox);
          }
        } else if (match->score >= config_.min_dudoso_score) {
          result.decision = Decision::Doubtful;
        } else {
          result.decision = Decision::Unknown;
          result.reason = std::string(reasons::kUnknown);
        }
      } else {
        result.decision = Decision::Unknown;
        result.reason = std::string(reasons::kEmptyGallery);
      }
    }
  } catch (const std::exception& error) {
    log_error("fallo el pipeline de asistencia", error);
    result.decision = Decision::Error;
    result.reason = std::string(reasons::kInternalError);
  } catch (...) {
    log_unknown_error("fallo el pipeline de asistencia");
    result.decision = Decision::Error;
    result.reason = std::string(reasons::kInternalError);
  }
  result.lat_ms.total = elapsed_ms(start);
  return result;
}

// La miniatura del reconocido del pase de lista: el mismo recorte del rostro (con margen), en
// 160x120 y JPEG en base64 dentro del tope, que usa el registro. Es opcional: si no se puede armar
// (no cabe, el recorte se sale de la foto o OpenCV lanza), el resultado sale igual y sin ella,
// porque la asistencia no depende de la foto. Nunca se guarda ni se escribe en el log.
std::optional<std::string> VisionService::attendance_thumbnail(const cv::Mat& frame,
                                                               const cv::Rect2f& face) const {
  try {
    return encode_thumbnail(frame, face);
  } catch (const std::exception& error) {
    log_error("no se pudo armar la miniatura del pase de lista", error);
  } catch (...) {
    log_unknown_error("no se pudo armar la miniatura del pase de lista");
  }
  return std::nullopt;
}

// Una rafaga de fotos. Antes de empezar se respeta el intervalo minimo desde el inicio de la
// anterior: la placa limita el ritmo y, sin esta espera, dos rafagas seguidas (el PIR
// rebota cada 2 s) acabarian en 4 respuestas 429 y en captura_fallida.
std::vector<cv::Mat> VisionService::capture_burst() {
  if (last_burst_start_ && config_.burst_interval.count() > 0) {
    const Millis elapsed =
        std::chrono::duration_cast<Millis>(deps_.clock.now() - *last_burst_start_);
    if (elapsed < config_.burst_interval) deps_.clock.sleep_for(config_.burst_interval - elapsed);
  }
  last_burst_start_ = deps_.clock.now();

  std::vector<cv::Mat> frames;
  Millis retry_budget = config_.max_retry_wait;
  CaptureStats stats;
  for (int i = 0; i < config_.burst_size; ++i) {
    cv::Mat frame = capture_frame(retry_budget, stats);
    if (!frame.empty()) frames.push_back(std::move(frame));
  }
  log_capture_stats(stats);
  return frames;
}

namespace {

// Lo que significa cada codigo que puede devolver la placa en vez de la foto
// (firmware/xiao/README.md). Nunca se vuelca el cuerpo ni la firma.
const char* capture_rejection_text(int http_status) {
  switch (http_status) {
    case 401:
      return "falta una cabecera de la firma o la firma no cuadra; revisa que "
             "CAPTURA_HMAC_SECRETO sea el mismo en la placa y en el borde";
    case 403:
      return "nonce repetido o ts mas antiguo que el ultimo que acepto; revisa que el reloj del "
             "borde no haya retrocedido";
    case 429:
      return "limite de ritmo de capturas de la placa";
    case 503:
      return "camara no lista, ocupada o por encima de la temperatura";
    default:
      return "respuesta inesperada en vez de la foto";
  }
}

// La espera antes de repetir una foto con 429: lo que diga Retry-After (en segundos enteros)
// o, si no vino, `fallback`.
Millis retry_delay(const HttpResponse& response, Millis fallback) {
  if (response.retry_after_s > 0) return Millis(static_cast<long long>(response.retry_after_s) * 1000);
  return fallback;
}

}  // namespace

// Un solo log por rafaga (o pose), con la cuenta: una linea por codigo de la placa y otra
// si hubo respuestas que no son autenticas.
void VisionService::log_capture_stats(const CaptureStats& stats) const {
  for (const auto& [code, count] : stats.rejected) {
    std::cerr << "[vision] la placa rechazo " << count << "/" << stats.requested
              << " fotos con " << code << ": " << capture_rejection_text(code) << '\n';
  }
  const int not_authentic = stats.auth_iv_missing + stats.auth_iv_invalid +
                            stats.auth_short_body + stats.auth_bad_tag;
  if (not_authentic > 0) {
    std::cerr << "[vision] respuesta no autentica: " << not_authentic << "/" << stats.requested
              << " fotos descartadas sin decodificar (" << stats.auth_iv_missing
              << " sin X-Captura-IV, " << stats.auth_iv_invalid << " con un IV mal formado, "
              << stats.auth_short_body << " con menos de " << kLongitudEtiqueta
              << " bytes, " << stats.auth_bad_tag
              << " que no verifican); alguien puede estar contestando en nombre de la placa, o "
                 "el secreto no coincide\n";
  }
}

// Matriz vacia si la peticion, la validacion o la decodificacion fallan.
//
// Con secreto, la peticion va firmada (HMAC-SHA256, vision/captura_firma.hpp) y cada foto
// de la rafaga lleva su propio ts y su propio nonce: la placa rechaza un nonce repetido.
// El camino firmado es "/capture" fijo, no el de la URL. Con secreto tambien se exige que
// el 200 venga cifrado (AES-256-GCM, vision/captura_firma.hpp) y se descifra y verifica con
// el ts y el nonce de esta peticion; si algo falla, la foto se descarta sin decodificarla.
//
// Ante un 429 se espera y se repite la foto una vez (con un ts y un nonce nuevos), dentro
// del presupuesto de espera extra de la rafaga.
cv::Mat VisionService::capture_frame(Millis& retry_budget, CaptureStats& stats) {
  ++stats.requested;
  const bool signing = !config_.capture_hmac_secret.empty();
  for (int attempt = 0; attempt < 2; ++attempt) {
    HttpResponse response;
    std::int64_t ts = 0;
    std::uint64_t nonce_value = 0;
    if (!signing) {
      response = deps_.http.get(config_.capture_url, config_.http_timeout, config_.max_body_bytes);
    } else {
      if (!deps_.http.supports_request_headers()) {
        std::cerr << "[vision] el cliente HTTP no admite cabeceras: la peticion de captura no "
                     "se puede firmar\n";
        return {};
      }
      // El ts es del reloj del BORDE: la placa no tiene uno confiable.
      ts = deps_.wall_clock.now_epoch_seconds();
      if (ts < kMinEpochPlausible || ts > kMaxEpochPlausible) {
        std::cerr << "[vision] la hora del borde no es plausible (epoch " << ts
                  << "): revisa el RTC y NTP; no se firma la peticion de captura\n";
        return {};
      }
      // La guardia del reloj de pared (reloj_guardia.hpp): un ts del futuro dejaria a la placa
      // con un piso imposible y rechazando con 403 las capturas buenas. Mide el avance contra
      // CLOCK_BOOTTIME (la suspension no es un salto) y ancla la primera firma tras arrancar al
      // RTC de hardware o al archivo de estado.
      const std::int64_t boottime_s =
          std::chrono::duration_cast<std::chrono::seconds>(deps_.clock.boottime()).count();
      const VeredictoReloj veredicto = clock_guard_.evaluar(ts, boottime_s);
      if (!veredicto.mensaje.empty()) std::cerr << "[vision] " << veredicto.mensaje << '\n';
      if (!veredicto.permitido) return {};
      const std::optional<std::uint64_t> nonce = nonce_aleatorio();
      if (!nonce) {
        std::cerr << "[vision] no se pudo obtener un nonce aleatorio del sistema: no se pide "
                     "la foto\n";
        return {};
      }
      nonce_value = *nonce;
      response = deps_.http.get(config_.capture_url, config_.http_timeout, config_.max_body_bytes,
                                cabeceras_captura(config_.capture_hmac_secret, ts, nonce_value));
    }

    if (response.status != 200) {
      if (response.rejected_status == 429 && attempt == 0) {
        const Millis wait = retry_delay(response, config_.retry_wait);
        if (wait <= retry_budget) {
          retry_budget -= wait;
          deps_.clock.sleep_for(wait);
          continue;
        }
      }
      if (response.rejected_status != 0) ++stats.rejected[response.rejected_status];
      return {};
    }
    if (signing) {
      // La respuesta viene cifrada (AES-256-GCM): se descifra y verifica ANTES de mirar la
      // imagen. Cualquier fallo descarta la foto sin decodificarla.
      HttpResponse plain;
      plain.status = 200;
      switch (descifrar_respuesta(*response_key_, response.resp_iv, ts, nonce_value,
                                  response.body.data(), response.body.size(), plain.body)) {
        case ResultadoDescifrado::Ok:
          break;
        case ResultadoDescifrado::IvAusente:
          ++stats.auth_iv_missing;
          return {};
        case ResultadoDescifrado::IvInvalido:
          ++stats.auth_iv_invalid;
          return {};
        case ResultadoDescifrado::CuerpoCorto:
          ++stats.auth_short_body;
          return {};
        case ResultadoDescifrado::NoAutentico:
          ++stats.auth_bad_tag;
          return {};
      }
      if (plain.body.empty()) return {};
      cv::Mat frame = decode_capture(plain);
      borrar_bytes(plain.body);  // la foto en claro no se queda en memoria
      return frame;
    }
    if (response.body.empty()) return {};
    return decode_capture(response);
  }
  return {};  // inalcanzable: la segunda vuelta siempre retorna
}

// El cuerpo viene de la red: se acota su tamano (la interfaz HTTP ya debe haber cortado la
// transferencia; esto es la segunda barrera) y se lee el formato y el tamano de la imagen
// en la cabecera, antes de decodificar, para que una foto enorme o enganosa no llegue a
// reservar memoria. Solo JPEG baseline/secuencial (y PNG si accept_png); lo demas se rechaza.
// Un fallo del decodificador descarta solo esta foto.
cv::Mat VisionService::decode_capture(const HttpResponse& response) const {
  if (response.body.size() > config_.max_body_bytes) {
    std::cerr << "[vision] foto descartada: " << response.body.size() << " bytes (maximo "
              << config_.max_body_bytes << ")\n";
    return {};
  }
  const std::optional<ImageHeader> header = peek_image_header(response.body);
  if (!header) {
    std::cerr << "[vision] foto descartada: no es un JPEG (baseline) ni un PNG con cabecera valida\n";
    return {};
  }
  if (header->format == ImageFormat::Png && !config_.accept_png) {
    std::cerr << "[vision] foto descartada: PNG no aceptado\n";
    return {};
  }
  if (header->size.width > config_.max_image_width || header->size.height > config_.max_image_height) {
    std::cerr << "[vision] foto descartada: " << header->size.width << "x" << header->size.height
              << " supera el maximo " << config_.max_image_width << "x" << config_.max_image_height
              << '\n';
    return {};
  }
  try {
    return cv::imdecode(response.body, cv::IMREAD_COLOR);
  } catch (const std::exception& error) {  // incluye cv::Exception
    log_error("foto descartada: fallo al decodificar", error);
    return {};
  }
}

// Detecta y elige el rostro (ver FaceSelection). No calcula el embedding.
VisionService::FaceOutcome VisionService::detect_face(const cv::Mat& frame, FaceSelection selection) {
  FaceOutcome outcome;
  const TimePoint det_start = deps_.clock.now();
  const std::vector<FaceDetection> faces = deps_.detector.detect(frame);
  outcome.det_ms = elapsed_ms(det_start);
  outcome.faces_detected = static_cast<int>(faces.size());
  if (faces.empty()) {
    outcome.reason = reasons::kNoFace;
    return outcome;
  }

  const FaceDetection& best =
      selection == FaceSelection::Enrollment
          ? rostro_principal(faces, config_.other_face_min_score)
          : *std::max_element(faces.begin(), faces.end(),
                              [](const FaceDetection& a, const FaceDetection& b) {
                                return a.score < b.score;
                              });
  outcome.bbox = best.bbox;
  if (selection == FaceSelection::Enrollment) {
    outcome.other_faces = contar_otros_rostros(faces, best, config_.other_face_min_area_ratio,
                                               config_.other_face_min_score);
    if (outcome.other_faces > 0) {
      // Dos personas en la foto: no se sabe cual es la que se registra.
      outcome.reason = enrollment_reasons::kSeveralFaces;
      return outcome;
    }
  }
  outcome.landmarks = best.landmarks;
  return outcome;
}

// Alinea con los landmarks del rostro elegido y calcula su embedding.
void VisionService::embed_face(const cv::Mat& frame, FaceOutcome& outcome) {
  const TimePoint emb_start = deps_.clock.now();
  outcome.embedding = deps_.embedder.embed(align_face(frame, outcome.landmarks));
  outcome.emb_ms = elapsed_ms(emb_start);
  if (outcome.embedding.empty()) {
    throw EmbeddingError("el embedder devolvio un vector vacio");
  }
}

// El pase de lista: el rostro con mayor score, con su embedding.
VisionService::FaceOutcome VisionService::analyze(const cv::Mat& frame) {
  FaceOutcome outcome = detect_face(frame, FaceSelection::BestScore);
  if (outcome.reason.empty()) embed_face(frame, outcome);
  return outcome;
}

bool VisionService::try_publish(std::string_view topic, const Payload& payload, const char* what) {
  try {
    deps_.bus.publish(topic, payload);
    return true;
  } catch (const std::exception& error) {
    std::cerr << "[vision] no se pudo publicar " << what << " en " << topic << ": " << error.what()
              << '\n';
  } catch (...) {
    std::cerr << "[vision] no se pudo publicar " << what << " en " << topic
              << ": excepcion desconocida\n";
  }
  return false;
}

void VisionService::publish_light(bool on) {
  ActuatorCommand command;
  command.light = on ? 1 : 0;
  try_publish(topics::kActuatorCmd, command, on ? "el encendido de la luz" : "el apagado de la luz");
}

// El estado del servicio (contrato v0.3): lo que esta haciendo y cuantos embeddings
// tiene en RAM. Lo publica el servicio en cada cambio de modo; el LWT {"en_linea":false}
// lo pone el adaptador MQTT, no el servicio.
void VisionService::publish_status() {
  VisionStatus status;
  status.timestamp = stamp_now();
  status.online = true;
  switch (state_) {
    case ServiceState::AttendanceInProgress:
      status.mode = VisionMode::Attendance;
      break;
    case ServiceState::Enrolling:
      status.mode = VisionMode::Enrolling;
      break;
    case ServiceState::Idle:
      status.mode = VisionMode::Idle;
      break;
  }
  status.gallery_size = deps_.gallery.size();
  status.model = config_.model_name;
  try_publish(topics::kVisionStatus, status, "el estado del servicio");
}

// La hora del contrato: ISO 8601 con zona. Si el reloj falla, se responde con la hora
// de reserva en vez de perder el mensaje.
std::string VisionService::stamp_now() const {
  try {
    return deps_.wall_clock.now_iso8601();
  } catch (const std::exception& error) {
    log_error("no se pudo obtener la hora", error);
  } catch (...) {
    log_unknown_error("no se pudo obtener la hora");
  }
  return std::string(kFallbackTimestamp);
}

int VisionService::elapsed_ms(TimePoint since) const {
  return static_cast<int>(
      std::chrono::duration_cast<Millis>(deps_.clock.now() - since).count());
}

namespace {

void log_invalid_clock(const std::string& timestamp) {
  std::cerr << "[vision] reloj invalido: no se puede registrar (ts recibido: \"" << timestamp
            << "\"); revisa NTP o el RTC\n";
}

}  // namespace

// --- Registro (plan 5.4) ----------------------------------------------------------

void VisionService::start_enrollment(const std::string& account,
                                     const std::optional<std::string>& name) {
  if (!is_valid_account(account)) {
    publish_refusal(account, reasons::kInvalidAccount);
    return;
  }
  if (state_ != ServiceState::Idle) {
    // Ya hay un registro o un pase de lista en curso.
    publish_refusal(account, reasons::kBusy);
    return;
  }
  // Sin una galeria de fiar no se puede comprobar al final si el rostro ya esta registrado:
  // se rechaza la orden antes de hacer posar al alumno (falla cerrado). Una galeria vacia tras
  // una carga correcta si es valida.
  if (!gallery_usable()) {
    std::cerr << "[vision] registro rechazado: la galeria no esta cargada (la ultima recarga "
                 "fallo): no se podria comprobar si el rostro ya esta registrado\n";
    publish_refusal(account, enrollment_reasons::kGalleryUnavailable);
    return;
  }
  // Sin hora valida el embedding final no se podria publicar: se rechaza la orden antes
  // de encender la luz o de hacer posar al alumno (motivo npu, como el resto de fallos).
  const std::string timestamp = stamp_now();
  if (!is_plausible_timestamp(timestamp)) {
    log_invalid_clock(timestamp);
    publish_refusal(account, enrollment_reasons::kNpu);
    return;
  }
  // El registro se arma en un local y solo entonces se instala junto con el estado: si algo
  // lanza, no queda un registro a medias ni un estado sin registro.
  Enrollment fresh;
  fresh.account = account;
  fresh.name = name;
  fresh.due = deps_.clock.now() + config_.pose_delay;
  enrollment_ = std::move(fresh);
  state_ = ServiceState::Enrolling;
  publish_light(true);
  publish_status();
  publish_progress(*enrollment_, EnrollmentStatus::WaitingPose);
}

// Captura la pose en curso. Si se rechaza, la repite hasta max_attempts_per_pose.
void VisionService::advance_enrollment() {
  Enrollment& e = *enrollment_;
  // La captura de una pose cuenta para el ritmo de la placa: una rafaga que venga justo
  // despues se espacia desde aqui.
  last_burst_start_ = deps_.clock.now();
  // Se avisa de que empieza la captura de la pose en curso.
  publish_progress(e, EnrollmentStatus::Capturing);
  std::string_view reason;
  std::vector<float> embedding;
  // Lo medido en esta captura, para decidir y para el diagnostico por stderr.
  std::optional<HeadPose> head;
  std::optional<float> front_similarity;  // coseno contra la pose "frente" aceptada
  int faces_detected = 0;
  int other_faces = 0;
  // La miniatura del rostro: solo sale en el mensaje de esta pose. No se guarda en
  // ningun sitio (ni disco, ni base de datos, ni galeria).
  std::optional<std::string> miniatura;
  bool internal_failure = false;
  try {
    Millis retry_budget = config_.max_retry_wait;
    CaptureStats stats;
    const cv::Mat frame = capture_frame(retry_budget, stats);
    log_capture_stats(stats);
    if (frame.empty()) {
      reason = enrollment_reasons::kCaptureFailure;
    } else {
      FaceOutcome face = detect_face(frame, FaceSelection::Enrollment);
      reason = face.reason;
      faces_detected = face.faces_detected;
      other_faces = face.other_faces;
      if (reason.empty()) {
        // La pose antes que el embedding: es lo barato (sin NPU) y lo que el alumno puede
        // corregir al momento.
        head = estimate_head_pose(face.landmarks, config_.pose_limits.invert_turn);
        reason = motivo_de_pose(e.step, head, e.front_pitch_deg.value_or(0.0), config_.pose_limits);
      }
      if (reason.empty()) {
        embed_face(frame, face);
        embedding = l2_normalized(face.embedding);
      }
      // Sin rostro no hay recorte: el campo "miniatura" no se manda.
      miniatura = encode_thumbnail(frame, face.bbox);
    }
  } catch (const std::exception& error) {
    log_error("fallo la captura de la pose", error);
    internal_failure = true;
  } catch (...) {
    log_unknown_error("fallo la captura de la pose");
    internal_failure = true;
  }

  if (internal_failure) {
    // Fallo interno del pipeline (NPU o embedder): reintentar la pose no tiene sentido.
    publish_progress(e, EnrollmentStatus::Error, enrollment_reasons::kNpu);
    finish_enrollment();
    return;
  }

  // Un embedder que cambia de dimension a mitad del registro no se arregla reintentando.
  if (reason.empty() && !e.embeddings.empty() &&
      embedding.size() != e.embeddings.front().size()) {
    std::cerr << "[vision] la pose " << e.step + 1 << " dio un embedding de dimension "
              << embedding.size() << " y las anteriores de " << e.embeddings.front().size() << '\n';
    publish_progress(e, EnrollmentStatus::Error, enrollment_reasons::kInconsistent);
    finish_enrollment();
    return;
  }

  // Identidad (plan 5.4, paso 5): cada pose despues de "frente" tiene que parecerse a la
  // pose "frente" aceptada. Si no, puede ser otra persona: la pose se rechaza y se repite
  // (NO se aborta el registro), y cuenta para los intentos como cualquier rechazo.
  // `frente_2` es la misma pose que "frente": umbral mas alto. Un coseno no finito vale 0.
  // Un umbral <= 0 desactiva la comprobacion (incluso con un coseno negativo); el rechazo se
  // cuenta en todo el registro (max_identity_rejections).
  if (reason.empty() && !e.embeddings.empty()) {
    const float similarity = cosine_similarity(embedding, e.embeddings.front());
    front_similarity = similarity;
    const float minimum = kEnrollmentPoses[e.step] == "frente_2" ? config_.min_front_2_similarity
                                                                  : config_.min_front_similarity;
    if (minimum > 0.0f && !(similarity >= minimum)) {
      reason = enrollment_reasons::kInconsistent;
      ++e.identity_rejections;
    }
  }

  if (!reason.empty()) {
    // Diagnostico de los rechazos de la verificacion (el resto de motivos ya se explican solos).
    if (reason == enrollment_reasons::kWrongPose || reason == enrollment_reasons::kExcessiveTurn ||
        reason == enrollment_reasons::kInconsistent) {
      avisar_pose_rechazada(e.step, reason, texto_medidas(head, front_similarity));
    } else if (reason == enrollment_reasons::kSeveralFaces) {
      avisar_pose_rechazada(e.step, reason,
                            std::to_string(other_faces) + " rostros mas, ademas del principal, de " +
                                std::to_string(faces_detected) + " detectados");
    }
    publish_progress(e, EnrollmentStatus::Rejected, reason, miniatura);
    // Se termina al agotar los intentos de la pose o los rechazos de identidad del registro; el
    // error lleva el motivo del ultimo rechazo ("npu" es solo de fallos reales del pipeline).
    const bool attempts_exhausted = ++e.attempts >= config_.max_attempts_per_pose;
    const bool identity_exhausted = reason == enrollment_reasons::kInconsistent &&
                                    e.identity_rejections >= config_.max_identity_rejections;
    if (attempts_exhausted || identity_exhausted) {
      if (identity_exhausted) {
        std::cerr << "[vision] registro terminado: " << e.identity_rejections
                  << " poses rechazadas por identidad (maximo " << config_.max_identity_rejections
                  << ")\n";
      }
      publish_progress(e, EnrollmentStatus::Error, reason);
      finish_enrollment();
    } else {
      e.due = deps_.clock.now() + config_.pose_delay;
    }
    return;
  }

  avisar_pose_aceptada(e.step, texto_medidas(head, front_similarity));
  if (e.step == 0 && head) {
    e.front_pitch_deg = head->pitch_deg;
    if (std::fabs(head->pitch_deg) > config_.pose_limits.max_reference_pitch_deg) {
      std::cerr << "[vision] AVISO: frente aceptada con un pitch de " << std::fixed
                << std::setprecision(1) << head->pitch_deg << " grados (referencia maximo "
                << config_.pose_limits.max_reference_pitch_deg
                << "): la pose arriba no se podra validar; pon la camara a la altura de la cara\n";
    }
  }
  publish_progress(e, EnrollmentStatus::Accepted, std::nullopt, miniatura);
  e.embeddings.push_back(std::move(embedding));
  e.attempts = 0;
  if (e.step + 1 < kEnrollmentPoses.size()) {
    ++e.step;
    publish_progress(e, EnrollmentStatus::WaitingPose);
    e.due = deps_.clock.now() + config_.pose_delay;
    return;
  }

  // Node-RED descarta el embedding sin hora valida y revierte el consentimiento. Si el
  // reloj se rompio a mitad del registro (1970, vacio o mal formado), falla aqui con el
  // motivo npu (sin motivos nuevos: el contrato no cambia), sin publicar "completado".
  const std::string timestamp = stamp_now();
  if (!is_plausible_timestamp(timestamp)) {
    log_invalid_clock(timestamp);
    publish_progress(e, EnrollmentStatus::Error, enrollment_reasons::kNpu);
    finish_enrollment();
    return;
  }

  // Descarte de outliers (plan 5.4, paso 5): se descartan (de una en una, la peor de
  // cada vuelta) las poses que se alejan de la media del resto, y el embedding sale de
  // las que quedan. Si no sobreviven al menos tres, el registro es inconsistente: no se
  // publica "completado" ni el embedding.
  const std::vector<std::size_t> supervivientes =
      keep_consistent_poses(e.embeddings, config_.min_pose_similarity);
  if (supervivientes.size() < 3) {
    std::cerr << "[vision] registro inconsistente: solo " << supervivientes.size() << " de "
              << e.embeddings.size() << " poses coinciden (hacen falta al menos 3)\n";
    publish_progress(e, EnrollmentStatus::Error, enrollment_reasons::kInconsistent);
    finish_enrollment();
    return;
  }

  // Las poses laterales se compararon con "frente" al aceptarlas, pero no con "frente_2" (la
  // segunda toma de la misma pose): un cambio de persona a mitad del registro tendria que
  // parecerse a las dos. Cada pose lateral superviviente se compara con "frente_2" con el
  // mismo umbral; un umbral <= 0 desactiva la comprobacion. Si alguna no cumple, el registro
  // termina como inconsistente. La referencia es la pose frente_2 ACEPTADA (la ultima de
  // e.embeddings), aunque el descarte final la haya sacado de los supervivientes: se acepto
  // contra frente y es la segunda toma de la misma pose, asi que sirve de ancla igual.
  if (config_.min_front_similarity > 0.0f) {
    const std::size_t last = e.embeddings.size() - 1;  // frente_2
    for (const std::size_t i : supervivientes) {
      if (i == 0 || i == last) continue;  // frente y frente_2 no son laterales
      const float similarity = cosine_similarity(e.embeddings[i], e.embeddings[last]);
      if (!(similarity >= config_.min_front_similarity)) {
        std::cerr << "[vision] registro inconsistente: la pose " << nombre_pose(i)
                  << " no se parece a frente_2 (coseno " << std::fixed << std::setprecision(2)
                  << similarity << ", minimo " << config_.min_front_similarity << ")\n";
        publish_progress(e, EnrollmentStatus::Error, enrollment_reasons::kInconsistent);
        finish_enrollment();
        return;
      }
    }
  }

  // Promedio de los embeddings normalizados que sobreviven, normalizado de nuevo.
  std::optional<EmbeddingMessage> message;
  try {
    EmbeddingMessage embedding_message;
    embedding_message.timestamp = timestamp;
    embedding_message.account = e.account;
    embedding_message.model = config_.model_name;
    embedding_message.embedding = l2_normalized(mean_embedding(e.embeddings, supervivientes));
    message = std::move(embedding_message);
  } catch (const std::exception& error) {
    log_error("no se pudo promediar el embedding", error);
  }
  if (!message) {
    publish_progress(e, EnrollmentStatus::Error, enrollment_reasons::kNpu);
    finish_enrollment();
    return;
  }

  // Duplicados: el rostro final no puede ser el de otra cuenta de la galeria (la propia puede
  // re-registrarse). Falla cerrado: si la galeria dejo de ser de fiar, o no se puede
  // consultar, el registro no se completa.
  const DuplicateCheck duplicate = find_duplicate(e.account, message->embedding);
  if (duplicate.unavailable) {
    publish_progress(e, EnrollmentStatus::Error, enrollment_reasons::kGalleryUnavailable);
    finish_enrollment();
    return;
  }
  if (duplicate.account) {
    publish_progress(e, EnrollmentStatus::Error, enrollment_reasons::kDuplicate, std::nullopt,
                     *duplicate.account);
    finish_enrollment();
    return;
  }

  // Las 5 poses aceptadas: se avisa con "completado" y despues se publica el embedding.
  publish_progress(e, EnrollmentStatus::Completed);
  if (!try_publish(topics::kVisionEmbedding, *message, "el embedding del registro")) {
    publish_progress(e, EnrollmentStatus::Error, enrollment_reasons::kNpu);
  }
  finish_enrollment();
}

// La galeria sirve para comprobar duplicados: la ultima recarga tuvo exito (o no hay forma de
// saberlo: pruebas y --dry-run).
bool VisionService::gallery_usable() const {
  if (!deps_.gallery_ready) return true;
  try {
    return deps_.gallery_ready();
  } catch (...) {
    return false;  // fallo cerrado
  }
}

// La cuenta de la galeria a la que ya pertenece este rostro (coseno >= min_duplicate_similarity,
// sin contar la cuenta que se registra). No registra la cuenta en el log.
//
// Falla cerrado: una galeria que dejo de ser de fiar, o una excepcion al compararla (una
// dimension distinta de la del embedder, bad_alloc), es `unavailable` y el registro termina con
// galeria_no_disponible. Se eligio ese motivo y no "npu" porque la causa es la galeria, no el
// pipeline de vision: el mensaje "fallo la NPU" mandaria a buscar donde no es. Una galeria
// vacia o sin otras cuentas tras una carga correcta es valida: no hay con quien chocar.
VisionService::DuplicateCheck VisionService::find_duplicate(const std::string& account,
                                                            const std::vector<float>& embedding) {
  DuplicateCheck result;
  if (!gallery_usable()) {
    std::cerr << "[vision] registro rechazado: la galeria dejo de estar disponible antes de "
                 "cerrar el registro\n";
    result.unavailable = true;
    return result;
  }
  try {
    const std::optional<GalleryMatch> match = deps_.gallery.match_excluding(embedding, account);
    if (!match) {
      std::cerr << "[vision] no hay otras cuentas en la galeria con las que comparar el rostro "
                   "del registro\n";
      return result;
    }
    if (match->score >= config_.min_duplicate_similarity) {
      std::cerr << "[vision] registro rechazado: el rostro ya esta registrado con otra cuenta "
                   "(coseno "
                << std::fixed << std::setprecision(2) << match->score << " >= "
                << config_.min_duplicate_similarity << ")\n";
      result.account = match->account;
    }
  } catch (const std::exception& error) {
    log_error("registro rechazado: no se pudo comprobar si el rostro ya esta registrado", error);
    result.unavailable = true;
  }
  return result;
}

// Sin cuenta, o con la del registro en curso, cancela; con cualquier otra (distinta o que ni
// siquiera es un no_cuenta valido) no hace nada. La capa JSON admite las dos formas:
// {"accion":"cancelar"} (cuenta vacia = el registro en curso) y
// {"accion":"cancelar","cuenta":"320068234"}.
void VisionService::cancel_enrollment(const std::string& account) {
  if (state_ != ServiceState::Enrolling) return;
  if (!account.empty() && account != enrollment_->account) return;
  publish_progress(*enrollment_, EnrollmentStatus::Cancelled);
  finish_enrollment();
}

// El estado se libera antes de publicar: un bus que falla no puede dejar el registro
// ni el servicio ocupados.
void VisionService::finish_enrollment() {
  enrollment_.reset();
  state_ = ServiceState::Idle;
  publish_light(false);
  publish_status();
}

// Salida ordenada (la llama el proceso al recibir SIGTERM o SIGINT).
//
// Sin esto, un SIGTERM en mitad de un registro dejaba la luz encendida y el registro
// a medias: el actuador queda con luz 1 y Node-RED y la pantalla esperando un registro
// que nunca termino. Aqui se cancela el enrolamiento en curso (publicando "cancelado"),
// se apaga la luz y el estado queda en inactivo.
//
// No se publica el estado: el proceso publica despues el estado final con el esquema
// completo (en_linea false), que es el que queda retenido.
void VisionService::shutdown() {
  if (state_ == ServiceState::Enrolling && enrollment_.has_value()) {
    publish_progress(*enrollment_, EnrollmentStatus::Cancelled);
  }
  enrollment_.reset();
  state_ = ServiceState::Idle;
  publish_light(false);
}

// `paso` es 1-based.
void VisionService::publish_progress(const Enrollment& e, EnrollmentStatus status,
                                     std::optional<std::string_view> reason,
                                     std::optional<std::string_view> miniatura,
                                     std::optional<std::string_view> similar_to) {
  EnrollmentProgress progress;
  progress.timestamp = stamp_now();
  progress.account = e.account;
  progress.name = e.name;
  progress.step = static_cast<int>(e.step) + 1;
  progress.pose = std::string(kEnrollmentPoses[e.step]);
  progress.status = status;
  if (reason) progress.reason = std::string(*reason);
  if (similar_to) progress.similar_to = std::string(*similar_to);
  // La miniatura viaja solo con la pose aceptada o rechazada (contrato MQTT): es la
  // foto de lo que la camara vio, y solo se ve mientras el registro esta abierto.
  if (miniatura && (status == EnrollmentStatus::Accepted || status == EnrollmentStatus::Rejected)) {
    progress.thumbnail = std::string(*miniatura);
  }
  try_publish(topics::kVisionEnrollment, progress, "el progreso del registro");
}

// Respuesta a una orden que no se puede atender; no toca el registro en curso. Lleva estado
// "error" y paso 0 para distinguirla del progreso del registro. La cuenta viene de la red:
// solo se repite si es un no_cuenta valido.
void VisionService::publish_refusal(const std::string& account, std::string_view reason) {
  EnrollmentProgress refusal;
  refusal.timestamp = stamp_now();
  refusal.account = is_valid_account(account) ? account : std::string();
  refusal.step = 0;
  refusal.pose = std::string();
  refusal.status = EnrollmentStatus::Error;
  refusal.reason = std::string(reason);
  try_publish(topics::kVisionEnrollment, refusal, "el rechazo de una orden");
}

}  // namespace vision
