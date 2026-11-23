#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <memory>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "minitest.hpp"
#include "vision/alignment.hpp"
#include "gcm_apoyo.hpp"
#include "vision/captura_firma.hpp"
#include "vision/fakes.hpp"
#include "vision/json_payload.hpp"
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

// La misma cara, pero con el recuadro que usa la miniatura.
FaceDetection face_con_bbox() {
  FaceDetection face = face_in_frame();
  face.bbox = cv::Rect2f(120.0f, 60.0f, 80.0f, 80.0f);
  return face;
}

// Una foto lisa (un disco claro): la miniatura de una foto de ruido no siempre cabe
// en el tope del contrato, y esta prueba no va de eso.
cv::Mat frame_liso() {
  cv::Mat imagen(240, 320, CV_8UC3, cv::Scalar(170, 170, 170));
  cv::circle(imagen, cv::Point(160, 120), 60, cv::Scalar(215, 215, 215), -1);
  return imagen;
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
  fakes::FakeAnclasReloj anchors;      // RTC, id de arranque y archivo de estado simulados
  AnclasReloj* anchors_ptr = nullptr;  // las pruebas del reloj lo ponen en &anchors
  ServiceConfig config;
  // Dice si la galeria es de fiar (GalleryStore::loaded_ok en produccion). Vacia = siempre.
  std::function<bool()> gallery_ready;
  std::unique_ptr<VisionService> service;

  explicit Harness(double min_sharpness = 0.0) {
    config.capture_url = kCaptureUrl;
    config.burst_size = 3;
    config.accept_png = true;  // las fotos de prueba son PNG
    config.min_sharpness = min_sharpness;
    detector.detections = {face_in_frame()};
    // En el registro, la cabeza esta en la pose que se pide (las pruebas de la verificacion
    // de pose cambian esto a mano).
    fakes::follow_enrollment_pose(detector, bus);
    embedder.outputs = {{1.0f, 0.0f, 0.0f, 0.0f}};
    gallery.upsert("320068234", std::vector<float>{1.0f, 0.0f, 0.0f, 0.0f});
    gallery.upsert("320068235", std::vector<float>{0.0f, 1.0f, 0.0f, 0.0f});
    service = std::make_unique<VisionService>(
        ServiceDeps{bus, http, detector, embedder, gallery, clock, wall_clock, anchors_ptr,
                    [this] { return gallery_ready ? gallery_ready() : true; }},
        config);
  }

  // Reconstruye el servicio con otra configuracion (la anterior se da de baja primero).
  void restart(const ServiceConfig& new_config) {
    service.reset();
    config = new_config;
    service = std::make_unique<VisionService>(
        ServiceDeps{bus, http, detector, embedder, gallery, clock, wall_clock, anchors_ptr,
                    [this] { return gallery_ready ? gallery_ready() : true; }},
        config);
  }

  void queue_frames(int count) {
    for (int i = 0; i < count; ++i) http.responses.push_back(ok(noise_frame(i + 1)));
  }

  void pir() { bus.inject(topics::kCameraPir, PirEvent{}); }
  void command(VisionAction action, std::string account = "") {
    bus.inject(topics::kVisionCmd, VisionCommand{action, std::move(account), std::nullopt});
  }

  // Ordena enrolar una cuenta con la galeria de las pruebas de pase de lista VACIA: el registro
  // rechaza un rostro que ya esta en la galeria con otra cuenta (duplicado), y las dos cuentas
  // del Harness tienen los mismos embeddings que el embedder falso da en el registro. Las
  // pruebas de duplicados y de galeria usan command(VisionAction::Enroll, ...) o agregan sus
  // cuentas despues de este comando.
  void enroll(std::string account) {
    gallery.remove("320068234");
    gallery.remove("320068235");
    command(VisionAction::Enroll, std::move(account));
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

  h.enroll("999000111");
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

// Corre un registro completo de cinco poses con los embeddings dados. Estas pruebas son del
// descarte FINAL de outliers, que es la red de seguridad: se apaga la comprobacion de
// identidad por pose (umbrales en 0), que si no rechazaria antes las poses que aqui se
// quieren ver llegar al descarte.
RegistroFinal correr_registro(Harness& h, const std::string& cuenta,
                               const std::vector<std::vector<float>>& poses) {
  ServiceConfig config = h.config;
  config.min_front_similarity = 0.0f;
  config.min_front_2_similarity = 0.0f;
  h.restart(config);
  h.embedder.outputs = poses;
  h.queue_frames(5);
  h.enroll(cuenta);
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

// --- Verificacion de la pose y de la identidad en el registro ----------------------------

namespace {

// Que se decidio en cada captura del registro, en orden: "pose:aceptada" o
// "pose:rechazada:motivo".
std::vector<std::string> decisiones(const Harness& h) {
  std::vector<std::string> out;
  for (const EnrollmentProgress& p : h.progress()) {
    if (p.status == EnrollmentStatus::Accepted) {
      out.push_back(p.pose + ":aceptada");
    } else if (p.status == EnrollmentStatus::Rejected) {
      out.push_back(p.pose + ":rechazada:" + p.reason.value_or("?"));
    }
  }
  return out;
}

// Abre un registro y deja listo el turno de la primera pose.
void abrir_registro(Harness& h, int fotos = 12) {
  h.queue_frames(fotos);
  h.enroll("999000111");
  h.service->poll();
}

// La cabeza queda fija en esta pose (grados) en todas las capturas siguientes, sin importar
// la que pide el registro.
void cabeza_fija(Harness& h, double yaw, double pitch) {
  h.detector.adjust = [yaw, pitch](std::vector<FaceDetection>& faces) {
    for (FaceDetection& face : faces) face.landmarks = fakes::synthetic_landmarks(yaw, pitch);
  };
}

void cabeza_sigue_la_pose(Harness& h) { fakes::follow_enrollment_pose(h.detector, h.bus); }

// Un rostro de frente, con recuadro, de `lado` px de ancho y centrado en (cx, cy).
FaceDetection rostro_de(float cx, float cy, float lado, float score) {
  FaceDetection face;
  face.score = score;
  face.bbox = cv::Rect2f(cx - lado / 2, cy - lado / 2, lado, lado);
  face.landmarks = fakes::synthetic_landmarks(0.0, 0.0, lado / 80.0, {cx, cy - lado * 0.15f});
  return face;
}

// Un embedding unitario de 4 componentes con coseno `c` contra (1, 0, 0, 0). `lado` elige la
// segunda componente: asi se arman poses con la similitud que se quiere.
std::vector<float> con_coseno(float c, float lado = 1.0f) {
  return {c, lado * std::sqrt(1.0f - c * c), 0.0f, 0.0f};
}

}  // namespace

TEST(el_registro_acepta_cada_pose_cuando_la_cabeza_esta_en_ella) {
  Harness h;
  CapturedStderr log;
  abrir_registro(h);
  for (int i = 0; i < 5; ++i) h.enrollment_step();
  CHECK(decisiones(h) == (std::vector<std::string>{"frente:aceptada", "izquierda:aceptada",
                                                    "derecha:aceptada", "arriba:aceptada",
                                                    "frente_2:aceptada"}));
  CHECK_EQ(h.embeddings().size(), std::size_t{1});
  // Cada pose aceptada deja sus medidas para calibrar.
  CHECK_EQ(log.count("pose aceptada: izquierda"), std::size_t{1});
  CHECK(log.count("yaw") >= 5);
}

TEST(una_pose_que_no_se_gira_sale_pose_incorrecta_y_se_repite) {
  Harness h;
  CapturedStderr log;
  abrir_registro(h);
  h.enrollment_step();  // frente, aceptada
  cabeza_fija(h, 0.0, 0.0);  // el alumno no gira
  h.enrollment_step();
  CHECK(decisiones(h) ==
        (std::vector<std::string>{"frente:aceptada", "izquierda:rechazada:pose_incorrecta"}));
  CHECK(h.service->state() == ServiceState::Enrolling);
  CHECK_EQ(h.embedder.calls, std::size_t{1});  // solo "frente": la pose rechazada no gasta NPU
  CHECK(h.progress().back().status == EnrollmentStatus::Rejected);
  CHECK_EQ(h.progress().back().pose, std::string("izquierda"));  // la misma pose
  // El rechazo deja los valores medidos por stderr, para calibrar.
  CHECK_EQ(log.count("pose rechazada: izquierda (motivo: pose_incorrecta"), std::size_t{1});
  CHECK(log.count("yaw ") >= 1);
  CHECK(log.count("pitch") >= 1);

  cabeza_sigue_la_pose(h);  // ahora si gira
  h.enrollment_step();
  CHECK(decisiones(h).back() == "izquierda:aceptada");
  CHECK_EQ(h.progress().back().pose, std::string("derecha"));
}

TEST(girar_hacia_el_lado_contrario_es_pose_incorrecta) {
  Harness h;
  abrir_registro(h);
  h.enrollment_step();
  cabeza_fija(h, -30.0, 0.0);  // gira a la derecha cuando se pide la izquierda
  h.enrollment_step();
  CHECK(decisiones(h).back() == "izquierda:rechazada:pose_incorrecta");
}

TEST(invertir_el_giro_cambia_el_sentido_de_izquierda_y_derecha) {
  Harness h;
  ServiceConfig config = h.config;
  config.pose_limits.invert_turn = true;
  h.restart(config);
  abrir_registro(h);
  h.enrollment_step();
  cabeza_fija(h, -30.0, 0.0);  // con la imagen en espejo, este es el giro de "izquierda"
  h.enrollment_step();
  CHECK(decisiones(h).back() == "izquierda:aceptada");
  h.enrollment_step();  // derecha, con la cabeza todavia a -30: ahora es el sentido contrario
  CHECK(decisiones(h).back() == "derecha:rechazada:pose_incorrecta");
}

TEST(un_giro_exagerado_sale_giro_excesivo) {
  Harness h;
  CapturedStderr log;
  abrir_registro(h);
  h.enrollment_step();
  cabeza_fija(h, 70.0, 0.0);
  h.enrollment_step();
  CHECK(decisiones(h).back() == "izquierda:rechazada:giro_excesivo");
  CHECK_EQ(log.count("pose rechazada: izquierda (motivo: giro_excesivo"), std::size_t{1});
}

TEST(arriba_sin_levantar_la_cara_sale_pose_incorrecta) {
  Harness h;
  abrir_registro(h);
  h.enrollment_step();
  h.enrollment_step();
  h.enrollment_step();  // frente, izquierda y derecha aceptadas
  cabeza_fija(h, 0.0, 0.0);
  h.enrollment_step();
  CHECK(decisiones(h).back() == "arriba:rechazada:pose_incorrecta");
  cabeza_fija(h, 0.0, -20.0);  // mirando hacia abajo tampoco
  h.enrollment_step();
  CHECK(decisiones(h).back() == "arriba:rechazada:pose_incorrecta");
  cabeza_fija(h, 0.0, 25.0);
  h.enrollment_step();
  CHECK(decisiones(h).back() == "arriba:aceptada");
}

TEST(el_cabeceo_de_arriba_se_mide_contra_el_de_frente) {
  Harness h;
  abrir_registro(h);
  // La camara esta alta: "frente" se mide a -15 grados y mirar de frente otra vez tambien.
  cabeza_fija(h, 0.0, -15.0);
  h.enrollment_step();
  CHECK(decisiones(h).back() == "frente:aceptada");
  h.detector.adjust = [](std::vector<FaceDetection>& faces) {
    for (FaceDetection& face : faces) face.landmarks = fakes::synthetic_landmarks(30.0, -15.0);
  };
  h.enrollment_step();  // izquierda
  h.detector.adjust = [](std::vector<FaceDetection>& faces) {
    for (FaceDetection& face : faces) face.landmarks = fakes::synthetic_landmarks(-30.0, -15.0);
  };
  h.enrollment_step();  // derecha
  cabeza_fija(h, 0.0, -15.0);  // sigue igual que en "frente": no levanto la cara
  h.enrollment_step();
  CHECK(decisiones(h).back() == "arriba:rechazada:pose_incorrecta");
  cabeza_fija(h, 0.0, 5.0);  // 20 grados mas que en "frente"
  h.enrollment_step();
  CHECK(decisiones(h).back() == "arriba:aceptada");
  cabeza_fija(h, 0.0, -15.0);  // "frente_2" se compara con "frente", no con el cero absoluto
  h.enrollment_step();
  CHECK(decisiones(h).back() == "frente_2:aceptada");
}

TEST(frente_2_tambien_exige_estar_de_frente) {
  Harness h;
  abrir_registro(h);
  for (int i = 0; i < 4; ++i) h.enrollment_step();
  cabeza_fija(h, 30.0, 0.0);
  h.enrollment_step();
  CHECK(decisiones(h).back() == "frente_2:rechazada:pose_incorrecta");
}

TEST(la_primera_pose_tambien_se_verifica) {
  Harness h;
  CapturedStderr log;
  abrir_registro(h);
  cabeza_fija(h, 30.0, 0.0);
  h.enrollment_step();
  CHECK(decisiones(h) == (std::vector<std::string>{"frente:rechazada:pose_incorrecta"}));
  CHECK_EQ(log.count("pose rechazada: frente"), std::size_t{1});
}

TEST(las_poses_rechazadas_por_pose_cuentan_para_los_intentos_y_abortan_el_registro) {
  Harness h;
  ServiceConfig config = h.config;
  config.max_attempts_per_pose = 3;
  h.restart(config);
  abrir_registro(h);
  cabeza_fija(h, 40.0, 0.0);  // "frente" con la cabeza girada, siempre
  for (int i = 0; i < 3; ++i) h.enrollment_step();
  CHECK_EQ(decisiones(h).size(), std::size_t{3});
  CHECK(h.service->state() == ServiceState::Idle);
  CHECK(h.progress().back().status == EnrollmentStatus::Error);
  // El error lleva el motivo del ULTIMO rechazo, no "npu" (que es de fallos del pipeline).
  CHECK_EQ(h.progress().back().reason.value(), std::string(enrollment_reasons::kWrongPose));
  CHECK(h.embeddings().empty());
  CHECK(h.light_commands() == (std::vector<int>{1, 0}));
}

// --- Varios rostros ---

TEST(dos_rostros_de_tamano_parecido_rechazan_la_pose_con_varios_rostros) {
  Harness h;
  CapturedStderr log;
  h.detector.adjust = nullptr;
  h.detector.detections = {rostro_de(100.0f, 120.0f, 80.0f, 0.95f),
                           rostro_de(240.0f, 120.0f, 76.0f, 0.90f)};
  abrir_registro(h);
  h.enrollment_step();
  CHECK(decisiones(h) == (std::vector<std::string>{"frente:rechazada:varios_rostros"}));
  CHECK_EQ(h.embedder.calls, std::size_t{0});  // ni siquiera se calcula el embedding
  CHECK_EQ(log.count("pose rechazada: frente (motivo: varios_rostros"), std::size_t{1});
  CHECK(h.progress().back().status == EnrollmentStatus::Rejected);
  CHECK(h.service->state() == ServiceState::Enrolling);
}

TEST(un_rostro_claramente_pequeno_de_fondo_se_ignora) {
  Harness h;
  h.detector.adjust = nullptr;
  // Area 24x24 contra 80x80: 0.09 de la principal, por debajo de 0.35.
  h.detector.detections = {rostro_de(100.0f, 120.0f, 80.0f, 0.95f),
                           rostro_de(260.0f, 60.0f, 24.0f, 0.90f)};
  abrir_registro(h);
  h.enrollment_step();
  CHECK(decisiones(h) == (std::vector<std::string>{"frente:aceptada"}));
}

TEST(una_deteccion_de_score_bajo_se_ignora_aunque_sea_grande) {
  Harness h;
  h.detector.adjust = nullptr;
  h.detector.detections = {rostro_de(100.0f, 120.0f, 80.0f, 0.95f),
                           rostro_de(240.0f, 120.0f, 80.0f, 0.45f)};
  abrir_registro(h);
  h.enrollment_step();
  CHECK(decisiones(h) == (std::vector<std::string>{"frente:aceptada"}));
}

TEST(en_el_registro_la_principal_es_la_de_mayor_area_y_no_la_de_mayor_score) {
  Harness h;
  h.detector.adjust = nullptr;
  // El rostro de fondo (chico) tiene mejor score y la cabeza girada; el del alumno es el
  // grande, de frente. Si se tomara el de mayor score la pose de frente saldria rechazada.
  FaceDetection fondo = rostro_de(260.0f, 60.0f, 24.0f, 0.99f);
  fondo.landmarks = fakes::synthetic_landmarks(40.0, 0.0, 0.3, {260.0f, 56.0f});
  h.detector.detections = {fondo, rostro_de(100.0f, 120.0f, 80.0f, 0.80f)};
  abrir_registro(h);
  h.enrollment_step();
  CHECK(decisiones(h) == (std::vector<std::string>{"frente:aceptada"}));
}

TEST(en_el_pase_de_lista_se_sigue_tomando_el_rostro_de_mayor_score) {
  Harness h;
  h.detector.adjust = nullptr;
  h.detector.detections = {rostro_de(100.0f, 120.0f, 80.0f, 0.80f),
                           rostro_de(240.0f, 120.0f, 76.0f, 0.95f)};
  h.embedder.outputs = {{1.0f, 0.0f, 0.0f, 0.0f}};
  h.queue_frames(3);
  h.pir();
  h.service->poll();
  CHECK_EQ(h.results().size(), std::size_t{1});
  CHECK(h.results()[0].decision == Decision::Recognized);  // dos rostros no son un rechazo aqui
}

// --- Identidad contra la pose "frente" ---

TEST(una_pose_de_otra_persona_sale_rechazada_inconsistente_y_el_registro_sigue) {
  Harness h;
  CapturedStderr log;
  // frente; izquierda de "otra persona" (coseno 0 con frente) y despues la buena.
  h.embedder.outputs = {{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, con_coseno(0.6f)};
  abrir_registro(h);
  h.enrollment_step();
  h.enrollment_step();
  CHECK(decisiones(h) ==
        (std::vector<std::string>{"frente:aceptada", "izquierda:rechazada:inconsistente"}));
  CHECK(h.service->state() == ServiceState::Enrolling);  // no se aborta
  CHECK(h.progress().back().status == EnrollmentStatus::Rejected);
  CHECK_EQ(h.progress().back().pose, std::string("izquierda"));
  CHECK_EQ(log.count("pose rechazada: izquierda (motivo: inconsistente"), std::size_t{1});
  CHECK(log.count("coseno con frente 0.00") >= 1);

  h.enrollment_step();
  CHECK(decisiones(h).back() == "izquierda:aceptada");
}

TEST(el_rechazo_por_identidad_cuenta_para_los_intentos_y_agota_el_registro) {
  Harness h;
  ServiceConfig config = h.config;
  config.max_attempts_per_pose = 2;
  h.restart(config);
  h.embedder.outputs = {{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}};  // repite la ultima
  abrir_registro(h);
  for (int i = 0; i < 3; ++i) h.enrollment_step();
  CHECK(decisiones(h) == (std::vector<std::string>{"frente:aceptada",
                                                    "izquierda:rechazada:inconsistente",
                                                    "izquierda:rechazada:inconsistente"}));
  CHECK(h.service->state() == ServiceState::Idle);
  CHECK(h.progress().back().status == EnrollmentStatus::Error);
  CHECK_EQ(h.progress().back().reason.value(), std::string(enrollment_reasons::kInconsistent));
  CHECK(h.embeddings().empty());
}

TEST(la_identidad_se_compara_con_frente_y_no_con_la_pose_anterior) {
  Harness h;
  // izquierda y derecha tienen coseno 0.6 con frente pero -0.28 entre si.
  h.embedder.outputs = {{1.0f, 0.0f, 0.0f, 0.0f}, con_coseno(0.6f, 1.0f), con_coseno(0.6f, -1.0f)};
  abrir_registro(h);
  for (int i = 0; i < 3; ++i) h.enrollment_step();
  CHECK(decisiones(h) == (std::vector<std::string>{"frente:aceptada", "izquierda:aceptada",
                                                    "derecha:aceptada"}));
}

TEST(el_umbral_de_identidad_de_las_poses_de_perfil_deja_pasar_un_coseno_de_0_3) {
  Harness h;
  // Una ESTIMACION, sin medir (lo unico medido es que dos personas distintas dan menos de 0.1):
  // la misma persona de perfil daria de 0.3 a 0.5 con la de frente.
  h.embedder.outputs = {{1.0f, 0.0f, 0.0f, 0.0f}, con_coseno(0.3f), con_coseno(0.3f, -1.0f),
                        con_coseno(0.3f, 1.0f)};
  abrir_registro(h);
  for (int i = 0; i < 4; ++i) h.enrollment_step();
  CHECK(decisiones(h) == (std::vector<std::string>{"frente:aceptada", "izquierda:aceptada",
                                                    "derecha:aceptada", "arriba:aceptada"}));
}

TEST(frente_2_exige_mas_parecido_a_frente_que_las_demas_poses) {
  Harness h;
  // frente_2 con coseno 0.3: bueno para una pose de perfil, insuficiente para la misma pose.
  h.embedder.outputs = {{1.0f, 0.0f, 0.0f, 0.0f}, con_coseno(0.9f), con_coseno(0.9f, -1.0f),
                        con_coseno(0.9f), con_coseno(0.3f), con_coseno(0.8f)};
  abrir_registro(h);
  for (int i = 0; i < 6; ++i) h.enrollment_step();
  const std::vector<std::string> d = decisiones(h);
  CHECK_EQ(d.size(), std::size_t{6});
  CHECK_EQ(d[4], std::string("frente_2:rechazada:inconsistente"));
  CHECK_EQ(d[5], std::string("frente_2:aceptada"));
}

TEST(con_los_umbrales_de_identidad_en_cero_no_se_comprueba_la_identidad) {
  Harness h;
  ServiceConfig config = h.config;
  config.min_front_similarity = 0.0f;
  config.min_front_2_similarity = 0.0f;
  h.restart(config);
  h.embedder.outputs = {{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}};
  abrir_registro(h);
  h.enrollment_step();
  h.enrollment_step();
  CHECK(decisiones(h).back() == "izquierda:aceptada");
}

TEST(un_embedding_de_otra_dimension_sigue_abortando_antes_de_comparar_identidad) {
  Harness h;
  h.embedder.outputs = {{1.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
  abrir_registro(h);
  h.enrollment_step();
  h.enrollment_step();
  CHECK(h.service->state() == ServiceState::Idle);
  CHECK(h.progress().back().status == EnrollmentStatus::Error);
  CHECK_EQ(h.progress().back().reason.value(), std::string(enrollment_reasons::kInconsistent));
}

TEST(la_pose_se_verifica_antes_que_la_identidad) {
  Harness h;
  CapturedStderr log;
  h.embedder.outputs = {{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}};
  abrir_registro(h);
  h.enrollment_step();
  cabeza_fija(h, 0.0, 0.0);  // ni gira ni se parece
  h.enrollment_step();
  CHECK(decisiones(h).back() == "izquierda:rechazada:pose_incorrecta");
}

TEST(la_configuracion_de_pose_e_identidad_invalida_se_rechaza_al_arrancar) {
  Harness h;
  const auto invalida = [&](const std::function<void(ServiceConfig&)>& cambio) {
    ServiceConfig config = h.config;
    cambio(config);
    h.service.reset();
    CHECK_THROWS(VisionService(ServiceDeps{h.bus, h.http, h.detector, h.embedder, h.gallery,
                                           h.clock, h.wall_clock, nullptr},
                               config),
                 std::invalid_argument);
  };
  const float nan = std::numeric_limits<float>::quiet_NaN();
  invalida([](ServiceConfig& c) { c.min_front_similarity = -0.1f; });
  invalida([](ServiceConfig& c) { c.min_front_similarity = 1.1f; });
  invalida([&](ServiceConfig& c) { c.min_front_similarity = nan; });
  invalida([](ServiceConfig& c) { c.min_front_2_similarity = 1.5f; });
  invalida([&](ServiceConfig& c) { c.min_front_2_similarity = nan; });
  // frente_2 es la misma pose que frente: no puede ser mas laxo.
  invalida([](ServiceConfig& c) {
    c.min_front_similarity = 0.4f;
    c.min_front_2_similarity = 0.3f;
  });
  invalida([](ServiceConfig& c) { c.other_face_min_area_ratio = 0.0f; });
  invalida([](ServiceConfig& c) { c.other_face_min_area_ratio = 1.5f; });
  invalida([&](ServiceConfig& c) { c.other_face_min_area_ratio = nan; });
  invalida([](ServiceConfig& c) { c.other_face_min_score = -0.5f; });
  invalida([&](ServiceConfig& c) { c.other_face_min_score = nan; });
  invalida([&](ServiceConfig& c) { c.pose_limits.max_yaw_deg = nan; });
  invalida([](ServiceConfig& c) { c.pose_limits.min_lateral_yaw_deg = 0.0f; });
  invalida([](ServiceConfig& c) { c.pose_limits.max_pitch_deg = c.pose_limits.min_up_pitch_deg; });
  // Reglas nuevas de los limites.
  invalida([](ServiceConfig& c) { c.pose_limits.min_lateral_yaw_deg = c.pose_limits.max_frontal_yaw_deg; });
  invalida([](ServiceConfig& c) { c.pose_limits.min_up_pitch_deg = 4.0f; });
  invalida([](ServiceConfig& c) { c.pose_limits.max_yaw_deg = 90.0f; });
  invalida([](ServiceConfig& c) { c.pose_limits.max_pitch_deg = 90.0f; });
  invalida([](ServiceConfig& c) {
    c.pose_limits.max_first_pitch_deg = c.pose_limits.max_frontal_pitch_deg - 1.0f;
  });
  // Controles casi apagados y tope de identidad.
  invalida([](ServiceConfig& c) { c.other_face_min_area_ratio = 0.81f; });
  invalida([](ServiceConfig& c) { c.other_face_min_score = 0.91f; });
  invalida([](ServiceConfig& c) { c.max_identity_rejections = 0; });
  // Umbral de duplicado: en (0, min_dudoso_score].
  invalida([](ServiceConfig& c) { c.min_duplicate_similarity = 0.0f; });
  invalida([](ServiceConfig& c) { c.min_duplicate_similarity = c.min_dudoso_score + 0.01f; });
  invalida([&](ServiceConfig& c) { c.min_duplicate_similarity = nan; });
  invalida([](ServiceConfig& c) { c.pose_limits.max_reference_pitch_deg = 0.0f; });
  invalida([](ServiceConfig& c) { c.pose_limits.max_reference_pitch_deg = 90.0f; });
}

// --- Duplicados en la galeria -------------------------------------------------------------

namespace {

// Un embedding unitario de 4 componentes con coseno `c` contra (1, 0, 0, 0), en el eje `eje`
// (1 a 3): poses con la similitud que se quiere y distintas entre si.
std::vector<float> en_eje(float c, int eje) {
  std::vector<float> v(4, 0.0f);
  v[0] = c;
  v[static_cast<std::size_t>(eje)] = std::sqrt(1.0f - c * c);
  return v;
}

// Corre las 5 poses de un registro abierto con abrir_registro.
void correr_cinco_poses(Harness& h) {
  for (int i = 0; i < 5; ++i) h.enrollment_step();
}

}  // namespace

TEST(un_rostro_que_ya_esta_en_la_galeria_con_otra_cuenta_cierra_como_duplicado) {
  Harness h;
  CapturedStderr log;
  abrir_registro(h);
  h.gallery.upsert("320068999", std::vector<float>{1.0f, 0.0f, 0.0f, 0.0f});  // el mismo rostro
  correr_cinco_poses(h);

  CHECK(h.service->state() == ServiceState::Idle);
  const auto progress = h.progress();
  CHECK(progress.back().status == EnrollmentStatus::Error);
  CHECK_EQ(progress.back().reason.value(), std::string(enrollment_reasons::kDuplicate));
  CHECK_EQ(progress.back().similar_to.value(), std::string("320068999"));
  for (const EnrollmentProgress& p : progress) CHECK(p.status != EnrollmentStatus::Completed);
  CHECK(h.embeddings().empty());  // no se publica el embedding
  CHECK(h.light_commands() == (std::vector<int>{1, 0}));
  CHECK_EQ(log.count("ya esta registrado con otra cuenta"), std::size_t{1});
  CHECK_EQ(log.count("320068999"), std::size_t{0});  // el log no lleva la cuenta
  CHECK_EQ(log.count("coseno 1.00"), std::size_t{1});
}

TEST(un_registro_que_cierra_como_duplicado_lleva_parecido_a_en_el_json_del_contrato) {
  Harness h;
  abrir_registro(h);
  h.gallery.upsert("320068999", std::vector<float>{1.0f, 0.0f, 0.0f, 0.0f});
  correr_cinco_poses(h);
  const std::optional<std::string> doc = json::serialize(h.progress().back());
  CHECK(doc.has_value());
  CHECK(doc->find("\"estado\":\"error\"") != std::string::npos);
  CHECK(doc->find("\"motivo\":\"duplicado\"") != std::string::npos);
  CHECK(doc->find("\"parecido_a\":\"320068999\"") != std::string::npos);
  CHECK(json::deserialize(topics::kVisionEnrollment, *doc).ok());
}

TEST(la_propia_cuenta_en_la_galeria_puede_re_registrarse) {
  Harness h;
  CapturedStderr log;
  abrir_registro(h);  // la cuenta es 999000111
  h.gallery.upsert("999000111", std::vector<float>{1.0f, 0.0f, 0.0f, 0.0f});
  correr_cinco_poses(h);
  CHECK(h.progress().back().status == EnrollmentStatus::Completed);
  CHECK_EQ(h.embeddings().size(), std::size_t{1});
  CHECK_EQ(log.count("no hay otras cuentas en la galeria"), std::size_t{1});  // sin otras: se dice
}

TEST(el_duplicado_tiene_umbral_propio_y_un_parecido_de_0_50_ya_bloquea) {
  {  // 0.40 < 0.45: no es duplicado
    Harness h;
    abrir_registro(h);
    h.gallery.upsert("320068999", en_eje(0.40f, 1));
    correr_cinco_poses(h);
    CHECK(h.progress().back().status == EnrollmentStatus::Completed);
    CHECK_EQ(h.embeddings().size(), std::size_t{1});
  }
  {  // 0.50 >= 0.45 aunque este por debajo de min_recognized_score (0.60): una misma persona
     // no puede tener dos cuentas con parecidos que el pase de lista ya trata como "dudoso"
    Harness h;
    abrir_registro(h);
    h.gallery.upsert("320068999", en_eje(0.50f, 1));
    correr_cinco_poses(h);
    CHECK_EQ(h.progress().back().reason.value(), std::string(enrollment_reasons::kDuplicate));
    CHECK_EQ(h.progress().back().similar_to.value(), std::string("320068999"));
    CHECK(h.embeddings().empty());
  }
}

TEST(el_umbral_de_duplicado_es_configurable_y_no_pasa_de_min_dudoso_score) {
  {  // mas laxo (0.55, con "dudoso" en 0.55): 0.50 ya no bloquea
    Harness h;
    ServiceConfig config = h.config;
    config.min_dudoso_score = 0.55f;
    config.min_duplicate_similarity = 0.55f;
    h.restart(config);
    abrir_registro(h);
    h.gallery.upsert("320068999", en_eje(0.50f, 1));
    correr_cinco_poses(h);
    CHECK(h.progress().back().status == EnrollmentStatus::Completed);
  }
  {  // mas estricto (0.30): 0.40 bloquea
    Harness h;
    ServiceConfig config = h.config;
    config.min_duplicate_similarity = 0.30f;
    h.restart(config);
    abrir_registro(h);
    h.gallery.upsert("320068999", en_eje(0.40f, 1));
    correr_cinco_poses(h);
    CHECK_EQ(h.progress().back().reason.value(), std::string(enrollment_reasons::kDuplicate));
  }
}

TEST(con_la_galeria_vacia_tras_una_carga_correcta_el_registro_sigue) {
  Harness h;
  CapturedStderr log;
  abrir_registro(h);
  CHECK_EQ(h.gallery.size(), std::size_t{0});
  correr_cinco_poses(h);
  CHECK(h.progress().back().status == EnrollmentStatus::Completed);
  CHECK_EQ(h.embeddings().size(), std::size_t{1});
  CHECK_EQ(log.count("no hay otras cuentas en la galeria"), std::size_t{1});
}

TEST(una_excepcion_al_comparar_con_la_galeria_falla_cerrado_con_galeria_no_disponible) {
  Harness h;
  CapturedStderr log;
  abrir_registro(h);
  h.gallery.upsert("320068999", std::vector<float>{1.0f, 0.0f});  // la galeria es de dimension 2
  correr_cinco_poses(h);
  CHECK(h.service->state() == ServiceState::Idle);
  const auto progress = h.progress();
  CHECK(progress.back().status == EnrollmentStatus::Error);
  CHECK_EQ(progress.back().reason.value(), std::string(enrollment_reasons::kGalleryUnavailable));
  for (const EnrollmentProgress& p : progress) CHECK(p.status != EnrollmentStatus::Completed);
  CHECK(h.embeddings().empty());
  CHECK(h.light_commands() == (std::vector<int>{1, 0}));
  CHECK_EQ(log.count("no se pudo comprobar si el rostro ya esta registrado"), std::size_t{1});
}

TEST(sin_una_galeria_cargada_el_registro_se_rechaza_antes_de_empezar) {
  Harness h;
  CapturedStderr log;
  h.gallery_ready = [] { return false; };  // la ultima recarga fallo
  h.queue_frames(5);
  h.enroll("999000111");
  h.service->poll();
  CHECK(h.service->state() == ServiceState::Idle);
  const auto progress = h.progress();
  CHECK_EQ(progress.size(), std::size_t{1});
  CHECK(progress[0].status == EnrollmentStatus::Error);
  CHECK_EQ(progress[0].reason.value(), std::string(enrollment_reasons::kGalleryUnavailable));
  CHECK_EQ(progress[0].step, 0);
  CHECK(h.light_commands().empty());      // ni se enciende la luz
  CHECK(h.http.requested_urls.empty());   // ni se captura
  CHECK_EQ(log.count("la galeria no esta cargada"), std::size_t{1});
  // El pase de lista no depende de esto: sigue funcionando.
  h.pir();
  h.queue_frames(3);
  h.service->poll();
  CHECK_EQ(h.results().size(), std::size_t{1});
}

TEST(la_galeria_que_deja_de_ser_de_fiar_a_mitad_del_registro_falla_cerrado) {
  Harness h;
  bool fiable = true;
  h.gallery_ready = [&fiable] { return fiable; };
  abrir_registro(h);
  for (int i = 0; i < 4; ++i) h.enrollment_step();
  fiable = false;  // una recarga fallo mientras el alumno posaba
  h.enrollment_step();
  CHECK(h.service->state() == ServiceState::Idle);
  CHECK(h.progress().back().status == EnrollmentStatus::Error);
  CHECK_EQ(h.progress().back().reason.value(), std::string(enrollment_reasons::kGalleryUnavailable));
  CHECK(h.embeddings().empty());
  for (const EnrollmentProgress& p : h.progress()) CHECK(p.status != EnrollmentStatus::Completed);
}

TEST(una_galeria_cargada_pero_vacia_no_bloquea_el_registro) {
  Harness h;
  h.gallery_ready = [] { return true; };
  abrir_registro(h);
  CHECK_EQ(h.gallery.size(), std::size_t{0});
  correr_cinco_poses(h);
  CHECK(h.progress().back().status == EnrollmentStatus::Completed);
}

TEST(el_duplicado_se_busca_con_la_media_final_y_no_con_las_poses_descartadas) {
  Harness h;
  ServiceConfig config = h.config;
  config.min_front_similarity = 0.0f;  // las poses raras llegan al descarte final
  config.min_front_2_similarity = 0.0f;
  h.restart(config);
  // Cuatro poses iguales y una ortogonal: la rara se descarta y la media es (1,0,0,0).
  h.embedder.outputs = {{1.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f},
                        {1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}};
  abrir_registro(h);
  h.gallery.upsert("320068999", std::vector<float>{0.0f, 1.0f, 0.0f, 0.0f});  // como la pose rara
  correr_cinco_poses(h);
  CHECK(h.progress().back().status == EnrollmentStatus::Completed);  // no es duplicado
}

// --- Tope global de rechazos por identidad ------------------------------------------------

TEST(el_tercer_rechazo_por_identidad_del_registro_lo_termina_con_error_inconsistente) {
  Harness h;
  CapturedStderr log;
  // frente; izquierda: mala y buena; derecha: mala y buena; arriba: mala (la tercera mala).
  const std::vector<float> mala = {0.0f, 1.0f, 0.0f, 0.0f};
  h.embedder.outputs = {{1.0f, 0.0f, 0.0f, 0.0f}, mala, con_coseno(0.6f), mala,
                        con_coseno(0.6f, -1.0f), mala};
  abrir_registro(h);
  for (int i = 0; i < 6; ++i) h.enrollment_step();
  CHECK(decisiones(h) == (std::vector<std::string>{
                             "frente:aceptada", "izquierda:rechazada:inconsistente",
                             "izquierda:aceptada", "derecha:rechazada:inconsistente",
                             "derecha:aceptada", "arriba:rechazada:inconsistente"}));
  // Cada pose tenia intentos de sobra (el tope es de TODO el registro).
  CHECK(h.service->state() == ServiceState::Idle);
  CHECK(h.progress().back().status == EnrollmentStatus::Error);
  CHECK_EQ(h.progress().back().reason.value(), std::string(enrollment_reasons::kInconsistent));
  CHECK(h.embeddings().empty());
  CHECK(h.light_commands() == (std::vector<int>{1, 0}));
  CHECK_EQ(log.count("3 poses rechazadas por identidad (maximo 3)"), std::size_t{1});
}

TEST(el_tope_de_rechazos_por_identidad_es_configurable) {
  const std::vector<float> mala = {0.0f, 1.0f, 0.0f, 0.0f};
  const std::vector<std::vector<float>> salidas = {{1.0f, 0.0f, 0.0f, 0.0f}, mala, mala};
  {  // 1: el primer rechazo termina el registro
    Harness h;
    ServiceConfig config = h.config;
    config.max_identity_rejections = 1;
    h.restart(config);
    h.embedder.outputs = salidas;
    abrir_registro(h);
    h.enrollment_step();
    h.enrollment_step();
    CHECK(h.service->state() == ServiceState::Idle);
    CHECK_EQ(h.progress().back().reason.value(), std::string(enrollment_reasons::kInconsistent));
  }
  {  // 4: tres rechazos seguidos todavia dejan seguir (la pose tiene 5 intentos)
    Harness h;
    ServiceConfig config = h.config;
    config.max_identity_rejections = 4;
    h.restart(config);
    h.embedder.outputs = salidas;
    abrir_registro(h);
    for (int i = 0; i < 4; ++i) h.enrollment_step();  // frente + 3 rechazos de izquierda
    CHECK(h.service->state() == ServiceState::Enrolling);
    h.enrollment_step();  // el cuarto
    CHECK(h.service->state() == ServiceState::Idle);
  }
}

// --- El motivo del error al agotar los intentos es el del ultimo rechazo ------------------

TEST(agotar_los_intentos_cierra_con_el_motivo_del_ultimo_rechazo) {
  const auto agota = [](const std::function<void(Harness&)>& preparar, std::string_view esperado) {
    Harness h;
    ServiceConfig config = h.config;
    config.max_attempts_per_pose = 2;
    h.restart(config);
    preparar(h);
    abrir_registro(h);
    h.enrollment_step();
    h.enrollment_step();
    CHECK(h.service->state() == ServiceState::Idle);
    CHECK(h.progress().back().status == EnrollmentStatus::Error);
    CHECK_EQ(h.progress().back().reason.value(), std::string(esperado));
    CHECK(h.embeddings().empty());
  };
  agota([](Harness& h) { h.detector.detections.clear(); }, enrollment_reasons::kNoFace);
  agota([](Harness& h) { cabeza_fija(h, 70.0, 0.0); }, enrollment_reasons::kExcessiveTurn);
  agota([](Harness& h) { cabeza_fija(h, 30.0, 0.0); }, enrollment_reasons::kWrongPose);
  agota(
      [](Harness& h) {
        h.detector.adjust = nullptr;
        h.detector.detections = {rostro_de(100.0f, 120.0f, 80.0f, 0.95f),
                                 rostro_de(240.0f, 120.0f, 76.0f, 0.90f)};
      },
      enrollment_reasons::kSeveralFaces);
}

TEST(un_fallo_real_del_pipeline_sigue_cerrando_con_npu) {
  Harness h;
  abrir_registro(h);
  h.detector.on_call = [] { throw std::runtime_error("npu caida"); };
  h.enrollment_step();
  CHECK(h.service->state() == ServiceState::Idle);
  CHECK_EQ(h.progress().back().reason.value(), std::string(enrollment_reasons::kNpu));
}

// --- Identidad desactivada y coseno negativo ----------------------------------------------

TEST(un_umbral_de_identidad_de_cero_acepta_incluso_un_coseno_negativo) {
  const std::vector<std::vector<float>> salidas = {{1.0f, 0.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f, 0.0f}};
  {  // desactivada: un rostro opuesto pasa
    Harness h;
    ServiceConfig config = h.config;
    config.min_front_similarity = 0.0f;
    config.min_front_2_similarity = 0.0f;
    h.restart(config);
    h.embedder.outputs = salidas;
    abrir_registro(h);
    h.enrollment_step();
    h.enrollment_step();
    CHECK(decisiones(h).back() == "izquierda:aceptada");
  }
  {  // activa (0.20): el mismo coseno de -1 se rechaza
    Harness h;
    h.embedder.outputs = salidas;
    abrir_registro(h);
    h.enrollment_step();
    h.enrollment_step();
    CHECK(decisiones(h).back() == "izquierda:rechazada:inconsistente");
  }
}

// --- Avisos al arrancar -------------------------------------------------------------------

TEST(el_servicio_avisa_al_arrancar_de_los_controles_desactivados_o_debilitados) {
  const auto avisos = [](const std::function<void(ServiceConfig&)>& cambio) {
    Harness h;
    h.service.reset();
    ServiceConfig config = h.config;
    cambio(config);
    CapturedStderr log;
    VisionService service(ServiceDeps{h.bus, h.http, h.detector, h.embedder, h.gallery, h.clock,
                                      h.wall_clock, nullptr, [] { return true; }},
                          config);
    return log.count("AVISO");
  };
  CHECK_EQ(avisos([](ServiceConfig&) {}), std::size_t{0});  // los valores por omision no avisan
  {  // sin gallery_ready el chequeo de galeria queda abierto: se avisa (sin lanzar)
    Harness h;
    h.service.reset();
    CapturedStderr log;
    VisionService service(ServiceDeps{h.bus, h.http, h.detector, h.embedder, h.gallery, h.clock,
                                      h.wall_clock, nullptr, nullptr},
                          h.config);
    CHECK_EQ(log.count("AVISO: sin gallery_ready"), std::size_t{1});
  }
  CHECK_EQ(avisos([](ServiceConfig& c) { c.min_front_similarity = 0.0f; }), std::size_t{1});
  CHECK_EQ(avisos([](ServiceConfig& c) {
             c.min_front_similarity = 0.0f;
             c.min_front_2_similarity = 0.0f;
           }),
           std::size_t{2});
  CHECK_EQ(avisos([](ServiceConfig& c) { c.other_face_min_area_ratio = 0.7f; }), std::size_t{1});
  CHECK_EQ(avisos([](ServiceConfig& c) { c.other_face_min_score = 0.85f; }), std::size_t{1});
  CHECK_EQ(avisos([](ServiceConfig& c) { c.max_identity_rejections = 11; }), std::size_t{1});
}

// --- Rostro principal ---------------------------------------------------------------------

TEST(un_falso_positivo_grande_de_score_bajo_no_es_el_rostro_principal) {
  Harness h;
  h.detector.adjust = nullptr;
  // El falso positivo es grande, con score 0.1 y la cabeza girada (si fuera el principal, la pose
  // de frente saldria rechazada); el rostro real es mas chico y de frente.
  FaceDetection falso = rostro_de(100.0f, 120.0f, 140.0f, 0.1f);
  falso.landmarks = fakes::synthetic_landmarks(40.0, 0.0, 1.5, {100.0f, 110.0f});
  h.detector.detections = {falso, rostro_de(250.0f, 120.0f, 80.0f, 0.9f)};
  abrir_registro(h);
  h.enrollment_step();
  CHECK(decisiones(h) == (std::vector<std::string>{"frente:aceptada"}));
}

TEST(un_score_no_finito_no_rompe_la_eleccion_y_cuenta_como_otro_rostro) {
  Harness h;
  h.detector.adjust = nullptr;
  const float nan = std::numeric_limits<float>::quiet_NaN();
  h.detector.detections = {rostro_de(100.0f, 120.0f, 80.0f, 0.9f), rostro_de(240.0f, 120.0f, 76.0f, nan)};  // grande: no es "chico"
  abrir_registro(h);
  h.enrollment_step();
  // Un score que no se puede comparar no se puede dar por "bajo": fallo cerrado.
  CHECK(decisiones(h) == (std::vector<std::string>{"frente:rechazada:varios_rostros"}));
}

// --- Un registro completo con la identidad activa y poses de perfil con coseno bajo -------

TEST(un_registro_con_la_identidad_activa_y_perfiles_de_coseno_bajo_llega_al_descarte_final) {
  Harness h;
  CapturedStderr log;
  // Lo esperado con perfiles de la misma persona: de 0.3 a 0.5 contra frente (arriba, 0.25, esta
  // apenas por encima del umbral de 0.20). Cada pose en su propio eje: se parecen poco entre si.
  const std::vector<std::vector<float>> poses = {
      {1.0f, 0.0f, 0.0f, 0.0f}, en_eje(0.40f, 1), en_eje(0.40f, 2), en_eje(0.25f, 3),
      en_eje(0.80f, 1)};
  h.embedder.outputs = poses;
  abrir_registro(h);
  correr_cinco_poses(h);

  // Ninguna pose se rechazo por identidad (todas por encima de su umbral)...
  const std::vector<std::string> d = decisiones(h);
  CHECK_EQ(d.size(), std::size_t{5});
  for (const std::string& decision : d) CHECK(decision.find("aceptada") != std::string::npos);
  // ...y el descarte final (min_pose_similarity 0.50, contra la media) sigue activo: con estos
  // cosenos descarta al menos una pose, y el embedding es la media de las que quedan.
  const std::vector<std::size_t> esperadas = keep_consistent_poses(poses, h.config.min_pose_similarity);
  CHECK(esperadas.size() >= 3 && esperadas.size() < 5);
  CHECK(log.count("pose descartada") >= 1);
  CHECK(h.progress().back().status == EnrollmentStatus::Completed);
  CHECK_EQ(h.embeddings().size(), std::size_t{1});
  std::vector<std::vector<float>> unidades;
  for (const std::vector<float>& pose : poses) unidades.push_back(l2_normalized(pose));
  const std::vector<float> media = l2_normalized(mean_embedding(unidades, esperadas));
  for (std::size_t i = 0; i < media.size(); ++i) {
    CHECK_NEAR(h.embeddings()[0].embedding[i], media[i], 1e-5);
  }
}

// --- "arriba" no se valida con una referencia de "frente" extrema -----------------------------

TEST(frente_en_menos_25_y_arriba_en_0_se_rechaza) {
  Harness h;
  CapturedStderr log;
  abrir_registro(h);
  // La camara esta muy alta (o el alumno miraba hacia abajo): "frente" sale a -25 grados. Es
  // aceptable como "frente" (el limite es 30) pero no como referencia de "arriba".
  const auto cabeza = [&h](double yaw, double pitch) { cabeza_fija(h, yaw, pitch); };
  cabeza(0.0, -25.0);
  h.enrollment_step();
  CHECK(decisiones(h).back() == "frente:aceptada");
  CHECK_EQ(log.count("AVISO: frente aceptada con un pitch de -25.0"), std::size_t{1});
  cabeza(30.0, -25.0);
  h.enrollment_step();
  cabeza(-30.0, -25.0);
  h.enrollment_step();
  cabeza(0.0, 0.0);  // una cara neutra: contra una referencia de -25 parecería 25 grados "arriba"
  h.enrollment_step();
  CHECK(decisiones(h).back() == "arriba:rechazada:pose_incorrecta");
  cabeza(0.0, 15.0);  // y ni siquiera levantandola: la referencia no sirve (falla cerrado)
  h.enrollment_step();
  CHECK(decisiones(h).back() == "arriba:rechazada:pose_incorrecta");
}

// --- Supervivientes contra frente_2 ---------------------------------------------------------

TEST(una_pose_lateral_que_no_se_parece_a_frente_2_termina_el_registro_como_inconsistente) {
  const std::vector<std::vector<float>> poses = {
      {1.0f, 0.0f, 0.0f, 0.0f},        // frente
      {0.3f, 0.954f, 0.0f, 0.0f},      // izquierda: 0.3 con frente (pasa el 0.20)...
      en_eje(0.5f, 2),                 // derecha
      en_eje(0.5f, 3),                 // arriba
      {0.5f, -0.5f, 0.7071f, 0.0f}};   // frente_2: 0.5 con frente, pero -0.33 con izquierda
  {
    Harness h;
    CapturedStderr log;
    ServiceConfig config = h.config;
    config.min_pose_similarity = 0.0f;  // el descarte final no quita ninguna: llegan las cinco
    h.restart(config);
    h.embedder.outputs = poses;
    abrir_registro(h);
    correr_cinco_poses(h);
    CHECK(h.service->state() == ServiceState::Idle);
    CHECK(h.progress().back().status == EnrollmentStatus::Error);
    CHECK_EQ(h.progress().back().reason.value(), std::string(enrollment_reasons::kInconsistent));
    CHECK(h.embeddings().empty());
    for (const EnrollmentProgress& p : h.progress()) CHECK(p.status != EnrollmentStatus::Completed);
    CHECK_EQ(log.count("la pose izquierda no se parece a frente_2"), std::size_t{1});
  }
  {  // con la identidad desactivada (umbral <= 0), esa comprobacion tampoco corre
    Harness h;
    ServiceConfig config = h.config;
    config.min_pose_similarity = 0.0f;
    config.min_front_similarity = 0.0f;
    config.min_front_2_similarity = 0.0f;
    h.restart(config);
    h.embedder.outputs = poses;
    abrir_registro(h);
    correr_cinco_poses(h);
    CHECK(h.progress().back().status == EnrollmentStatus::Completed);
  }
}

TEST(enrollment_pose_names_follow_the_plan) {
  Harness h;
  h.queue_frames(5);
  h.enroll("100000001");
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
  h.enroll("100000001");
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
  h.enroll("100000001");
  h.pir();  // llega en el mismo lote que el comando, pero despues
  h.service->poll();

  CHECK(h.results().empty());
  CHECK(h.service->state() == ServiceState::Enrolling);
}

TEST(cancel_mid_enrollment_stops_capturing_and_publishes_no_embedding) {
  Harness h;
  h.queue_frames(5);
  h.enroll("100000001");
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
  h.enroll("100000001");
  h.service->poll();
  h.enroll("200000002");
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
  h.enroll("100000001");
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
  h.enroll("");
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
  h.enroll("111111111");
  h.service->poll();

  h.command(VisionAction::Cancel, "222222222");
  h.service->poll();
  CHECK(h.service->state() == ServiceState::Enrolling);
  CHECK(h.progress().back().status == EnrollmentStatus::WaitingPose);

  h.command(VisionAction::Cancel, "");  // vacia = la actual
  h.service->poll();
  CHECK(h.service->state() == ServiceState::Idle);
  CHECK(h.progress().back().status == EnrollmentStatus::Cancelled);

  h.enroll("333333333");
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
  h.enroll("100000001");
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
  h.enroll("100000001");
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
  h.enroll("100000001");
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
  h.enroll("100000001");
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
  // El motivo del ultimo rechazo (aqui, una captura sin foto), no "npu".
  CHECK_EQ(h.progress().back().reason.value(), std::string(enrollment_reasons::kCaptureFailure));
  CHECK(h.light_commands() == (std::vector<int>{1, 0}));
}

// La miniatura del rostro viaja solo en el mensaje de la pose (aceptada o rechazada):
// es efimera, no se guarda en ningun sitio y los demas mensajes del registro no la
// llevan.
TEST(la_pose_aceptada_lleva_miniatura_y_los_demas_mensajes_no) {
  Harness h;
  h.detector.detections = {face_con_bbox()};
  h.http.responses.push_back(ok(frame_liso()));
  h.enroll("100000001");
  h.service->poll();
  h.enrollment_step();

  const auto progress = h.progress();
  int con_miniatura = 0;
  for (const EnrollmentProgress& p : progress) {
    if (p.status == EnrollmentStatus::Accepted) {
      CHECK(p.thumbnail.has_value());
      CHECK(!p.thumbnail->empty());
      ++con_miniatura;
    } else {
      CHECK(!p.thumbnail.has_value());
    }
  }
  CHECK_EQ(con_miniatura, 1);
}

// Sin rostro no hay recorte: el mensaje de la pose rechazada va sin miniatura (el
// campo se omite, no se manda vacio).
TEST(una_pose_sin_rostro_no_lleva_miniatura) {
  Harness h;
  h.detector.detections.clear();
  h.http.responses.push_back(ok(frame_liso()));
  h.enroll("100000001");
  h.service->poll();
  h.enrollment_step();

  const auto progress = h.progress();
  CHECK(progress.back().status == EnrollmentStatus::Rejected);
  CHECK(!progress.back().thumbnail.has_value());
}

// Sin foto no hay miniatura tampoco: el fallo de captura se rechaza sin ella.
TEST(una_pose_sin_foto_no_lleva_miniatura) {
  Harness h;
  h.http.responses.push_back({500, {}});  // la placa no contesta
  h.enroll("100000001");
  h.service->poll();
  h.enrollment_step();

  const auto progress = h.progress();
  CHECK(progress.back().status == EnrollmentStatus::Rejected);
  CHECK_EQ(progress.back().reason.value(), std::string(enrollment_reasons::kCaptureFailure));
  CHECK(!progress.back().thumbnail.has_value());
}

// El frontend cuenta 5-4-3-2-1 con este mismo plazo (ESPERA_POSE_MS, web/src/api/registroRespuestas.ts).
TEST(the_pose_delay_is_5_seconds_like_the_frontend_countdown) {
  CHECK_EQ(ServiceConfig{}.pose_delay.count(), 5000);
}

TEST(enrollment_waits_for_the_pose_delay_before_capturing) {
  Harness h;
  h.queue_frames(5);
  h.enroll("100000001");
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
  h.enroll("100000001");
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
  h.enroll("100000001");
  h.clock.on_now = [] { throw std::runtime_error("reloj roto"); };
  h.service->poll();
  h.clock.on_now = nullptr;

  CHECK(h.service->state() == ServiceState::Idle);
  CHECK(!ServiceTestAccess::has_enrollment(*h.service));
  h.enroll("100000001");  // y se puede volver a intentar
  h.service->poll();
  CHECK(h.service->state() == ServiceState::Enrolling);
  CHECK(ServiceTestAccess::has_enrollment(*h.service));
}

// advance_enrollment va despues del bucle de eventos y tambien esta aislado.
TEST(poll_isolates_a_throwing_enrollment_step) {
  Harness h;
  h.queue_frames(5);
  h.enroll("100000001");
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
  // Sin progreso publicado el detector no sabe que pose se pide: la cabeza sigue el orden del
  // registro (una captura por pose, sin repeticiones).
  h.detector.adjust = [&h](std::vector<FaceDetection>& faces) {
    const double yaw[] = {0.0, 30.0, -30.0, 0.0, 0.0};
    const double pitch[] = {0.0, 0.0, 0.0, 25.0, 0.0};
    // Con un reintento (mas de 5 llamadas) el indice se acota y el CHECK_EQ de abajo falla con
    // un mensaje claro, en vez de leer fuera del arreglo.
    const std::size_t i = std::min<std::size_t>(static_cast<std::size_t>(h.detector.calls - 1), 4);
    for (FaceDetection& face : faces) face.landmarks = fakes::synthetic_landmarks(yaw[i], pitch[i]);
  };
  h.bus.on_publish = [](std::string_view topic, const Payload&) {
    if (topic == topics::kVisionEnrollment) throw std::runtime_error("sin broker");
  };
  h.enroll("100000001");
  h.service->poll();
  for (int i = 0; i < 5; ++i) h.enrollment_step();

  CHECK(h.service->state() == ServiceState::Idle);
  CHECK_EQ(h.http.requested_urls.size(), std::size_t{5});  // cada pose se capturo una vez
  CHECK_EQ(h.detector.calls, 5);  // sin reintentos: la pose sale de la cuenta de llamadas
  CHECK_EQ(h.embeddings().size(), std::size_t{1});
  CHECK(h.light_commands() == (std::vector<int>{1, 0}));
}

TEST(embedding_publish_failure_fails_the_enrollment_once) {
  Harness h;
  h.queue_frames(5);
  h.bus.on_publish = [](std::string_view topic, const Payload&) {
    if (topic == topics::kVisionEmbedding) throw std::runtime_error("sin broker");
  };
  h.enroll("100000001");
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
  h.enroll(account);
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
  h.enroll("100000009");
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
  h.enroll("100000001");
  h.service->poll();
  h.command(VisionAction::Cancel);
  h.service->poll();

  CHECK(state_at_light_off.has_value());
  CHECK(*state_at_light_off == ServiceState::Idle);
  CHECK(h.service->state() == ServiceState::Idle);

  h.bus.on_publish = nullptr;  // y un registro nuevo puede empezar
  h.enroll("200000002");
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
  h.enroll("100000001");
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

// --- Miniatura del pase de lista (contrato v0.17) -------------------------------------------

// Una rafaga de fotos lisas (la miniatura de una de ruido no siempre cabe en el tope) y un
// detector con el recuadro del rostro.
void preparar_pase_con_rostro(Harness& h) {
  h.detector.detections = {face_con_bbox()};
  for (int i = 0; i < 3; ++i) h.http.responses.push_back(ok(frame_liso()));
}

// Tope del contrato: unos 8 KB de JPEG, que en base64 son 10924 caracteres.
constexpr std::size_t kMaxMiniaturaBase64 = ((8 * 1024 + 2) / 3) * 4;

// El reconocido del pase de lista lleva la miniatura del rostro: la misma que el registro
// (recorte del rostro con margen, JPEG en base64, dentro del tope). El resto del resultado es
// el de siempre.
TEST(el_reconocido_del_pase_lleva_la_miniatura_del_rostro) {
  Harness h;
  preparar_pase_con_rostro(h);
  h.pir();
  h.service->poll();

  const AttendanceResult result = h.results().at(0);
  CHECK(result.mode == AttendanceMode::Attendance);
  CHECK(result.decision == Decision::Recognized);
  CHECK_EQ(result.account.value(), std::string("320068234"));
  CHECK_NEAR(result.confidence.value(), 1.0, 1e-6);
  CHECK(!result.reason.has_value());
  CHECK(result.thumbnail.has_value());
  CHECK(!result.thumbnail->empty());
  CHECK(result.thumbnail->size() <= kMaxMiniaturaBase64);
  // Va entera en el mensaje que se publica, muy por debajo del tope de documento de la capa JSON.
  const std::optional<std::string> documento = json::serialize(result);
  CHECK(documento.has_value());
  CHECK(documento->size() < json::kMaxDocumentBytes);
  CHECK(documento->find("\"miniatura\":\"") != std::string::npos);
}

// Solo el reconocido del pase lleva miniatura: la prueba del registro, el dudoso, el
// desconocido, la imagen insuficiente y el "sin rostro" la dejan fuera (no son una asistencia).
TEST(solo_el_reconocido_del_pase_lleva_miniatura) {
  {
    Harness h;  // prueba: reconocido, pero en modo "prueba"
    preparar_pase_con_rostro(h);
    h.command(VisionAction::Test);
    h.service->poll();
    CHECK(h.results().at(0).decision == Decision::Recognized);
    CHECK(h.results().at(0).mode == AttendanceMode::Test);
    CHECK(!h.results().at(0).thumbnail.has_value());
  }
  {
    Harness h;  // dudoso
    preparar_pase_con_rostro(h);
    h.embedder.outputs = {{0.5f, 0.5f, 0.5f, 0.5f}};
    h.pir();
    h.service->poll();
    CHECK(h.results().at(0).decision == Decision::Doubtful);
    CHECK(!h.results().at(0).thumbnail.has_value());
  }
  {
    Harness h;  // desconocido
    preparar_pase_con_rostro(h);
    h.embedder.outputs = {{0.0f, 0.0f, 1.0f, 0.0f}};
    h.pir();
    h.service->poll();
    CHECK(h.results().at(0).decision == Decision::Unknown);
    CHECK(!h.results().at(0).thumbnail.has_value());
  }
  {
    Harness h;  // sin rostro
    preparar_pase_con_rostro(h);
    h.detector.detections.clear();
    h.pir();
    h.service->poll();
    CHECK(h.results().at(0).decision == Decision::NoFace);
    CHECK(!h.results().at(0).thumbnail.has_value());
  }
  {
    Harness h;  // sin fotos: captura fallida
    h.pir();
    h.service->poll();
    CHECK(h.results().at(0).decision == Decision::InsufficientImage);
    CHECK(!h.results().at(0).thumbnail.has_value());
  }
}

// Si la miniatura no se puede armar (aqui, un recuadro fuera de la foto), el resultado se
// publica igual y sin el campo: la asistencia no depende de la foto.
TEST(un_fallo_al_armar_la_miniatura_no_impide_publicar_el_resultado) {
  Harness h;
  preparar_pase_con_rostro(h);
  FaceDetection fuera = face_in_frame();
  fuera.bbox = cv::Rect2f(5000.0f, 5000.0f, 80.0f, 80.0f);
  h.detector.detections = {fuera};
  h.pir();
  h.service->poll();

  CHECK_EQ(h.results().size(), std::size_t{1});
  const AttendanceResult result = h.results().at(0);
  CHECK(result.decision == Decision::Recognized);
  CHECK_EQ(result.account.value(), std::string("320068234"));
  CHECK(!result.thumbnail.has_value());
  CHECK(h.service->state() == ServiceState::Idle);
}

// Aunque la foto sea de ruido (el peor caso para el JPEG), la miniatura o cabe en el tope o no
// va: nunca se manda una que el contrato no admita.
TEST(la_miniatura_del_pase_respeta_el_tope_con_una_foto_de_ruido) {
  Harness h;
  h.detector.detections = {face_con_bbox()};
  h.queue_frames(3);
  h.pir();
  h.service->poll();

  const AttendanceResult result = h.results().at(0);
  CHECK(result.decision == Decision::Recognized);
  if (result.thumbnail.has_value()) CHECK(result.thumbnail->size() <= kMaxMiniaturaBase64);
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

  for (int i = 0; i < 4; ++i) h.enroll("100000001");  // llenan la cola
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
  h.enroll("100000001");
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
  h.enroll("100000001");
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
    h.enroll(i % 2 ? "12345678a" : "1234567890");
  }
  h.enroll("");
  h.enroll("<b>hi</b>1");

  CHECK_EQ(ServiceTestAccess::inbox_size(*h.service), std::size_t{0});  // no ocupan lugar
  CHECK_EQ(log.count("enrolar con cuenta invalida"), std::size_t{1});   // una linea por poll
  CHECK_EQ(log.count("cola de eventos llena"), std::size_t{0});
  CHECK_EQ(log.count("12345678a"), std::size_t{0});                     // sin el contenido
  h.service->poll();
  CHECK(h.bus.published.empty());  // y no responden
  CHECK(h.service->state() == ServiceState::Idle);

  h.enroll("100000001");  // uno valido si pasa
  h.service->poll();
  CHECK(h.service->state() == ServiceState::Enrolling);

  for (int i = 0; i < 3; ++i) h.enroll("bad");  // el log vuelve a salir tras un poll
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
  h.enroll("100000001");
  h.enroll("200000002");
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
  h.enroll("100000001");
  CHECK_EQ(ServiceTestAccess::inbox_size(*h.service), std::size_t{4});
  h.service->poll();
  CHECK_EQ(h.results().size(), std::size_t{2});  // el PIR y el probar
  CHECK(h.service->state() == ServiceState::Enrolling);
}

TEST(cancel_with_an_invalid_account_is_ignored_while_empty_means_current) {
  Harness h;
  h.enroll("100000001");
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
  h.enroll("100000001");
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
  h.enroll("100000001");
  h.service->poll();
  h.enroll("200000002");
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
  config.max_body_bytes = 3000016;  // la foto mas los 16 bytes de la etiqueta
  h.restart(config);
  h.queue_frames(3);
  h.pir();
  h.service->poll();

  CHECK_EQ(h.http.requested_max_body_bytes.size(), std::size_t{3});
  for (const std::size_t cap : h.http.requested_max_body_bytes) CHECK_EQ(cap, std::size_t{3000016});
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
  config.min_duplicate_similarity = 0.55f;
  h.restart(config);
  h.pir();
  h.service->poll();
  CHECK(h.results().at(1).decision == Decision::Unknown);

  config.min_recognized_score = 0.40f;  // ahora 0.5 queda por encima
  config.min_dudoso_score = 0.30f;
  config.min_duplicate_similarity = 0.30f;  // no puede pasar de "dudoso"
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
  h.enroll("100000001");
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
  h.enroll("100000001");
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
  h.enroll("100000001");
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
  h.enroll("100000001");
  h.service->poll();
  h.enrollment_step();

  CHECK(h.progress().back().status == EnrollmentStatus::Rejected);
  CHECK_EQ(h.progress().back().reason.value(), std::string("sin_rostro"));
}

TEST(las_ordenes_rechazadas_usan_el_estado_error) {
  Harness h;
  h.enroll("100000001");
  h.service->poll();
  h.enroll("200000002");  // ocupado
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
  h.enroll("100000001");
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
  h.enroll("100000001");
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

// --- La firma de la peticion de captura -------------------------------------------
// La placa (XIAO) rechaza con 401 lo que no trae X-Captura-TS, X-Captura-Nonce y
// X-Captura-HMAC; el servicio los manda cuando hay secreto (capture_hmac_secret).

namespace {

const std::string kSecretoPrueba = "secreto-de-prueba-con-mas-de-32-caracteres";

// El valor de una cabecera "Nombre:valor" de la lista; vacio si no esta.
std::string header_value(const std::vector<std::string>& headers, const std::string& name) {
  const std::string prefix = name + ":";
  for (const std::string& header : headers) {
    if (header.rfind(prefix, 0) == 0) return header.substr(prefix.size());
  }
  return {};
}

// Un cliente HTTP que no admite cabeceras (el comportamiento por omision de la interfaz).
class HeaderlessHttpClient final : public HttpClient {
 public:
  HttpResponse get(const std::string& url, Millis, std::size_t) override {
    requested_urls.push_back(url);
    return {};
  }
  std::vector<std::string> requested_urls;
};

// Como la placa: a cada 200 de la cola le cifra el cuerpo (AES-256-GCM, clave derivada del
// secreto, AAD con el ts y el nonce de la ULTIMA peticion) y le pone X-Captura-IV. Un 200
// que ya trae su IV (una prueba que lo altera a proposito) se deja como esta.
std::string iv_hex_numero(int n) {
  char hex[25];
  std::snprintf(hex, sizeof hex, "%024x", n);
  return std::string(hex);
}

void encrypt_like_the_board(HttpResponse& response, std::int64_t ts, std::uint64_t nonce,
                            const std::string& secret = kSecretoPrueba, int iv_number = 1) {
  response.resp_iv = iv_hex_numero(iv_number);
  response.body = gcm_apoyo::cifrar(derivar_clave_respuesta(secret),
                                    gcm_apoyo::bytes_de_hex(response.resp_iv),
                                    aad_respuesta(ts, nonce), response.body);
}

void sign_responses_like_the_board(Harness& h) {
  auto counter = std::make_shared<int>(0);
  h.http.on_call = [&h, counter] {
    if (h.http.responses.empty() || h.http.requested_headers.empty()) return;
    HttpResponse& response = h.http.responses.front();
    if (response.status != 200 || !response.resp_iv.empty()) return;
    const auto& headers = h.http.requested_headers.back();
    encrypt_like_the_board(response, std::stoll(header_value(headers, kHeaderCapturaTs)),
                           std::stoull(header_value(headers, kHeaderCapturaNonce)),
                           kSecretoPrueba, ++*counter);
  };
}

// Reconstruye el servicio del arnes con secreto de firma (y, si se pide, otra URL), y con
// una placa que firma sus respuestas.
void enable_signing(Harness& h, const std::string& capture_url = kCaptureUrl) {
  ServiceConfig config = h.config;
  config.capture_url = capture_url;
  config.capture_hmac_secret = kSecretoPrueba;
  h.restart(config);
  sign_responses_like_the_board(h);
}

}  // namespace

// Sin secreto la peticion es la de siempre: sin cabeceras (camara_falsa las ignora).
TEST(sin_secreto_la_captura_no_manda_cabeceras) {
  Harness h;
  h.queue_frames(3);

  h.pir();
  h.service->poll();

  CHECK_EQ(h.http.requested_urls.size(), std::size_t{3});
  CHECK(h.http.requested_headers.empty());
  CHECK_EQ(h.results().at(0).account.value(), std::string("320068234"));
}

// Cada foto de la rafaga lleva su ts del reloj del borde, su propio nonce y la firma que
// verifica la placa (HMAC de "GET\n/capture\n<ts>\n<nonce>").
TEST(con_secreto_cada_captura_va_firmada_con_el_ts_del_reloj_y_un_nonce_propio) {
  Harness h;
  enable_signing(h);
  h.wall_clock.epoch_seconds = 1793805153;
  h.queue_frames(3);

  h.pir();
  h.service->poll();

  CHECK_EQ(h.http.requested_headers.size(), std::size_t{3});
  std::set<std::string> nonces;
  for (const std::vector<std::string>& headers : h.http.requested_headers) {
    CHECK_EQ(headers.size(), std::size_t{3});
    CHECK_EQ(header_value(headers, kHeaderCapturaTs), std::string("1793805153"));
    const std::string nonce_text = header_value(headers, kHeaderCapturaNonce);
    CHECK(!nonce_text.empty());
    CHECK(nonce_text.find_first_not_of("0123456789") == std::string::npos);
    nonces.insert(nonce_text);
    const std::uint64_t nonce = std::stoull(nonce_text);
    CHECK_EQ(header_value(headers, kHeaderCapturaHmac),
             firma_captura(kSecretoPrueba, 1793805153, nonce));
  }
  CHECK_EQ(nonces.size(), std::size_t{3});  // la placa rechaza un nonce repetido
  CHECK_EQ(h.results().at(0).account.value(), std::string("320068234"));
}

// El ts se lee del reloj en cada peticion, no una vez por rafaga.
TEST(con_secreto_el_ts_se_lee_del_reloj_en_cada_peticion) {
  Harness h;
  enable_signing(h);
  h.wall_clock.epoch_seconds = 1793805000;
  h.wall_clock.on_now = [&] { ++h.wall_clock.epoch_seconds; };
  h.queue_frames(3);

  h.pir();
  h.service->poll();

  std::set<std::string> instantes;
  for (const std::vector<std::string>& headers : h.http.requested_headers) {
    instantes.insert(header_value(headers, kHeaderCapturaTs));
  }
  CHECK_EQ(instantes.size(), std::size_t{3});
}

// El camino firmado es "/capture" fijo: la URL puede llevar "?res=SVGA" (se manda tal
// cual) y apuntar a un puente con otro camino, y la firma no cambia.
TEST(la_firma_usa_el_camino_fijo_y_la_url_se_manda_con_su_query) {
  const std::string url = "http://10.0.0.7:8080/capture?res=SVGA";
  Harness h;
  enable_signing(h, url);
  h.queue_frames(3);
  h.pir();
  h.service->poll();
  CHECK_EQ(h.http.requested_urls.at(0), url);
  const auto& headers = h.http.requested_headers.at(0);
  CHECK_EQ(header_value(headers, kHeaderCapturaHmac),
           firma_captura(kSecretoPrueba, 1793805153,
                         std::stoull(header_value(headers, kHeaderCapturaNonce))));

  Harness otro;
  enable_signing(otro, "http://10.0.0.7:8080/otro/camino?res=SVGA");
  otro.queue_frames(3);
  otro.pir();
  otro.service->poll();
  const auto& otras = otro.http.requested_headers.at(0);
  CHECK_EQ(header_value(otras, kHeaderCapturaHmac),
           firma_captura(kSecretoPrueba, 1793805153,
                         std::stoull(header_value(otras, kHeaderCapturaNonce))));
}

// Con el reloj del borde en 1970 (RTC sin pila) no se firma ni se pide la foto: la placa
// aceptaria esa hora y rechazaria despues las buenas.
TEST(con_secreto_y_el_reloj_en_1970_no_se_pide_la_foto) {
  Harness h;
  enable_signing(h);
  h.wall_clock.epoch_seconds = 12;
  CapturedStderr log;

  h.pir();
  h.service->poll();

  CHECK(h.http.requested_urls.empty());
  CHECK_EQ(h.results().at(0).reason.value(), std::string(reasons::kCaptureFailed));
  CHECK(log.count("no es plausible") > 0);
  CHECK_EQ(log.count(kSecretoPrueba), std::size_t{0});
}

// Con secreto y un cliente HTTP sin cabeceras no se manda la peticion sin firma.
TEST(con_secreto_y_un_cliente_sin_cabeceras_no_se_pide_la_foto) {
  fakes::FakeMessageBus bus;
  HeaderlessHttpClient http;
  fakes::FakeFaceDetector detector;
  fakes::FakeFaceEmbedder embedder;
  Gallery gallery;
  fakes::ManualClock clock;
  fakes::FixedWallClock wall_clock;
  ServiceConfig config;
  config.capture_url = kCaptureUrl;
  config.burst_size = 3;
  config.capture_hmac_secret = kSecretoPrueba;
  VisionService service(ServiceDeps{bus, http, detector, embedder, gallery, clock, wall_clock},
                        config);
  CapturedStderr log;

  bus.inject(topics::kCameraPir, PirEvent{});
  service.poll();

  CHECK(http.requested_urls.empty());
  CHECK(log.count("no admite cabeceras") > 0);
}

// Cada codigo de la placa deja su propia linea de log, UNA por rafaga y con la cuenta, sin
// volcar el cuerpo, la firma ni el secreto. (El 429 tiene su propio comportamiento: se
// reintenta; ver mas abajo.)
TEST(el_log_distingue_los_codigos_de_la_placa_una_vez_por_rafaga) {
  struct Caso {
    int codigo;
    const char* texto;
  };
  const Caso casos[] = {{401, "rechazo 3/3 fotos con 401"},
                        {403, "rechazo 3/3 fotos con 403"},
                        {503, "rechazo 3/3 fotos con 503"},
                        {500, "rechazo 3/3 fotos con 500"}};
  for (const Caso& caso : casos) {
    Harness h;
    enable_signing(h);
    HttpResponse rechazo;
    rechazo.rejected_status = caso.codigo;
    for (int i = 0; i < 3; ++i) h.http.responses.push_back(rechazo);
    CapturedStderr log;

    h.pir();
    h.service->poll();

    CHECK_EQ(log.count(caso.texto), std::size_t{1});
    CHECK_EQ(log.count("la placa rechazo"), std::size_t{1});  // una sola linea por rafaga
    CHECK_EQ(log.count(kSecretoPrueba), std::size_t{0});
    CHECK_EQ(log.count("X-Captura"), std::size_t{0});
    CHECK_EQ(h.results().at(0).reason.value(), std::string(reasons::kCaptureFailed));
  }
}

// Un fallo de red (sin respuesta HTTP) no se confunde con un codigo de la placa.
TEST(un_fallo_de_red_no_se_registra_como_un_codigo_de_la_placa) {
  Harness h;
  enable_signing(h);
  CapturedStderr log;

  h.pir();
  h.service->poll();  // sin respuestas: status 0 y rejected_status 0

  CHECK_EQ(log.count("la placa rechazo"), std::size_t{0});
}

// El suelo del reloj plausible es el 1 de enero de 2026.
TEST(el_suelo_del_reloj_es_el_1_de_enero_de_2026) {
  Harness antes;
  enable_signing(antes);
  antes.wall_clock.epoch_seconds = 1767225599;  // 2025-12-31T23:59:59Z
  antes.queue_frames(3);
  antes.pir();
  antes.service->poll();
  CHECK(antes.http.requested_urls.empty());

  Harness justo;
  enable_signing(justo);
  justo.wall_clock.epoch_seconds = 1767225600;  // 2026-01-01T00:00:00Z
  justo.queue_frames(3);
  justo.pir();
  justo.service->poll();
  CHECK_EQ(justo.http.requested_urls.size(), std::size_t{3});
}

// --- El espaciado entre rafagas y el 429 -------------------------------------------
// La placa permite 4 capturas por ventana de 2000 ms y despues exige 1000 ms de pausa: la
// siguiente rafaga no puede empezar antes de 3000 ms despues de la primera foto de la
// anterior. El PIR rebota cada 2 s, asi que dos rafagas seguidas daban 4 x 429.

namespace {

HttpResponse too_many_requests(int retry_after_s = 0) {
  HttpResponse response;
  response.rejected_status = 429;
  response.retry_after_s = retry_after_s;
  return response;
}

std::size_t count_sleeps(const Harness& h, Millis duration) {
  return static_cast<std::size_t>(std::count(h.clock.sleeps.begin(), h.clock.sleeps.end(), duration));
}

}  // namespace

TEST(dos_rafagas_seguidas_empiezan_con_al_menos_el_intervalo_entre_ellas) {
  Harness h;
  ServiceConfig config = h.config;
  config.burst_interval = 3000ms;
  h.restart(config);
  std::vector<TimePoint> request_times;
  h.http.on_call = [&] { request_times.push_back(h.clock.now()); };
  h.queue_frames(6);

  h.pir();
  h.service->poll();
  h.pir();  // el PIR rebota: otra rafaga justo despues
  h.service->poll();

  CHECK_EQ(request_times.size(), std::size_t{6});
  CHECK(request_times[3] - request_times[0] >= 3000ms);
  // La primera rafaga no espera nada por esto: no habia una anterior.
  CHECK(request_times[3] - request_times[0] < 3000ms + 1ms);
  CHECK_EQ(h.results().size(), std::size_t{2});
  CHECK_EQ(h.results().at(1).account.value(), std::string("320068234"));
}

TEST(una_rafaga_que_empieza_despues_del_intervalo_no_espera_de_mas) {
  Harness h;
  ServiceConfig config = h.config;
  config.burst_interval = 3000ms;
  h.restart(config);
  std::vector<TimePoint> request_times;
  h.http.on_call = [&] { request_times.push_back(h.clock.now()); };
  h.queue_frames(6);

  h.pir();
  h.service->poll();
  h.clock.advance(10s);  // pasa mucho mas que el intervalo
  const std::size_t sleeps_before = h.clock.sleeps.size();
  h.pir();
  h.service->poll();

  // Entre las dos solo la espera de la luz (settle_time), ningun sleep de espaciado.
  CHECK_EQ(h.clock.sleeps.size() - sleeps_before, std::size_t{1});
  CHECK(request_times[3] - request_times[0] >= 10s);
}

// Con intervalo 0 (el valor por omision del servicio) no se espera nada de mas.
TEST(con_intervalo_cero_no_hay_espaciado) {
  Harness h;
  h.queue_frames(6);
  h.pir();
  h.service->poll();
  h.pir();
  h.service->poll();
  CHECK_EQ(h.clock.sleeps.size(), std::size_t{2});  // solo la espera de la luz de cada una
}

TEST(un_429_se_repite_una_vez_y_recupera_la_foto) {
  Harness h;
  enable_signing(h);
  h.http.responses.push_back(too_many_requests());
  h.queue_frames(3);
  CapturedStderr log;

  h.pir();
  h.service->poll();

  // 3 fotos + 1 reintento; el reintento espero los 1100 ms por omision.
  CHECK_EQ(h.http.requested_urls.size(), std::size_t{4});
  CHECK_EQ(count_sleeps(h, 1100ms), std::size_t{1});
  CHECK_EQ(h.results().at(0).account.value(), std::string("320068234"));
  CHECK_EQ(log.count("la placa rechazo"), std::size_t{0});  // se recupero: nada que avisar
  // El reintento es una peticion nueva: otro ts y otro nonce, no la misma repetida.
  std::set<std::string> nonces;
  for (const auto& headers : h.http.requested_headers) {
    nonces.insert(header_value(headers, kHeaderCapturaNonce));
  }
  CHECK_EQ(nonces.size(), std::size_t{4});
}

TEST(el_reintento_del_429_usa_el_retry_after_de_la_placa) {
  Harness h;
  h.http.responses.push_back(too_many_requests(2));
  h.queue_frames(3);

  h.pir();
  h.service->poll();

  CHECK_EQ(count_sleeps(h, 2000ms), std::size_t{1});
  CHECK_EQ(count_sleeps(h, 1100ms), std::size_t{0});
  CHECK_EQ(h.results().at(0).account.value(), std::string("320068234"));
}

// El reintento es UNO: si el segundo intento tambien da 429, la foto se pierde (no se sigue
// insistiendo con lo que quede de presupuesto).
TEST(un_429_se_repite_solo_una_vez_por_foto) {
  Harness h;
  h.http.responses.push_back(too_many_requests());
  h.http.responses.push_back(too_many_requests());
  h.queue_frames(2);
  CapturedStderr log;

  h.pir();
  h.service->poll();

  // 1a foto: 429 y 429 (se pierde); 2a y 3a: bien. 4 peticiones, no 5.
  CHECK_EQ(h.http.requested_urls.size(), std::size_t{4});
  CHECK_EQ(count_sleeps(h, 1100ms), std::size_t{1});
  CHECK_EQ(log.count("la placa rechazo 1/3 fotos con 429"), std::size_t{1});
  CHECK_EQ(h.results().at(0).account.value(), std::string("320068234"));
}

// Un Retry-After que no cabe en el tope de espera extra no se espera: la foto falla.
TEST(un_retry_after_mayor_que_el_tope_no_se_espera) {
  Harness h;
  h.http.responses.push_back(too_many_requests(4));  // 4 s > 3 s de tope
  h.queue_frames(2);

  h.pir();
  h.service->poll();

  CHECK_EQ(count_sleeps(h, 4000ms), std::size_t{0});
  CHECK_EQ(h.http.requested_urls.size(), std::size_t{3});  // sin reintento: 1 + 2 fotos
}

// Un 429 que no cede termina en la via de fallo de siempre, sin pasar del tope de espera
// extra de la rafaga (3 s en total).
TEST(un_429_persistente_termina_en_captura_fallida_dentro_del_tope) {
  Harness h;
  enable_signing(h);
  for (int i = 0; i < 20; ++i) h.http.responses.push_back(too_many_requests());
  CapturedStderr log;

  h.pir();
  h.service->poll();

  CHECK_EQ(h.results().at(0).reason.value(), std::string(reasons::kCaptureFailed));
  CHECK(h.results().at(0).decision == Decision::InsufficientImage);
  // Esperas por 429: solo las que caben en 3000 ms (dos de 1100 ms).
  CHECK_EQ(count_sleeps(h, 1100ms), std::size_t{2});
  // Una sola linea por rafaga, con la cuenta.
  CHECK_EQ(log.count("la placa rechazo 3/3 fotos con 429"), std::size_t{1});
  CHECK_EQ(log.count("la placa rechazo"), std::size_t{1});
  // 3 fotos + 2 reintentos, nada mas: ningun bucle.
  CHECK_EQ(h.http.requested_urls.size(), std::size_t{5});
}

// --- La respuesta de la placa va cifrada y autenticada (AES-256-GCM) ---------------------

// Un 200 sin X-Captura-IV (una placa suplantada que contesta con una foto cualquiera) se
// descarta sin decodificar. El cuerpo es una foto VALIDA: si se mirara, llegaria al detector.
TEST(con_secreto_un_200_sin_iv_se_descarta_sin_decodificar) {
  Harness h;
  enable_signing(h);
  h.http.on_call = nullptr;  // la placa "suplantada" no cifra
  h.queue_frames(3);
  CapturedStderr log;

  h.pir();
  h.service->poll();

  CHECK_EQ(h.detector.calls, 0);
  CHECK_EQ(log.count("respuesta no autentica: 3/3"), std::size_t{1});
  CHECK_EQ(log.count("3 sin X-Captura-IV"), std::size_t{1});
  CHECK_EQ(log.count("foto descartada"), std::size_t{0});  // nunca se decodifico
  CHECK_EQ(h.results().at(0).reason.value(), std::string(reasons::kCaptureFailed));
}

TEST(con_secreto_un_iv_mal_formado_o_un_cuerpo_corto_se_descartan_y_se_cuentan_por_motivo) {
  Harness h;
  enable_signing(h);
  h.http.on_call = nullptr;
  HttpResponse iv_malo = ok(noise_frame(1));
  iv_malo.resp_iv = "zz";
  HttpResponse corto;
  corto.status = 200;
  corto.body = std::vector<std::uint8_t>(10, 1);
  corto.resp_iv = iv_hex_numero(1);
  HttpResponse sin_iv = ok(noise_frame(3));
  h.http.responses = {iv_malo, corto, sin_iv};
  CapturedStderr log;

  h.pir();
  h.service->poll();

  CHECK_EQ(h.detector.calls, 0);
  CHECK_EQ(log.count("respuesta no autentica: 3/3"), std::size_t{1});
  CHECK_EQ(log.count("1 sin X-Captura-IV, 1 con un IV mal formado, 1 con menos de 16 bytes, 0 que"),
           std::size_t{1});
}

// Cada manera de que la respuesta no sea de ESTA peticion o de esta placa falla la etiqueta.
TEST(con_secreto_una_respuesta_que_no_verifica_descarta_la_foto_sin_decodificar) {
  struct Caso {
    const char* nombre;
    std::function<void(HttpResponse&, std::int64_t, std::uint64_t)> fabrica;
  };
  const Caso casos[] = {
      {"otro nonce (la respuesta de otra peticion)",
       [](HttpResponse& r, std::int64_t ts, std::uint64_t nonce) {
         encrypt_like_the_board(r, ts, nonce + 1);
       }},
      {"otro ts",
       [](HttpResponse& r, std::int64_t ts, std::uint64_t nonce) {
         encrypt_like_the_board(r, ts + 1, nonce);
       }},
      {"otro secreto",
       [](HttpResponse& r, std::int64_t ts, std::uint64_t nonce) {
         encrypt_like_the_board(r, ts, nonce, "otro-secreto-de-mas-de-32-caracteres-ok");
       }},
      {"etiqueta alterada",
       [](HttpResponse& r, std::int64_t ts, std::uint64_t nonce) {
         encrypt_like_the_board(r, ts, nonce);
         r.body.back() ^= 0x01;
       }},
      {"texto cifrado alterado un byte",
       [](HttpResponse& r, std::int64_t ts, std::uint64_t nonce) {
         encrypt_like_the_board(r, ts, nonce);
         r.body[r.body.size() / 2] ^= 0x01;
       }},
      {"IV distinto del que cifro",
       [](HttpResponse& r, std::int64_t ts, std::uint64_t nonce) {
         encrypt_like_the_board(r, ts, nonce);
         r.resp_iv = iv_hex_numero(99);
       }},
      {"foto en claro con un IV inventado",
       [](HttpResponse& r, std::int64_t, std::uint64_t) { r.resp_iv = iv_hex_numero(5); }},
  };
  for (const Caso& caso : casos) {
    Harness h;
    enable_signing(h);
    h.queue_frames(3);
    h.http.on_call = [&h, &caso] {
      if (h.http.responses.empty()) return;
      const auto& headers = h.http.requested_headers.back();
      caso.fabrica(h.http.responses.front(), std::stoll(header_value(headers, kHeaderCapturaTs)),
                   std::stoull(header_value(headers, kHeaderCapturaNonce)));
    };
    CapturedStderr log;

    h.pir();
    h.service->poll();

    CHECK_EQ(h.detector.calls, 0);
    CHECK_EQ(log.count("respuesta no autentica: 3/3"), std::size_t{1});
    CHECK_EQ(log.count("3 que no verifican"), std::size_t{1});
    CHECK_EQ(log.count("foto descartada"), std::size_t{0});
    CHECK_EQ(log.count(kSecretoPrueba), std::size_t{0});
    CHECK_EQ(h.results().at(0).reason.value(), std::string(reasons::kCaptureFailed));
  }
}

// La respuesta cifrada buena llega al detector; una sola mala de la rafaga no tumba las demas.
TEST(con_secreto_solo_se_descartan_las_fotos_que_no_verifican) {
  Harness h;
  enable_signing(h);
  h.queue_frames(3);
  // La 2a foto llega con un IV valido pero que no es el de su cifrado.
  h.http.responses[1].resp_iv = iv_hex_numero(77);
  h.http.responses[1].body = std::vector<std::uint8_t>(64, 7);
  CapturedStderr log;

  h.pir();
  h.service->poll();

  CHECK_EQ(h.results().at(0).account.value(), std::string("320068234"));
  CHECK_EQ(log.count("respuesta no autentica: 1/3"), std::size_t{1});
}

// El tope del cuerpo se aplica a lo que viaja (cifrado || etiqueta), 16 bytes mas que la foto.
TEST(con_secreto_el_tope_de_bytes_se_pasa_al_cliente_http_tal_cual) {
  Harness h;
  enable_signing(h);
  ServiceConfig config = h.config;
  config.max_body_bytes = 3000016;  // la foto mas los 16 bytes de la etiqueta
  h.restart(config);
  sign_responses_like_the_board(h);
  h.queue_frames(3);

  h.pir();
  h.service->poll();

  CHECK_EQ(h.http.requested_max_body_bytes.at(0), std::size_t{3000016});
  CHECK_EQ(h.results().at(0).account.value(), std::string("320068234"));
}

// Un texto vacio con etiqueta valida no es una foto: se descarta sin ser un fallo de
// autenticidad.
TEST(con_secreto_un_cuerpo_cifrado_vacio_no_es_una_foto) {
  Harness h;
  enable_signing(h);
  for (int i = 0; i < 3; ++i) {
    HttpResponse vacia;
    vacia.status = 200;
    h.http.responses.push_back(vacia);
  }
  CapturedStderr log;

  h.pir();
  h.service->poll();

  CHECK_EQ(log.count("respuesta no autentica"), std::size_t{0});
  CHECK_EQ(h.detector.calls, 0);
  CHECK_EQ(h.results().at(0).reason.value(), std::string(reasons::kCaptureFailed));
}

// Sin secreto no hay peticion firmada ni cifrado (camara_falsa sirve la JPEG en claro).
TEST(sin_secreto_no_se_exige_la_respuesta_cifrada) {
  Harness h;
  h.queue_frames(3);
  h.pir();
  h.service->poll();
  CHECK_EQ(h.results().at(0).account.value(), std::string("320068234"));
}

// --- Guardia del reloj del borde ---------------------------------------------------

// Un salto de reloj de pared hacia adelante (mas de 10 minutos de mas respecto del
// monotonico) no se firma: la placa quedaria con un "ultimo ts" en el futuro.
TEST(un_salto_del_reloj_de_pared_hacia_adelante_no_se_firma) {
  Harness h;
  enable_signing(h);
  h.wall_clock.epoch_seconds = 1793805153;
  h.queue_frames(6);

  h.pir();
  h.service->poll();  // la primera peticion tras arrancar siempre se permite
  CHECK_EQ(h.http.requested_urls.size(), std::size_t{3});

  h.wall_clock.epoch_seconds += 700;  // el monotonico no se movio: salto de ~11 minutos
  CapturedStderr log;
  h.pir();
  h.service->poll();

  CHECK_EQ(h.http.requested_urls.size(), std::size_t{3});  // no se pidio nada mas
  CHECK(log.count("salto hacia adelante") > 0);
  CHECK_EQ(h.results().at(1).reason.value(), std::string(reasons::kCaptureFailed));
}

// Hasta 10 minutos de mas se tolera; y lo que avanza el reloj monotonico se descuenta.
TEST(el_reloj_de_pared_puede_avanzar_con_el_monotonico_o_hasta_10_minutos_de_mas) {
  Harness h;
  enable_signing(h);
  h.wall_clock.epoch_seconds = 1793805153;
  h.queue_frames(9);

  h.pir();
  h.service->poll();
  CHECK_EQ(h.http.requested_urls.size(), std::size_t{3});

  h.wall_clock.epoch_seconds += 590;  // 590 s de pared y ~0 de monotonico: dentro del margen
  h.pir();
  h.service->poll();
  CHECK_EQ(h.http.requested_urls.size(), std::size_t{6});

  // Una hora de pared que SI paso en el monotonico (el servicio estuvo ocioso): normal.
  h.wall_clock.epoch_seconds += 3600;
  h.clock.advance(3600s);
  h.pir();
  h.service->poll();
  CHECK_EQ(h.http.requested_urls.size(), std::size_t{9});
}

// Un salto a una hora imposible (2100 en adelante) se rechaza aunque sea la primera peticion.
TEST(un_ts_mas_alla_del_techo_de_2100_no_se_firma) {
  Harness arriba;
  enable_signing(arriba);
  arriba.wall_clock.epoch_seconds = 4102444801;  // 2100-01-01T00:00:01Z
  arriba.queue_frames(3);
  CapturedStderr log;
  arriba.pir();
  arriba.service->poll();
  CHECK(arriba.http.requested_urls.empty());
  CHECK(log.count("no es plausible") > 0);

  Harness techo;
  enable_signing(techo);
  techo.wall_clock.epoch_seconds = 4102444800;  // el techo mismo
  techo.queue_frames(3);
  techo.pir();
  techo.service->poll();
  CHECK_EQ(techo.http.requested_urls.size(), std::size_t{3});
}

// --- Las poses del registro cuentan para el ritmo de la placa ----------------------

TEST(una_pose_del_registro_espacia_la_rafaga_que_viene_despues) {
  Harness h;
  ServiceConfig config = h.config;
  config.burst_interval = 3000ms;
  h.restart(config);
  std::vector<TimePoint> request_times;
  h.http.on_call = [&] { request_times.push_back(h.clock.now()); };
  h.queue_frames(5);

  h.enroll("100000001");
  h.service->poll();
  h.enrollment_step();  // captura la primera pose (una foto)
  CHECK_EQ(request_times.size(), std::size_t{1});
  h.service->shutdown();  // cierra el registro: la rafaga siguiente es del pase de lista

  h.pir();
  h.service->poll();

  CHECK_EQ(request_times.size(), std::size_t{4});
  CHECK(request_times[1] - request_times[0] >= 3000ms);
}

// --- La primera firma tras arrancar se ancla a algo absoluto -------------------------------

namespace {

// El servicio del arnes con las anclas del reloj conectadas (RTC, id de arranque y estado).
void connect_clock_anchors(Harness& h) {
  h.anchors_ptr = &h.anchors;
  h.restart(h.config);
  sign_responses_like_the_board(h);
}

}  // namespace

TEST(la_primera_firma_sin_coincidencia_con_el_rtc_no_se_firma) {
  Harness h;
  enable_signing(h);
  connect_clock_anchors(h);
  h.wall_clock.epoch_seconds = 1793805153;
  h.anchors.rtc = 1793805153 - 5000;  // el RTC no confirma la hora de pared
  h.queue_frames(3);
  CapturedStderr log;

  h.pir();
  h.service->poll();

  CHECK(h.http.requested_urls.empty());
  CHECK(log.count("RTC de hardware") > 0);
  CHECK(log.count("NTP") > 0);
  CHECK_EQ(log.count("reinicia el servicio"), std::size_t{0});
  CHECK_EQ(h.results().at(0).reason.value(), std::string(reasons::kCaptureFailed));
}

TEST(la_primera_firma_con_el_rtc_de_acuerdo_se_firma) {
  Harness h;
  enable_signing(h);
  connect_clock_anchors(h);
  h.wall_clock.epoch_seconds = 1793805153;
  h.anchors.rtc = 1793805153 + 30;
  h.queue_frames(3);

  h.pir();
  h.service->poll();

  CHECK_EQ(h.http.requested_urls.size(), std::size_t{3});
  CHECK_EQ(h.results().at(0).account.value(), std::string("320068234"));
}

// Un salto hacia adelante que el RTC confirma se acepta sin reiniciar el servicio.
TEST(un_salto_que_el_rtc_confirma_se_firma_sin_reiniciar) {
  Harness h;
  enable_signing(h);
  connect_clock_anchors(h);
  h.wall_clock.epoch_seconds = 1793805153;
  h.anchors.rtc = 1793805153;
  h.queue_frames(6);
  h.pir();
  h.service->poll();
  CHECK_EQ(h.http.requested_urls.size(), std::size_t{3});

  h.wall_clock.epoch_seconds += 4000;  // NTP corrige: el monotonico no se movio
  h.pir();
  h.service->poll();
  CHECK_EQ(h.http.requested_urls.size(), std::size_t{3});  // el RTC aun no lo confirma: no

  h.anchors.rtc = h.wall_clock.epoch_seconds;  // el RTC ya se puso al dia
  h.pir();
  h.service->poll();
  CHECK_EQ(h.http.requested_urls.size(), std::size_t{6});
}

// Una suspension: BOOTTIME avanza y el reloj monotonico no. Sin RTC, no es un salto.
TEST(una_suspension_no_se_confunde_con_un_salto_del_reloj) {
  Harness h;
  enable_signing(h);
  connect_clock_anchors(h);
  h.wall_clock.epoch_seconds = 1793805153;
  h.queue_frames(6);
  h.pir();
  h.service->poll();
  CHECK_EQ(h.http.requested_urls.size(), std::size_t{3});

  h.wall_clock.epoch_seconds += 7200;  // dos horas de pared...
  h.clock.suspend(7200s);              // ...que son dos horas de BOOTTIME, no de monotonico
  h.pir();
  h.service->poll();
  CHECK_EQ(h.http.requested_urls.size(), std::size_t{6});
}

// Sin RTC, el archivo de estado ancla la primera firma tras un "reinicio del equipo".
TEST(sin_rtc_el_estado_guardado_ancla_la_primera_firma_tras_un_reinicio) {
  Harness h;
  enable_signing(h);
  connect_clock_anchors(h);
  h.wall_clock.epoch_seconds = 1793805153;
  h.queue_frames(6);
  CapturedStderr primera;
  h.pir();
  h.service->poll();
  CHECK_EQ(h.http.requested_urls.size(), std::size_t{3});
  CHECK(h.anchors.estado.has_value());                       // se dejo el estado
  CHECK(primera.count("primera instalacion") > 0);           // y se aviso de que no habia ancla

  // El equipo se reinicia: otro boot_id y el reloj arranca con una hora vieja.
  h.anchors.boot = "bbbbbbbb-0000-0000-0000-000000000002";
  h.wall_clock.epoch_seconds = 1793805153 - 86400;
  h.restart(h.config);
  sign_responses_like_the_board(h);
  CapturedStderr tras;
  h.pir();
  h.service->poll();
  CHECK_EQ(h.http.requested_urls.size(), std::size_t{3});  // no se pidio nada mas
  CHECK(tras.count("anterior a la ultima firma") > 0);

  // NTP sincroniza: la hora ya es posterior a la guardada.
  h.wall_clock.epoch_seconds = 1793805153 + 600;
  h.pir();
  h.service->poll();
  CHECK_EQ(h.http.requested_urls.size(), std::size_t{6});
}

// Estado corrupto o ausente: se firma con un aviso fuerte (ver reloj_guardia.hpp).
TEST(con_el_estado_corrupto_se_firma_con_un_aviso) {
  Harness h;
  enable_signing(h);
  connect_clock_anchors(h);
  h.wall_clock.epoch_seconds = 1793805153;
  h.anchors.estado = "###";
  h.queue_frames(3);
  CapturedStderr log;

  h.pir();
  h.service->poll();

  CHECK_EQ(h.http.requested_urls.size(), std::size_t{3});
  CHECK(log.count("AVISO") > 0);
  CHECK(log.count("corrupto") > 0);
}
