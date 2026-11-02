#include "vision/mqtt_bus.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <mosquitto.h>

#include "vision/json_payload.hpp"

namespace vision {

namespace {

// --- QoS y retencion por topico (seccion 2 de docs/contrato-mqtt.md) -------------
constexpr TopicRule kTopicRules[] = {
    {topics::kCameraPir, 0, false},            // XIAO -> vision
    {topics::kActuatorCmd, 1, false},          // vision -> XIAO
    {topics::kVisionResult, 1, false},         // vision -> Node-RED
    {topics::kVisionCmd, 1, false},            // Node-RED -> vision
    {topics::kVisionEnrollment, 0, false},     // vision -> frontend, pantalla
    {topics::kVisionEmbedding, 1, false},      // vision -> Node-RED
    {topics::kVisionStatus, 1, true},          // vision -> Node-RED, frontend (retenido)
    {topics::kVisionGalleryAdd, 1, false},     // Node-RED -> vision
    {topics::kVisionGalleryRemove, 1, false},  // Node-RED -> vision
};

// Nombre del tipo de error de la conversion: nunca el contenido del mensaje.
const char* error_name(json::Error error) {
  switch (error) {
    case json::Error::kNone:
      return "sin_error";
    case json::Error::kInvalidJson:
      return "json_invalido";
    case json::Error::kUnknownTopic:
      return "topico_desconocido";
    case json::Error::kMissingField:
      return "falta_campo";
    case json::Error::kWrongType:
      return "tipo_incorrecto";
    case json::Error::kInvalidValue:
      return "valor_invalido";
    case json::Error::kTooLarge:
      return "demasiado_grande";
    case json::Error::kTooDeep:
      return "demasiado_anidado";
  }
  return "error_desconocido";
}

// --- inicializacion de la biblioteca --------------------------------------------
// mosquitto_lib_init/cleanup son globales: se lleva la cuenta de quantos buses hay
// para no limpiar la biblioteca mientras otro bus sigue vivo.
std::mutex g_lib_mutex;
int g_lib_users = 0;

void lib_acquire() {
  std::lock_guard lock(g_lib_mutex);
  if (g_lib_users++ == 0) {
    mosquitto_lib_init();
  }
}

void lib_release() {
  std::lock_guard lock(g_lib_mutex);
  if (--g_lib_users == 0) {
    mosquitto_lib_cleanup();
  }
}

// --- estado de una suscripcion --------------------------------------------------
// Cada suscripcion tiene su propio estado: el manejador corre en el hilo de red y la
// baja (que puede venir de otro hilo) espera a que termine.
struct SubscriptionState {
  std::mutex mutex;
  std::condition_variable cv;
  bool active = true;    // false cuando ya se dio de baja
  int in_flight = 0;     // manejadores en curso
  std::thread::id running_thread;  // hilo del manejador en curso
  MessageHandler handler;
};

using SubscriptionStatePtr = std::shared_ptr<SubscriptionState>;

// --- estado de un topico ---------------------------------------------------------
struct TopicState {
  int qos = 0;
  bool subscribed = false;  // la suscripcion esta viva en el broker
  std::vector<SubscriptionStatePtr> subscribers;
};

}  // namespace

const TopicRule* find_topic_rule(std::string_view topic) {
  for (const TopicRule& rule : kTopicRules) {
    if (rule.topic == topic) return &rule;
  }
  return nullptr;
}

namespace {

// QoS con el que se suscribe a un topico del contrato.
int qos_for_topic(std::string_view topic) {
  const TopicRule* rule = find_topic_rule(topic);
  return rule != nullptr ? rule->qos : 0;
}

// Tope del cuerpo de un mensaje entrante (64 KiB). El broker permite 256 KiB por
// paquete: este tope es la barrera del cliente.
constexpr int kMaxIncomingPacketBytes = 64 * 1024;

}  // namespace

struct MosquittoBus::Impl {
  explicit Impl(MosquittoOptions bus_options) : options(std::move(bus_options)) {}

  MosquittoOptions options;
  struct mosquitto* mosq = nullptr;
  std::mutex mutex;  // protege topics_
  std::map<std::string, TopicState> topics_;
  std::atomic<bool> connected{false};
  // El broker rechazo la conexion (credenciales o permiso): no hay que esperar al
  // plazo de conexion para saberlo.
  std::atomic<bool> connect_failed{false};
  bool started = false;
  bool stopped = false;
  int connections = 0;  // conexiones hechas (la primera, las reconexiones)
  // Manejador de conexion (ver set_connect_listener). Se invoca en el hilo de red.
  std::function<void()> connect_listener;

  // Publicaciones en vuelo: las que todavia no ha confirmado el broker (QoS 1, con su
  // PUBACK) o que no han salido todavia hacia el sistema operativo (QoS 0). El hilo de
  // red las descuenta en el manejador de publicacion.
  std::mutex out_mutex;
  std::condition_variable out_cv;
  int en_vuelo = 0;

  // Da de alta un manejador para el topico.
  //
  // NO se llama a mosquitto_subscribe() aqui: esta funcion puede correr en un hilo
  // distinto del de red (el ABBA de libmosquitto, medido en la placa, tambien afecta
  // a subscribe). La suscripcion en el broker la hace resubscribe_all(), en el hilo de
  // red, en el ciclo de conexion. Una suscripcion registrada despues de start() queda
  // anotada y se activa en la siguiente reconexion.
  void add_subscriber(const std::string& topic, int qos, const SubscriptionStatePtr& state) {
    std::lock_guard lock(mutex);
    TopicState& topic_state = topics_[topic];
    topic_state.qos = qos;
    topic_state.subscribers.push_back(state);
  }

  // Da de baja un manejador. Cuando retorna, ningun manejador de esa suscripcion esta
  // en curso ni volvera a ejecutarse.
  //
  // NO se llama a mosquitto_unsubscribe() ni aqui ni en stop(). Medido en la placa:
  // llamar a unsubscribe desde un hilo distinto del bucle de red produce un ABBA
  // entre los mutex internos de libmosquitto (el hilo que llama toma el mutex de
  // salida y pide el de la conexion; el hilo de red, al mismo tiempo, toma el de la
  // conexion y pide el de salida) y el proceso se queda colgado: 10 de 10 pruebas
  // colgaron con la llamada y 0 de 10 colgaron sin ella. Ademas, libmosquitto no
  // garantiza que unsubscribe sea seguro desde otro hilo.
  //
  // El costo de no desuscribirse es trafico inutil: el broker sigue mandando ese
  // topico y el adaptador lo descarta al no haber manejador. La suscripcion en el
  // broker se pierde al desconectar (la sesion es limpia, ver mosquitto_new) y
  // resubscribe_all() vuelve a suscribir los topicos vivos en cada reconexion.
  void remove_subscriber(const std::string& topic, const SubscriptionStatePtr& state) {
    {
      std::unique_lock lock(state->mutex);
      if (!state->active) return;
      state->active = false;
      if (state->in_flight > 0 && state->running_thread == std::this_thread::get_id()) {
        // La baja se pide desde el propio manejador: esperar aqui seria un deadlock
        // (el manejador no puede terminar hasta que la baja vuelva). Se documenta el
        // caso y no se espera; el manejador sigue hasta que termine.
        std::cerr << "[vision] la baja de " << topic
                  << " se pidio desde su propio manejador: no se espera a que termine\n";
      } else {
        state->cv.wait(lock, [&state] { return state->in_flight == 0; });
      }
    }
    {
      std::lock_guard lock(mutex);
      const auto it = topics_.find(topic);
      if (it == topics_.end()) return;
      std::erase_if(it->second.subscribers,
                    [&state](const SubscriptionStatePtr& other) { return other == state; });
      if (!it->second.subscribers.empty()) return;
      topics_.erase(it);
    }
    // El topico se queda suscrito en el broker hasta la desconexion: ver la nota
    // sobre mosquitto_unsubscribe() de esta funcion.
  }

  // Vuelve a suscribir todos los topicos con manejadores (la sesion es limpia: tras
  // reconectar, el broker no recuerda las suscripciones).
  void resubscribe_all() {
    std::lock_guard lock(mutex);
    for (auto& [topic, topic_state] : topics_) {
      if (topic_state.subscribers.empty() || mosq == nullptr) continue;
      int mid = 0;
      // El estado se pone a false antes de intentarlo: si el SUSCRIBE falla, se puede
      // reintentar (por ejemplo, al_latir una suscripcion nueva).
      topic_state.subscribed = false;
      const int rc = mosquitto_subscribe(mosq, &mid, topic.c_str(), topic_state.qos);
      if (rc == MOSQ_ERR_SUCCESS) {
        topic_state.subscribed = true;
      } else {
        std::cerr << "[vision] no se pudo volver a suscribir a " << topic << ": "
                  << mosquitto_strerror(rc) << '\n';
      }
    }
  }

  // Anota una publicacion en vuelo. La descuenta el manejador de publicacion de
  // libmosquitto (en el hilo de red) cuando el broker confirma la entrega.
  void anotar_publicacion() {
    std::lock_guard lock(out_mutex);
    ++en_vuelo;
  }

  void confirmar_publicacion() {
    {
      std::lock_guard lock(out_mutex);
      if (en_vuelo > 0) --en_vuelo;
    }
    out_cv.notify_all();
  }

  // Entrega un mensaje del broker a los manejadores del topico.
  void dispatch(std::string_view topic, std::string_view document) {
    const json::ParseResult parsed = json::deserialize(topic, document);
    if (!parsed.ok()) {
      // Solo el tipo de error y el nombre del campo: nunca el contenido del mensaje.
      std::cerr << "[vision] mensaje descartado en " << topic << ": " << error_name(parsed.error);
      if (!parsed.field.empty()) std::cerr << " (campo " << parsed.field << ')';
      std::cerr << '\n';
      return;
    }

    std::vector<SubscriptionStatePtr> targets;
    {
      std::lock_guard lock(mutex);
      const auto it = topics_.find(std::string(topic));
      if (it == topics_.end()) return;
      targets = it->second.subscribers;
    }

    for (const SubscriptionStatePtr& state : targets) {
      {
        std::lock_guard lock(state->mutex);
        if (!state->active) continue;
        ++state->in_flight;
        state->running_thread = std::this_thread::get_id();
      }
      try {
        state->handler(*parsed.payload);
      } catch (const std::exception& error) {
        // Un manejador que falla no puede tumbar el hilo de red.
        std::cerr << "[vision] el manejador de " << topic << " fallo: " << error.what() << '\n';
      } catch (...) {
        std::cerr << "[vision] el manejador de " << topic << " fallo: excepcion desconocida\n";
      }
      {
        std::lock_guard lock(state->mutex);
        --state->in_flight;
        state->running_thread = std::thread::id();
        state->cv.notify_all();
      }
    }
  }
};

MosquittoBus::MosquittoBus(MosquittoOptions options)
    : impl_(std::make_shared<Impl>(std::move(options))) {
  lib_acquire();
}

MosquittoBus::~MosquittoBus() {
  stop();
  impl_.reset();
  lib_release();
}

void MosquittoBus::start() {
  if (impl_->started) return;
  impl_->started = true;

  struct mosquitto* mosq =
      mosquitto_new(impl_->options.client_id.c_str(), /*clean_session=*/true, impl_.get());
  if (mosq == nullptr) {
    throw std::runtime_error("no se pudo crear el cliente MQTT");
  }
  impl_->mosq = mosq;

  int protocol = MQTT_PROTOCOL_V311;
  mosquitto_opts_set(mosq, MOSQ_OPT_PROTOCOL_VERSION, &protocol);

  if (!impl_->options.user.empty()) {
    const int rc = mosquitto_username_pw_set(mosq, impl_->options.user.c_str(),
                                             impl_->options.password.c_str());
    if (rc != MOSQ_ERR_SUCCESS) {
      throw std::runtime_error("no se pudo configurar el usuario MQTT");
    }
  }

  if (impl_->options.will.has_value()) {
    const Will& will = *impl_->options.will;
    mosquitto_will_set(mosq, will.topic.c_str(), static_cast<int>(will.payload.size()),
                       will.payload.data(), will.qos, will.retain);
  }

  mosquitto_connect_callback_set(mosq, &MosquittoBus::handle_connect);
  mosquitto_disconnect_callback_set(mosq, &MosquittoBus::handle_disconnect);
  mosquitto_message_callback_set(mosq, &MosquittoBus::handle_message);
  // El manejador de publicacion avisa de que el broker confirmo el envio (QoS 1) o de
  // que el mensaje salio hacia el sistema operativo (QoS 0). Es lo que permite esperar
  // a que la cola de salida se vacie (flush).
  mosquitto_publish_callback_set(mosq, &MosquittoBus::handle_publish);
  // Reconexion automatica: la sesion es limpia, asi que hay que volver a suscribirse.
  mosquitto_reconnect_delay_set(mosq, 1, 30, true);

  const int rc = mosquitto_connect(mosq, impl_->options.host.c_str(), impl_->options.port,
                                   impl_->options.keepalive_seconds);
  if (rc != MOSQ_ERR_SUCCESS) {
    std::cerr << "[vision] no se pudo conectar con el broker " << impl_->options.host << ':'
              << impl_->options.port << ": " << mosquitto_strerror(rc) << '\n';
    throw std::runtime_error("no se pudo conectar con el broker");
  }

  const int loop_rc = mosquitto_loop_start(mosq);
  if (loop_rc != MOSQ_ERR_SUCCESS) {
    std::cerr << "[vision] no se pudo arrancar el hilo de red: " << mosquitto_strerror(loop_rc)
              << '\n';
    throw std::runtime_error("no se pudo arrancar el hilo de red");
  }

  // El CONNACK lo procesa el hilo de red: start() espera a que llegue, para que al
  // volver el bus ya este conectado (si no, el primer publish fallaria sin motivo).
  const auto limite = std::chrono::steady_clock::now() +
                      std::chrono::milliseconds(impl_->options.connect_timeout_ms);
  while (!impl_->connected.load() && !impl_->connect_failed.load() &&
         std::chrono::steady_clock::now() < limite) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (!impl_->connected.load()) {
    std::cerr << "[vision] el broker no acepto la conexion en "
              << impl_->options.connect_timeout_ms << " ms\n";
    throw std::runtime_error("el broker no acepto la conexion");
  }
}

void MosquittoBus::stop() {
  if (!impl_ || impl_->stopped) return;
  impl_->stopped = true;

  struct mosquitto* mosq = impl_->mosq;
  if (mosq == nullptr) {
    impl_->connected.store(false);
    return;
  }

  // Orden de liberacion: primero el bucle (para que ningun callback quede corriendo),
  // luego las suscripciones y por ultimo la conexion.
  mosquitto_loop_stop(mosq, true);

  {
    std::lock_guard lock(impl_->mutex);
    // No se llama a mosquitto_unsubscribe(): el ABBA de libmosquitto esta explicado en
    // remove_subscriber(). Al desconectar con la sesion limpia, el broker olvida las
    // suscripciones.
    for (auto& entry : impl_->topics_) {
      entry.second.subscribed = false;
    }
    // Los manejadores quedan inactivos: aunque alguien conserve la Subscription, no
    // volveran a ejecutarse.
    for (auto& [topic, topic_state] : impl_->topics_) {
      for (const SubscriptionStatePtr& state : topic_state.subscribers) {
        std::lock_guard state_lock(state->mutex);
        state->active = false;
      }
    }
  }

  mosquitto_disconnect(mosq);
  mosquitto_destroy(mosq);
  impl_->mosq = nullptr;
  impl_->connected.store(false);
}

bool MosquittoBus::connected() const { return impl_ && impl_->connected.load(); }

void MosquittoBus::publish(std::string_view topic, const Payload& payload) {
  const std::optional<std::string> document = json::serialize(payload);
  if (!document.has_value()) {
    std::cerr << "[vision] no se pudo serializar el mensaje de " << topic << '\n';
    throw std::runtime_error("el payload no se pudo serializar");
  }

  int qos = 0;
  bool retain = false;
  if (const TopicRule* rule = find_topic_rule(topic)) {
    qos = rule->qos;
    retain = rule->retain;
  } else {
    std::cerr << "[vision] el topico " << topic
              << " no esta en el contrato: se publica con QoS 0\n";
  }

  // El cliente se lee bajo el mutex: stop() lo destruye y, sin esto, un publish
  // concurrente usaria un puntero liberado.
  std::lock_guard lock(impl_->mutex);
  if (!impl_->connected.load() || impl_->mosq == nullptr) {
    std::cerr << "[vision] no se pudo publicar en " << topic
              << ": no hay conexion con el broker\n";
    throw std::runtime_error("no hay conexion con el broker");
  }

  const std::string topic_name(topic);
  int mid = 0;
  // La publicacion se anota ANTES de salir: el manejador de publicacion (en el hilo de
  // red) puede ejecutarse antes de que esta llamada vuelva.
  impl_->anotar_publicacion();
  const int rc = mosquitto_publish(impl_->mosq, &mid, topic_name.c_str(),
                                   static_cast<int>(document->size()), document->data(), qos,
                                   retain);
  if (rc != MOSQ_ERR_SUCCESS) {
    impl_->confirmar_publicacion();  // esta publicacion nunca va a salir
    std::cerr << "[vision] no se pudo publicar en " << topic << ": " << mosquitto_strerror(rc)
              << '\n';
    throw std::runtime_error("no se pudo publicar en el broker");
  }
}

bool MosquittoBus::flush(std::chrono::milliseconds timeout) {
  if (!impl_) return true;
  std::unique_lock lock(impl_->out_mutex);
  const bool vaciada = impl_->out_cv.wait_for(lock, timeout,
                                              [this] { return impl_->en_vuelo == 0; });
  if (!vaciada) {
    std::cerr << "[vision] la cola de salida no se vacio en " << timeout.count() << " ms\n";
  }
  return vaciada;
}

Subscription MosquittoBus::subscribe(std::string_view topic, MessageHandler handler) {
  auto state = std::make_shared<SubscriptionState>();
  state->handler = std::move(handler);
  const std::string topic_name(topic);
  const int qos = qos_for_topic(topic);
  std::weak_ptr<Impl> weak_impl = impl_;
  if (impl_->started) {
    // Suscribir despues de start() es el patron peligroso: la suscripcion en el broker
    // no se puede pedir desde este hilo (ABBA de libmosquitto). Queda registrada y se
    // activa en la siguiente reconexion.
    std::cerr << "[vision] la suscripcion a " << topic_name
              << " se registro despues de start(): se activara en la proxima "
                 "reconexion\n";
  }
  impl_->add_subscriber(topic_name, qos, state);
  return Subscription([weak_impl, state, topic_name]() {
    const std::shared_ptr<Impl> impl = weak_impl.lock();
    if (!impl) return;  // el bus ya no existe
    impl->remove_subscriber(topic_name, state);
  });
}

void MosquittoBus::set_connect_listener(std::function<void()> listener) {
  std::lock_guard lock(impl_->mutex);
  impl_->connect_listener = std::move(listener);
}

void MosquittoBus::handle_connect(struct mosquitto*, void* obj, int rc) {
  Impl* impl = static_cast<Impl*>(obj);
  if (rc != 0) {
    impl->connected.store(false);
    impl->connect_failed.store(true);
    std::cerr << "[vision] el broker rechazo la conexion (codigo " << rc << ")\n";
    return;
  }
  impl->connect_failed.store(false);
  ++impl->connections;
  if (impl->connections == 1) {
    std::cerr << "[vision] conectado al broker\n";
  } else {
    std::cerr << "[vision] reconectado al broker\n";
  }
  // La sesion es limpia: hay que volver a suscribirse. Se hace ANTES de anunciarse
  // como conectado, para que cuando start() vuelve las suscripciones ya estan activas
  // en el broker.
  impl->resubscribe_all();
  impl->connected.store(true);

  // El aviso de conexion (la recarga de la galeria) va despues de resuscribir: el
  // manejador ve el bus ya en su estado final.
  std::function<void()> listener;
  {
    std::lock_guard lock(impl->mutex);
    listener = impl->connect_listener;
  }
  if (listener) {
    try {
      listener();
    } catch (const std::exception& error) {
      std::cerr << "[vision] fallo el manejador de conexion: " << error.what() << '\n';
    } catch (...) {
      std::cerr << "[vision] fallo el manejador de conexion: excepcion desconocida\n";
    }
  }
}

void MosquittoBus::handle_disconnect(struct mosquitto*, void* obj, int rc) {
  Impl* impl = static_cast<Impl*>(obj);
  impl->connected.store(false);
  if (rc == 0) {
    std::cerr << "[vision] desconectado del broker\n";
  } else {
    std::cerr << "[vision] se perdio la conexion con el broker (codigo " << rc << ")\n";
  }
}

void MosquittoBus::handle_publish(struct mosquitto*, void* obj, int) {
  Impl* impl = static_cast<Impl*>(obj);
  impl->confirmar_publicacion();
}

void MosquittoBus::handle_message(struct mosquitto*, void* obj,
                                  const struct mosquitto_message* message) {
  Impl* impl = static_cast<Impl*>(obj);
  if (message == nullptr || message->topic == nullptr) return;
  // Tope del mensaje entrante: segunda barrera, independiente de la del broker (que
  // permite 256 KiB por paquete) y de la de la capa JSON.
  //
  // libmosquitto 2.0 no tiene MOSQ_OPT_MAX_INCOMING_PACKET_SIZE (esa opcion no existe
  // en su API), asi que el tope se comprueba aqui, en el unico punto por donde entra
  // un mensaje.
  if (message->payloadlen > kMaxIncomingPacketBytes) {
    std::cerr << "[vision] mensaje descartado en " << message->topic << ": "
              << message->payloadlen << " bytes (maximo " << kMaxIncomingPacketBytes
              << ")\n";
    return;
  }
  const char* data = static_cast<const char*>(message->payload);
  const std::string_view document =
      (data == nullptr || message->payloadlen <= 0)
          ? std::string_view()
          : std::string_view(data, static_cast<std::size_t>(message->payloadlen));
  impl->dispatch(message->topic, document);
}

}  // namespace vision
