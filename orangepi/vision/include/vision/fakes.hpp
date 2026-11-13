#pragma once

// Dobles en memoria de las interfaces del servicio, para pruebas y para el
// recorrido en seco de vision_service. No hay adaptadores reales de MQTT/HTTP.

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "vision/clock.hpp"
#include "vision/face_detector.hpp"
#include "vision/face_embedder.hpp"
#include "vision/http_client.hpp"
#include "vision/message_bus.hpp"
#include "vision/reloj_guardia.hpp"
#include "vision/wall_clock.hpp"

namespace vision::fakes {

// Reloj manual: sleep_for solo adelanta la hora y deja registro de lo que se pidio. La hora
// esta protegida por un mutex porque Clock::now() se llama tambien desde el hilo del bus;
// `sleeps` y los hooks son solo del hilo de la prueba.
class ManualClock final : public Clock {
 public:
  TimePoint now() const override {
    if (on_now) on_now();
    std::lock_guard lock(mutex_);
    return now_;
  }
  void sleep_for(Millis duration) override {
    sleeps.push_back(duration);
    if (on_sleep) on_sleep();
    advance(duration);
  }
  // BOOTTIME: lo que cuenta now() mas el tiempo de suspension (que now() no cuenta).
  Millis boottime() const override {
    std::lock_guard lock(mutex_);
    return std::chrono::duration_cast<Millis>(now_.time_since_epoch()) + suspended_;
  }
  void advance(Millis duration) {
    std::lock_guard lock(mutex_);
    now_ += duration;
  }
  // El equipo se suspende: BOOTTIME avanza y el reloj monotonico no.
  void suspend(Millis duration) {
    std::lock_guard lock(mutex_);
    suspended_ += duration;
  }

  std::vector<Millis> sleeps;
  std::function<void()> on_sleep;       // puede lanzar para simular un fallo
  std::function<void()> on_now;         // idem, al leer la hora

 private:
  mutable std::mutex mutex_;
  TimePoint now_{};
  Millis suspended_{0};
};

struct PublishedMessage {
  std::string topic;
  Payload payload;
};
// Reloj de pared fijo: devuelve siempre la misma hora, para que las pruebas puedan
// comprobar el "ts" sin depender del reloj de la placa.
class FixedWallClock final : public WallClock {
 public:
  std::string now_iso8601() const override {
    if (on_now) on_now();
    return value;
  }
  // El epoch del mismo instante que "value" (2026-11-04T09:12:33-06:00). Es un campo
  // aparte: las pruebas que cambian "value" no mueven el epoch, y al reves.
  std::int64_t now_epoch_seconds() const override {
    if (on_now) on_now();
    return epoch_seconds;
  }

  std::string value = "2026-11-04T09:12:33.120-06:00";
  std::int64_t epoch_seconds = 1793805153;
  std::function<void()> on_now;  // puede lanzar para simular un fallo
};

// Anclas del reloj en memoria: el RTC, el id de arranque y el archivo de estado que las
// pruebas controlan (y pueden "reiniciar" cambiando boot_id sin tocar el estado).
class FakeAnclasReloj final : public AnclasReloj {
 public:
  std::optional<std::int64_t> rtc_epoch_seconds() override { return rtc; }
  std::optional<std::string> boot_id() override { return boot; }
  std::optional<std::string> leer_estado() override { return estado; }
  bool guardar_estado(const std::string& contenido) override {
    ++escrituras;
    if (!se_puede_guardar) return false;
    estado = contenido;
    return true;
  }

  std::optional<std::int64_t> rtc;
  std::optional<std::string> boot = "aaaaaaaa-0000-0000-0000-000000000001";
  std::optional<std::string> estado;
  bool se_puede_guardar = true;
  int escrituras = 0;
};

// Entrega los mensajes en el hilo que llama a inject. Cumple el contrato de MessageBus:
// la baja de una suscripcion espera a un handler en curso y, al retornar, ya no se
// invoca. EXCEPCION (como en el bus real): si la baja se pide desde el propio handler,
// no se espera (el handler no puede terminar hasta que la baja vuelva) y se avisa por
// stderr. publish no usa ese mutex.
class FakeMessageBus final : public MessageBus {
 public:
  void publish(std::string_view topic, const Payload& payload) override {
    if (on_publish) on_publish(topic, payload);
    published.push_back({std::string(topic), payload});
  }

  Subscription subscribe(std::string_view topic, MessageHandler handler) override {
    if (on_subscribe) on_subscribe(topic);
    std::lock_guard lock(mutex_);
    const int id = next_id_++;
    handlers_.push_back({id, std::string(topic), std::move(handler)});
    return Subscription([this, id] { baja(id); });
  }

  // Simula un mensaje que llega del broker. Devuelve a cuantos handlers se entrego.
  std::size_t inject(std::string_view topic, const Payload& payload) {
    std::vector<MessageHandler> objetivo;
    {
      std::lock_guard lock(mutex_);
      for (const Entry& entry : handlers_) {
        if (entry.topic == topic) objetivo.push_back(entry.handler);
      }
      if (objetivo.empty()) return 0;
      ++en_curso_;
      if (en_curso_ == 1) hilo_manejador_ = std::this_thread::get_id();
    }
    // El handler se llama sin el mutex: asi un handler puede darse de baja a si mismo
    // (la excepcion del contrato) sin bloquearse.
    struct Salida {
      FakeMessageBus* bus;
      ~Salida() {
        std::lock_guard lock(bus->mutex_);
        if (--bus->en_curso_ == 0) {
          bus->hilo_manejador_ = std::thread::id();
          bus->aviso_.notify_all();
        }
      }
    } salida{this};

    std::size_t delivered = 0;
    for (const MessageHandler& handler : objetivo) {
      handler(payload);
      ++delivered;
    }
    return delivered;
  }

  std::size_t subscriber_count() const {
    std::lock_guard lock(mutex_);
    return handlers_.size();
  }

  // Payloads de tipo T publicados en un topico, en orden.
  template <typename T>
  std::vector<T> published_as(std::string_view topic) const {
    std::vector<T> out;
    for (const PublishedMessage& m : published) {
      if (m.topic == topic) {
        if (const T* value = std::get_if<T>(&m.payload)) out.push_back(*value);
      }
    }
    return out;
  }

  std::vector<PublishedMessage> published;
  // Se ejecutan antes de registrar la publicacion o la suscripcion; pueden lanzar para
  // simular un fallo del broker (el mensaje fallido no queda en `published`).
  std::function<void(std::string_view topic, const Payload& payload)> on_publish;
  std::function<void(std::string_view topic)> on_subscribe;

 private:
  struct Entry {
    int id;
    std::string topic;
    MessageHandler handler;
  };

  // La baja de una suscripcion: espera a los handlers en curso, salvo que la baja venga
  // del propio handler (el caso excepcion del contrato).
  void baja(int id) {
    std::unique_lock lock(mutex_);
    const bool desde_el_propio_manejador =
        en_curso_ > 0 && hilo_manejador_ == std::this_thread::get_id();
    if (desde_el_propio_manejador) {
      std::cerr << "[vision] la baja se pidio desde su propio manejador: no se espera a "
                   "que termine\n";
      std::erase_if(handlers_, [id](const Entry& e) { return e.id == id; });
      return;
    }
    aviso_.wait(lock, [this] { return en_curso_ == 0; });
    std::erase_if(handlers_, [id](const Entry& e) { return e.id == id; });
  }

  mutable std::mutex mutex_;
  std::condition_variable aviso_;
  std::vector<Entry> handlers_;
  int next_id_ = 0;
  // Handlers en curso (en total) y el hilo que los esta ejecutando.
  int en_curso_ = 0;
  std::thread::id hilo_manejador_;
};

// Responde con la cola de respuestas; sin respuestas devuelve status 0 (error de red).
class FakeHttpClient final : public HttpClient {
 public:
  HttpResponse get(const std::string& url, Millis timeout, std::size_t max_body_bytes) override {
    requested_urls.push_back(url);
    requested_timeouts.push_back(timeout);
    requested_max_body_bytes.push_back(max_body_bytes);
    if (on_call) on_call();
    if (responses.empty()) return {};
    HttpResponse response = std::move(responses.front());
    responses.pop_front();
    return response;
  }

  // Con cabeceras: se registran y se responde igual que sin ellas.
  HttpResponse get(const std::string& url, Millis timeout, std::size_t max_body_bytes,
                   const std::vector<std::string>& headers) override {
    requested_headers.push_back(headers);
    return get(url, timeout, max_body_bytes);
  }

  [[nodiscard]] bool supports_request_headers() const noexcept override { return true; }

  std::deque<HttpResponse> responses;
  std::vector<std::string> requested_urls;
  std::vector<Millis> requested_timeouts;
  std::vector<std::size_t> requested_max_body_bytes;
  std::vector<std::vector<std::string>> requested_headers;
  std::function<void()> on_call;  // se ejecuta antes de responder
};

class FakeFaceDetector final : public FaceDetector {
 public:
  std::vector<FaceDetection> detect(const cv::Mat& bgr) override {
    ++calls;
    last_input = bgr.clone();
    if (on_call) on_call();
    return detections;
  }

  std::vector<FaceDetection> detections;
  int calls = 0;
  cv::Mat last_input;
  std::function<void()> on_call;  // puede lanzar para simular un fallo
};

// Devuelve outputs[i] en la llamada i y repite el ultimo despues.
class FakeFaceEmbedder final : public FaceEmbedder {
 public:
  std::vector<float> embed(const cv::Mat& aligned_bgr_112) override {
    last_input_size = aligned_bgr_112.size();
    if (on_call) on_call();
    if (outputs.empty()) throw std::logic_error("FakeFaceEmbedder: outputs vacio");
    const std::size_t index = std::min<std::size_t>(calls++, outputs.size() - 1);
    return outputs[index];
  }

  std::vector<std::vector<float>> outputs;
  std::size_t calls = 0;
  cv::Size last_input_size;
  std::function<void()> on_call;
};

}  // namespace vision::fakes
