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
//
// Modo de fallo: en produccion (sin --dry-run) el proceso NO arranca con una
// configuracion ausente, sin la contrasena del broker o sin el token de la galeria.
// Los tres casos son de "fallar cerrado": es preferible que el servicio no este a que
// este sin autenticar. El modo tolerante queda para --dry-run, que es el recorrido de
// pruebas.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
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
#include "vision/wall_clock.hpp"

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
      << "  --permitir-anonimo\n"
      << "                   Solo con --dry-run: permite conectar sin usuario ni\n"
      << "                   contrasena (para el broker de pruebas)\n"
      << "  --help           Muestra esta ayuda y sale\n";
}

// Los modelos .nb no se versionan (plan, seccion 11). Estas son las rutas donde se
// espera encontrarlos; el nombre del modelo de reconocimiento sale de la
// configuracion ("servicio.modelo").
constexpr const char* kRutaDetector = "/opt/autolist/models/detector.nb";

// Codigo de salida propio para "faltan los modelos" (EX_CONFIG de sysexits.h). Es
// un problema de configuracion de la maquina, no un fallo del proceso: con
// Restart=on-failure, systemd lo reiniciaria cada 5 s para siempre (y llenaria el
// journal). La unidad lleva RestartPreventExitStatus=78.
constexpr int kSalidaFaltanModelos = 78;

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
//
// El estado que sale aqui lleva el esquema COMPLETO (ts, en_linea, modo, galeria y
// modelo). El Will del broker es la forma MINIMA del mismo topico (solo "en_linea");
// la capa JSON acepta las dos formas (ver include/vision/json_payload.hpp).
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

// Cola de los cambios de la galeria.
//
// Acotada: si se llena, lo que llega se descarta (con un aviso). El mensaje del broker
// solo trae la cuenta y lo que hay que hacer es recargar la galeria entera, asi que
// una cola sin cota solo gastaria memoria.
//
// Las altas se agrupan: un alta mientras hay otra alta pendiente se descarta, porque
// la recarga que viene descarga todo. Asi, un lote de altas produce UNA peticion HTTP
// (no una por alta), y el servicio no se queda sin atender el PIR ni las capturas
// mientras recarga.
class ColaGaleria {
 public:
  bool encolar(bool alta, const std::string& account) {
    std::lock_guard lock(mutex_);
    if (alta && alta_pendiente_) {
      ++altas_agrupadas_;
      std::cerr << "[vision] alta de galeria agrupada en la recarga pendiente\n";
      return false;
    }
    if (cambios_.size() >= kMaximo) {
      ++descartados_;
      std::cerr << "[vision] cola de la galeria llena (" << kMaximo
                << "): se descarta un cambio\n";
      return false;
    }
    cambios_.push_back(CambioGaleria{alta, account});
    if (alta) alta_pendiente_ = true;
    return true;
  }

  // Saca todos los cambios pendientes.
  std::vector<CambioGaleria> Vaciar() {
    std::lock_guard lock(mutex_);
    std::vector<CambioGaleria> cambios;
    cambios.swap(cambios_);
    alta_pendiente_ = false;
    return cambios;
  }

  // Altas que se agruparon en una recarga pendiente.
  std::size_t altas_agrupadas() const {
    std::lock_guard lock(mutex_);
    return altas_agrupadas_;
  }

  // Cambios que se descartaron por cola llena.
  std::size_t descartados() const {
    std::lock_guard lock(mutex_);
    return descartados_;
  }

 private:
  static constexpr std::size_t kMaximo = 64;
  mutable std::mutex mutex_;
  std::vector<CambioGaleria> cambios_;
  bool alta_pendiente_ = false;
  std::size_t altas_agrupadas_ = 0;
  std::size_t descartados_ = 0;
};

}  // namespace

int main(int argc, char** argv) {
  std::string ruta_config = "/etc/pase-lista/vision.conf";
  bool dry_run = false;
  bool permitir_anonimo = false;

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
    if (arg == "--permitir-anonimo") {
      permitir_anonimo = true;
      continue;
    }
    std::cerr << "opcion desconocida: " << arg << '\n';
    uso(argv[0]);
    return 2;
  }

  if (permitir_anonimo && !dry_run) {
    std::cerr << "[vision] --permitir-anonimo solo tiene sentido con --dry-run\n";
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
    if (!dry_run) {
      // En produccion, arrancar sin configuracion es arrancar a ciegas: el proceso
      // sale con error en vez de usar los valores por omision.
      std::cerr << "[vision] no existe " << ruta_config
                << ": en produccion el servicio no arranca con los valores por omision\n";
      return 1;
    }
    std::cerr << "[vision] no existe " << ruta_config
              << ": se usan los valores por omision (--dry-run)\n";
  }

  // Una placa sin NTP ni RTC arranca en 1970. Solo se avisa: el pase de lista no
  // depende de la hora, pero el registro (embedding) fallaria con el motivo npu.
  {
    std::string ahora;
    try {
      ahora = SystemWallClock().now_iso8601();
    } catch (...) {
      // Se trata como una hora ilegible, igual que una vacia.
    }
    if (!is_plausible_timestamp(ahora)) {
      std::cerr << "[vision] AVISO: el reloj del sistema marca \"" << ahora
                << "\" (anterior al ano " << kMinPlausibleYear
                << " o ilegible): los registros fallaran con el motivo npu hasta que "
                   "NTP o el RTC pongan la hora\n";
    }
  }

  // Las credenciales no estan en el archivo de configuracion: se leen del entorno.
  std::string contrasena;
  const bool hay_contrasena = leer_entorno(config.mqtt.password_env, contrasena);
  if (!hay_contrasena && !config.mqtt.user.empty()) {
    // La configuracion pide un usuario MQTT: sin contrasena no se puede autenticar y
    // el proceso no debe seguir como anonimo (fallar cerrado).
    if (!dry_run) {
      std::cerr << "[vision] no esta la variable de entorno " << config.mqtt.password_env
                << ": el broker exige usuario y contrasena\n";
      return 1;
    }
    if (!permitir_anonimo) {
      std::cerr << "[vision] AVISO: no esta la variable de entorno "
                << config.mqtt.password_env
                << ": se conecta sin usuario (--dry-run). Con --permitir-anonimo se "
                   "acepta explicitamente\n";
    } else {
      std::cerr << "[vision] sin contrasena: se conecta sin usuario (--permitir-anonimo)\n";
    }
    contrasena.clear();
  }

  std::string token;
  if (!config.gallery.token_env.empty() && !leer_entorno(config.gallery.token_env, token)) {
    // Igual que la contrasena del broker: sin token la galeria se pediria sin
    // autenticar.
    if (!dry_run) {
      std::cerr << "[vision] no esta la variable de entorno " << config.gallery.token_env
                << ": la galeria se pediria sin token\n";
      return 1;
    }
    std::cerr << "[vision] AVISO: no esta la variable de entorno " << config.gallery.token_env
              << ": la galeria se pide sin token (--dry-run)\n";
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
      // Codigo propio: es una configuracion que falta, no un fallo del proceso. Con
      // Restart=on-failure, systemd lo relanzaria cada 5 s sin parar.
      return kSalidaFaltanModelos;
    }
    std::cerr << "[vision] el detector y el embedder sobre la NPU todavia no estan "
                 "implementados\n";
    return 1;
  }

  // --- Adaptadores -------------------------------------------------------------
  HttpClientOptions http_options;
  http_options.allow_invalid_certificates = config.http.allow_invalid_certificates;
  http_options.ca_file = config.http.ca_file;
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

  // Las altas y las bajas de la galeria las suscribe el proceso, no el servicio. Las
  // suscripciones se hacen ANTES de start() (ver la restriccion en mqtt_bus.hpp).
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
  // La construccion del servicio puede lanzar (configuracion invalida): sin try/catch
  // el proceso terminaria con std::terminate.
  std::unique_ptr<VisionService> servicio;
  try {
    servicio = std::make_unique<VisionService>(
        ServiceDeps{bus, http, detector, embedder, galeria, reloj, reloj_pared},
        config.to_service_config());
  } catch (const std::exception& error) {
    std::cerr << "[vision] no se pudo construir el servicio: " << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "[vision] no se pudo construir el servicio: excepcion desconocida\n";
    return 1;
  }

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
    bool recargar = recarga_pendiente.exchange(false);
    // Altas y bajas de la galeria (las aplica el hilo de trabajo).
    std::string cuenta_alta;
    for (const CambioGaleria& cambio : cola_galeria.Vaciar()) {
      if (cambio.alta) {
        // Las altas del lote se resuelven con UNA recarga (el alta del contrato solo
        // trae la cuenta: la galeria se recarga entera).
        if (cuenta_alta.empty()) cuenta_alta = cambio.account;
        recargar = true;
      } else {
        store.apply_remove(cambio.account);
        // El estado retenido tiene que decir la verdad sobre la galeria.
        publicar_estado(bus, store, config, true);
      }
    }
    if (recargar) {
      if (!store.reload()) {
        std::cerr << "[vision] no se pudo cargar la galeria; se reintenta en la proxima "
                     "reconexion\n";
      } else {
        publicar_estado(bus, store, config, true);
      }
    }
    servicio->poll();
    reloj.sleep_for(Millis(config.service.poll_ms));
  }

  // --- Salida ordenada ---------------------------------------------------------
  std::cerr << "[vision] parando\n";
  // El servicio cancela el enrolamiento en curso y apaga la luz: si no, el actuador se
  // queda con luz 1 y el registro queda a medias.
  servicio->shutdown();
  // El estado final lleva el esquema completo (ts, en_linea false, modo, galeria y
  // modelo) y queda retenido en el broker.
  publicar_estado(bus, store, config, false);
  // La cola de salida tiene que vaciarse antes de cortar el bus: mosquitto_publish es
  // asincrono y, si no, el estado final se puede quedar en la cola. Como la
  // desconexion es limpia, el Will no salta y el valor retenido se queda en
  // en_linea true para siempre.
  if (!bus.flush(std::chrono::milliseconds(2000))) {
    std::cerr << "[vision] la cola de salida no se vacio antes de cortar el bus\n";
  }
  bus.stop();
  return 0;
}
