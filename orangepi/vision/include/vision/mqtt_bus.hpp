#pragma once

// Adaptador de MessageBus sobre libmosquitto (el broker del borde).
//
// Reparte los mensajes del contrato MQTT (docs/contrato-mqtt.md v0.3) al hilo de red
// de libmosquitto, que es distinto del que publica. Cumple el contrato de Subscription:
// cuando la baja retorna, ningun manejador de esa suscripcion esta en curso ni volvera
// a ejecutarse.
//
// Vida: el bus se destruye despues de sus suscripciones (la Subscription debe quedar
// dada de baja antes que el bus).

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "vision/message_bus.hpp"
#include "vision/mqtt_contract.hpp"

// Tipos de libmosquitto, solo declarados: la cabecera no arrastra mosquitto.h.
struct mosquitto;
struct mosquitto_message;

namespace vision {

// Ultimo Will del servicio de vision. El contrato lo fija en aula/vision/estado
// (retenido, QoS 1) con {"en_linea":false}: si la conexion se cae sin cerrar limpio,
// el broker publica ese mensaje y el frontend ve que la vision ya no esta en linea.
struct Will {
  std::string topic;
  std::string payload;
  int qos = 1;
  bool retain = true;
};

struct MosquittoOptions {
  std::string host = "127.0.0.1";
  int port = 1883;
  std::string user;      // vacio = sin usuario (broker de pruebas)
  std::string password;  // nunca se registra en ningun log
  std::string client_id = "vision";
  int keepalive_seconds = 30;
  // Plazo que espera start() a que el broker acepte la conexion (CONNACK). Pasado el
  // plazo, start() lanza: es preferible saberlo al arrancar que no publicar sin
  // conexion.
  int connect_timeout_ms = 5000;
  // Por omision, el Will del contrato (aula/vision/estado, {"en_linea":false}).
  std::optional<Will> will =
      Will{std::string(topics::kVisionStatus), R"({"en_linea":false})", 1, true};
};

// QoS y retencion por topico, segun la seccion 2 del contrato. Los topicos que el
// servicio publica salen retenidos solo si el contrato lo dice (aula/vision/estado).
struct TopicRule {
  std::string_view topic;
  int qos;
  bool retain;
};

// Reglas de los topicos del contrato (seccion 2 de docs/contrato-mqtt.md).
const TopicRule* find_topic_rule(std::string_view topic);

class MosquittoBus final : public MessageBus {
 public:
  explicit MosquittoBus(MosquittoOptions options);
  ~MosquittoBus() override;

  MosquittoBus(const MosquittoBus&) = delete;
  MosquittoBus& operator=(const MosquittoBus&) = delete;
  MosquittoBus(MosquittoBus&&) = delete;
  MosquittoBus& operator=(MosquittoBus&&) = delete;

  // Conecta con el broker y arranca el hilo de red. Espera a que el broker acepte la
  // conexion (CONNACK): al volver, el bus ya esta conectado. Lanza std::runtime_error
  // si no se puede conectar (aun asi, el destructor libera lo que se haya abierto).
  void start();

  // Para el hilo de red, cancela las suscripciones y desconecta. Es idempotente.
  void stop();

  // true si hay conexion con el broker ahora mismo.
  [[nodiscard]] bool connected() const;

  // Publica el payload en el topico, con el QoS (y la retencion) que fija el contrato.
  // Lanza std::runtime_error si el payload no se puede serializar o si no hay conexion
  // con el broker: el servicio captura la excepcion y sigue.
  void publish(std::string_view topic, const Payload& payload) override;

  // Se suscribe a un topico del contrato. Los manejadores se invocan desde el hilo de
  // red. La Subscription devuelta es la unica forma de darse de baja.
  [[nodiscard]] Subscription subscribe(std::string_view topic, MessageHandler handler) override;

  // Aviso de conexion: el manejador se invoca en CADA conexion al broker, incluida la
  // primera. El contrato exige recargar la galeria completa al arrancar y en cada
  // reconexion (docs/contrato-mqtt.md, seccion 4).
  //
  // El manejador corre en el hilo de red de libmosquitto: que no haga trabajo bloqueante
  // (el proceso solo marca una bandera y recarga en su hilo de trabajo).
  void set_connect_listener(std::function<void()> listener);

 private:
  struct Impl;

  // Callbacks de libmosquitto (se invocan en el hilo de red).
  static void handle_connect(struct mosquitto* mosq, void* obj, int rc);
  static void handle_disconnect(struct mosquitto* mosq, void* obj, int rc);
  static void handle_message(struct mosquitto* mosq, void* obj,
                             const struct mosquitto_message* message);

  // Compartido con las suscripciones: si el bus ya no existe, la baja no hace nada.
  std::shared_ptr<Impl> impl_;
};

}  // namespace vision
