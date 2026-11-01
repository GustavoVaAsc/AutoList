#pragma once

// Dobles en memoria de las interfaces del servicio, para pruebas y para el
// recorrido en seco de vision_service. No hay adaptadores reales de MQTT/HTTP.

#include <algorithm>
#include <deque>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "vision/clock.hpp"
#include "vision/face_detector.hpp"
#include "vision/face_embedder.hpp"
#include "vision/http_client.hpp"
#include "vision/message_bus.hpp"
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
  void advance(Millis duration) {
    std::lock_guard lock(mutex_);
    now_ += duration;
  }

  std::vector<Millis> sleeps;
  std::function<void()> on_sleep;       // puede lanzar para simular un fallo
  std::function<void()> on_now;         // idem, al leer la hora

 private:
  mutable std::mutex mutex_;
  TimePoint now_{};
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

  std::string value = "2026-11-04T09:12:33.120-06:00";
  std::function<void()> on_now;  // puede lanzar para simular un fallo
};

// Entrega los mensajes en el hilo que llama a inject. Cumple el contrato de MessageBus:
// la baja de una suscripcion toma el mismo mutex que la entrega, asi que espera a un
// handler en curso y, al retornar, ya no se invoca. Por eso un handler no puede darse de
// baja ni llamar a inject (se bloquearia); publish no usa ese mutex.
class FakeMessageBus final : public MessageBus {
 public:
  void publish(std::string_view topic, const Payload& payload) override {
    if (on_publish) on_publish(topic, payload);
    published.push_back({std::string(topic), payload});
  }

  Subscription subscribe(std::string_view topic, MessageHandler handler) override {
    if (on_subscribe) on_subscribe(topic);
    std::lock_guard lock(dispatch_mutex_);
    const int id = next_id_++;
    handlers_.push_back({id, std::string(topic), std::move(handler)});
    return Subscription([this, id] {
      std::lock_guard guard(dispatch_mutex_);
      std::erase_if(handlers_, [id](const Entry& e) { return e.id == id; });
    });
  }

  // Simula un mensaje que llega del broker. Devuelve a cuantos handlers se entrego.
  std::size_t inject(std::string_view topic, const Payload& payload) {
    std::lock_guard lock(dispatch_mutex_);
    std::size_t delivered = 0;
    for (const Entry& entry : handlers_) {
      if (entry.topic == topic) {
        entry.handler(payload);
        ++delivered;
      }
    }
    return delivered;
  }

  std::size_t subscriber_count() const {
    std::lock_guard lock(dispatch_mutex_);
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

  mutable std::mutex dispatch_mutex_;
  std::vector<Entry> handlers_;
  int next_id_ = 0;
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
