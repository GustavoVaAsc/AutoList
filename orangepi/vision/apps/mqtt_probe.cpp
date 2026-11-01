// Sonda MQTT: publica un mensaje en un topico y/o escucha un topico e imprime lo que
// llega. Es la herramienta para hacer sonar el broker a mano (en la placa no hay
// mosquitto_pub).
//
// Uso: mqtt_probe [--host H] [--puerto P] [--usuario U] [--clave C]
//                 [--publicar <topico> <json>] [--escuchar <topico>] [--segundos N]
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>

#include <mosquitto.h>

namespace {

std::string ahora() {
  const auto momento = std::chrono::system_clock::now();
  const std::time_t segundos = std::chrono::system_clock::to_time_t(momento);
  std::tm local{};
  localtime_r(&segundos, &local);
  char buffer[32];
  if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &local) == 0) return {};
  return buffer;
}

// El estado compartido con los callbacks del hilo de red.
struct Estado {
  struct mosquitto* cliente = nullptr;
  std::string topico;
  int mensajes = 0;
  // El PUBACK de la publicacion: sin el, mosquitto_disconnect() se queda esperando
  // un mensaje que ya no va a salir (el hilo de red esta parado).
  bool ack = false;

  std::mutex mutex;
  std::condition_variable aviso;
};

void al_recibir(struct mosquitto*, void* objeto, const struct mosquitto_message* mensaje) {
  if (mensaje == nullptr || mensaje->topic == nullptr) return;
  Estado* estado = static_cast<Estado*>(objeto);
  const char* datos = static_cast<const char*>(mensaje->payload);
  const std::string cuerpo =
      (datos == nullptr || mensaje->payloadlen <= 0)
          ? std::string()
          : std::string(datos, static_cast<std::size_t>(mensaje->payloadlen));
  std::cout << ahora() << ' ' << mensaje->topic << ' ' << cuerpo << std::endl;
  std::lock_guard lock(estado->mutex);
  ++estado->mensajes;
}

void al_publicar(struct mosquitto*, void* objeto, int) {
  Estado* estado = static_cast<Estado*>(objeto);
  std::lock_guard lock(estado->mutex);
  estado->ack = true;
  estado->aviso.notify_all();
}

void al_conectar(struct mosquitto*, void* objeto, int codigo) {
  Estado* estado = static_cast<Estado*>(objeto);
  if (codigo != 0) {
    std::cerr << "el broker rechazo la conexion (codigo " << codigo << ")\n";
    return;
  }
  if (!estado->topico.empty()) {
    int id = 0;
    const int rc = mosquitto_subscribe(estado->cliente, &id, estado->topico.c_str(), 1);
    if (rc != MOSQ_ERR_SUCCESS) {
      std::cerr << "no se pudo suscribirse a " << estado->topico << ": "
                << mosquitto_strerror(rc) << '\n';
    } else {
      std::cerr << "suscrito a " << estado->topico << '\n';
    }
  }
}

// Cierra el cliente sin dejar nada a medias: detiene el hilo de red, espera el PUBACK
// de lo publicado (si lo hubo) y se desconecta.
void cerrar(struct mosquitto* cliente) {
  mosquitto_loop_stop(cliente, true);
  mosquitto_disconnect(cliente);
  mosquitto_destroy(cliente);
  mosquitto_lib_cleanup();
}

}  // namespace

int main(int argc, char** argv) {
  std::string host = "127.0.0.1";
  int puerto = 1883;
  std::string usuario;
  std::string clave;
  std::string topico_publicar;
  std::string cuerpo_publicar;
  std::string topico_escuchar;
  int segundos = 5;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      std::cout << "Uso: mqtt_probe [--host H] [--puerto P] [--usuario U] [--clave C]\n"
                   "                [--publicar <topico> <json>] [--escuchar <topico>]\n"
                   "                [--segundos N]\n"
                   "\n"
                   "Publica un mensaje y/o escucha un topico e imprime lo que llega.\n"
                   "Con --publicar y --escuchar a la vez, se suscribe primero y despues\n"
                   "publica.\n";
      return 0;
    }
    if (arg == "--host" && i + 1 < argc) {
      host = argv[++i];
      continue;
    }
    if (arg == "--puerto" && i + 1 < argc) {
      puerto = std::atoi(argv[++i]);
      continue;
    }
    if (arg == "--usuario" && i + 1 < argc) {
      usuario = argv[++i];
      continue;
    }
    if (arg == "--clave" && i + 1 < argc) {
      clave = argv[++i];
      continue;
    }
    if (arg == "--publicar" && i + 2 < argc) {
      topico_publicar = argv[++i];
      cuerpo_publicar = argv[++i];
      continue;
    }
    if (arg == "--escuchar" && i + 1 < argc) {
      topico_escuchar = argv[++i];
      continue;
    }
    if (arg == "--segundos" && i + 1 < argc) {
      segundos = std::atoi(argv[++i]);
      continue;
    }
    std::cerr << "opcion desconocida o incompleta: " << arg << '\n';
    return 2;
  }

  if (topico_publicar.empty() && topico_escuchar.empty()) {
    std::cerr << "hace falta --publicar <topico> <json> o --escuchar <topico>\n";
    return 2;
  }
  if (segundos < 0) segundos = 0;

  mosquitto_lib_init();
  // El id lleva el pid para no chocar con otra sonda ni con el servicio de vision.
  const std::string client_id = "mqtt-probe-" + std::to_string(getpid());
  struct mosquitto* cliente = mosquitto_new(client_id.c_str(), true, nullptr);
  if (cliente == nullptr) {
    std::cerr << "no se pudo crear el cliente MQTT\n";
    mosquitto_lib_cleanup();
    return 1;
  }

  Estado estado;
  estado.cliente = cliente;
  estado.topico = topico_escuchar;
  mosquitto_connect_callback_set(cliente, al_conectar);
  mosquitto_message_callback_set(cliente, al_recibir);
  mosquitto_publish_callback_set(cliente, al_publicar);
  mosquitto_user_data_set(cliente, &estado);

  if (!usuario.empty()) {
    if (mosquitto_username_pw_set(cliente, usuario.c_str(), clave.c_str()) != MOSQ_ERR_SUCCESS) {
      std::cerr << "no se pudo configurar el usuario MQTT\n";
      mosquitto_destroy(cliente);
      mosquitto_lib_cleanup();
      return 1;
    }
  }

  const int rc = mosquitto_connect(cliente, host.c_str(), puerto, 30);
  if (rc != MOSQ_ERR_SUCCESS) {
    std::cerr << "no se pudo conectar con el broker " << host << ':' << puerto << ": "
              << mosquitto_strerror(rc) << '\n';
    mosquitto_destroy(cliente);
    mosquitto_lib_cleanup();
    return 1;
  }
  if (mosquitto_loop_start(cliente) != MOSQ_ERR_SUCCESS) {
    std::cerr << "no se pudo arrancar el hilo de red\n";
    mosquitto_destroy(cliente);
    mosquitto_lib_cleanup();
    return 1;
  }

  // La suscripcion se hace en el callback de conexion: hay que darle un instante al
  // hilo de red antes de publicar, si no el mensaje sale antes de la suscripcion.
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  if (!topico_publicar.empty()) {
    int id = 0;
    const int pub_rc = mosquitto_publish(cliente, &id, topico_publicar.c_str(),
                                         static_cast<int>(cuerpo_publicar.size()),
                                         cuerpo_publicar.data(), 1, false);
    if (pub_rc != MOSQ_ERR_SUCCESS) {
      std::cerr << "no se pudo publicar en " << topico_publicar << ": "
                << mosquitto_strerror(pub_rc) << '\n';
      cerrar(cliente);
      return 1;
    }
    // Se espera el PUBACK: con QoS 1 el broker tiene que confirmar la publicacion.
    {
      std::unique_lock lock(estado.mutex);
      estado.aviso.wait_for(lock, std::chrono::seconds(5), [&estado] { return estado.ack; });
      if (!estado.ack) {
        std::cerr << "el broker no confirmo la publicacion en " << topico_publicar << '\n';
      }
    }
    std::cerr << "publicado en " << topico_publicar << ": " << cuerpo_publicar << '\n';
  }

  if (!topico_escuchar.empty()) {
    std::this_thread::sleep_for(std::chrono::seconds(segundos));
  }

  cerrar(cliente);
  return 0;
}
