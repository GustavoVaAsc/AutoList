// ARNES DE RECORRIDO EN SECO, NO el binario de produccion: cablea la maquina de estados
// real con los dobles en memoria (sin camara, NPU ni broker) y reproduce un pase de lista
// y un registro. Los adaptadores reales de MQTT y HTTP ya existen (MosquittoBus,
// HttplibClient y GalleryStore) y los usa el proceso de produccion; aqui lo unico que
// se sustituye son el detector y el embedder, que siguen sin existir sobre la NPU.
#include <iostream>
#include <thread>

#include <opencv2/imgcodecs.hpp>

#include "vision/alignment.hpp"
#include "vision/fakes.hpp"
#include "vision/vision_service.hpp"

namespace {

using namespace vision;

struct Printer {
  void operator()(const ActuatorCommand& c) const { std::cout << "luz=" << c.light.value_or(-1); }
  void operator()(const AttendanceResult& r) const {
    std::cout << "ts=" << r.timestamp << " cuenta=" << r.account.value_or("-")
              << " conf=" << r.confidence.value_or(-1.0f) << " decision="
              << to_wire(r.decision) << " motivo=" << r.reason.value_or("-")
              << " modo=" << to_wire(r.mode) << " lat_ms{cap=" << r.lat_ms.cap.value_or(-1)
              << " det=" << r.lat_ms.det.value_or(-1) << " emb=" << r.lat_ms.emb.value_or(-1)
              << " total=" << r.lat_ms.total.value_or(-1) << "}";
  }
  void operator()(const EnrollmentProgress& p) const {
    std::cout << "ts=" << p.timestamp << " cuenta=" << p.account << " paso=" << p.step << "/"
              << p.total << " pose=" << p.pose << " estado=" << to_wire(p.status)
              << " motivo=" << p.reason.value_or("-");
  }
  void operator()(const EmbeddingMessage& m) const {
    std::cout << "ts=" << m.timestamp << " cuenta=" << m.account << " dim=" << m.dim()
              << " modelo=" << m.model;
  }
  void operator()(const VisionStatus& s) const {
    std::cout << "ts=" << s.timestamp << " en_linea=" << s.online << " modo=" << to_wire(s.mode)
              << " galeria=" << s.gallery_size << " modelo=" << s.model;
  }
  void operator()(const VisionCommand& c) const {
    std::cout << "accion=" << to_wire(c.action) << " cuenta=" << c.account;
  }
  void operator()(const GalleryAccount& g) const {
    std::cout << "cuenta=" << g.account;
  }
  void operator()(const auto&) const {}
};

cv::Mat synthetic_frame() {
  cv::Mat frame(480, 640, CV_8UC3);
  cv::randu(frame, cv::Scalar::all(0), cv::Scalar::all(255));
  return frame;
}

}  // namespace

int main() {
  fakes::FakeMessageBus bus;
  fakes::FakeHttpClient http;
  fakes::FakeFaceDetector detector;
  fakes::FakeFaceEmbedder embedder;
  Gallery gallery;
  SystemClock clock;
  SystemWallClock wall_clock;

  FaceDetection face;
  face.score = 0.99f;
  for (std::size_t i = 0; i < face.landmarks.size(); ++i) {
    face.landmarks[i] = kArcFaceReference[i] * 2.0f + cv::Point2f(200.0f, 100.0f);
  }
  detector.detections = {face};
  // La cabeza sigue la pose que pide el registro (si no, el servicio rechazaria izquierda,
  // derecha y arriba por no estar en la pose).
  fakes::follow_enrollment_pose(detector, bus);
  embedder.outputs = {{0.9f, 0.1f, 0.2f, 0.3f}};
  gallery.upsert("320068234", embedder.outputs.front());

  std::vector<std::uint8_t> jpeg;
  cv::imencode(".jpg", synthetic_frame(), jpeg);
  http.on_call = [&] { http.responses.push_back({200, jpeg}); };

  ServiceConfig config;
  config.capture_url = "http://fake/capture";
  config.pose_delay = Millis(100);
  VisionService service(ServiceDeps{bus, http, detector, embedder, gallery, clock, wall_clock},
                        config);

  std::size_t shown = 0;
  const auto show_new_messages = [&] {
    for (; shown < bus.published.size(); ++shown) {
      const fakes::PublishedMessage& m = bus.published[shown];
      std::cout << "  " << m.topic << "  ";
      std::visit(Printer{}, m.payload);
      std::cout << '\n';
    }
  };

  std::cout << "PIR -> pase de lista\n";
  bus.inject(topics::kCameraPir, PirEvent{});
  service.poll();
  show_new_messages();

  std::cout << "enrolar\n";
  // El embedder falso da el mismo vector siempre: con la cuenta de arriba en la galeria, el
  // registro cerraria como duplicado. Se quita para recorrer el registro completo.
  gallery.remove("320068234");
  bus.inject(topics::kVisionCmd, VisionCommand{VisionAction::Enroll, "320068235", std::nullopt});
  while (true) {
    service.poll();
    show_new_messages();
    if (service.state() == ServiceState::Idle) break;
    std::this_thread::sleep_for(Millis(20));
  }
  return 0;
}
