// ARNES DE RECORRIDO EN SECO, NO el binario de produccion: cablea la maquina de estados
// real con los dobles en memoria (sin camara, NPU ni broker) y reproduce un pase de lista
// y un registro. Se queda asi hasta que esten los adaptadores reales de MQTT y HTTP y los
// modelos; entonces aqui se cablean en lugar de los dobles.
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
    std::cout << "cuenta=" << r.account.value_or("-") << " conf=" << r.confidence
              << " motivo=" << r.reason.value_or("-") << " modo=" << to_wire(r.mode) << " lat_ms{cap=" << r.lat_ms.cap
              << " det=" << r.lat_ms.det << " emb=" << r.lat_ms.emb
              << " total=" << r.lat_ms.total << "}";
  }
  void operator()(const EnrollmentProgress& p) const {
    std::cout << "cuenta=" << p.account << " paso=" << p.step << "/" << p.total
              << " pose=" << p.pose << " estado=" << static_cast<int>(p.status)
              << " motivo=" << p.reason.value_or("-");
  }
  void operator()(const EmbeddingMessage& m) const {
    std::cout << "cuenta=" << m.account << " dim=" << m.embedding.size();
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

  FaceDetection face;
  face.score = 0.99f;
  for (std::size_t i = 0; i < face.landmarks.size(); ++i) {
    face.landmarks[i] = kArcFaceReference[i] * 2.0f + cv::Point2f(200.0f, 100.0f);
  }
  detector.detections = {face};
  embedder.outputs = {{0.9f, 0.1f, 0.2f, 0.3f}};
  gallery.upsert("320068234", embedder.outputs.front());

  std::vector<std::uint8_t> jpeg;
  cv::imencode(".jpg", synthetic_frame(), jpeg);
  http.on_call = [&] { http.responses.push_back({200, jpeg}); };

  ServiceConfig config;
  config.capture_url = "http://fake/capture";
  config.pose_delay = Millis(100);
  VisionService service(ServiceDeps{bus, http, detector, embedder, gallery, clock}, config);

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
  bus.inject(topics::kVisionCmd, VisionCommand{VisionAction::Enroll, "320068235"});
  while (true) {
    service.poll();
    show_new_messages();
    if (service.state() == ServiceState::Idle) break;
    std::this_thread::sleep_for(Millis(20));
  }
  return 0;
}
