#include <atomic>
#include <cmath>
#include <iostream>
#include <optional>
#include <sstream>
#include <thread>
#include <type_traits>
#include <utility>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "minitest.hpp"
#include "vision/alignment.hpp"
#include "vision/fakes.hpp"
#include "vision/vision_service.hpp"

namespace vision {

// Amigo de VisionService: deja a las pruebas llegar a lo privado (la defensa en profundidad que
// la cola ya filtra, el estado interno, el tamano de la cola) sin ampliar la interfaz.
struct ServiceTestAccess {
  static void handle(VisionService& service, const VisionCommand& command, TimePoint received) {
    service.handle(command, received);
  }
  static bool has_enrollment(const VisionService& service) { return service.enrollment_.has_value(); }
  static std::size_t inbox_size(VisionService& service) {
    std::lock_guard lock(service.inbox_mutex_);
    return service.inbox_.size();
  }
};

}  // namespace vision

namespace {

using namespace vision;
using namespace std::chrono_literals;

const std::string kCaptureUrl = "http://fake/capture";

std::vector<std::uint8_t> encode_png(const cv::Mat& image) {
  std::vector<std::uint8_t> bytes;
  cv::imencode(".png", image, bytes);
  return bytes;
}

cv::Mat noise_frame(int seed) {
  cv::Mat image(240, 320, CV_8UC3);
  cv::RNG rng(seed);
  rng.fill(image, cv::RNG::UNIFORM, 0, 256);
  return image;
}

HttpResponse ok(const cv::Mat& image) { return {200, encode_png(image)}; }

FaceDetection face_in_frame() {
  FaceDetection face;
  face.score = 0.9f;
  for (std::size_t i = 0; i < face.landmarks.size(); ++i) {
    face.landmarks[i] = kArcFaceReference[i] * 1.5f + cv::Point2f(80.0f, 40.0f);
  }
  return face;
}

// Servicio completo con dobles. La dimension del embedding (4) es arbitraria a proposito.
struct Harness {
  fakes::FakeMessageBus bus;
  fakes::FakeHttpClient http;
  fakes::FakeFaceDetector detector;
  fakes::FakeFaceEmbedder embedder;
  Gallery gallery;
  fakes::ManualClock clock;
  ServiceConfig config;
  std::unique_ptr<VisionService> service;

  explicit Harness(double min_sharpness = 0.0) {
    config.capture_url = kCaptureUrl;
    config.burst_size = 3;
    config.accept_png = true;  // las fotos de prueba son PNG
    config.min_sharpness = min_sharpness;
    detector.detections = {face_in_frame()};
    embedder.outputs = {{1.0f, 0.0f, 0.0f, 0.0f}};
    gallery.upsert("320068234", std::vector<float>{1.0f, 0.0f, 0.0f, 0.0f});
    gallery.upsert("320068235", std::vector<float>{0.0f, 1.0f, 0.0f, 0.0f});
    service = std::make_unique<VisionService>(
        ServiceDeps{bus, http, detector, embedder, gallery, clock}, config);
  }

  // Reconstruye el servicio con otra configuracion (la anterior se da de baja primero).
  void restart(const ServiceConfig& new_config) {
    service.reset();
    config = new_config;
    service = std::make_unique<VisionService>(
        ServiceDeps{bus, http, detector, embedder, gallery, clock}, config);
  }

  void queue_frames(int count) {
    for (int i = 0; i < count; ++i) http.responses.push_back(ok(noise_frame(i + 1)));
  }

  void pir() { bus.inject(topics::kCameraPir, PirEvent{}); }
  void command(VisionAction action, std::string account = "") {
    bus.inject(topics::kVisionCmd, VisionCommand{action, std::move(account)});
  }

  std::vector<AttendanceResult> results() const {
    return bus.published_as<AttendanceResult>(topics::kVisionResult);
  }
  std::vector<EnrollmentProgress> progress() const {
    return bus.published_as<EnrollmentProgress>(topics::kVisionEnrollment);
  }
  std::vector<EmbeddingMessage> embeddings() const {
    return bus.published_as<EmbeddingMessage>(topics::kVisionEmbedding);
  }
  // Valores de "luz" publicados, en orden.
  std::vector<int> light_commands() const {
    std::vector<int> lights;
    for (const ActuatorCommand& c : bus.published_as<ActuatorCommand>(topics::kActuatorCmd)) {
      lights.push_back(c.light.value());
    }
    return lights;
  }

  // Un paso del registro: espera la pose y atiende.
  void enrollment_step() {
    clock.advance(config.pose_delay);
    service->poll();
  }
};

// Valor de "luz" de un payload publicado en aula/actuador/cmd; -1 si no es de ese tipo.
int light_of(const Payload& payload) {
  const auto* command = std::get_if<ActuatorCommand>(&payload);
  return command != nullptr && command->light ? *command->light : -1;
}

// Captura std::cerr mientras vive (los CHECK lanzan, asi que se restaura en el destructor).
struct CapturedStderr {
  std::ostringstream text;
  std::streambuf* previous = std::cerr.rdbuf(text.rdbuf());
  ~CapturedStderr() { std::cerr.rdbuf(previous); }
  std::size_t count(const std::string& needle) const {
    std::size_t n = 0;
    for (std::size_t at = text.str().find(needle); at != std::string::npos;
         at = text.str().find(needle, at + needle.size())) {
      ++n;
    }
    return n;
  }
};

// JPEG de ruido recortado justo antes de los datos (marcador SOS): la cabecera es valida
// y el decodificador no tiene nada que decodificar.
std::vector<std::uint8_t> headers_only_jpeg() {
  std::vector<std::uint8_t> jpeg;
  cv::imencode(".jpg", noise_frame(3), jpeg);
  for (std::size_t i = 0; i + 1 < jpeg.size(); ++i) {
    if (jpeg[i] == 0xFF && jpeg[i + 1] == 0xDA) {
      jpeg.resize(i);
      return jpeg;
    }
  }
  throw std::runtime_error("el JPEG de prueba no tiene SOS");
}

std::vector<std::uint8_t> encode_jpeg(const cv::Mat& image) {
  std::vector<std::uint8_t> bytes;
  cv::imencode(".jpg", image, bytes);
  return bytes;
}

}  // namespace

// --- Pase de lista ------------------------------------------------------------

TEST(attendance_happy_path_publishes_exactly_one_result) {
  Harness h;
  h.queue_frames(3);
  h.http.on_call = [&] { h.clock.advance(20ms); };
  h.detector.on_call = [&] { h.clock.advance(12ms); };
  h.embedder.on_call = [&] { h.clock.advance(6ms); };

  h.pir();
  h.service->poll();

  const auto results = h.results();
  CHECK_EQ(results.size(), std::size_t{1});
  CHECK_EQ(results[0].account.value(), std::string("320068234"));
  CHECK(!results[0].reason.has_value());
  CHECK_NEAR(results[0].confidence, 1.0, 1e-6);
  CHECK_EQ(results[0].lat_ms.cap, 60);
  CHECK_EQ(results[0].lat_ms.det, 12);
  CHECK_EQ(results[0].lat_ms.emb, 6);
  CHECK_EQ(results[0].lat_ms.total, 300 + 60 + 12 + 6);  // incluye la espera de la luz
  CHECK(h.service->state() == ServiceState::Idle);
}

TEST(attendance_turns_light_on_then_waits_then_turns_it_off) {
  Harness h;
  h.queue_frames(3);
  h.pir();
  h.service->poll();

  CHECK_EQ(h.clock.sleeps.size(), std::size_t{1});
  CHECK_EQ(h.clock.sleeps[0].count(), 300);
  CHECK_EQ(h.http.requested_urls.size(), std::size_t{3});
  CHECK_EQ(h.http.requested_urls[0], kCaptureUrl);
  CHECK(h.light_commands() == (std::vector<int>{1, 0}));
  // Orden global: luz encendida, resultado, luz apagada.
  CHECK_EQ(h.bus.published.size(), std::size_t{3});
  CHECK(h.bus.published[0].topic == topics::kActuatorCmd);
  CHECK(h.bus.published[1].topic == topics::kVisionResult);
  CHECK(h.bus.published[2].topic == topics::kActuatorCmd);
}

TEST(attendance_feeds_the_sharpest_frame_to_detector_aligned_to_112) {
  Harness h;
  cv::Mat blurry;
  cv::GaussianBlur(noise_frame(7), blurry, cv::Size(0, 0), 3.0);
  const cv::Mat sharp = noise_frame(8);
  h.http.responses.push_back(ok(blurry));
  h.http.responses.push_back(ok(sharp));
  h.http.responses.push_back(ok(blurry));

  h.pir();
  h.service->poll();

  CHECK_EQ(h.detector.calls, 1);
  CHECK_EQ(cv::norm(h.detector.last_input, sharp, cv::NORM_INF), 0.0);
  CHECK_EQ(h.embedder.last_input_size.width, 112);
  CHECK_EQ(h.embedder.last_input_size.height, 112);
}

TEST(attendance_uses_embedder_dimension_whatever_it_is) {
  Harness h;
  std::vector<float> v(512, 0.0f);
  v[5] = 1.0f;
  h.gallery.remove("320068234");
  h.gallery.remove("320068235");
  h.gallery.upsert("320068234", v);
  h.embedder.outputs = {v};
  h.queue_frames(3);

  h.pir();
  h.service->poll();

  CHECK_EQ(h.results().at(0).account.value(), std::string("320068234"));
}

TEST(attendance_capture_failure_publishes_reason_instead_of_crashing) {
  Harness h;  // sin respuestas HTTP: todas las peticiones fallan con status 0
  h.pir();
  h.service->poll();

  const auto results = h.results();
  CHECK_EQ(results.size(), std::size_t{1});
  CHECK(!results[0].account.has_value());
  CHECK_EQ(results[0].reason.value(), std::string(reasons::kCaptureFailed));
  CHECK_EQ(h.detector.calls, 0);
  CHECK(h.light_commands() == (std::vector<int>{1, 0}));
  CHECK(h.service->state() == ServiceState::Idle);
}

TEST(attendance_capture_failure_on_http_error_and_undecodable_body) {
  Harness h;
  h.http.responses.push_back({500, encode_png(noise_frame(1))});
  h.http.responses.push_back({200, {1, 2, 3, 4}});
  h.http.responses.push_back({200, {}});

  h.pir();
  h.service->poll();

  CHECK_EQ(h.results().at(0).reason.value(), std::string(reasons::kCaptureFailed));
}

TEST(attendance_continues_when_only_some_frames_fail) {
  Harness h;
  h.http.responses.push_back({503, {}});
  h.http.responses.push_back(ok(noise_frame(1)));
  h.http.responses.push_back({});

  h.pir();
  h.service->poll();

  CHECK_EQ(h.results().at(0).account.value(), std::string("320068234"));
}

TEST(attendance_reports_insufficient_image_below_sharpness_threshold) {
  Harness h(1e9);
  h.queue_frames(3);
  h.pir();
  h.service->poll();

  CHECK_EQ(h.results().at(0).reason.value(), std::string(reasons::kInsufficientImage));
  CHECK_EQ(h.detector.calls, 0);
}

TEST(attendance_reports_no_face) {
  Harness h;
  h.detector.detections.clear();
  h.queue_frames(3);
  h.pir();
  h.service->poll();

  CHECK_EQ(h.results().at(0).reason.value(), std::string(reasons::kNoFace));
  CHECK_EQ(h.embedder.calls, std::size_t{0});
}

TEST(attendance_picks_the_highest_scoring_face) {
  Harness h;
  FaceDetection weak = face_in_frame();
  weak.score = 0.3f;
  h.detector.detections = {weak, face_in_frame()};
  h.queue_frames(3);
  h.pir();
  h.service->poll();

  CHECK_EQ(h.embedder.calls, std::size_t{1});
  CHECK(h.results().at(0).account.has_value());
}

TEST(attendance_reports_unknown_below_match_threshold_but_keeps_confidence) {
  Harness h;
  h.embedder.outputs = {{0.0f, 0.0f, 1.0f, 0.0f}};  // ortogonal a toda la galeria
  h.queue_frames(3);
  h.pir();
  h.service->poll();

  const auto result = h.results().at(0);
  CHECK(!result.account.has_value());
  CHECK_EQ(result.reason.value(), std::string(reasons::kUnknown));
  CHECK_NEAR(result.confidence, 0.0, 1e-6);
}

TEST(attendance_with_empty_gallery_reports_reason) {
  Harness h;
  h.gallery.remove("320068234");
  h.gallery.remove("320068235");
  h.queue_frames(3);
  h.pir();
  h.service->poll();

  CHECK_EQ(h.results().at(0).reason.value(), std::string(reasons::kEmptyGallery));
}

TEST(attendance_detector_exception_becomes_internal_error_result) {
  Harness h;
  h.detector.on_call = [] { throw std::runtime_error("npu caida"); };
  h.queue_frames(3);
  h.pir();
  h.service->poll();

  CHECK_EQ(h.results().size(), std::size_t{1});
  CHECK_EQ(h.results()[0].reason.value(), std::string(reasons::kInternalError));
  CHECK(h.light_commands() == (std::vector<int>{1, 0}));
  CHECK(h.service->state() == ServiceState::Idle);
}

TEST(attendance_embedding_of_wrong_dimension_becomes_internal_error) {
  Harness h;
  h.embedder.outputs = {{1.0f, 0.0f}};  // la galeria es de dimension 4
  h.queue_frames(3);
  h.pir();
  h.service->poll();

  CHECK_EQ(h.results().at(0).reason.value(), std::string(reasons::kInternalError));
}

TEST(pir_arriving_during_attendance_is_ignored) {
  Harness h;
  h.queue_frames(3);
  h.detector.on_call = [&] {
    CHECK(h.service->state() == ServiceState::AttendanceInProgress);
    h.pir();
  };
  h.pir();
  h.service->poll();
  h.http.on_call = nullptr;
  h.queue_frames(3);
  h.service->poll();  // el PIR de en medio no debe disparar otro pase

  CHECK_EQ(h.results().size(), std::size_t{1});
  CHECK_EQ(h.detector.calls, 1);
}

TEST(duplicate_pir_events_queued_together_run_one_attendance) {
  Harness h;
  h.queue_frames(6);
  h.pir();
  h.pir();
  h.service->poll();

  CHECK_EQ(h.results().size(), std::size_t{1});
}

TEST(pir_without_motion_is_ignored) {
  Harness h;
  h.queue_frames(3);
  h.bus.inject(topics::kCameraPir, PirEvent{false});
  h.service->poll();

  CHECK(h.results().empty());
  CHECK(h.bus.published.empty());
}

TEST(second_pir_after_first_attendance_runs_again) {
  Harness h;
  h.queue_frames(6);
  h.pir();
  h.service->poll();
  h.pir();
  h.service->poll();

  CHECK_EQ(h.results().size(), std::size_t{2});
}

TEST(test_command_runs_the_attendance_pipeline) {
  Harness h;
  h.queue_frames(3);
  h.command(VisionAction::Test);
  h.service->poll();

  CHECK_EQ(h.results().size(), std::size_t{1});
  CHECK_EQ(h.results()[0].account.value(), std::string("320068234"));
}

// --- Registro -----------------------------------------------------------------

TEST(enrollment_runs_five_poses_and_publishes_normalized_mean_embedding) {
  Harness h;
  // Normas distintas a proposito: cada pose se normaliza antes de promediar.
  h.embedder.outputs = {{4.0f, 0.0f, 0.0f, 0.0f},
                        {0.0f, 0.5f, 0.0f, 0.0f},
                        {4.0f, 0.0f, 0.0f, 0.0f},
                        {0.0f, 0.5f, 0.0f, 0.0f},
                        {4.0f, 0.0f, 0.0f, 0.0f}};
  h.queue_frames(5);

  h.command(VisionAction::Enroll, "999000111");
  h.service->poll();
  CHECK(h.service->state() == ServiceState::Enrolling);
  CHECK_EQ(h.progress().size(), std::size_t{1});
  CHECK_EQ(h.progress()[0].pose, std::string("frente"));
  CHECK(h.progress()[0].status == EnrollmentStatus::Pending);
  CHECK(h.http.requested_urls.empty());  // aun no se captura: el alumno adopta la pose

  for (int i = 0; i < 5; ++i) h.enrollment_step();

  CHECK(h.service->state() == ServiceState::Idle);
  CHECK_EQ(h.http.requested_urls.size(), std::size_t{5});

  // Por pose: aceptada y, salvo en la ultima, el aviso de la siguiente.
  const auto progress = h.progress();
  CHECK_EQ(progress.size(), std::size_t{1 + 5 + 4});
  int accepted = 0;
  for (const EnrollmentProgress& p : progress) {
    CHECK_EQ(p.account, std::string("999000111"));
    CHECK_EQ(p.total, 5);
    if (p.status == EnrollmentStatus::Accepted) {
      ++accepted;
      CHECK_EQ(p.step, accepted);
    }
  }
  CHECK_EQ(accepted, 5);
  CHECK_EQ(progress.back().pose, std::string("frente"));
  CHECK_EQ(progress.back().step, 5);

  const auto embeddings = h.embeddings();
  CHECK_EQ(embeddings.size(), std::size_t{1});
  CHECK_EQ(embeddings[0].account, std::string("999000111"));
  CHECK_EQ(embeddings[0].embedding.size(), std::size_t{4});
  // Promedio de unitarios = (3, 2, 0, 0) / 5, normalizado.
  const double norm = std::sqrt(13.0);
  CHECK_NEAR(embeddings[0].embedding[0], 3.0 / norm, 1e-6);
  CHECK_NEAR(embeddings[0].embedding[1], 2.0 / norm, 1e-6);
  CHECK_NEAR(embeddings[0].embedding[2], 0.0, 1e-6);
  CHECK(h.light_commands() == (std::vector<int>{1, 0}));
}

TEST(enrollment_pose_names_follow_the_plan) {
  Harness h;
  h.queue_frames(5);
  h.command(VisionAction::Enroll, "100000001");
  h.service->poll();
  for (int i = 0; i < 5; ++i) h.enrollment_step();

  std::vector<std::string> prompted;
  for (const EnrollmentProgress& p : h.progress()) {
    if (p.status == EnrollmentStatus::Pending) prompted.push_back(p.pose);
  }
  CHECK(prompted == (std::vector<std::string>{"frente", "izquierda", "derecha", "arriba", "frente"}));
}

TEST(pir_is_ignored_while_enrolling) {
  Harness h;
  h.queue_frames(5);
  h.command(VisionAction::Enroll, "100000001");
  h.service->poll();
  const std::size_t published_before = h.bus.published.size();

  h.pir();
  h.service->poll();

  CHECK(h.service->state() == ServiceState::Enrolling);
  CHECK(h.results().empty());
  CHECK_EQ(h.bus.published.size(), published_before);  // ni luz ni resultado
  CHECK_EQ(h.detector.calls, 0);
  CHECK(h.http.requested_urls.empty());
}

TEST(pir_queued_with_enroll_command_is_ignored_too) {
  Harness h;
  h.queue_frames(5);
  h.command(VisionAction::Enroll, "100000001");
  h.pir();  // llega en el mismo lote que el comando, pero despues
  h.service->poll();

  CHECK(h.results().empty());
  CHECK(h.service->state() == ServiceState::Enrolling);
}

TEST(cancel_mid_enrollment_stops_capturing_and_publishes_no_embedding) {
  Harness h;
  h.queue_frames(5);
  h.command(VisionAction::Enroll, "100000001");
  h.service->poll();
  h.enrollment_step();  // pose 1 aceptada
  h.enrollment_step();  // pose 2 aceptada
  CHECK_EQ(h.http.requested_urls.size(), std::size_t{2});

  h.command(VisionAction::Cancel, "100000001");
  h.clock.advance(h.config.pose_delay);
  h.service->poll();

  CHECK(h.service->state() == ServiceState::Idle);
  CHECK_EQ(h.http.requested_urls.size(), std::size_t{2});  // la pose 3 no se captura
  CHECK(h.embeddings().empty());
  CHECK(h.progress().back().status == EnrollmentStatus::Cancelled);
  CHECK(h.light_commands() == (std::vector<int>{1, 0}));

  // Tras cancelar, el PIR vuelve a funcionar.
  h.queue_frames(3);
  h.pir();
  h.service->poll();
  CHECK_EQ(h.results().size(), std::size_t{1});
}

TEST(cancel_when_not_enrolling_does_nothing) {
  Harness h;
  h.command(VisionAction::Cancel);
  h.service->poll();

  CHECK(h.bus.published.empty());
  CHECK(h.service->state() == ServiceState::Idle);
}

TEST(second_enroll_while_enrolling_is_rejected_as_busy) {
  Harness h;
  h.command(VisionAction::Enroll, "100000001");
  h.service->poll();
  h.command(VisionAction::Enroll, "200000002");
  h.service->poll();

  const auto progress = h.progress();
  CHECK_EQ(progress.size(), std::size_t{2});
  CHECK_EQ(progress[1].account, std::string("200000002"));
  CHECK(progress[1].status == EnrollmentStatus::Error);
  CHECK_EQ(progress[1].reason.value(), std::string(reasons::kBusy));
  CHECK(h.service->state() == ServiceState::Enrolling);
}

TEST(test_command_while_enrolling_replies_busy_and_enrollment_continues) {
  Harness h;
  h.queue_frames(5);
  h.command(VisionAction::Enroll, "100000001");
  h.service->poll();
  h.enrollment_step();  // pose 1 aceptada
  const std::size_t before = h.progress().size();

  h.command(VisionAction::Test);
  h.service->poll();

  CHECK(h.results().empty());
  CHECK_EQ(h.detector.calls, 1);  // solo la pose 1
  CHECK(h.service->state() == ServiceState::Enrolling);
  const auto progress = h.progress();
  CHECK_EQ(progress.size(), before + 1);
  CHECK(progress.back().status == EnrollmentStatus::Error);
  CHECK_EQ(progress.back().reason.value(), std::string(reasons::kBusy));
  CHECK_EQ(progress.back().step, 0);  // no es parte del registro en curso

  // El registro sigue y termina con normalidad.
  for (int i = 0; i < 4; ++i) h.enrollment_step();
  CHECK(h.service->state() == ServiceState::Idle);
  CHECK_EQ(h.embeddings().size(), std::size_t{1});
}

TEST(enroll_with_empty_account_is_dropped_silently_and_starts_no_session) {
  Harness h;
  CapturedStderr log;
  h.command(VisionAction::Enroll, "");
  h.service->poll();

  CHECK(h.service->state() == ServiceState::Idle);
  CHECK(h.bus.published.empty());  // ni respuesta ni luz
  CHECK_EQ(log.count("enrolar con cuenta invalida"), std::size_t{1});
  h.clock.advance(h.config.pose_delay);
  h.service->poll();
  CHECK(h.http.requested_urls.empty());
}

TEST(cancel_for_another_account_is_ignored_but_empty_or_matching_cancels) {
  Harness h;
  h.command(VisionAction::Enroll, "111111111");
  h.service->poll();

  h.command(VisionAction::Cancel, "222222222");
  h.service->poll();
  CHECK(h.service->state() == ServiceState::Enrolling);
  CHECK(h.progress().back().status == EnrollmentStatus::Pending);

  h.command(VisionAction::Cancel, "");  // vacia = la actual
  h.service->poll();
  CHECK(h.service->state() == ServiceState::Idle);
  CHECK(h.progress().back().status == EnrollmentStatus::Cancelled);

  h.command(VisionAction::Enroll, "333333333");
  h.service->poll();
  h.command(VisionAction::Cancel, "333333333");
  h.service->poll();
  CHECK(h.service->state() == ServiceState::Idle);
  CHECK(h.progress().back().status == EnrollmentStatus::Cancelled);
  CHECK_EQ(h.progress().back().account, std::string("333333333"));
}

TEST(embedding_dimension_change_between_poses_fails_the_enrollment) {
  Harness h;
  h.embedder.outputs = {{1.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f}};
  h.queue_frames(5);
  h.command(VisionAction::Enroll, "100000001");
  h.service->poll();
  h.enrollment_step();
  h.enrollment_step();  // la pose 2 llega con 2 dimensiones

  CHECK(h.service->state() == ServiceState::Idle);
  CHECK(h.embeddings().empty());
  CHECK(h.progress().back().status == EnrollmentStatus::Failed);
  CHECK_EQ(h.progress().back().reason.value(), std::string(reasons::kInternalError));
  CHECK_EQ(h.http.requested_urls.size(), std::size_t{2});  // no se reintenta ni se sigue
  CHECK(h.light_commands() == (std::vector<int>{1, 0}));
}

TEST(zero_embedding_for_a_pose_is_rejected_and_retried) {
  Harness h;
  h.embedder.outputs = {{0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}};
  h.queue_frames(5);
  h.command(VisionAction::Enroll, "100000001");
  h.service->poll();
  h.enrollment_step();

  CHECK(h.progress().back().status == EnrollmentStatus::Rejected);
  CHECK_EQ(h.progress().back().reason.value(), std::string(reasons::kInternalError));
  CHECK(h.service->state() == ServiceState::Enrolling);
}

TEST(rejected_pose_is_retried_then_accepted) {
  Harness h;
  h.http.responses.push_back({500, {}});  // primer intento de la pose 1 falla
  h.queue_frames(5);
  h.command(VisionAction::Enroll, "100000001");
  h.service->poll();
  h.enrollment_step();

  auto progress = h.progress();
  CHECK(progress.back().status == EnrollmentStatus::Rejected);
  CHECK_EQ(progress.back().step, 1);
  CHECK_EQ(progress.back().reason.value(), std::string(reasons::kCaptureFailed));

  h.enrollment_step();  // reintento de la misma pose
  progress = h.progress();
  CHECK(progress[progress.size() - 2].status == EnrollmentStatus::Accepted);
  CHECK_EQ(progress[progress.size() - 2].step, 1);
  CHECK_EQ(progress.back().step, 2);
}

TEST(enrollment_aborts_after_too_many_rejections_of_one_pose) {
  Harness h;  // sin respuestas HTTP
  CHECK_EQ(h.config.max_attempts_per_pose, 5);
  h.command(VisionAction::Enroll, "100000001");
  h.service->poll();
  for (int i = 0; i < 4; ++i) h.enrollment_step();
  CHECK(h.service->state() == ServiceState::Enrolling);  // 4 rechazos: aun se reintenta
  h.enrollment_step();

  int rejected = 0;
  for (const EnrollmentProgress& p : h.progress()) rejected += p.status == EnrollmentStatus::Rejected;
  CHECK_EQ(rejected, 5);
  CHECK(h.service->state() == ServiceState::Idle);
  CHECK(h.embeddings().empty());
  CHECK(h.progress().back().status == EnrollmentStatus::Failed);
  CHECK(h.light_commands() == (std::vector<int>{1, 0}));
}

TEST(enrollment_waits_for_the_pose_delay_before_capturing) {
  Harness h;
  h.queue_frames(5);
  h.command(VisionAction::Enroll, "100000001");
  h.service->poll();
  h.clock.advance(h.config.pose_delay - 1ms);
  h.service->poll();
  CHECK(h.http.requested_urls.empty());
  h.clock.advance(1ms);
  h.service->poll();
  CHECK_EQ(h.http.requested_urls.size(), std::size_t{1});
}

// --- Configuracion y suscripciones ------------------------------------------------

TEST(service_rejects_invalid_config) {
  Harness h;
  const auto rejects = [&](const ServiceConfig& config) {
    CHECK_THROWS(VisionService(ServiceDeps{h.bus, h.http, h.detector, h.embedder, h.gallery, h.clock}, config),
                 std::invalid_argument);
  };
  ServiceConfig bad = h.config;
  bad.capture_url.clear();
  rejects(bad);
  bad = h.config;
  bad.burst_size = 0;
  rejects(bad);
  bad = h.config;
  bad.http_timeout = 0ms;
  rejects(bad);
  bad = h.config;
  bad.max_body_bytes = 0;
  rejects(bad);
  bad = h.config;
  bad.max_image_height = 0;
  rejects(bad);
  bad = h.config;
  bad.max_inbox_events = 3;  // PIR, probar y enrolar tienen que caber ademas de un cancelar
  rejects(bad);
  bad.max_inbox_events = 1;
  rejects(bad);
  bad.max_inbox_events = 0;
  rejects(bad);
}

TEST(topics_match_plan_section_7_1) {
  CHECK(topics::kCameraPir == "aula/camara/pir");
  CHECK(topics::kCameraEnergy == "aula/camara/energia");
  CHECK(topics::kCameraStatus == "aula/camara/estado");
  CHECK(topics::kActuatorCmd == "aula/actuador/cmd");
  CHECK(topics::kActuatorStatus == "aula/actuador/estado");
  CHECK(topics::kVisionResult == "aula/vision/resultado");
  CHECK(topics::kVisionCmd == "aula/vision/cmd");
  CHECK(topics::kVisionEnrollment == "aula/vision/enrolamiento");
  CHECK(topics::kVisionEmbedding == "aula/vision/embedding");
  CHECK(topics::kSessionCmd == "aula/sesion/cmd");
  CHECK(topics::kScreenRestart == "aula/pantalla/reinicio");
  CHECK(topics::kSessionStatus == "aula/sesion/estado");
  CHECK(topics::kSystemMetrics == "aula/sistema/metricas");
}

// --- Vida de las suscripciones -------------------------------------------------------

TEST(destroyed_service_is_unsubscribed_and_receives_nothing) {
  Harness h;
  CHECK_EQ(h.bus.subscriber_count(), std::size_t{2});
  h.service.reset();

  CHECK_EQ(h.bus.subscriber_count(), std::size_t{0});
  CHECK_EQ(h.bus.inject(topics::kCameraPir, PirEvent{}), std::size_t{0});
  CHECK_EQ(h.bus.inject(topics::kVisionCmd, VisionCommand{VisionAction::Enroll, "100000001"}), std::size_t{0});
  CHECK(h.bus.published.empty());
}

TEST(constructor_failure_after_first_subscribe_leaves_no_subscription) {
  Harness h;
  h.service.reset();
  h.bus.on_subscribe = [](std::string_view topic) {
    if (topic == topics::kVisionCmd) throw std::runtime_error("broker caido");
  };

  CHECK_THROWS(VisionService(ServiceDeps{h.bus, h.http, h.detector, h.embedder, h.gallery, h.clock}, h.config),
               std::runtime_error);
  CHECK_EQ(h.bus.subscriber_count(), std::size_t{0});
  CHECK_EQ(h.bus.inject(topics::kCameraPir, PirEvent{}), std::size_t{0});
}

TEST(subscription_is_move_only_and_releases_once) {
  fakes::FakeMessageBus bus;
  int calls = 0;
  Subscription first = bus.subscribe(topics::kCameraPir, [&](const Payload&) { ++calls; });
  Subscription second = std::move(first);
  CHECK(!first);
  CHECK(second);
  CHECK_EQ(bus.inject(topics::kCameraPir, PirEvent{}), std::size_t{1});

  Subscription other = bus.subscribe(topics::kCameraPir, [&](const Payload&) { ++calls; });
  CHECK_EQ(bus.subscriber_count(), std::size_t{2});
  other = std::move(second);  // suelta la suya y toma la otra
  CHECK_EQ(bus.subscriber_count(), std::size_t{1});
  other.reset();
  other.reset();
  CHECK_EQ(bus.subscriber_count(), std::size_t{0});
  CHECK_EQ(calls, 1);
}

// Contrato de MessageBus: al retornar la baja, ningun handler esta en curso.
TEST(unsubscribe_waits_for_a_handler_in_flight) {
  fakes::FakeMessageBus bus;
  std::atomic<bool> entered{false}, release{false}, finished{false}, unsubscribed{false};
  Subscription subscription = bus.subscribe(topics::kCameraPir, [&](const Payload&) {
    entered = true;
    while (!release) std::this_thread::sleep_for(1ms);
    finished = true;
  });

  std::thread injector([&] { bus.inject(topics::kCameraPir, PirEvent{}); });
  while (!entered) std::this_thread::sleep_for(1ms);
  bool finished_when_unsubscribed = false;
  std::thread unsubscriber([&] {
    subscription.reset();
    finished_when_unsubscribed = finished;
    unsubscribed = true;
  });
  std::this_thread::sleep_for(50ms);
  const bool still_waiting = !unsubscribed;  // el handler sigue bloqueado: la baja no retorna
  release = true;
  injector.join();
  unsubscriber.join();

  CHECK(still_waiting);
  CHECK(finished_when_unsubscribed);
  CHECK_EQ(bus.inject(topics::kCameraPir, PirEvent{}), std::size_t{0});
}

// --- Fallos del bus y excepciones --------------------------------------------------

TEST(attendance_light_on_publish_failure_does_not_block_the_attendance) {
  Harness h;
  h.queue_frames(3);
  h.bus.on_publish = [](std::string_view, const Payload& p) {
    if (light_of(p) == 1) throw std::runtime_error("sin broker");
  };
  h.pir();
  h.service->poll();

  CHECK_EQ(h.results().size(), std::size_t{1});
  CHECK(h.results()[0].account.has_value());
  CHECK(h.light_commands() == (std::vector<int>{0}));
  CHECK(h.service->state() == ServiceState::Idle);
}

TEST(attendance_result_publish_failure_still_turns_light_off_and_frees_the_service) {
  Harness h;
  h.queue_frames(6);
  bool fail = true;
  h.bus.on_publish = [&](std::string_view topic, const Payload&) {
    if (fail && topic == topics::kVisionResult) throw std::runtime_error("sin broker");
  };
  h.pir();
  h.service->poll();

  CHECK(h.results().empty());
  CHECK(h.light_commands() == (std::vector<int>{1, 0}));
  CHECK(h.service->state() == ServiceState::Idle);

  fail = false;  // el PIR siguiente no debe quedar bloqueado por un pendiente fantasma
  h.pir();
  h.service->poll();
  CHECK_EQ(h.results().size(), std::size_t{1});
}

TEST(attendance_light_off_publish_failure_leaves_service_idle) {
  Harness h;
  h.queue_frames(6);
  bool fail = true;
  h.bus.on_publish = [&](std::string_view, const Payload& p) {
    if (fail && light_of(p) == 0) throw std::runtime_error("sin broker");
  };
  h.pir();
  h.service->poll();

  CHECK_EQ(h.results().size(), std::size_t{1});
  CHECK(h.service->state() == ServiceState::Idle);
  fail = false;
  h.pir();
  h.service->poll();
  CHECK_EQ(h.results().size(), std::size_t{2});
}

TEST(attendance_non_std_exception_in_pipeline_becomes_internal_error) {
  Harness h;
  h.detector.on_call = [] { throw 42; };
  h.queue_frames(3);
  h.pir();
  h.service->poll();

  CHECK_EQ(h.results().size(), std::size_t{1});
  CHECK_EQ(h.results()[0].reason.value(), std::string(reasons::kInternalError));
  CHECK(h.light_commands() == (std::vector<int>{1, 0}));
  CHECK(h.service->state() == ServiceState::Idle);
}

TEST(attendance_cleans_up_and_reports_internal_error_when_the_clock_throws) {
  Harness h;
  h.queue_frames(6);
  bool fail = true;
  h.clock.on_sleep = [&] {
    if (fail) throw std::runtime_error("reloj roto");
  };
  h.pir();
  h.service->poll();  // no debe propagar la excepcion

  CHECK(h.service->state() == ServiceState::Idle);
  CHECK(h.light_commands() == (std::vector<int>{1, 0}));
  // El pedido no se queda sin respuesta: error_interno, con su modo.
  CHECK_EQ(h.results().size(), std::size_t{1});
  CHECK_EQ(h.results()[0].reason.value(), std::string(reasons::kInternalError));
  CHECK(h.results()[0].mode == AttendanceMode::Attendance);
  CHECK(!h.results()[0].account.has_value());
  CHECK_EQ(h.detector.calls, 0);

  fail = false;
  h.pir();  // y el pendiente del PIR se limpio
  h.service->poll();
  CHECK_EQ(h.results().size(), std::size_t{2});
  CHECK(h.results()[1].account.has_value());
}

TEST(test_command_reports_internal_error_with_its_mode_when_the_clock_throws) {
  Harness h;
  h.clock.on_sleep = [] { throw std::runtime_error("reloj roto"); };
  h.command(VisionAction::Test);
  h.service->poll();

  CHECK_EQ(h.results().size(), std::size_t{1});
  CHECK(h.results()[0].mode == AttendanceMode::Test);
  CHECK_EQ(h.results()[0].reason.value(), std::string(reasons::kInternalError));
}

// Un evento que lanza no descarta el resto del lote. Aqui el reloj falla justo al fijar el
// plazo de la primera orden, que es lo unico que sale de handle() sin capturarse.
TEST(poll_keeps_processing_the_batch_after_an_event_throws) {
  Harness h;
  h.queue_frames(3);
  h.command(VisionAction::Enroll, "100000001");
  h.command(VisionAction::Test);  // en el mismo lote
  bool fail = true;
  h.clock.on_now = [&] {
    if (std::exchange(fail, false)) throw std::runtime_error("reloj roto");
  };
  h.service->poll();

  // La orden de registro fallo sin dejar el servicio a medias, y el probar se atendio.
  CHECK(h.service->state() == ServiceState::Idle);
  CHECK(!ServiceTestAccess::has_enrollment(*h.service));
  CHECK(h.progress().empty());
  CHECK_EQ(h.results().size(), std::size_t{1});
  CHECK(h.results()[0].mode == AttendanceMode::Test);
}

// Si start_enrollment falla al fijar el plazo, no queda un registro a medias.
TEST(start_enrollment_failure_leaves_no_partial_enrollment) {
  Harness h;
  h.command(VisionAction::Enroll, "100000001");
  h.clock.on_now = [] { throw std::runtime_error("reloj roto"); };
  h.service->poll();
  h.clock.on_now = nullptr;

  CHECK(h.service->state() == ServiceState::Idle);
  CHECK(!ServiceTestAccess::has_enrollment(*h.service));
  h.command(VisionAction::Enroll, "100000001");  // y se puede volver a intentar
  h.service->poll();
  CHECK(h.service->state() == ServiceState::Enrolling);
  CHECK(ServiceTestAccess::has_enrollment(*h.service));
}

// advance_enrollment va despues del bucle de eventos y tambien esta aislado.
TEST(poll_isolates_a_throwing_enrollment_step) {
  Harness h;
  h.queue_frames(5);
  h.command(VisionAction::Enroll, "100000001");
  h.service->poll();
  bool armed = false, fired = false;
  h.bus.on_publish = [&](std::string_view, const Payload& p) {
    const auto* progress = std::get_if<EnrollmentProgress>(&p);
    if (progress != nullptr && progress->status == EnrollmentStatus::Pending && progress->step == 2) {
      armed = true;
    }
  };
  h.clock.on_now = [&] {
    if (armed && !fired) {
      fired = true;
      throw std::runtime_error("reloj roto");
    }
  };
  h.enrollment_step();  // pose 1 aceptada; al fijar el plazo de la pose 2 el reloj lanza

  CHECK(fired);  // lo que importa: poll() no dejo escapar la excepcion
  CHECK(h.service->state() == ServiceState::Enrolling);
  const std::size_t captured = h.http.requested_urls.size();
  h.enrollment_step();  // el registro sigue vivo y avanza
  CHECK(h.http.requested_urls.size() > captured);
}

TEST(enrollment_completes_even_if_every_progress_publish_fails) {
  Harness h;
  h.queue_frames(5);
  h.bus.on_publish = [](std::string_view topic, const Payload&) {
    if (topic == topics::kVisionEnrollment) throw std::runtime_error("sin broker");
  };
  h.command(VisionAction::Enroll, "100000001");
  h.service->poll();
  for (int i = 0; i < 5; ++i) h.enrollment_step();

  CHECK(h.service->state() == ServiceState::Idle);
  CHECK_EQ(h.http.requested_urls.size(), std::size_t{5});  // cada pose se capturo una vez
  CHECK_EQ(h.embeddings().size(), std::size_t{1});
  CHECK(h.light_commands() == (std::vector<int>{1, 0}));
}

TEST(embedding_publish_failure_fails_the_enrollment_once) {
  Harness h;
  h.queue_frames(5);
  h.bus.on_publish = [](std::string_view topic, const Payload&) {
    if (topic == topics::kVisionEmbedding) throw std::runtime_error("sin broker");
  };
  h.command(VisionAction::Enroll, "100000001");
  h.service->poll();
  for (int i = 0; i < 5; ++i) h.enrollment_step();

  CHECK(h.service->state() == ServiceState::Idle);
  CHECK(h.embeddings().empty());
  CHECK(h.progress().back().status == EnrollmentStatus::Failed);
  CHECK_EQ(h.progress().back().reason.value(), std::string(reasons::kInternalError));
  CHECK(h.light_commands() == (std::vector<int>{1, 0}));

  h.queue_frames(5);  // mas polls no reintentan nada
  for (int i = 0; i < 3; ++i) h.enrollment_step();
  CHECK_EQ(h.http.requested_urls.size(), std::size_t{5});
  CHECK(h.embeddings().empty());
}

TEST(finish_enrollment_frees_the_state_before_publishing_light_off) {
  Harness h;
  h.queue_frames(2);
  std::optional<ServiceState> state_at_light_off;
  h.bus.on_publish = [&](std::string_view, const Payload& p) {
    if (light_of(p) == 0) {
      state_at_light_off = h.service->state();
      throw std::runtime_error("sin broker");
    }
  };
  h.command(VisionAction::Enroll, "100000001");
  h.service->poll();
  h.command(VisionAction::Cancel);
  h.service->poll();

  CHECK(state_at_light_off.has_value());
  CHECK(*state_at_light_off == ServiceState::Idle);
  CHECK(h.service->state() == ServiceState::Idle);

  h.bus.on_publish = nullptr;  // y un registro nuevo puede empezar
  h.command(VisionAction::Enroll, "200000002");
  h.service->poll();
  CHECK(h.service->state() == ServiceState::Enrolling);
}

// --- Latencia, red y fotos no fiables --------------------------------------------------

TEST(attendance_total_latency_counts_from_the_pir_including_queue_wait) {
  Harness h;
  h.queue_frames(3);
  h.pir();
  h.clock.advance(50ms);  // el evento espera en cola hasta el siguiente poll
  h.service->poll();

  CHECK_EQ(h.results().at(0).lat_ms.total, 50 + 300);
}

TEST(captures_use_the_configured_http_timeout) {
  Harness h;
  ServiceConfig config = h.config;
  config.http_timeout = 750ms;
  h.restart(config);
  h.queue_frames(8);

  h.pir();
  h.service->poll();
  h.command(VisionAction::Enroll, "100000001");
  h.service->poll();
  h.enrollment_step();

  CHECK_EQ(h.http.requested_timeouts.size(), std::size_t{4});  // 3 de la rafaga + 1 de la pose
  for (const Millis timeout : h.http.requested_timeouts) CHECK_EQ(timeout.count(), 750);
}

TEST(body_larger_than_the_limit_is_rejected_before_decoding) {
  Harness h;
  const std::size_t size = ok(noise_frame(1)).body.size();
  ServiceConfig config = h.config;

  config.max_body_bytes = size;  // justo en el limite: pasa
  h.restart(config);
  h.queue_frames(3);
  h.pir();
  h.service->poll();
  CHECK(h.results().at(0).account.has_value());

  config.max_body_bytes = size - 1;  // un byte mas: se descarta
  h.restart(config);
  h.queue_frames(3);
  h.pir();
  h.service->poll();
  CHECK_EQ(h.results().at(1).reason.value(), std::string(reasons::kCaptureFailed));
  CHECK_EQ(h.detector.calls, 1);  // solo la primera asistencia llego al detector
}

TEST(image_larger_than_the_dimension_limits_is_rejected) {
  Harness h;  // las fotos de prueba miden 320x240
  ServiceConfig config = h.config;

  config.max_image_width = 320;
  config.max_image_height = 240;  // justo en el limite: pasa
  h.restart(config);
  h.queue_frames(3);
  h.pir();
  h.service->poll();
  CHECK(h.results().at(0).account.has_value());

  config.max_image_width = 319;
  h.restart(config);
  h.queue_frames(3);
  h.pir();
  h.service->poll();
  CHECK_EQ(h.results().at(1).reason.value(), std::string(reasons::kCaptureFailed));

  config.max_image_width = 320;
  config.max_image_height = 239;
  h.restart(config);
  h.queue_frames(3);
  h.pir();
  h.service->poll();
  CHECK_EQ(h.results().at(2).reason.value(), std::string(reasons::kCaptureFailed));
  CHECK_EQ(h.detector.calls, 1);
}

TEST(default_limits_accept_the_ov3660_resolution_and_reject_bigger_images) {
  Harness h;
  h.http.responses.push_back(ok(cv::Mat(1536, 2048, CV_8UC3, cv::Scalar(90, 90, 90))));
  h.http.responses.push_back({200, {}});
  h.http.responses.push_back({200, {}});
  h.pir();
  h.service->poll();
  CHECK(h.results().at(0).account.has_value());  // 2048x1536 pasa
  CHECK_EQ(h.detector.last_input.cols, 2048);
  CHECK_EQ(h.detector.last_input.rows, 1536);

  h.http.responses.push_back(ok(cv::Mat(100, 2049, CV_8UC3, cv::Scalar::all(7))));
  h.http.responses.push_back(ok(cv::Mat(1537, 100, CV_8UC3, cv::Scalar::all(7))));
  h.http.responses.push_back({200, {}});
  h.pir();
  h.service->poll();
  CHECK_EQ(h.results().at(1).reason.value(), std::string(reasons::kCaptureFailed));
}

TEST(forged_jpeg_header_with_huge_dimensions_is_rejected) {
  Harness h;
  std::vector<std::uint8_t> jpeg;
  cv::imencode(".jpg", noise_frame(1), jpeg);
  for (std::size_t i = 0; i + 8 < jpeg.size(); ++i) {
    if (jpeg[i] == 0xFF && jpeg[i + 1] == 0xC0) {
      jpeg[i + 5] = 0x70;  // alto 0x7000 = 28672
      jpeg[i + 7] = 0x70;  // ancho 0x7000
      break;
    }
  }
  for (int i = 0; i < 3; ++i) h.http.responses.push_back({200, jpeg});
  h.pir();
  h.service->poll();

  CHECK_EQ(h.results().at(0).reason.value(), std::string(reasons::kCaptureFailed));
  CHECK_EQ(h.detector.calls, 0);
}

TEST(fake_embedder_without_outputs_throws_instead_of_indexing) {
  fakes::FakeFaceEmbedder embedder;
  CHECK_THROWS(embedder.embed(cv::Mat()), std::logic_error);
}

// --- Modo del resultado ---------------------------------------------------------------

TEST(attendance_result_mode_is_pase_for_pir_and_prueba_for_probar) {
  Harness h;
  h.queue_frames(6);
  h.pir();
  h.service->poll();
  h.command(VisionAction::Test);
  h.service->poll();

  const auto results = h.results();
  CHECK_EQ(results.size(), std::size_t{2});
  CHECK(results[0].mode == AttendanceMode::Attendance);
  CHECK(results[1].mode == AttendanceMode::Test);
  CHECK(to_wire(results[0].mode) == "pase");
  CHECK(to_wire(results[1].mode) == "prueba");
  CHECK(modes::kField == "modo");
}

TEST(failed_attendance_keeps_its_mode) {
  Harness h;  // sin fotos: captura fallida
  h.command(VisionAction::Test);
  h.service->poll();
  CHECK(h.results().at(0).mode == AttendanceMode::Test);
  CHECK_EQ(h.results().at(0).reason.value(), std::string(reasons::kCaptureFailed));
}

// --- Cola de eventos -----------------------------------------------------------------------

TEST(inbox_flood_beyond_the_cap_is_dropped_with_one_log_line) {
  Harness h;
  CHECK_EQ(h.config.max_inbox_events, std::size_t{16});
  CapturedStderr log;
  for (int i = 0; i < 40; ++i) h.command(VisionAction::Cancel, "100000001");

  CHECK_EQ(ServiceTestAccess::inbox_size(*h.service), std::size_t{13});  // cancelar: max - 3
  CHECK_EQ(log.count("cola de eventos llena"), std::size_t{1});
  CHECK_EQ(log.count("100000001"), std::size_t{0});  // el log no lleva el contenido
  CHECK(h.bus.published.empty());                    // y el descarte no responde nada

  h.service->poll();
  CHECK_EQ(ServiceTestAccess::inbox_size(*h.service), std::size_t{0});
  h.command(VisionAction::Cancel);  // despues del poll la cola vuelve a aceptar
  CHECK_EQ(ServiceTestAccess::inbox_size(*h.service), std::size_t{1});
}

TEST(inbox_cap_is_configurable_and_overflow_is_logged_again_after_a_poll) {
  Harness h;
  ServiceConfig config = h.config;
  config.max_inbox_events = 5;
  h.restart(config);
  CapturedStderr log;

  for (int i = 0; i < 5; ++i) h.command(VisionAction::Cancel);
  CHECK_EQ(ServiceTestAccess::inbox_size(*h.service), std::size_t{2});
  h.service->poll();
  CHECK_EQ(log.count("cola de eventos llena (5)"), std::size_t{1});
  for (int i = 0; i < 5; ++i) h.command(VisionAction::Cancel);
  CHECK_EQ(log.count("cola de eventos llena (5)"), std::size_t{2});
}

TEST(full_inbox_releases_the_pending_flags_of_dropped_events) {
  Harness h;
  ServiceConfig config = h.config;
  config.max_inbox_events = 4;
  h.restart(config);
  h.queue_frames(6);
  CapturedStderr log;

  for (int i = 0; i < 4; ++i) h.command(VisionAction::Enroll, "100000001");  // llenan la cola
  h.pir();                                          // descartados: no deben dejar su bandera tomada
  h.command(VisionAction::Test);
  CHECK_EQ(log.count("cola de eventos llena"), std::size_t{1});
  h.service->poll();
  CHECK(h.results().empty());                    // ni el PIR ni el probar entraron
  CHECK_EQ(h.progress().size(), std::size_t{4});  // Pending + 3 ocupado
  h.pir();
  h.service->poll();
  CHECK(h.results().empty());  // el PIR se ignora: hay un registro en curso
  h.command(VisionAction::Cancel);
  h.service->poll();
  CHECK(h.service->state() == ServiceState::Idle);
  h.pir();
  h.service->poll();
  CHECK_EQ(h.results().size(), std::size_t{1});

  h.command(VisionAction::Test);
  h.service->poll();
  CHECK_EQ(h.results().size(), std::size_t{2});
  h.command(VisionAction::Enroll, "100000001");
  h.service->poll();
  CHECK(h.service->state() == ServiceState::Enrolling);
}

TEST(probar_is_coalesced_like_pir) {
  Harness h;
  h.queue_frames(9);
  h.command(VisionAction::Test);
  h.command(VisionAction::Test);
  h.command(VisionAction::Test);
  h.service->poll();
  CHECK_EQ(h.results().size(), std::size_t{1});
  CHECK(h.results()[0].mode == AttendanceMode::Test);

  h.command(VisionAction::Test);  // la bandera se libero: otro probar funciona
  h.service->poll();
  CHECK_EQ(h.results().size(), std::size_t{2});

  // PIR y probar se coalescen por separado.
  h.pir();
  h.command(VisionAction::Test);
  h.pir();
  h.command(VisionAction::Test);
  h.service->poll();
  CHECK_EQ(h.results().size(), std::size_t{4});
}

TEST(probar_during_enrollment_still_gets_one_busy_refusal_and_enrollment_continues) {
  Harness h;
  h.queue_frames(5);
  h.command(VisionAction::Enroll, "100000001");
  h.service->poll();
  const std::size_t before = h.progress().size();

  h.command(VisionAction::Test);
  h.command(VisionAction::Test);
  h.service->poll();
  CHECK_EQ(h.progress().size(), before + 1);  // los dos probar en cola cuentan uno
  CHECK(h.progress().back().status == EnrollmentStatus::Error);
  CHECK_EQ(h.progress().back().reason.value(), std::string(reasons::kBusy));
  CHECK(h.service->state() == ServiceState::Enrolling);

  h.command(VisionAction::Test);  // y la bandera no quedo tomada
  h.service->poll();
  CHECK_EQ(h.progress().size(), before + 2);
  for (int i = 0; i < 5; ++i) h.enrollment_step();
  CHECK_EQ(h.embeddings().size(), std::size_t{1});
}

TEST(a_failure_while_enqueuing_never_escapes_and_leaves_no_stuck_flag) {
  Harness h;
  h.queue_frames(6);
  CapturedStderr log;
  bool fail = true;
  h.clock.on_now = [&] {
    if (std::exchange(fail, false)) throw std::runtime_error("reloj roto");
  };

  // El fallo ocurre despues de tomar la bandera del PIR; el handler (hilo del bus) no lanza.
  CHECK_EQ(h.bus.inject(topics::kCameraPir, PirEvent{}), std::size_t{1});
  CHECK_EQ(log.count("no se pudo encolar un mensaje"), std::size_t{1});
  h.service->poll();
  CHECK(h.results().empty());

  h.pir();  // la bandera se solto: este PIR si pasa
  h.service->poll();
  CHECK_EQ(h.results().size(), std::size_t{1});

  fail = true;  // lo mismo con probar
  CHECK_EQ(h.bus.inject(topics::kVisionCmd, VisionCommand{VisionAction::Test, ""}), std::size_t{1});
  h.command(VisionAction::Test);
  h.service->poll();
  CHECK_EQ(h.results().size(), std::size_t{2});
  CHECK(h.results()[1].mode == AttendanceMode::Test);
}

// --- Cuentas ------------------------------------------------------------------------------

TEST(enroll_with_an_invalid_account_is_dropped_before_taking_a_slot) {
  Harness h;
  CapturedStderr log;
  for (int i = 0; i < 100; ++i) {
    h.command(VisionAction::Enroll, i % 2 ? "12345678a" : "1234567890");
  }
  h.command(VisionAction::Enroll, "");
  h.command(VisionAction::Enroll, "<b>hi</b>1");

  CHECK_EQ(ServiceTestAccess::inbox_size(*h.service), std::size_t{0});  // no ocupan lugar
  CHECK_EQ(log.count("enrolar con cuenta invalida"), std::size_t{1});   // una linea por poll
  CHECK_EQ(log.count("cola de eventos llena"), std::size_t{0});
  CHECK_EQ(log.count("12345678a"), std::size_t{0});                     // sin el contenido
  h.service->poll();
  CHECK(h.bus.published.empty());  // y no responden
  CHECK(h.service->state() == ServiceState::Idle);

  h.command(VisionAction::Enroll, "100000001");  // uno valido si pasa
  h.service->poll();
  CHECK(h.service->state() == ServiceState::Enrolling);

  for (int i = 0; i < 3; ++i) h.command(VisionAction::Enroll, "bad");  // el log vuelve a salir tras un poll
  CHECK_EQ(log.count("enrolar con cuenta invalida"), std::size_t{2});
}

// Defensa en profundidad: la cola ya filtra estas cuentas, pero start_enrollment tambien.
TEST(start_enrollment_refuses_an_invalid_account_without_echoing_it) {
  Harness h;
  for (const char* bad : {"", "1", "12345678", "1234567890", "12345678a", "1234 5678", "<b>hi</b>1", "-12345678"}) {
    ServiceTestAccess::handle(*h.service, VisionCommand{VisionAction::Enroll, bad}, h.clock.now());
    CHECK(h.service->state() == ServiceState::Idle);
    CHECK(h.progress().back().status == EnrollmentStatus::Error);
    CHECK_EQ(h.progress().back().reason.value(), std::string(reasons::kInvalidAccount));
    CHECK_EQ(h.progress().back().account, std::string());
    CHECK_EQ(h.progress().back().step, 0);
  }
  CHECK(h.light_commands().empty());
  CHECK(!ServiceTestAccess::has_enrollment(*h.service));
}

TEST(second_valid_enroll_in_one_poll_gets_busy_and_the_first_proceeds) {
  Harness h;
  h.queue_frames(5);
  h.command(VisionAction::Enroll, "100000001");
  h.command(VisionAction::Enroll, "200000002");
  CHECK_EQ(ServiceTestAccess::inbox_size(*h.service), std::size_t{2});  // sin coalescer
  h.service->poll();
  CHECK(h.service->state() == ServiceState::Enrolling);
  CHECK_EQ(h.progress().size(), std::size_t{2});
  CHECK(h.progress()[0].status == EnrollmentStatus::Pending);
  CHECK_EQ(h.progress()[0].account, std::string("100000001"));
  CHECK(h.progress()[1].status == EnrollmentStatus::Error);
  CHECK_EQ(h.progress()[1].reason.value(), std::string(reasons::kBusy));
  CHECK_EQ(h.progress()[1].account, std::string("200000002"));
  CHECK_EQ(h.progress()[1].step, 0);

  h.clock.advance(h.config.pose_delay);  // el primero sigue su curso
  h.service->poll();
  CHECK(h.progress().back().status == EnrollmentStatus::Accepted || h.progress().back().status == EnrollmentStatus::Pending);
  CHECK_EQ(h.progress().back().account, std::string("100000001"));
  CHECK(h.service->state() == ServiceState::Enrolling);
}

TEST(cancels_with_an_invalid_account_are_dropped_without_taking_a_slot) {
  Harness h;
  CapturedStderr log;
  for (int i = 0; i < 100; ++i) h.command(VisionAction::Cancel, i % 2 ? "12345678a" : "1234567890");
  CHECK_EQ(ServiceTestAccess::inbox_size(*h.service), std::size_t{0});
  CHECK_EQ(log.count("cancelar con cuenta invalida"), std::size_t{1});
  CHECK_EQ(log.count("cola de eventos llena"), std::size_t{0});
  CHECK_EQ(log.count("12345678a"), std::size_t{0});
  CHECK(h.bus.published.empty());
  h.command(VisionAction::Cancel, "");  // vacio y valido si pasan
  h.command(VisionAction::Cancel, "100000001");
  CHECK_EQ(ServiceTestAccess::inbox_size(*h.service), std::size_t{2});
}

TEST(cancels_cannot_exceed_the_cap_minus_three) {
  Harness h;
  ServiceConfig config = h.config;
  config.max_inbox_events = 6;
  h.restart(config);
  for (int i = 0; i < 20; ++i) h.command(VisionAction::Cancel, "100000001");
  CHECK_EQ(ServiceTestAccess::inbox_size(*h.service), std::size_t{3});
}

TEST(pir_probar_and_enrolar_still_get_in_when_the_inbox_is_full_of_cancels) {
  Harness h;
  ServiceConfig config = h.config;
  config.max_inbox_events = 4;
  h.restart(config);
  h.queue_frames(6);
  for (int i = 0; i < 5; ++i) h.command(VisionAction::Cancel, "100000001");
  CHECK_EQ(ServiceTestAccess::inbox_size(*h.service), std::size_t{1});
  h.pir();
  h.command(VisionAction::Test);
  h.command(VisionAction::Enroll, "100000001");
  CHECK_EQ(ServiceTestAccess::inbox_size(*h.service), std::size_t{4});
  h.service->poll();
  CHECK_EQ(h.results().size(), std::size_t{2});  // el PIR y el probar
  CHECK(h.service->state() == ServiceState::Enrolling);
}

TEST(cancel_with_an_invalid_account_is_ignored_while_empty_means_current) {
  Harness h;
  h.command(VisionAction::Enroll, "100000001");
  h.service->poll();
  for (const char* bad : {"1", "10000000a", "1000000011", "../1"}) {
    h.command(VisionAction::Cancel, bad);
    h.service->poll();
    CHECK(h.service->state() == ServiceState::Enrolling);
  }
  h.command(VisionAction::Cancel, "");
  h.service->poll();
  CHECK(h.service->state() == ServiceState::Idle);
}

TEST(refusals_clear_an_invalid_account_but_echo_a_valid_one) {
  Harness h;
  h.command(VisionAction::Enroll, "100000001");
  h.service->poll();

  h.command(VisionAction::Test, "not-an-account");
  h.service->poll();
  CHECK_EQ(h.progress().back().account, std::string());
  CHECK_EQ(h.progress().back().reason.value(), std::string(reasons::kBusy));

  h.command(VisionAction::Test, "200000002");
  h.service->poll();
  CHECK_EQ(h.progress().back().account, std::string("200000002"));
}

TEST(refusals_use_error_status_with_step_zero_and_never_failed) {
  Harness h;
  h.queue_frames(5);
  h.command(VisionAction::Enroll, "100000001");
  h.service->poll();
  h.command(VisionAction::Enroll, "200000002");
  h.command(VisionAction::Test);
  h.service->poll();
  ServiceTestAccess::handle(*h.service, VisionCommand{VisionAction::Enroll, "bad"}, h.clock.now());

  const auto progress = h.progress();
  CHECK_EQ(progress.size(), std::size_t{4});
  for (std::size_t i = 1; i < progress.size(); ++i) {
    CHECK(progress[i].status == EnrollmentStatus::Error);
    CHECK_EQ(progress[i].step, 0);
    CHECK_EQ(progress[i].pose, std::string());
  }
  CHECK(h.service->state() == ServiceState::Enrolling);
}

TEST(is_valid_account_accepts_exactly_nine_ascii_digits) {
  CHECK(is_valid_account("320068234"));
  CHECK(is_valid_account("000000000"));
  CHECK(!is_valid_account(""));
  CHECK(!is_valid_account("32006823"));
  CHECK(!is_valid_account("3200682345"));
  CHECK(!is_valid_account("32006823x"));
  CHECK(!is_valid_account(std::string_view("32006823\0", 9)));
  CHECK(!is_valid_account(" 32006823"));
}

// --- Cuerpo, formato y decodificacion de las fotos -------------------------------------------

TEST(captures_pass_the_body_cap_to_the_http_client) {
  Harness h;
  ServiceConfig config = h.config;
  config.max_body_bytes = 123456;
  h.restart(config);
  h.queue_frames(3);
  h.pir();
  h.service->poll();

  CHECK_EQ(h.http.requested_max_body_bytes.size(), std::size_t{3});
  for (const std::size_t cap : h.http.requested_max_body_bytes) CHECK_EQ(cap, std::size_t{123456});
}

TEST(png_is_rejected_unless_accept_png_is_set) {
  Harness h;
  CHECK(h.config.accept_png);  // el Harness lo activa; el valor por defecto del servicio es false
  CHECK(!ServiceConfig{}.accept_png);

  ServiceConfig config = h.config;
  config.accept_png = false;
  h.restart(config);
  h.queue_frames(3);  // PNG
  h.pir();
  h.service->poll();
  CHECK_EQ(h.results().at(0).reason.value(), std::string(reasons::kCaptureFailed));
  CHECK_EQ(h.detector.calls, 0);

  for (int i = 0; i < 3; ++i) h.http.responses.push_back({200, encode_jpeg(noise_frame(i + 1))});
  h.pir();
  h.service->poll();  // el JPEG si se acepta
  CHECK(h.results().at(1).account.has_value());
}

TEST(progressive_jpeg_is_rejected_before_decoding) {
  Harness h;
  std::vector<std::uint8_t> progressive;
  cv::imencode(".jpg", noise_frame(1), progressive, {cv::IMWRITE_JPEG_PROGRESSIVE, 1});
  for (int i = 0; i < 3; ++i) h.http.responses.push_back({200, progressive});
  h.pir();
  h.service->poll();

  CHECK_EQ(h.results().at(0).reason.value(), std::string(reasons::kCaptureFailed));
  CHECK_EQ(h.detector.calls, 0);
}

TEST(one_undecodable_frame_fails_alone_and_the_burst_is_still_evaluated) {
  Harness h;
  CapturedStderr log;
  h.http.responses.push_back({200, headers_only_jpeg()});  // cabecera valida, sin datos
  h.http.responses.push_back(ok(noise_frame(1)));
  h.http.responses.push_back(ok(noise_frame(2)));
  h.pir();
  h.service->poll();

  CHECK_EQ(h.results().at(0).account.value(), std::string("320068234"));
  CHECK_EQ(h.detector.calls, 1);
}

// --- Modo a prueba de olvidos, reloj roto y log de la cola -----------------------------------------

TEST(attendance_result_has_no_default_mode) {
  static_assert(!std::is_default_constructible_v<AttendanceResult>);
  static_assert(std::is_constructible_v<AttendanceResult, AttendanceMode>);
  CHECK(AttendanceResult(AttendanceMode::Test).mode == AttendanceMode::Test);
}

TEST(attendance_still_answers_with_internal_error_when_the_clock_fails_building_the_answer) {
  Harness h;
  h.clock.on_sleep = [] { throw std::runtime_error("reloj roto"); };
  h.pir();
  bool broken = true;  // desde ahora cualquier lectura de la hora lanza, tambien al armar el error
  h.clock.on_now = [&] {
    if (broken) throw std::runtime_error("reloj roto");
  };
  h.service->poll();

  CHECK(h.service->state() == ServiceState::Idle);
  CHECK_EQ(h.results().size(), std::size_t{1});
  CHECK_EQ(h.results()[0].reason.value(), std::string(reasons::kInternalError));
  CHECK_EQ(h.results()[0].lat_ms.total, 0);
  CHECK(h.results()[0].mode == AttendanceMode::Attendance);
  CHECK(h.light_commands() == (std::vector<int>{1, 0}));
}

TEST(enqueue_failures_are_logged_once_per_poll) {
  Harness h;
  CapturedStderr log;
  h.clock.on_now = [] { throw std::runtime_error("reloj roto"); };
  for (int i = 0; i < 5; ++i) CHECK_EQ(h.bus.inject(topics::kVisionCmd, VisionCommand{VisionAction::Cancel, ""}), std::size_t{1});
  CHECK_EQ(log.count("no se pudo encolar un mensaje"), std::size_t{1});
  h.service->poll();
  CHECK_EQ(h.bus.inject(topics::kVisionCmd, VisionCommand{VisionAction::Cancel, ""}), std::size_t{1});
  CHECK_EQ(log.count("no se pudo encolar un mensaje"), std::size_t{2});
}

TEST(coalesced_probar_is_logged_once_per_poll) {
  Harness h;
  CapturedStderr log;
  for (int i = 0; i < 4; ++i) h.command(VisionAction::Test);
  CHECK_EQ(log.count("orden repetida descartada"), std::size_t{1});
  h.service->poll();
  for (int i = 0; i < 4; ++i) h.command(VisionAction::Test);
  CHECK_EQ(log.count("orden repetida descartada"), std::size_t{2});
}
