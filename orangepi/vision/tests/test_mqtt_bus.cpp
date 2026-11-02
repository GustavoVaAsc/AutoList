// Prueba de integracion del bus MQTT: necesita un broker real.
//
// El broker se toma de la variable de entorno VISION_BROKER ("host:puerto"). Si no
// esta, la prueba se omite con un mensaje claro y no falla. Tambien se puede fijar al
// configurar CMake (-DVISION_BROKER=host:puerto). La prueba lleva la etiqueta
// `integration`: se puede excluir con ctest -LE integration.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include <unistd.h>

#include "minitest.hpp"
#include "vision/mqtt_bus.hpp"

namespace {

using namespace vision;
using SteadyClock = std::chrono::steady_clock;

struct Broker {
  std::string host = "127.0.0.1";
  int port = 1883;
  bool configured = false;
};

Broker broker_from_environment() {
  Broker broker;
  const char* value = std::getenv("VISION_BROKER");
  if (value == nullptr || *value == '\0') return broker;
  const std::string text(value);
  const std::size_t colon = text.rfind(':');
  if (colon == std::string::npos) {
    broker.host = text;
  } else {
    broker.host = text.substr(0, colon);
    broker.port = std::atoi(text.c_str() + colon + 1);
  }
  broker.configured = true;
  return broker;
}

// Sin broker de pruebas, la prueba se omite (no falla) con un mensaje claro.
bool hay_broker(Broker& broker) {
  broker = broker_from_environment();
  if (!broker.configured) {
    std::cout << "[SKIP] bus MQTT: no hay broker de pruebas (VISION_BROKER=host:puerto)\n";
    return false;
  }
  return true;
}

MosquittoOptions options_para(const Broker& broker, const std::string& sufijo) {
  MosquittoOptions options;
  options.host = broker.host;
  options.port = broker.port;
  options.client_id = "vision-prueba-" + sufijo + "-" + std::to_string(::getpid());
  return options;
}

// Espera a que se cumpla una condicion, con plazo.
template <typename Predicate>
bool wait_until(Predicate predicate, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return predicate();
}

// Dato recibido por el manejador, protegido con su propio mutex (el manejador corre en
// el hilo de red del bus).
class Received {
 public:
  void put(std::string value) {
    {
      std::lock_guard lock(mutex_);
      value_ = std::move(value);
      ready_ = true;
    }
    cv_.notify_all();
  }

  bool wait(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    return cv_.wait_for(lock, timeout, [this] { return ready_; });
  }

  [[nodiscard]] std::string value() {
    std::lock_guard lock(mutex_);
    return value_;
  }

  void clear() {
    std::lock_guard lock(mutex_);
    value_.clear();
    ready_ = false;
  }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  bool ready_ = false;
  std::string value_;
};

}  // namespace

TEST(mqtt_bus_integracion) {
  Broker broker;
  if (!hay_broker(broker)) return;

  MosquittoOptions options = options_para(broker, "integracion");
  MosquittoBus bus(options);

  // Las suscripciones se hacen ANTES de start(): es el unico patron seguro (ver la
  // restriccion en mqtt_bus.hpp).
  // --- recibir una orden en aula/vision/cmd ---------------------------------
  std::atomic<int> comandos{0};
  Received orden;
  Subscription sub_cmd = bus.subscribe(topics::kVisionCmd, [&](const Payload& payload) {
    const auto* command = std::get_if<VisionCommand>(&payload);
    if (command == nullptr) return;
    ++comandos;
    orden.put(std::string(to_wire(command->action)));
  });

  // --- recibir un resultado en aula/vision/resultado -------------------------
  std::atomic<int> resultados{0};
  Received resultado;
  Subscription sub_result = bus.subscribe(topics::kVisionResult, [&](const Payload& payload) {
    const auto* value = std::get_if<AttendanceResult>(&payload);
    if (value == nullptr) return;
    ++resultados;
    resultado.put(std::string(to_wire(value->decision)));
  });

  bus.start();

  CHECK(wait_until([&bus] { return bus.connected(); }, std::chrono::milliseconds(3000)));

  VisionCommand command;
  command.action = VisionAction::Enroll;
  command.account = "320068234";
  command.name = "Ana Lopez";
  bus.publish(topics::kVisionCmd, command);

  CHECK(orden.wait(std::chrono::milliseconds(3000)));
  CHECK_EQ(orden.value(), std::string("enrolar"));
  CHECK_EQ(comandos.load(), 1);

  AttendanceResult attendance(AttendanceMode::Test);
  attendance.timestamp = "2026-11-04T09:12:33.120-06:00";
  attendance.decision = Decision::Recognized;
  attendance.account = "320068234";
  attendance.confidence = 0.78f;
  attendance.margin = 0.21f;
  attendance.lat_ms.cap = 180;
  attendance.lat_ms.det = 12;
  attendance.lat_ms.emb = 6;
  attendance.lat_ms.total = 240;
  bus.publish(topics::kVisionResult, attendance);

  CHECK(resultado.wait(std::chrono::milliseconds(3000)));
  CHECK_EQ(resultado.value(), std::string("reconocido"));
  CHECK_EQ(resultados.load(), 1);

  // --- la baja de la suscripcion deja de entregar ----------------------------
  sub_cmd.reset();
  orden.clear();
  bus.publish(topics::kVisionCmd, command);
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  CHECK_EQ(comandos.load(), 1);

  sub_result.reset();
  bus.stop();
}

// El contrato de Subscription: cuando la baja retorna, ningun manejador de esa
// suscripcion esta en curso ni volvera a ejecutarse. La baja espera al manejador.
TEST(mqtt_bus_baja_espera_al_manejador) {
  Broker broker;
  if (!hay_broker(broker)) return;

  MosquittoOptions options = options_para(broker, "baja");
  MosquittoBus bus(options);

  std::atomic<int> llamadas{0};
  std::atomic<bool> en_curso{false};
  std::atomic<bool> terminado{false};

  // La suscripcion se hace antes de start() (ver la restriccion en mqtt_bus.hpp).
  Subscription sub = bus.subscribe(topics::kVisionCmd, [&](const Payload&) {
    ++llamadas;
    en_curso.store(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    terminado.store(true);
  });

  bus.start();

  VisionCommand command;
  command.action = VisionAction::Enroll;
  command.account = "320068234";
  command.name = "Ana Lopez";
  bus.publish(topics::kVisionCmd, command);

  CHECK(wait_until([&] { return en_curso.load(); }, std::chrono::milliseconds(3000)));

  const SteadyClock::time_point inicio = SteadyClock::now();
  sub.reset();  // la baja espera a que termine el manejador en curso
  const long long ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(SteadyClock::now() - inicio)
          .count();
  CHECK(terminado.load());
  CHECK(ms >= 400);

  // Y el manejador no vuelve a ejecutarse.
  bus.publish(topics::kVisionCmd, command);
  std::this_thread::sleep_for(std::chrono::milliseconds(800));
  CHECK_EQ(llamadas.load(), 1);

  bus.stop();
}

// La baja pedida desde el propio hilo del manejador no puede esperar a si misma:
// no se bloquea (el hilo de red seguiria vivo).
TEST(mqtt_bus_baja_desde_el_propio_manejador_no_se_bloquea) {
  Broker broker;
  if (!hay_broker(broker)) return;

  MosquittoOptions options = options_para(broker, "autobaja");
  MosquittoBus bus(options);

  std::atomic<int> llamadas{0};
  // El manejador necesita su propia Subscription: se guarda en un puntero compartido
  // que se rellena justo despues de suscribirse.
  auto sub = std::make_shared<Subscription>();
  *sub = bus.subscribe(topics::kVisionCmd, [&](const Payload&) {
    ++llamadas;
    sub->reset();  // baja desde el propio manejador
  });

  // La segunda suscripcion tambien antes de start().
  std::atomic<int> otras{0};
  Subscription otra = bus.subscribe(topics::kVisionResult, [&](const Payload&) { ++otras; });

  bus.start();

  VisionCommand command;
  command.action = VisionAction::Enroll;
  command.account = "320068234";
  command.name = "Ana Lopez";
  bus.publish(topics::kVisionCmd, command);

  CHECK(wait_until([&] { return llamadas.load() >= 1; }, std::chrono::milliseconds(3000)));

  // El bus sigue vivo: otra suscripcion recibe mensajes.
  AttendanceResult attendance(AttendanceMode::Test);
  attendance.timestamp = "2026-11-04T09:12:33.120-06:00";
  attendance.decision = Decision::Recognized;
  attendance.account = "320068234";
  attendance.confidence = 0.78f;
  attendance.margin = 0.21f;
  bus.publish(topics::kVisionResult, attendance);
  CHECK(wait_until([&] { return otras.load() >= 1; }, std::chrono::milliseconds(3000)));

  otra.reset();
  bus.stop();
}
