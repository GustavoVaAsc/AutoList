#include "vision/vision_service.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <opencv2/imgcodecs.hpp>

#include "vision/alignment.hpp"
#include "vision/image_header.hpp"
#include "vision/mqtt_contract.hpp"
#include "vision/sharpness.hpp"

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

// Promedio de embeddings ya normalizados y de la misma dimension (el servicio lo
// comprueba al aceptar cada pose).
std::vector<float> mean_embedding(const std::vector<std::vector<float>>& embeddings) {
  std::vector<float> mean(embeddings.front().size(), 0.0f);
  for (const std::vector<float>& e : embeddings) {
    for (std::size_t i = 0; i < mean.size(); ++i) mean[i] += e[i];
  }
  for (float& x : mean) x /= static_cast<float>(embeddings.size());
  return mean;
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

VisionService::VisionService(ServiceDeps deps, ServiceConfig config)
    : deps_(deps), config_(std::move(config)) {
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

std::vector<cv::Mat> VisionService::capture_burst() {
  std::vector<cv::Mat> frames;
  for (int i = 0; i < config_.burst_size; ++i) {
    cv::Mat frame = capture_frame();
    if (!frame.empty()) frames.push_back(std::move(frame));
  }
  return frames;
}

// Matriz vacia si la peticion, la validacion o la decodificacion fallan.
cv::Mat VisionService::capture_frame() {
  const HttpResponse response = deps_.http.get(config_.capture_url, config_.http_timeout, config_.max_body_bytes);
  if (response.status != 200 || response.body.empty()) return {};
  return decode_capture(response);
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

// Detecta, alinea y calcula el embedding del rostro con mayor score.
VisionService::FaceOutcome VisionService::analyze(const cv::Mat& frame) {
  FaceOutcome outcome;
  const TimePoint det_start = deps_.clock.now();
  const std::vector<FaceDetection> faces = deps_.detector.detect(frame);
  outcome.det_ms = elapsed_ms(det_start);
  if (faces.empty()) {
    outcome.reason = reasons::kNoFace;
    return outcome;
  }

  const FaceDetection& best = *std::max_element(
      faces.begin(), faces.end(),
      [](const FaceDetection& a, const FaceDetection& b) { return a.score < b.score; });
  const TimePoint emb_start = deps_.clock.now();
  outcome.embedding = deps_.embedder.embed(align_face(frame, best.landmarks));
  outcome.emb_ms = elapsed_ms(emb_start);
  if (outcome.embedding.empty()) {
    throw EmbeddingError("el embedder devolvio un vector vacio");
  }
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
  // Se avisa de que empieza la captura de la pose en curso.
  publish_progress(e, EnrollmentStatus::Capturing);
  std::string_view reason;
  std::vector<float> embedding;
  bool internal_failure = false;
  try {
    const cv::Mat frame = capture_frame();
    if (frame.empty()) {
      reason = enrollment_reasons::kCaptureFailure;
    } else {
      const FaceOutcome face = analyze(frame);
      reason = face.reason;
      if (reason.empty()) embedding = l2_normalized(face.embedding);
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

  if (!reason.empty()) {
    publish_progress(e, EnrollmentStatus::Rejected, reason);
    if (++e.attempts >= config_.max_attempts_per_pose) {
      publish_progress(e, EnrollmentStatus::Error, enrollment_reasons::kNpu);
      finish_enrollment();
    } else {
      e.due = deps_.clock.now() + config_.pose_delay;
    }
    return;
  }

  // Un embedder que cambia de dimension a mitad del registro no se arregla reintentando.
  if (!e.embeddings.empty() && embedding.size() != e.embeddings.front().size()) {
    std::cerr << "[vision] la pose " << e.step + 1 << " dio un embedding de dimension "
              << embedding.size() << " y las anteriores de " << e.embeddings.front().size() << '\n';
    publish_progress(e, EnrollmentStatus::Error, enrollment_reasons::kInconsistent);
    finish_enrollment();
    return;
  }

  publish_progress(e, EnrollmentStatus::Accepted);
  e.embeddings.push_back(std::move(embedding));
  e.attempts = 0;
  if (e.step + 1 < kEnrollmentPoses.size()) {
    ++e.step;
    publish_progress(e, EnrollmentStatus::WaitingPose);
    e.due = deps_.clock.now() + config_.pose_delay;
    return;
  }

  // Las 5 poses aceptadas: se avisa con "completado" y despues se publica el embedding.
  publish_progress(e, EnrollmentStatus::Completed);
  // Promedio de embeddings normalizados, normalizado de nuevo (el descarte de outliers
  // queda pendiente).
  std::optional<EmbeddingMessage> message;
  try {
    EmbeddingMessage embedding_message;
    embedding_message.timestamp = stamp_now();
    embedding_message.account = e.account;
    embedding_message.model = config_.model_name;
    embedding_message.embedding = l2_normalized(mean_embedding(e.embeddings));
    message = std::move(embedding_message);
  } catch (const std::exception& error) {
    log_error("no se pudo promediar el embedding", error);
  }
  if (!message || !try_publish(topics::kVisionEmbedding, *message, "el embedding del registro")) {
    publish_progress(e, EnrollmentStatus::Error, enrollment_reasons::kNpu);
  }
  finish_enrollment();
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
                                     std::optional<std::string_view> reason) {
  EnrollmentProgress progress;
  progress.timestamp = stamp_now();
  progress.account = e.account;
  progress.name = e.name;
  progress.step = static_cast<int>(e.step) + 1;
  progress.pose = std::string(kEnrollmentPoses[e.step]);
  progress.status = status;
  if (reason) progress.reason = std::string(*reason);
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
