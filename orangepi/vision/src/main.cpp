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
// pruebas. El secreto de la firma de la captura es la excepcion controlada: solo es
// obligatorio con captura.hmac_requerido (ver process_config.hpp).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "vision/alignment.hpp"
#include "vision/fakes.hpp"
#include "vision/gallery.hpp"
#include "vision/gallery_retry.hpp"
#include "vision/gallery_store.hpp"
#include "vision/httplib_client.hpp"
#include "vision/mqtt_bus.hpp"
#include "vision/mqtt_contract.hpp"
#include "vision/npu.hpp"
#include "vision/npu_face_detector.hpp"
#include "vision/npu_face_embedder.hpp"
#include "vision/process_config.hpp"
#include "vision/reloj_guardia.hpp"
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

// Los modelos .nb no se versionan (plan, seccion 11). Estos son el directorio donde se
// espera encontrarlos y el nombre del detector; el nombre del modelo de reconocimiento
// sale de la configuracion ("servicio.modelo"). Las rutas se construyen con
// vision::ruta_modelo, que no deja que el nombre se salga del directorio.
constexpr const char* kDirectorioModelos = "/opt/autolist/models";
constexpr const char* kNombreDetector = "detector";

// Codigo de salida propio para los errores de CONFIGURACION (EX_CONFIG de sysexits.h):
// "faltan los modelos .nb" y "el .nb no encaja con el codigo". No es un fallo del
// proceso, es una configuracion que falta o que no vale: con Restart=on-failure,
// systemd lo reiniciaria cada 5 s para siempre (y llenaria el journal). La unidad lleva
// RestartPreventExitStatus=78.
//
// El 1 se sigue usando para los demas fallos (el bus no arranca, el servicio no se
// puede construir, el driver de la NPU no responde).
constexpr int kSalidaConfig = 78;

std::filesystem::path ruta_detector() {
  return ruta_modelo(kNombreDetector, kDirectorioModelos);
}

std::filesystem::path ruta_embedding(const ProcessConfig& config) {
  return ruta_modelo(config.service.model_name, kDirectorioModelos);
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
    return kSalidaConfig;
  }
  if (!encontrado) {
    if (!dry_run) {
      // En produccion, arrancar sin configuracion es arrancar a ciegas: el proceso
      // sale con error en vez de usar los valores por omision.
      std::cerr << "[vision] no existe " << ruta_config
                << ": en produccion el servicio no arranca con los valores por omision\n";
      return kSalidaConfig;
    }
    std::cerr << "[vision] no existe " << ruta_config
              << ": se usan los valores por omision (--dry-run)\n";
  }
  // Los ordenes de color del detector y del embedder son obligatorios: no se deducen
  // del modelo y equivocarlos degrada el resultado sin fallar. En produccion es un error
  // de configuracion (78), como un archivo invalido; en --dry-run los modelos no se
  // cargan, asi que solo se avisa.
  if (!comprobar_colores_npu(config.npu, error)) {
    if (!dry_run) {
      std::cerr << "[vision] la configuracion " << ruta_config << " no es valida: " << error
                << '\n';
      return kSalidaConfig;
    }
    std::cerr << "[vision] AVISO: " << error << " (--dry-run: los modelos no se cargan)\n";
  }
  std::cerr << "[vision] npu: " << resumen_npu(config.npu) << '\n';

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
      return kSalidaConfig;
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
      return kSalidaConfig;
    }
    std::cerr << "[vision] AVISO: no esta la variable de entorno " << config.gallery.token_env
              << ": la galeria se pide sin token (--dry-run)\n";
    token.clear();
  }

  // El secreto de la firma de la captura: viene del entorno, como el token de la galeria
  // (el archivo de configuracion no lleva credenciales). Es el MISMO valor que lleva la
  // placa; si no cuadra, la placa rechaza la peticion con 401. Con captura.hmac_requerido
  // (y sin --dry-run) un secreto ausente, el placeholder o corto es un error de
  // configuracion; si no, solo se avisa y la peticion va sin firma. Nunca se imprime el
  // valor, solo el nombre de la variable.
  std::string secreto_captura;
  {
    std::optional<std::string> entorno;
    std::string leido;
    if (!config.capture.hmac_secret_env.empty() &&
        leer_entorno(config.capture.hmac_secret_env, leido)) {
      entorno = std::move(leido);
    }
    SecretoCaptura secreto = resolver_secreto_captura(config.capture, entorno, dry_run);
    if (secreto.fatal) {
      std::cerr << "[vision] " << secreto.mensaje << '\n';
      return kSalidaConfig;
    }
    if (!secreto.mensaje.empty()) std::cerr << "[vision] " << secreto.mensaje << '\n';
    secreto_captura = std::move(secreto.valor);
  }
  // Una linea de arranque para ver de un vistazo si la captura sale firmada.
  std::cerr << "[vision] firma de la captura: "
            << (secreto_captura.empty() ? "inactiva (la peticion va sin firma y la respuesta "
                                          "no se descifra)"
                                        : "activa (peticion firmada y respuesta cifrada verificada)")
            << '\n';

  // --- Modelos -----------------------------------------------------------------
  // Sin los modelos .nb no hay detector ni embedder: el proceso solo arranca en
  // --dry-run. Cuando existan, se enchufan los adaptadores reales sobre la NPU
  // (include/vision/npu_face_detector.hpp y include/vision/npu_face_embedder.hpp).
  //
  // Las rutas se construyen aqui, dentro de un try: ruta_modelo falla cerrado si el
  // nombre no es valido, y una excepcion fuera de un try terminaria el proceso con
  // std::terminate en vez de un mensaje claro.
  std::filesystem::path detector_nb;
  std::filesystem::path embedding_nb;
  if (!dry_run) {
    try {
      detector_nb = ruta_detector();
      embedding_nb = ruta_embedding(config);
    } catch (const std::exception& error) {
      std::cerr << "[vision] no se pudo construir la ruta de los modelos .nb: "
                << error.what() << '\n';
      return kSalidaConfig;
    }
    if (!std::filesystem::exists(detector_nb) || !std::filesystem::exists(embedding_nb)) {
      // Solo se registran los NOMBRES de los modelos: las rutas absolutas no se
      // imprimen (el arranque va al journal).
      std::cerr << "[vision] faltan los modelos .nb (" << kNombreDetector << ".nb, "
                << config.service.model_name << ".nb); usa --dry-run para verificar el "
                "cableado\n";
      // Codigo propio: es una configuracion que falta, no un fallo del proceso. Con
      // Restart=on-failure, systemd lo relanzaria cada 5 s sin parar.
      return kSalidaConfig;
    }
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

  // El detector y el embedder: en --dry-run son los dobles en memoria; en produccion,
  // los adaptadores reales sobre la NPU.
  //
  // El driver de la NPU se declara ANTES que las redes: se destruyen en orden inverso y
  // el driver tiene que vivir mas que ellas. La NPU se usa desde el hilo de trabajo (el
  // mismo que llama a poll()), nunca desde otro hilo: las redes no son seguras entre
  // hilos.
  fakes::FakeFaceDetector detector_falso;
  fakes::FakeFaceEmbedder embedder_falso;
  std::unique_ptr<npu::Driver> driver_npu;
  std::unique_ptr<NpuFaceDetector> detector_npu;
  std::unique_ptr<NpuFaceEmbedder> embedder_npu;
  FaceDetector* detector = &detector_falso;
  FaceEmbedder* embedder = &embedder_falso;

  if (dry_run) {
    FaceDetection cara;
    cara.score = 0.99f;
    for (std::size_t i = 0; i < cara.landmarks.size(); ++i) {
      cara.landmarks[i] = kArcFaceReference[i] * 2.0f + cv::Point2f(200.0f, 100.0f);
    }
    detector_falso.detections = {cara};
    embedder_falso.outputs = {{0.9f, 0.1f, 0.2f, 0.3f}};
  } else {
    // Los modelos .nb existen (ya se comprobo arriba). Si no se pueden cargar, el
    // proceso avisa y termina: en produccion no se arranca a ciegas.
    try {
      driver_npu = std::make_unique<npu::Driver>();
      // Los parametros vienen del bloque "npu" de la configuracion: el orden de color
      // (obligatorio, ya comprobado arriba), el modo de escala y el tamano de entrada del
      // detector (0 = el que tenga el .nb).
      NpuDetectorOptions opciones_detector;
      opciones_detector.input_width = config.npu.detector_width;
      opciones_detector.input_height = config.npu.detector_height;
      opciones_detector.color_order = config.npu.color_order;
      opciones_detector.scale_mode = config.npu.scale_mode;
      detector_npu =
          std::make_unique<NpuFaceDetector>(*driver_npu, detector_nb, opciones_detector);
      // El embedder es otro modelo: su color, su escala y su tamano de entrada (0 = el
      // del .nb) son suyos y no heredan los del detector.
      NpuEmbedderOptions opciones_embedder;
      opciones_embedder.input_width = config.npu.embed_width;
      opciones_embedder.input_height = config.npu.embed_height;
      opciones_embedder.color_order = config.npu.embed_color_order.value();
      opciones_embedder.scale_mode = config.npu.embed_scale_mode;
      embedder_npu = std::make_unique<NpuFaceEmbedder>(*driver_npu, embedding_nb,
                                                       opciones_embedder);
      detector = detector_npu.get();
      embedder = embedder_npu.get();
    } catch (const std::invalid_argument& error) {
      // El .nb no encaja con el codigo: su disposicion de salidas, su tamano de entrada
      // o el formato de su tensor de entrada no son los que el codigo espera. Es un
      // error de configuracion, igual que "faltan los modelos": con el 78, systemd no
      // reinicia el servicio cada 5 s para siempre.
      std::cerr << "[vision] el .nb no encaja con el codigo: " << error.what() << '\n';
      return kSalidaConfig;
    } catch (const std::exception& error) {
      // Un .nb que el driver rechaza (truncado, corrupto, de otra version) es un
      // archivo malo, no un fallo transitorio del driver: con el 78, systemd no lo
      // reinicia cada 5 s para siempre.
      std::cerr << "[vision] no se pudieron cargar los modelos .nb en la NPU: "
                << error.what() << '\n';
      return kSalidaConfig;
    } catch (...) {
      std::cerr << "[vision] no se pudieron cargar los modelos .nb en la NPU: excepcion "
                   "desconocida\n";
      return kSalidaConfig;
    }
    // Solo el nombre del modelo, no la ruta completa.
    std::cerr << "[vision] detector y embedder sobre la NPU cargados (" << kNombreDetector
              << ".nb, " << config.service.model_name << ".nb)\n";
  }

  SystemClock reloj;
  SystemWallClock reloj_pared;
  // Las anclas absolutas del reloj para la primera firma (RTC y archivo de estado).
  AnclasRelojSistema anclas_reloj(config.capture.clock_state_file);
  // La construccion del servicio puede lanzar (configuracion invalida): sin try/catch
  // el proceso terminaria con std::terminate.
  std::unique_ptr<VisionService> servicio;
  try {
    // El secreto de la firma se pasa en memoria (el archivo de configuracion no lleva
    // credenciales) y se MUEVE: solo el servicio conserva una copia.
    ServiceConfig service_config = config.to_service_config();
    service_config.capture_hmac_secret = std::move(secreto_captura);
    servicio = std::make_unique<VisionService>(
        ServiceDeps{bus, http, *detector, *embedder, galeria, reloj, reloj_pared, &anclas_reloj,
                    // Sin una galeria cargada el registro falla cerrado. Con --dry-run (dobles
                    // en memoria, sin servidor de galeria) no se comprueba.
                    dry_run ? std::function<bool()>() : [&store] { return store.loaded_ok(); }},
        std::move(service_config));
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

  // Reintento de la recarga de la galeria (backoff).
  GalleryRetry reintento_galeria(Millis(config.gallery.retry_ms),
                                 enrollment_limits::kGalleryRetryMaxFactor);

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
    // Mientras la galeria no sea de fiar (la recarga fallo, o cargo pero descarto filas:
    // GalleryStore::loaded_ok()), el registro esta cerrado: se reintenta con backoff (el
    // intervalo se duplica en cada intento, con tope) en vez de esperar a la proxima
    // reconexion al broker o a un alta, que no llega con el registro cerrado.
    if (!recargar && reintento_galeria.due(reloj.now())) recargar = true;
    if (recargar) {
      const bool cargada = store.reload();
      const Millis espera = reintento_galeria.record(cargada && store.loaded_ok(), reloj.now());
      if (!cargada) {
        std::cerr << "[vision] no se pudo cargar la galeria";
      } else if (espera > Millis::zero()) {
        std::cerr << "[vision] la galeria se cargo con filas descartadas";
      }
      if (espera > Millis::zero()) {
        std::cerr << "; se reintenta en " << espera.count() / 1000
                  << " s (mientras, el registro esta cerrado)\n";
      }
      // La asistencia usa la galeria cargada, aunque el registro siga cerrado.
      if (cargada) publicar_estado(bus, store, config, true);
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
