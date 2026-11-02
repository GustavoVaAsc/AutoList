#pragma once

#include <functional>
#include <string_view>
#include <utility>

#include "vision/mqtt_contract.hpp"

namespace vision {

using MessageHandler = std::function<void(const Payload&)>;

// Baja de una suscripcion: se da de baja al destruirse o con reset(). Solo se puede
// mover. Debe destruirse antes que el bus que la creo.
class Subscription {
 public:
  using Release = std::function<void()>;

  Subscription() = default;
  explicit Subscription(Release release) : release_(std::move(release)) {}
  Subscription(Subscription&& other) noexcept : release_(std::exchange(other.release_, nullptr)) {}
  Subscription& operator=(Subscription&& other) noexcept {
    if (this != &other) {
      reset();
      release_ = std::exchange(other.release_, nullptr);
    }
    return *this;
  }
  Subscription(const Subscription&) = delete;
  Subscription& operator=(const Subscription&) = delete;
  ~Subscription() { reset(); }

  void reset() noexcept {
    if (Release release = std::exchange(release_, nullptr)) release();
  }
  explicit operator bool() const { return static_cast<bool>(release_); }

 private:
  Release release_;
};

// La libreria MQTT aun no esta decidida; solo existe esta interfaz y un fake en memoria.
// Los payloads son structs tipados: el adaptador concreto se encarga del JSON.
//
// Hilos: los handlers pueden invocarse desde un hilo distinto al que llama a publish.
// Vida: la Subscription devuelta por subscribe() es la unica forma de darse de baja.
// Cuando su baja retorna, ningun handler de esa suscripcion esta ejecutandose ni volvera
// a ejecutarse (la baja espera a un handler en curso), asi que el propietario del handler ya
// puede destruir lo que el handler captura.
//
// EXCEPCION: si la baja se pide desde el propio handler, no se espera (el handler no
// puede terminar hasta que la baja vuelva) y se avisa por stderr. En ese caso el handler
// sigue hasta que termina y el propietario todavia no puede destruir lo que el handler
// captura. La baja no lanza excepciones.
class MessageBus {
 public:
  virtual ~MessageBus() = default;
  virtual void publish(std::string_view topic, const Payload& payload) = 0;
  [[nodiscard]] virtual Subscription subscribe(std::string_view topic, MessageHandler handler) = 0;
};

}  // namespace vision
