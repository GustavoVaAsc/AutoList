#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
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
  fakes::FixedWallClock wall_clock;
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
        ServiceDeps{bus, http, detector, embedder, gallery, clock, wall_clock}, config);
  }

  // Reconstruye el servicio con otra configuracion (la anterior se da de baja primero).
  void restart(const ServiceConfig& new_config) {
    service.reset();
    config = new_config;
    service = std::make_unique<VisionService>(
        ServiceDeps{bus, http, detector, embedder, gallery, clock, wall_clock}, config);
  }

  void queue_frames(int count) {
    for (int i = 0; i < count; ++i) http.responses.push_back(ok(noise_frame(i + 1)));
  }

  void pir() { bus.inject(topics::kCameraPir, PirEvent{}); }
  void command(VisionAction action, std::string account = "") {
    bus.inject(topics::kVisionCmd, VisionCommand{action, std::move(account), std::nullopt});
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
  std::vector<VisionStatus> statuses() const {
    return bus.published_as<VisionStatus>(topics::kVisionStatus);
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
  CHECK(results[0].decision == Decision::Recognized);
  CHECK_NEAR(results[0].confidence.value(), 1.0, 1e-6);
  CHECK_EQ(results[0].lat_ms.cap.value(), 60);
  CHECK_EQ(results[0].lat_ms.det.value(), 12);
  CHECK_EQ(results[0].lat_ms.emb.value(), 6);
  CHECK_EQ(results[0].lat_ms.total.value(), 300 + 60 + 12 + 6);  // incluye la espera de la luz
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
  // Orden global: estado (pase), luz encendida, resultado, luz apagada, estado (inactivo).
  CHECK_EQ(h.bus.published.size(), std::size_t{5});
  CHECK(h.bus.published[0].topic == topics::kVisionStatus);
  CHECK(h.bus.published[1].topic == topics::kActuatorCmd);
  CHECK(h.bus.published[2].topic == topics::kVisionResult);
  CHECK(h.bus.published[3].topic == topics::kActuatorCmd);
  CHECK(h.bus.published[4].topic == topics::kVisionStatus);
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
  CHECK(!results[0].confidence.has_value());
  CHECK(results[0].decision == Decision::InsufficientImage);
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
  CHECK(h.results().at(0).decision == Decision::InsufficientImage);
  CHECK_EQ(h.detector.calls, 0);
}

TEST(attendance_reports_no_face) {
  Harness h;
  h.detector.detections.clear();
  h.queue_frames(3);
  h.pir();
  h.service->poll();

  CHECK_EQ(h.results().at(0).reason.value(), std::string(reasons::kNoFace));
  CHECK(h.results().at(0).decision == Decision::NoFace);
  CHECK(!h.results().at(0).account.has_value());
  CHECK(!h.results().at(0).confidence.has_value());
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

TEST(attendance_reports_unknown_below_match_threshold_but_keeps_the_best_candidate) {
  Harness h;
  h.embedder.outputs = {{0.0f, 0.0f, 1.0f, 0.0f}};  // ortogonal a toda la galeria
  h.queue_frames(3);
  h.pir();
  h.service->poll();

  const auto result = h.results().at(0);
  CHECK(result.decision == Decision::Unknown);
  CHECK_EQ(result.reason.value(), std::string(reasons::kUnknown));
  CHECK_NEAR(result.confidence.value(), 0.0, 1e-6);
  // Con "desconocido" se manda el mejor candidato, para que Node-RED pueda auditarlo.
  CHECK(result.account.has_value());
  CHECK_EQ(result.account.value(), std::string("320068234"));
}

TEST(attendance_with_empty_gallery_reports_reason) {
  Harness h;
  h.gallery.remove("320068234");
  h.gallery.remove("320068235");
  h.queue_frames(3);
  h.pir();
  h.service->poll();

  CHECK_EQ(h.results().at(0).reason.value(), std::string(reasons::kEmptyGallery));
  CHECK(h.results().at(0).decision == Decision::Unknown);
  CHECK(!h.results().at(0).account.has_value());
}

TEST(attendance_detector_exception_becomes_internal_error_result) {
  Harness h;
  h.detector.on_call = [] { throw std::runtime_error("npu caida"); };
  h.queue_frames(3);
  h.pir();
  h.service->poll();

  CHECK_EQ(h.results().size(), std::size_t{1});
  CHECK_EQ(h.results()[0].reason.value(), std::string(reasons::kInternalError));
  CHECK(h.results()[0].decision == Decision::Error);
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
  // Normas distintas a proposito: cada pose se normaliza antes de promediar. Las cinco
  // direcciones se parecen entre si (ninguna se descarta como anomala).
  h.embedder.outputs = {{5.0f, 0.0f, 0.0f, 0.0f},
                        {4.0f, 3.0f, 0.0f, 0.0f},
                        {3.0f, 4.0f, 0.0f, 0.0f},
                        {4.0f, 3.0f, 0.0f, 0.0f},
                        {10.0f, 0.0f, 0.0f, 0.0f}};
  h.queue_frames(5);

  h.command(VisionAction::Enroll, "999000111");
  h.service->poll();
  CHECK(h.service->state() == ServiceState::Enrolling);
  CHECK_EQ(h.progress().size(), std::size_t{1});
  CHECK_EQ(h.progress()[0].pose, std::string("frente"));
  CHECK(h.progress()[0].status == EnrollmentStatus::WaitingPose);
  CHECK(h.http.requested_urls.empty());  // aun no se captura: el alumno adopta la pose

  for (int i = 0; i < 5; ++i) h.enrollment_step();

  CHECK(h.service->state() == ServiceState::Idle);
  CHECK_EQ(h.http.requested_urls.size(), std::size_t{5});

  // Por pose: capturando, aceptada y, salvo en la ultima, el aviso de la siguiente.
  // Al final, el estado "completado".
  const auto progress = h.progress();
  CHECK_EQ(progress.size(), std::size_t{5 + 5 + 5 + 1});
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
  CHECK_EQ(progress.back().pose, std::string("frente_2"));
  CHECK_EQ(progress.back().step, 5);
  CHECK(progress.back().status == EnrollmentStatus::Completed);

  const auto embeddings = h.embeddings();
  CHECK_EQ(embeddings.size(), std::size_t{1});
  CHECK_EQ(embeddings[0].account, std::string("999000111"));
  CHECK_EQ(embeddings[0].embedding.size(), std::size_t{4});
  // Promedio de los unitarios de las cinco poses, normalizado.
  const std::vector<std::vector<float>> unidades = {{1.0f, 0.0f}, {0.8f, 0.6f}, {0.6f, 0.8f},
                                                     {0.8f, 0.6f}, {1.0f, 0.0f}};
  double media0 = 0.0;
  double media1 = 0.0;
  for (const std::vector<float>& u : unidades) {
    media0 += u[0];
    media1 += u[1];
  }
  media0 /= static_cast<double>(unidades.size());
  media1 /= static_cast<double>(unidades.size());
  const double norma = std::sqrt(media0 * media0 + media1 * media1);
  CHECK_NEAR(embeddings[0].embedding[0], media0 / norma, 1e-5);
  CHECK_NEAR(embeddings[0].embedding[1], media1 / norma, 1e-5);
  CHECK_NEAR(embeddings[0].embedding[2], 0.0, 1e-6);
  CHECK(h.light_commands() == (std::vector<int>{1, 0}));
}

// --- Descarte de outliers del registro (plan 5.4, paso 5) -------------------------------

// Las cinco poses del registro, en el embedder falso, y el embedding publicado.
namespace {

struct RegistroFinal {
  std::vector<EnrollmentProgress> progress;
  std::vector<EmbeddingMessage> embeddings;
  bool completo = false;
};

// Corre un registro completo de cinco poses con los embeddings dados.
RegistroFinal correr_registro(Harness& h, const std::string& cuenta,
                               const std::vector<std::vector<float>>& poses) {
  h.embedder.outputs = poses;
  h.queue_frames(5);
  h.command(VisionAction::Enroll, cuenta);
  h.service->poll();
  for (int i = 0; i < 5; ++i) h.enrollment_step();

  RegistroFinal resultado;
  resultado.progress = h.progress();
  resultado.embeddings = h.embeddings();
  for (const EnrollmentProgress& p : resultado.progress) {
    if (p.status == EnrollmentStatus::Completed) resultado.completo = true;
  }
  return resultado;
}

// Cinco poses con similitud baja entre pares (rho = 0.1): las caras de un mismo alumno
// son deliberadamente distintas (frente, izquierda, derecha, arriba, frente_2), asi que
// un alumno legitimo puede tenerlas poco parecidas.
//
// Se construyen como un simplex regular en R^5 (coseno -1/4 entre las u_i) con un
// componente comun: v_i = (a * u_i, b) con a^2 = 0.72 y b^2 = 0.28, de modo que el coseno
// entre dos v_i es 0.72 * (-1/4) + 0.28 = 0.1. La media de las cinco es (0, 0, 0, 0, 0, b).
std::vector<std::vector<float>> poses_poco_parecidas() {
  constexpr double kComponente = 0.2;  // 1/5: el centro del simplex
  const double a = std::sqrt(0.72);
  const double b = std::sqrt(0.28);
  std::vector<std::vector<float>> poses;
  for (int i = 0; i < 5; ++i) {
    std::vector<float> pose(6, static_cast<float>(b));
    for (int j = 0; j < 5; ++j) {
      const double s = (j == i ? 1.0 : 0.0) - kComponente;
      pose[j] = static_cast<float>(a * s / std::sqrt(0.8));
    }
    poses.push_back(pose);
  }
  return poses;
}

}  // namespace

TEST(el_registro_descarta_la_pose_anomala_y_pro_media_el_resto) {
  Harness h;
  CapturedStderr log;
  // Cuatro poses iguales y una ortogonal: la rara se descarta y no entra en la media.
  const RegistroFinal registro = correr_registro(
      h, "100000001", {{1.0f, 0.0f, 0.0f, 0.0f},
                       {1.0f, 0.0f, 0.0f, 0.0f},
                       {1.0f, 0.0f, 0.0f, 0.0f},
                       {1.0f, 0.0f, 0.0f, 0.0f},
                       {0.0f, 1.0f, 0.0f, 0.0f}});

  CHECK_EQ(log.count("pose descartada"), std::size_t{1});
  CHECK(log.count("similitud") >= 1);
  // El aviso dice QUE pose se descarto (la quinta, frente_2).
  CHECK_EQ(log.count("pose descartada: frente_2"), std::size_t{1});
  CHECK(registro.completo);
  CHECK_EQ(registro.embeddings.size(), std::size_t{1});
  // La media de las cuatro buenas es (1,0,0,0), ya normalizada.
  CHECK_NEAR(registro.embeddings[0].embedding[0], 1.0, 1e-6);
  CHECK_NEAR(registro.embeddings[0].embedding[1], 0.0, 1e-6);
  CHECK_NEAR(registro.embeddings[0].embedding[2], 0.0, 1e-6);
  CHECK(h.service->state() == ServiceState::Idle);
}

TEST(el_registro_falla_como_inconsistente_si_solo_concuerdan_dos_poses) {
  Harness h;
  CapturedStderr log;
  // Solo las dos primeras poses concuerdan entre si: ninguna llega al umbral.
  const RegistroFinal registro = correr_registro(
      h, "100000001", {{1.0f, 0.0f, 0.0f, 0.0f},
                       {1.0f, 0.0f, 0.0f, 0.0f},
                       {0.0f, 1.0f, 0.0f, 0.0f},
                       {0.0f, 0.0f, 1.0f, 0.0f},
                       {0.0f, 0.0f, 0.0f, 1.0f}});

  CHECK_EQ(log.count("pose descartada"), std::size_t{3});
  CHECK_EQ(log.count("pose descartada: derecha"), std::size_t{1});
  CHECK_EQ(log.count("pose descartada: arriba"), std::size_t{1});
  CHECK_EQ(log.count("pose descartada: frente_2"), std::size_t{1});
  CHECK(registro.embeddings.empty());    // no se publica el embedding
  CHECK(!registro.completo);             // ni el estado "completado"
  CHECK(registro.progress.back().status == EnrollmentStatus::Error);
  CHECK_EQ(registro.progress.back().reason.value(),
           std::string(enrollment_reasons::kInconsistent));
  CHECK(h.service->state() == ServiceState::Idle);
  CHECK(h.light_commands() == (std::vector<int>{1, 0}));
}

TEST(el_registro_con_poses_coherentes_promedia_las_cinco) {
  Harness h;
  CapturedStderr log;
  const RegistroFinal registro = correr_registro(
      h, "100000001", {{5.0f, 0.0f, 0.0f, 0.0f},
                       {4.0f, 3.0f, 0.0f, 0.0f},
                       {3.0f, 4.0f, 0.0f, 0.0f},
                       {4.0f, 3.0f, 0.0f, 0.0f},
                       {10.0f, 0.0f, 0.0f, 0.0f}});

  CHECK_EQ(log.count("pose descartada"), std::size_t{0});
  CHECK(registro.completo);
  CHECK_EQ(registro.embeddings.size(), std::size_t{1});
  const std::vector<std::vector<float>> unidades = {{1.0f, 0.0f}, {0.8f, 0.6f}, {0.6f, 0.8f},
                                                     {0.8f, 0.6f}, {1.0f, 0.0f}};
  double media0 = 0.0;
  double media1 = 0.0;
  for (const std::vector<float>& u : unidades) {
    media0 += u[0];
    media1 += u[1];
  }
  media0 /= static_cast<double>(unidades.size());
  media1 /= static_cast<double>(unidades.size());
  const double norma = std::sqrt(media0 * media0 + media1 * media1);
  CHECK_NEAR(registro.embeddings[0].embedding[0], media0 / norma, 1e-5);
  CHECK_NEAR(registro.embeddings[0].embedding[1], media1 / norma, 1e-5);
}

// --- Regla iterativa del descarte (regresion del hallazgo del codigo) -------------------

// Un alumno legitimo puede tener las cinco caras poco parecidas entre si (rho bajo):
// las poses del registro son deliberadamente distintas. Con la regla anterior (comparar
// cada pose con la media de las OTRAS cuatro, media que arrastra a los outliers) y el
// umbral antiguo (0.60) las cinco se descartaban y el registro moria como inconsistente:
// el alumno no se podia registrar. Con la regla iterativa y el umbral 0.50 sobreviven
// las cinco.
TEST(el_registro_con_poses_poco_parecidas_entre_si_no_descarta_ninguna) {
  Harness h;
  CapturedStderr log;
  const RegistroFinal registro = correr_registro(h, "100000001", poses_poco_parecidas());

  CHECK_EQ(log.count("pose descartada"), std::size_t{0});
  CHECK(registro.completo);
  CHECK_EQ(registro.embeddings.size(), std::size_t{1});
  CHECK_EQ(registro.embeddings[0].embedding.size(), std::size_t{6});
  // El embedding publicado es la media de las cinco (normalizada): la media de las cinco
  // poses queda en el eje del componente comun.
  for (std::size_t i = 0; i < 5; ++i) CHECK_NEAR(registro.embeddings[0].embedding[i], 0.0, 1e-6);
  CHECK_NEAR(registro.embeddings[0].embedding[5], 1.0, 1e-6);
}

// La regla, comprobada directamente: cinco poses con similitud 0.1 entre pares sobreviven
// todas con el umbral por omision (0.50).
TEST(el_descarte_conserva_las_poses_poco_parecidas_entre_si) {
  const std::vector<std::size_t> supervivientes = keep_consistent_poses(poses_poco_parecidas(),
                                                                        0.50f);
  CHECK_EQ(supervivientes.size(), std::size_t{5});
}

// Una pose con un valor no finito (NaN) no se puede comparar: se descarta (fallo
// cerrado), en vez de conservarse como pasaba antes (la comparacion con NaN era falsa).
TEST(el_descarte_descarta_la_pose_con_valores_no_finitos) {
  CapturedStderr log;
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const std::vector<std::vector<float>> poses = {{1.0f, 0.0f, 0.0f, 0.0f},
                                                 {1.0f, 0.0f, 0.0f, 0.0f},
                                                 {1.0f, 0.0f, 0.0f, 0.0f},
                                                 {1.0f, 0.0f, 0.0f, 0.0f},
                                                 {nan, 1.0f, 0.0f, 0.0f}};
  const std::vector<std::size_t> supervivientes = keep_consistent_poses(poses, 0.50f);

  CHECK_EQ(supervivientes.size(), std::size_t{4});
  CHECK(std::find(supervivientes.begin(), supervivientes.end(), std::size_t{4}) ==
        supervivientes.end());
  CHECK_EQ(log.count("pose descartada: frente_2"), std::size_t{1});
}

// Si solo concuerdan dos poses, el descarte deja menos de tres: el registro se cierra
// como inconsistente (sin embedding), con el motivo de siempre.
TEST(el_descarte_deja_menos_de_tres_candidatas_si_solo_concuerdan_dos) {
  const std::vector<std::vector<float>> poses = {{1.0f, 0.0f, 0.0f, 0.0f},
                                                 {1.0f, 0.0f, 0.0f, 0.0f},
                                                 {0.0f, 1.0f, 0.0f, 0.0f},
                                                 {0.0f, 0.0f, 1.0f, 0.0f},
                                                 {0.0f, 0.0f, 0.0f, 1.0f}};
  CHECK(keep_consistent_poses(poses, 0.50f).size() < 3);
}

// Una media sin muestras no es un embedding: lanza.
TEST(el_promedio_sin_muestras_lanza_error) {
  CHECK_THROWS(mean_embedding({{1.0f, 0.0f}}, {}), std::invalid_argument);
}

// Un promedio de embeddings de distintas dimensiones leeria fuera de rango: lanza.
TEST(el_promedio_con_dimensiones_distintas_lanza_error) {
  const std::vector<std::vector<float>> poses = {{1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}};
  CHECK_THROWS(mean_embedding(poses, {0, 1}), std::invalid_argument);
}

// Un grupo coherente de tres poses basta: las dos anomalas se descartan y el registro
// sale con las tres (el descarte se detiene al llegar a tres, porque bajar de tres a dos
// no puede producir un registro valido).
TEST(el_descarte_conserva_un_grupo_coherente_de_tres) {
  const std::vector<std::vector<float>> poses = {{1.0f, 0.0f, 0.0f, 0.0f},
                                                 {1.0f, 0.0f, 0.0f, 0.0f},
                                                 {1.0f, 0.0f, 0.0f, 0.0f},
                                                 {0.0f, 1.0f, 0.0f, 0.0f},
                                                 {0.0f, 0.0f, 1.0f, 0.0f}};
  const std::vector<std::size_t> supervivientes = keep_consistent_poses(poses, 0.50f);
  CHECK_EQ(supervivientes.size(), std::size_t{3});
  CHECK(std::find(supervivientes.begin(), supervivientes.end(), std::size_t{3}) ==
        supervivientes.end());
  CHECK(std::find(supervivientes.begin(), supervivientes.end(), std::size_t{4}) ==
        supervivientes.end());
}

TEST(enrollment_pose_names_follow_the_plan) {
  Harness h;
  h.queue_frames(5);
  h.command(VisionAction::Enroll, "100000001");
  h.service->poll();
  for (int i = 0; i < 5; ++i) h.enrollment_step();

  std::vector<std::string> prompted;
  for (const EnrollmentProgress& p : h.progress()) {
    if (p.status == EnrollmentStatus::WaitingPose) prompted.push_back(p.pose);
  }
  CHECK(prompted == (std::vector<std::string>{"frente", "izquierda", "derecha", "arriba", "frente_2"}));
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
  CHECK(h.progress().back().status == EnrollmentStatus::WaitingPose);

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
  CHECK(h.progress().back().status == EnrollmentStatus::Error);
  CHECK_EQ(h.progress().back().reason.value(), std::string(enrollment_reasons::kInconsistent));
  CHECK_EQ(h.http.requested_urls.size(), std::size_t{2});  // no se reintenta ni se sigue
  CHECK(h.light_commands() == (std::vector<int>{1, 0}));
}

TEST(zero_embedding_for_a_pose_fails_the_enrollment_with_the_npu_motive) {
  Harness h;
  h.embedder.outputs = {{0.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}};
  h.queue_frames(5);
  h.command(VisionAction::Enroll, "100000001");
  h.service->poll();
  h.enrollment_step();

  // Un embedding de norma cero es un fallo de la NPU, no una pose rechazable.
  CHECK(h.progress().back().status == EnrollmentStatus::Error);
  CHECK_EQ(h.progress().back().reason.value(), std::string(enrollment_reasons::kNpu));
  CHECK(h.service->state() == ServiceState::Idle);
  CHECK(h.embeddings().empty());
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
  CHECK_EQ(progress.back().reason.value(), std::string(enrollment_reasons::kCaptureFailure));

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
  CHECK(h.progress().back().status == EnrollmentStatus::Error);
  CHECK_EQ(h.progress().back().reason.value(), std::string(enrollment_reasons::kNpu));
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
    CHECK_THROWS(VisionService(ServiceDeps{h.bus, h.http, h.detector, h.embedder, h.gallery, h.clock,
                                          h.wall_clock},
                               config),
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

TEST(topics_match_the_contract) {
  CHECK(topics::kCameraPir == "aula/camara/pir");
  CHECK(topics::kCameraEnergy == "aula/camara/energia");
  CHECK(topics::kCameraStatus == "aula/camara/estado");
  CHECK(topics::kActuatorCmd == "aula/actuador/cmd");
  CHECK(topics::kActuatorStatus == "aula/actuador/estado");
  CHECK(topics::kVisionResult == "aula/vision/resultado");
  CHECK(topics::kVisionCmd == "aula/vision/cmd");
  CHECK(topics::kVisionEnrollment == "aula/vision/enrolamiento");
  CHECK(topics::kVisionEmbedding == "aula/vision/embedding");
  CHECK(topics::kVisionStatus == "aula/vision/estado");
  CHECK(topics::kVisionGalleryAdd == "aula/vision/galeria/alta");
  CHECK(topics::kVisionGalleryRemove == "aula/vision/galeria/baja");
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
  CHECK_EQ(h.bus.inject(topics::kVisionCmd, VisionCommand{VisionAction::Enroll, "100000001", std::nullopt}), std::size_t{0});
  CHECK(h.bus.published.empty());
}

TEST(constructor_failure_after_first_subscribe_leaves_no_subscription) {
  Harness h;
  h.service.reset();
  h.bus.on_subscribe = [](std::string_view topic) {
    if (topic == topics::kVisionCmd) throw std::runtime_error("broker caido");
  };

  CHECK_THROWS(VisionService(ServiceDeps{h.bus, h.http, h.detector, h.embedder, h.gallery, h.clock,
                                        h.wall_clock},
                             h.config),
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
    if (progress != nullptr && progress->status == EnrollmentStatus::WaitingPose && progress->step == 2) {
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
  CHECK(h.progress().back().status == EnrollmentStatus::Error);
  CHECK_EQ(h.progress().back().reason.value(), std::string(enrollment_reasons::kNpu));
  CHECK(h.light_commands() == (std::vector<int>{1, 0}));

  h.queue_frames(5);  // mas polls no reintentan nada
  for (int i = 0; i < 3; ++i) h.enrollment_step();
  CHECK_EQ(h.http.requested_urls.size(), std::size_t{5});
  CHECK(h.embeddings().empty());
}

// Un registro completo en el que el reloj se rompe DESPUES de aceptarse el enrolar (la
// guarda del final): `break_clock` se aplica tras el primer poll.
template <typename BreakClock>
void run_enrollment_breaking_clock(Harness& h, const std::string& account, BreakClock break_clock) {
  h.queue_frames(5);
  h.command(VisionAction::Enroll, account);
  h.service->poll();
  CHECK(h.service->state() == ServiceState::Enrolling);
  break_clock();
  for (int i = 0; i < 5; ++i) h.enrollment_step();
}

bool has_status(const std::vector<EnrollmentProgress>& progress, EnrollmentStatus status) {
  for (const EnrollmentProgress& p : progress) {
    if (p.status == status) return true;
  }
  return false;
}

// Comprobaciones comunes de un registro cortado por el reloj al final.
void check_clock_guard_failure(Harness& h, const CapturedStderr& log) {
  CHECK(h.service->state() == ServiceState::Idle);
  CHECK(h.embeddings().empty());
  CHECK(!has_status(h.progress(), EnrollmentStatus::Completed));
  CHECK(h.progress().back().status == EnrollmentStatus::Error);
  CHECK_EQ(h.progress().back().reason.value(), std::string(enrollment_reasons::kNpu));
  CHECK(log.count("reloj invalido") >= 1);
  CHECK(h.light_commands() == (std::vector<int>{1, 0}));
}

TEST(registro_con_reloj_vacio_al_final_no_publica_embedding_ni_completado) {
  Harness h;
  CapturedStderr log;
  run_enrollment_breaking_clock(h, "100000002", [&] { h.wall_clock.value = ""; });
  check_clock_guard_failure(h, log);
}

TEST(registro_con_hora_de_1970_al_final_no_publica_embedding_ni_completado) {
  Harness h;
  CapturedStderr log;
  run_enrollment_breaking_clock(
      h, "100000003", [&] { h.wall_clock.value = "1970-01-01T00:00:12.000+00:00"; });
  check_clock_guard_failure(h, log);
  CHECK(log.count("1970-01-01T00:00:12.000+00:00") >= 1);
  CHECK_EQ(log.count("100000003"), std::size_t{0});  // sin datos del alumno en el log
}

TEST(registro_con_reloj_que_lanza_al_final_no_publica_embedding_ni_completado) {
  Harness h;
  CapturedStderr log;
  run_enrollment_breaking_clock(h, "100000004", [&] {
    h.wall_clock.on_now = [] { throw std::runtime_error("sin reloj"); };
  });
  check_clock_guard_failure(h, log);
}

TEST(registro_con_hora_mal_formada_al_final_no_publica_embedding_ni_completado) {
  Harness h;
  CapturedStderr log;
  run_enrollment_breaking_clock(h, "100000005", [&] { h.wall_clock.value = "ayer por la tarde"; });
  check_clock_guard_failure(h, log);
}

TEST(registro_con_hora_del_ultimo_instante_de_1999_falla) {
  Harness h;
  CapturedStderr log;
  run_enrollment_breaking_clock(
      h, "100000007", [&] { h.wall_clock.value = "1999-12-31T23:59:59.999+00:00"; });
  check_clock_guard_failure(h, log);
}

TEST(registro_con_hora_del_primer_instante_de_2000_publica_el_embedding) {
  Harness h;
  run_enrollment_breaking_clock(
      h, "100000008", [&] { h.wall_clock.value = "2000-01-01T00:00:00.000+00:00"; });
  CHECK_EQ(h.embeddings().size(), std::size_t{1});
  CHECK(has_status(h.progress(), EnrollmentStatus::Completed));
}

TEST(registro_con_hora_valida_sigue_publicando_el_embedding) {
  Harness h;
  run_enrollment_breaking_clock(h, "100000006", [] {});
  CHECK_EQ(h.embeddings().size(), std::size_t{1});
  CHECK(h.progress().back().status == EnrollmentStatus::Completed);
}

// El rechazo temprano: con el reloj ya invalido, el enrolar ni empieza.
void check_enroll_rejected_by_clock(Harness& h, const CapturedStderr& log) {
  h.command(VisionAction::Enroll, "100000009");
  h.service->poll();
  CHECK(h.service->state() == ServiceState::Idle);
  CHECK(!ServiceTestAccess::has_enrollment(*h.service));
  CHECK_EQ(h.progress().size(), std::size_t{1});
  CHECK(h.progress()[0].status == EnrollmentStatus::Error);
  CHECK_EQ(h.progress()[0].step, 0);
  CHECK(h.progress()[0].pose.empty());
  CHECK_EQ(h.progress()[0].reason.value(), std::string(enrollment_reasons::kNpu));
  CHECK(h.light_commands().empty());
  CHECK(h.embeddings().empty());
  CHECK(h.http.requested_urls.empty());
  CHECK(log.count("reloj invalido") >= 1);
  CHECK_EQ(log.count("100000009"), std::size_t{0});
}

TEST(enrolar_con_reloj_de_1970_se_rechaza_sin_empezar_el_registro) {
  Harness h;
  h.wall_clock.value = "1970-01-01T00:00:05.000+00:00";
  CapturedStderr log;
  check_enroll_rejected_by_clock(h, log);
}

TEST(enrolar_con_reloj_vacio_se_rechaza_sin_empezar_el_registro) {
  Harness h;
  h.wall_clock.value = "";
  CapturedStderr log;
  check_enroll_rejected_by_clock(h, log);
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

  CHECK_EQ(h.results().at(0).lat_ms.total.value(), 50 + 300);
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
  CHECK_EQ(h.bus.inject(topics::kVisionCmd, VisionCommand{VisionAction::Test, "", std::nullopt}), std::size_t{1});
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
    ServiceTestAccess::handle(*h.service, VisionCommand{VisionAction::Enroll,  bad, std::nullopt}, h.clock.now());
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
  CHECK(h.progress()[0].status == EnrollmentStatus::WaitingPose);
  CHECK_EQ(h.progress()[0].account, std::string("100000001"));
  CHECK(h.progress()[1].status == EnrollmentStatus::Error);
  CHECK_EQ(h.progress()[1].reason.value(), std::string(reasons::kBusy));
  CHECK_EQ(h.progress()[1].account, std::string("200000002"));
  CHECK_EQ(h.progress()[1].step, 0);

  h.clock.advance(h.config.pose_delay);  // el primero sigue su curso
  h.service->poll();
  CHECK(h.progress().back().status == EnrollmentStatus::Accepted || h.progress().back().status == EnrollmentStatus::WaitingPose);
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
  ServiceTestAccess::handle(*h.service, VisionCommand{VisionAction::Enroll, "bad", std::nullopt}, h.clock.now());

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
  CHECK(!h.results()[0].lat_ms.total.has_value());  // sin reloj no hay total medible
  CHECK(h.results()[0].mode == AttendanceMode::Attendance);
  CHECK(h.light_commands() == (std::vector<int>{1, 0}));
}

TEST(enqueue_failures_are_logged_once_per_poll) {
  Harness h;
  CapturedStderr log;
  h.clock.on_now = [] { throw std::runtime_error("reloj roto"); };
  for (int i = 0; i < 5; ++i) CHECK_EQ(h.bus.inject(topics::kVisionCmd, VisionCommand{VisionAction::Cancel, "", std::nullopt}), std::size_t{1});
  CHECK_EQ(log.count("no se pudo encolar un mensaje"), std::size_t{1});
  h.service->poll();
  CHECK_EQ(h.bus.inject(topics::kVisionCmd, VisionCommand{VisionAction::Cancel, "", std::nullopt}), std::size_t{1});
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

// --- Decision, zona gris y umbral (contrato v0.3) -------------------------------------------

// La galeria de pruebas tiene dos alumnos: (1,0,0,0) y (0,1,0,0).
TEST(decision_reconocido_por_encima_del_umbral) {
  Harness h;
  h.embedder.outputs = {{1.0f, 0.0f, 0.0f, 0.0f}};  // coseno 1.0
  h.queue_frames(3);
  h.pir();
  h.service->poll();

  const auto result = h.results().at(0);
  CHECK(result.decision == Decision::Recognized);
  CHECK_EQ(result.account.value(), std::string("320068234"));
  CHECK_NEAR(result.confidence.value(), 1.0, 1e-6);
  CHECK(!result.reason.has_value());
}

TEST(decision_dudoso_en_la_zona_gris) {
  Harness h;
  h.embedder.outputs = {{0.5f, 0.5f, 0.5f, 0.5f}};  // coseno 0.5 con los dos
  h.queue_frames(3);
  h.pir();
  h.service->poll();

  const auto result = h.results().at(0);
  CHECK(result.decision == Decision::Doubtful);
  // Con "dudoso" tambien se manda el mejor candidato, para la auditoria.
  CHECK(result.account.has_value());
  CHECK_NEAR(result.confidence.value(), 0.5, 1e-6);
  CHECK(!result.reason.has_value());
}

TEST(decision_desconocido_por_debajo_del_umbral) {
  Harness h;
  h.embedder.outputs = {{0.0f, 0.0f, 1.0f, 0.0f}};  // coseno 0.0 con los dos
  h.queue_frames(3);
  h.pir();
  h.service->poll();

  const auto result = h.results().at(0);
  CHECK(result.decision == Decision::Unknown);
  CHECK_EQ(result.reason.value(), std::string(reasons::kUnknown));
  CHECK(result.account.has_value());  // el mejor candidato, para la auditoria
  CHECK_NEAR(result.confidence.value(), 0.0, 1e-6);
}

TEST(los_umbrales_mueven_la_zona_gris) {
  Harness h;
  ServiceConfig config = h.config;
  config.min_recognized_score = 0.60f;
  config.min_dudoso_score = 0.45f;
  h.restart(config);
  h.embedder.outputs = {{0.5f, 0.5f, 0.5f, 0.5f}};  // coseno 0.5
  h.queue_frames(9);  // tres pases de lista

  h.pir();
  h.service->poll();
  CHECK(h.results().at(0).decision == Decision::Doubtful);

  config.min_dudoso_score = 0.55f;  // ahora 0.5 queda por debajo
  h.restart(config);
  h.pir();
  h.service->poll();
  CHECK(h.results().at(1).decision == Decision::Unknown);

  config.min_recognized_score = 0.40f;  // ahora 0.5 queda por encima
  config.min_dudoso_score = 0.30f;
  h.restart(config);
  h.pir();
  h.service->poll();
  CHECK(h.results().at(2).decision == Decision::Recognized);
}

TEST(umbrales_invalidos_son_rechazados_al_arrancar) {
  Harness h;
  ServiceConfig config = h.config;
  config.min_dudoso_score = 0.9f;
  config.min_recognized_score = 0.5f;  // el dudoso por encima del reconocido
  CHECK_THROWS(VisionService(ServiceDeps{h.bus, h.http, h.detector, h.embedder, h.gallery, h.clock,
                                        h.wall_clock},
                             config),
               std::invalid_argument);
}

TEST(el_margen_solo_aparece_con_dos_candidatos_o_mas) {
  Harness h;
  h.gallery.remove("320068235");  // queda un solo alumno: no hay con quien comparar
  h.embedder.outputs = {{1.0f, 0.0f, 0.0f, 0.0f}};
  h.queue_frames(3);
  h.pir();
  h.service->poll();

  const auto result = h.results().at(0);
  CHECK(result.decision == Decision::Recognized);
  CHECK(!result.margin.has_value());

  Harness h2;
  h2.embedder.outputs = {{1.0f, 0.0f, 0.0f, 0.0f}};
  h2.queue_frames(3);
  h2.pir();
  h2.service->poll();

  const auto with_two = h2.results().at(0);
  CHECK(with_two.margin.has_value());
  CHECK_NEAR(with_two.margin.value(), 1.0, 1e-6);  // 1.0 contra 0.0
}

TEST(el_margen_es_la_diferencia_con_el_segundo_candidato) {
  Harness h;
  h.gallery.upsert("320068234", std::vector<float>{0.0f, 1.0f, 0.0f, 0.0f});
  h.gallery.upsert("320068235", std::vector<float>{0.6f, 0.8f, 0.0f, 0.0f});
  h.embedder.outputs = {{1.0f, 0.0f, 0.0f, 0.0f}};
  h.queue_frames(3);
  h.pir();
  h.service->poll();

  const auto result = h.results().at(0);
  CHECK_EQ(result.account.value(), std::string("320068235"));
  CHECK_NEAR(result.confidence.value(), 0.6, 1e-6);
  CHECK(result.margin.has_value());
  CHECK_NEAR(result.margin.value(), 0.6, 1e-6);  // 0.6 contra 0.0
}

// --- ts y estado del servicio ------------------------------------------------------------

TEST(todos_los_mensajes_del_servicio_llevan_ts_en_iso_8601_con_zona) {
  Harness h;
  h.queue_frames(5);
  h.command(VisionAction::Enroll, "100000001");
  h.service->poll();
  for (int i = 0; i < 5; ++i) h.enrollment_step();
  h.pir();
  h.service->poll();

  int checked = 0;
  for (const fakes::PublishedMessage& message : h.bus.published) {
    std::string timestamp;
    if (const auto* result = std::get_if<AttendanceResult>(&message.payload)) {
      timestamp = result->timestamp;
    } else if (const auto* progress = std::get_if<EnrollmentProgress>(&message.payload)) {
      timestamp = progress->timestamp;
    } else if (const auto* embedding = std::get_if<EmbeddingMessage>(&message.payload)) {
      timestamp = embedding->timestamp;
    } else if (const auto* status = std::get_if<VisionStatus>(&message.payload)) {
      timestamp = status->timestamp;
    } else {
      continue;  // la luz y el comando no llevan ts
    }
    CHECK(is_iso8601_with_zone(timestamp));
    CHECK_EQ(timestamp, std::string("2026-11-04T09:12:33.120-06:00"));
    ++checked;
  }
  CHECK(checked > 0);
}

TEST(el_estado_del_servicio_cambia_de_modo) {
  Harness h;
  h.queue_frames(3);
  h.pir();
  h.service->poll();

  const auto statuses = h.statuses();
  CHECK_EQ(statuses.size(), std::size_t{2});
  CHECK(statuses[0].mode == VisionMode::Attendance);
  CHECK(statuses[1].mode == VisionMode::Idle);
  for (const VisionStatus& status : statuses) {
    CHECK(status.online);
    CHECK_EQ(status.gallery_size, std::size_t{2});
    CHECK_EQ(status.model, std::string("w600k_mbf-uint8-v1"));
  }
}

TEST(el_estado_del_servicio_durante_el_registro) {
  Harness h;
  h.queue_frames(5);
  h.command(VisionAction::Enroll, "100000001");
  h.service->poll();

  const auto statuses = h.statuses();
  CHECK_EQ(statuses.size(), std::size_t{1});
  CHECK(statuses[0].mode == VisionMode::Enrolling);

  for (int i = 0; i < 5; ++i) h.enrollment_step();
  CHECK(h.statuses().back().mode == VisionMode::Idle);
}

// --- Motivos del enrolamiento y estados ----------------------------------------------------

TEST(el_registro_mapea_los_motivos_al_vocabulario_del_contrato) {
  Harness h;
  h.http.responses.push_back({500, {}});  // la primera captura de la pose 1 falla
  h.queue_frames(5);
  h.command(VisionAction::Enroll, "100000001");
  h.service->poll();
  h.enrollment_step();

  // Una captura fallida se rechaza con "fallo_captura" (no "captura_fallida").
  const auto progress = h.progress();
  CHECK(progress.back().status == EnrollmentStatus::Rejected);
  CHECK_EQ(progress.back().reason.value(), std::string("fallo_captura"));
  CHECK_EQ(progress.back().reason.value(), std::string(enrollment_reasons::kCaptureFailure));
}

TEST(un_rostro_que_no_se_ve_se_rechaza_con_sin_rostro) {
  Harness h;
  h.detector.detections.clear();
  h.queue_frames(5);
  h.command(VisionAction::Enroll, "100000001");
  h.service->poll();
  h.enrollment_step();

  CHECK(h.progress().back().status == EnrollmentStatus::Rejected);
  CHECK_EQ(h.progress().back().reason.value(), std::string("sin_rostro"));
}

TEST(las_ordenes_rechazadas_usan_el_estado_error) {
  Harness h;
  h.command(VisionAction::Enroll, "100000001");
  h.service->poll();
  h.command(VisionAction::Enroll, "200000002");  // ocupado
  h.service->poll();

  const auto progress = h.progress();
  CHECK(progress[1].status == EnrollmentStatus::Error);
  CHECK_EQ(progress[1].reason.value(), std::string("ocupado"));
  CHECK_EQ(progress[1].step, 0);
  CHECK_EQ(progress[1].pose, std::string());
}

TEST(el_estado_completado_se_publica_antes_del_embedding) {
  Harness h;
  h.queue_frames(5);
  h.command(VisionAction::Enroll, "100000001");
  h.service->poll();
  for (int i = 0; i < 5; ++i) h.enrollment_step();

  int completed_at = -1;
  int embedding_at = -1;
  for (std::size_t i = 0; i < h.bus.published.size(); ++i) {
    const fakes::PublishedMessage& message = h.bus.published[i];
    if (message.topic == topics::kVisionEnrollment) {
      const auto* progress = std::get_if<EnrollmentProgress>(&message.payload);
      if (progress != nullptr && progress->status == EnrollmentStatus::Completed) {
        completed_at = static_cast<int>(i);
      }
    }
    if (message.topic == topics::kVisionEmbedding) embedding_at = static_cast<int>(i);
  }
  CHECK(completed_at >= 0);
  CHECK(embedding_at >= 0);
  CHECK(completed_at < embedding_at);
}

TEST(el_nombre_del_comando_enrolar_llega_al_registro) {
  Harness h;
  h.queue_frames(5);
  h.bus.inject(topics::kVisionCmd, VisionCommand{VisionAction::Enroll, "100000001", "Ana López"});
  h.service->poll();

  CHECK_EQ(h.progress()[0].name.value(), std::string("Ana López"));
  CHECK_EQ(h.progress()[0].account, std::string("100000001"));
}

// La salida ordenada: con un SIGTERM en mitad de un registro, la luz se queda
// encendida y el registro a medias. shutdown() cancela el enrolamiento, apaga la luz
// y deja el servicio inactivo.
TEST(shutdown_cancela_el_registro_apaga_la_luz_y_deja_el_servicio_inactivo) {
  Harness h;
  h.command(VisionAction::Enroll, "100000001");
  h.service->poll();
  CHECK(h.service->state() == ServiceState::Enrolling);
  CHECK(h.light_commands() == (std::vector<int>{1}));

  h.service->shutdown();

  // El registro en curso se cancela (el frontend y Node-RED lo ven).
  bool cancelado = false;
  for (const EnrollmentProgress& p : h.progress()) {
    if (p.status == EnrollmentStatus::Cancelled) cancelado = true;
  }
  CHECK(cancelado);

  // La luz se apaga.
  CHECK(h.light_commands() == (std::vector<int>{1, 0}));

  // Y el servicio queda inactivo: no queda un registro a medias.
  CHECK(h.service->state() == ServiceState::Idle);
}

// La salida ordenada sin enrolamiento en curso: la luz se apaga igual (puede haber
// quedado encendida por un pase de lista).
TEST(shutdown_sin_registro_apaga_la_luz) {
  Harness h;
  h.service->shutdown();
  CHECK(h.light_commands() == (std::vector<int>{0}));
  CHECK(h.service->state() == ServiceState::Idle);
  CHECK(h.progress().empty());
}

// La excepcion del contrato de Subscription: si la baja se pide desde el propio
// manejador, no se espera (el manejador no puede terminar hasta que la baja vuelva) y
// se avisa por stderr. El bus real hace lo mismo (ver remove_subscriber en
// src/mqtt_bus.cpp).
TEST(la_baja_desde_el_propio_manejador_no_se_bloquea) {
  fakes::FakeMessageBus bus;
  auto subscription = std::make_shared<Subscription>();
  *subscription = bus.subscribe(topics::kCameraPir, [&](const Payload&) {
    subscription->reset();  // baja desde el propio manejador
  });

  CHECK_EQ(bus.inject(topics::kCameraPir, PirEvent{}), std::size_t{1});
  // La baja ya se aplico: el manejador no vuelve a ejecutarse.
  CHECK_EQ(bus.inject(topics::kCameraPir, PirEvent{}), std::size_t{0});
}
