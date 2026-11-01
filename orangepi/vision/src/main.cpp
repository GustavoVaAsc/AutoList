// Proceso de produccion del servicio de vision (plan 6.2).
//
// Un solo hilo de trabajo: el bucle llama a VisionService::poll() cada poll_ms y el
// hilo de red de libmosquitto solo encola mensajes. La galeria en RAM se cambia
// siempre en el hilo de trabajo (las altas y las bajas del broker se encolan), para
// que el servicio no la lea mientras otro hilo la cambia.
//
// El cableado completo (bus MQTT real, cliente HTTP real, galeria real) se puede
// verificar sin los modelos .nb con --dry-run: lo unico que se sustituye son el
// detector y el embedder.
#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "vision/alignment.hpp"
#include "vision/fakes.hpp"
#include "vision/gallery.hpp"
#include "vision/gallery_store.hpp"
#include "vision/httplib_client.hpp"
#include "vision/mqtt_bus.hpp"
#include "vision/mqtt_contract.hpp"
#include "vision/process_config.hpp"
#include "vision/vision_service.hpp"

namespace {

using namespace vision;

// La bandera de parada: la escriben los manejadores de senal y la lee el bucle.
// El manejador no hace nada mas (nada bloqueante, nada que pueda lanzar).
volatile std::sig_atomic_t g_salir = 0;

extern "C" void manejar_senal(int) { g_salir = 1; }

void instalar_senales() {
  struct sigaction accion {};
  accion.sa_handler = manejar_senal;
  sigemptyset(&accion.sa_mask);
  // Sin SA_RESTART: una espera interrumpida hace que el bucle reaccione de inmediato.
  accion.sa_flags = 0;
  sigaction(SIGINT, &accion, nullptr);
  sigaction(SIGTERM, &accion, nullptr);
}

void uso(const char* programa) {
  std::cout
      << "Uso: " << programa << " [opciones]\n"
      << "\n"
      << "Opciones:\n"
      << "  --config <ruta>  Archivo de configuracion JSON\n"
      << "                   (por omision /etc/pase-lista/vision.conf)\n"
      << "  --dry-run        Bus MQTT y cliente HTTP reales, pero detector y embedder\n"
      << "                   falsos: verifica el cableado sin los modelos .nb\n"
      << "  --help           Muestra esta ayuda y sale\n";
}

// Los modelos .nb no se versionan (plan, seccion 11). Estas son las rutas donde se
// espera encontrarlos; el nombre del modelo de reconocimiento sale de la
// configuracion ("servicio.modelo").
constexpr const char* kRutaDetector = "/opt/autolist/models/detector.nb";

std::string ruta_embedding(const ProcessConfig& config) {
  return "/opt/autolist/models/" + config.service.model_name + ".nb";
}

bool leer_entorno(const std::string& nombre, std::string& valor) {
  const char* crudo = std::getenv(nombre.c_str());
  if (crudo == nullptr) return false;
  valor = crudo;
  return true;
}

// El estado que ve el frontend (contrato v0.3). El proceso lo publica al arrancar
// (en_linea true) y al salir limpio (en_linea false, retenido), para que el frontend
// no se quede con el servicio "en linea" cuando el proceso ya no esta.
void publicar_estado(MessageBus& bus, const GalleryStore& galeria, const ProcessConfig& config,
                     bool en_linea) {
  VisionStatus status;
  status.timestamp = SystemWallClock().now_iso8601();
  status.online = en_linea;
  status.mode = VisionMode::Idle;
  status.gallery_size = galeria.size();
  status.model = config.service.model_name;
  try {
    bus.publish(topics::kVisionStatus, status);
  } catch (const std::exception& error) {
    std::cerr << "[vision] no se pudo publicar el estado: " << error.what() << '\n';
  } catch (...) {
    std::cerr << "[vision] no se pudo publicar el estado: excepcion desconocida\n";
  }
}

// Los cambios de la galeria llegan en el hilo de red del bus, pero la galeria en RAM la
// lee el servicio en el hilo de trabajo: los cambios se encolan y se aplican en el
// hilo de trabajo, que es el unico que la modifica.
struct CambioGaleria {
  bool alta = false;  // true = alta, false = baja
  std::string account;
};

class ColaGaleria {
 public:
  void encolar(bool alta, const std::string& account) {
    std::lock_guard lock(mutex_);
    cambios_.push_back(CambioGaleria{alta, account});
  }

  // Saca todos los cambios pendientes.
  std::vector<CambioGaleria>Vaciar() {
    std::lock_guard lock(mutex_);
    std::vector<CambioGaleria> cambios;
    cambios.swap(cambios_);
    return cambios;
  }

 private:
  std::mutex mutex_;
  std::vector<CambioGaleria> cambios_;
};

}  // namespace

int main(int argc, char** argv) {
  std::string ruta_config = "/etc/pase-lista/vision.conf";
  bool dry_run = false;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      uso(argv[0]);
      return 0;
    }
    if (arg == "--config") {
      if (i + 1 >= argc) {
        std::cerr << "falta la ruta del archivo de configuracion\n";
        return 2;
      }
      ruta_config = argv[++i];
      continue;
    }
    if (arg == "--dry-run") {
      dry_run = true;
      continue;
    }
    std::cerr << "opcion desconocida: " << arg << '\n';
    uso(argv[0]);
    return 2;
  }

  // --- Configuracion -----------------------------------------------------------
  ProcessConfig config;
  bool encontrado = false;
  std::string error;
  if (!load_process_config(ruta_config, config, encontrado, error)) {
    std::cerr << "[vision] la configuracion " << ruta_config << " no es valida: " << error
              << '\n';
    return 1;
  }
  if (!encontrado) {
    std::cerr << "[vision] no existe " << ruta_config << ": se usan los valores por omision\n";
  }

  // Las credenciales no estan en el archivo de configuracion: se leen del entorno.
  std::string contrasena;
  if (!leer_entorno(config.mqtt.password_env, contrasena)) {
    std::cerr << "[vision] no esta la variable de entorno " << config.mqtt.password_env
              << ": se intenta conectar al broker sin usuario\n";
    contrasena.clear();
  }
  std::string token;
  if (!leer_entorno(config.gallery.token_env, token)) {
    std::cerr << "[vision] no esta la variable de entorno " << config.gallery.token_env
              << ": la galeria se pedira sin token\n";
    token.clear();
  }

  // --- Modelos -----------------------------------------------------------------
  // Sin los modelos .nb no hay detector ni embedder: el proceso solo arranca en
  // --dry-run. Cuando existan, aqui se enchufran (include/vision/npu.hpp).
  if (!dry_run) {
    const std::string detector = kRutaDetector;
    const std::string embedding = ruta_embedding(config);
    if (!std::filesystem::exists(detector) || !std::filesystem::exists(embedding)) {
      std::cerr << "[vision] faltan los modelos .nb (" << detector << ", " << embedding
                << "); usa --dry-run para verificar el cableado\n";
      return 1;
    }
    std::cerr << "[vision] el detector y el embedder sobre la NPU todavia no estan "
                 "implementados\n";
    return 1;
  }

  // --- Adaptadores -------------------------------------------------------------
  HttpClientOptions http_options;
  http_options.allow_invalid_certificates = config.http.allow_invalid_certificates;
  // El plazo del socket no puede ser menor que el plazo total de la peticion: si no,
  // una peticion lenta se cortaria antes de tiempo. El mayor de los dos plazos
  // (captura y galeria) acota los tres.
  const int plazo_http =
      std::max(config.capture.http_timeout_ms, config.gallery.timeout_ms);
  http_options.connect_timeout_ms = plazo_http;
  http_options.read_timeout_ms = plazo_http;
  http_options.write_timeout_ms = plazo_http;
  HttplibClient http(http_options);

  MosquittoOptions bus_options;
  bus_options.host = config.mqtt.host;
  bus_options.port = config.mqtt.port;
  bus_options.client_id = config.mqtt.client_id;
  if (!config.mqtt.user.empty() && !contrasena.empty()) {
    bus_options.user = config.mqtt.user;
    bus_options.password = contrasena;
  } else {
    // Sin contrasena no hay usuario: el broker de pruebas acepta anonimos.
    bus_options.user.clear();
  }

  // La declaracion importa: se destruyen en orden inverso. El bus debe vivir mas que
  // el servicio y que las suscripciones, y la bandera de recarga mas que el bus.
  Gallery galeria;
  ColaGaleria cola_galeria;
  std::atomic<bool> recarga_pendiente{false};
  MosquittoBus bus(bus_options);

  GalleryStoreConfig galeria_config;
  galeria_config.url = config.gallery.url;
  galeria_config.token = token;
  galeria_config.timeout = Millis(config.gallery.timeout_ms);
  GalleryStore store(galeria, http, galeria_config);

  // Las altas y las bajas de la galeria las suscribe el proceso, no el servicio.
  Subscription suscripcion_alta =
      bus.subscribe(topics::kVisionGalleryAdd, [&cola_galeria](const Payload& payload) {
        if (const auto* cuenta = std::get_if<GalleryAccount>(&payload)) {
          cola_galeria.encolar(true, cuenta->account);
        }
      });
  Subscription suscripcion_baja =
      bus.subscribe(topics::kVisionGalleryRemove, [&cola_galeria](const Payload& payload) {
        if (const auto* cuenta = std::get_if<GalleryAccount>(&payload)) {
          cola_galeria.encolar(false, cuenta->account);
        }
      });

  // El bus avisa de cada conexion (la primera incluida): la galeria se recarga
  // completa al arrancar y en cada reconexion (contrato v0.3, seccion 4). El manejador
  // solo marca la bandera: la recarga se hace en el hilo de trabajo, no en el hilo de
  // red del bus.
  bus.set_connect_listener([&recarga_pendiente] { recarga_pendiente.store(true); });

  // El detector y el embedder: en --dry-run son los dobles en memoria. Cuando haya
  // modelos, aqui se enchufan los reales sobre la NPU.
  fakes::FakeFaceDetector detector;
  fakes::FakeFaceEmbedder embedder;
  {
    FaceDetection cara;
    cara.score = 0.99f;
    for (std::size_t i = 0; i < cara.landmarks.size(); ++i) {
      cara.landmarks[i] = kArcFaceReference[i] * 2.0f + cv::Point2f(200.0f, 100.0f);
    }
    detector.detections = {cara};
    embedder.outputs = {{0.9f, 0.1f, 0.2f, 0.3f}};
  }

  SystemClock reloj;
  SystemWallClock reloj_pared;
  VisionService servicio(
      ServiceDeps{bus, http, detector, embedder, galeria, reloj, reloj_pared},
      config.to_service_config());

  instalar_senales();

  try {
    bus.start();
  } catch (const std::exception& error) {
    std::cerr << "[vision] no se pudo arrancar el bus MQTT: " << error.what() << '\n';
    return 1;
  }
  publicar_estado(bus, store, config, true);
  std::cerr << "[vision] servicio en linea (modelo " << config.service.model_name << ")\n";

  // --- Bucle -------------------------------------------------------------------
  while (g_salir == 0) {
    // Recarga de la galeria: al arrancar y en cada reconexion al broker.
    if (recarga_pendiente.exchange(false)) {
      if (!store.reload()) {
        std::cerr << "[vision] no se pudo cargar la galeria; se reintenta en la proxima "
                     "reconexion\n";
      }
    }
    // Altas y bajas de la galeria (las aplica el hilo de trabajo).
    for (const CambioGaleria& cambio : cola_galeria.Vaciar()) {
      if (cambio.alta) {
        store.apply_add(cambio.account);
      } else {
        store.apply_remove(cambio.account);
      }
    }
    servicio.poll();
    reloj.sleep_for(Millis(config.service.poll_ms));
  }

  // --- Salida ordenada ---------------------------------------------------------
  std::cerr << "[vision] parando\n";
  publicar_estado(bus, store, config, false);
  bus.stop();
  return 0;
}
