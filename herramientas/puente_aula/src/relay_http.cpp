#include "puente_aula/relay_http.hpp"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <thread>

#include <sys/socket.h>

#include <httplib.h>

#include "puente_aula/cliente_http.hpp"
#include "puente_aula/registro.hpp"
#include "puente_aula/sistema.hpp"

namespace puente_aula {

namespace {

using Reloj = std::chrono::steady_clock;

constexpr const char* kComponente = "http";
constexpr const char* kCaminoCaptura = "/capture";
// Las unicas cabeceras del cliente que se reenvian a la placa.
constexpr const char* kCabecerasFirma[] = {"X-Captura-TS", "X-Captura-Nonce", "X-Captura-HMAC"};
// La query de la peticion (res=SVGA) es corta; mas que esto no es una peticion de la
// Orange Pi.
constexpr std::size_t kQueryMax = 512;
// Hilos del servidor: 4 fijos bastan para contestar 503 a los concurrentes mientras uno espera
// a la placa. Cuatro conexiones que se queden mudas (la lectura de cabeceras dura hasta
// kTiempoClienteS) los ocuparian todos, asi que se admiten hasta 8 hilos (los dinamicos se
// retiran a los 3 s) y, pasados esos, una cola de solo 8 conexiones: la siguiente se cierra
// al aceptarla. Ni los hilos ni la cola crecen con el abuso.
constexpr std::size_t kHilosBase = 4;
constexpr std::size_t kHilosMax = 8;
constexpr std::size_t kColaMax = 8;
// Cuanto espera el servidor a que un cliente mande su peticion (y a que reciba la respuesta). Es el
// tiempo maximo que una conexion muda ocupa un hilo ANTES de que se decida por IP (httplib no deja
// decidir antes de leer la cabecera), asi que es corto: la Orange Pi manda su peticion al instante.
constexpr time_t kTiempoClienteS = 2;

std::string ms_desde(Reloj::time_point inicio) {
  return std::to_string(
      std::chrono::duration_cast<std::chrono::milliseconds>(Reloj::now() - inicio).count());
}

// Una query que se puede reenviar sin reinterpretarla: ASCII imprimible, sin '#'.
bool query_valida(const std::string& query) {
  if (query.size() > kQueryMax) return false;
  for (const char caracter : query) {
    const auto byte = static_cast<unsigned char>(caracter);
    if (byte <= 0x20 || byte >= 0x7f || caracter == '#') return false;
  }
  return true;
}

void respuesta_local(httplib::Response& res, int estado, const char* texto) {
  res.status = estado;
  res.set_content(texto, "text/plain");
  res.set_header("Cache-Control", "no-store");
}

}  // namespace

struct RelayHttp::Impl {
  explicit Impl(ConfigHttp configuracion) : config(std::move(configuracion)) {}

  ConfigHttp config;
  httplib::Server servidor;
  registro::LimiteAvisos avisos;
  std::string error_socket;  // si no se pudo atar a la interfaz
  std::thread hilo;
  std::atomic<bool> hilo_termino{false};  // el hilo de escucha acabo (por detener() o no)
  std::atomic<bool> deteniendo{false};    // alguien llamo a detener()
  // Una sola peticion a la placa a la vez: try_lock, nunca espera.
  std::mutex mutex_placa;

  void configurar() {
    servidor.set_keep_alive_max_count(1);  // una peticion por conexion
    servidor.set_read_timeout(kTiempoClienteS);
    servidor.set_write_timeout(kTiempoClienteS);
    servidor.set_payload_max_length(1024);
    servidor.new_task_queue = [] { return new httplib::ThreadPool(kHilosBase, kHilosMax, kColaMax); };
    // Opciones del socket de escucha. NO se llama a httplib::default_socket_options: en 0.64 pone
    // SO_REUSEPORT (permitiria a OTRO proceso del mismo usuario enlazar el mismo ip:puerto y
    // repartirse las conexiones). Se pone SO_REUSEADDR (reiniciar sin esperar el TIME_WAIT) y la
    // interfaz. Si algo falla, iniciar() lo informa y no escucha.
    servidor.set_socket_options([this](socket_t socket) {
      const int uno = 1;
      if (::setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &uno, sizeof uno) != 0) {
        error_socket = std::string("SO_REUSEADDR: ") + std::strerror(errno);
        return;
      }
      std::string error;
      if (!atar_a_interfaz(socket, config.escucha.interfaz, error)) error_socket = error;
    });

    // Antes de enrutar nada: el origen y despues el metodo y el camino. Lo que no es GET
    // /capture se contesta aqui; nada de eso llega a la placa. httplib no ofrece un gancho
    // al aceptar el socket, asi que el origen se comprueba al tener la cabecera de la
    // peticion: ya no se lee el cuerpo, no se enruta y no se toca la placa.
    servidor.set_pre_routing_handler(
        [this](const httplib::Request& req, httplib::Response& res) {
          const auto origen = parsear_ip(req.remote_addr);
          if (!origen || !ip_permitida(*origen, config.permitidos)) {
            respuesta_local(res, 403, "origen no permitido");
            avisar_rechazo(req);
            return httplib::Server::HandlerResponse::Handled;
          }
          if (req.path != kCaminoCaptura) {
            respuesta_local(res, 404, "no encontrado");
            registrar(req, 404, 0, Reloj::now());
            return httplib::Server::HandlerResponse::Handled;
          }
          if (req.method != "GET") {
            respuesta_local(res, 405, "metodo no permitido");
            res.set_header("Allow", "GET");
            registrar(req, 405, 0, Reloj::now());
            return httplib::Server::HandlerResponse::Handled;
          }
          return httplib::Server::HandlerResponse::Unhandled;
        });
    servidor.Get(kCaminoCaptura, [this](const httplib::Request& req, httplib::Response& res) {
      reenviar(req, res);
    });
  }

  void avisar_rechazo(const httplib::Request& req) {
    std::uint64_t omitidos = 0;
    if (!avisos.permitir(omitidos)) return;
    std::string texto = "origen=" + registro::limpiar(req.remote_addr, 45) + " rechazado=no_permitida";
    if (omitidos > 0) texto += " omitidos=" + std::to_string(omitidos);
    registro::linea(kComponente, texto);
  }

  // Una linea por peticion: origen, camino sin query, estado, bytes y latencia. Nunca
  // cuerpos ni valores de cabeceras.
  void registrar(const httplib::Request& req, int estado, std::size_t bytes,
                 Reloj::time_point inicio) const {
    registro::linea(kComponente,
                    "origen=" + req.remote_addr + " ruta=" + registro::limpiar(req.path, 64) +
                        " estado=" + std::to_string(estado) + " bytes=" + std::to_string(bytes) +
                        " ms=" + ms_desde(inicio));
  }

  void reenviar(const httplib::Request& req, httplib::Response& res) {
    const auto inicio = Reloj::now();
    res.set_header("Cache-Control", "no-store");

    // La query, tal como llego (req.params esta decodificada; req.target no).
    std::string objetivo = kCaminoCaptura;
    const std::size_t signo = req.target.find('?');
    if (signo != std::string::npos) {
      const std::string query = req.target.substr(signo);
      if (!query_valida(query)) {
        respuesta_local(res, 400, "query invalida");
        registrar(req, 400, 0, inicio);
        return;
      }
      objetivo += query;
    }

    // Solo las tres cabeceras de la firma, sin tocar. Una repetida o con basura es un 400
    // local: la placa no debe ver nada ambiguo.
    Cabeceras cabeceras;
    for (const char* nombre : kCabecerasFirma) {
      const std::size_t veces = req.get_header_value_count(nombre);
      if (veces == 0) continue;
      const std::string valor = req.get_header_value(nombre);
      if (veces > 1 || !valor_cabecera_valido(valor)) {
        respuesta_local(res, 400, "cabecera invalida");
        registrar(req, 400, 0, inicio);
        return;
      }
      cabeceras.emplace_back(nombre, valor);
    }

    std::unique_lock<std::mutex> cerrojo(mutex_placa, std::try_to_lock);
    if (!cerrojo.owns_lock()) {
      respuesta_local(res, 503, "ocupado");
      res.set_header("Retry-After", "1");
      res.set_header("X-Puente", "ocupado");  // distingue este 503 de uno de la placa
      registrar(req, 503, 0, inicio);
      return;
    }

    OpcionesCliente opciones;
    opciones.host = a_texto(config.upstream.ip);
    opciones.puerto = config.upstream.puerto;
    opciones.connect_timeout_ms = config.connect_timeout_ms;
    opciones.read_timeout_ms = config.read_timeout_ms;
    opciones.total_timeout_ms = config.total_timeout_ms;
    opciones.max_body_bytes = config.max_body_bytes;
    RespuestaCaptura placa = pedir(opciones, objetivo, cabeceras);
    cerrojo.unlock();

    switch (placa.falla) {
      case Falla::Ninguna:
        break;
      case Falla::TiempoConexion:
      case Falla::TiempoLectura:
      case Falla::TiempoTotal:
        respuesta_local(res, 504, "la placa no contesto a tiempo");
        registrar(req, 504, 0, inicio);
        return;
      case Falla::CuerpoExcedido:
        respuesta_local(res, 502, "respuesta de la placa demasiado grande");
        registrar(req, 502, 0, inicio);
        return;
      case Falla::Conexion:
      case Falla::Otra:
        respuesta_local(res, 502, "placa inalcanzable");
        registrar(req, 502, 0, inicio);
        return;
    }

    // 1xx y 3xx no tienen sentido para una captura (y no se reenvia Location).
    if (placa.estado < 200 || placa.estado > 599 ||
        (placa.estado >= 300 && placa.estado < 400)) {
      respuesta_local(res, 502, "respuesta invalida de la placa");
      registrar(req, 502, 0, inicio);
      return;
    }

    res.status = placa.estado;
    for (const auto& [nombre, valor] : placa.cabeceras) res.set_header(nombre, valor);
    const std::size_t bytes = placa.cuerpo.size();
    // El tipo ya viene normalizado (image/jpeg, text/plain u octet-stream).
    std::string tipo = placa.content_type;
    if (tipo.empty() && bytes > 0) tipo = "application/octet-stream";
    res.set_content(std::move(placa.cuerpo), tipo);
    registrar(req, placa.estado, bytes, inicio);
  }
};

RelayHttp::RelayHttp(ConfigHttp config) : impl_(std::make_unique<Impl>(std::move(config))) {
  impl_->configurar();
}

RelayHttp::~RelayHttp() { detener(); }

int RelayHttp::iniciar() {
  const std::string ip = a_texto(impl_->config.escucha.ip);
  int puerto = impl_->config.escucha.puerto;
  const bool enlazado = puerto == 0 ? (puerto = impl_->servidor.bind_to_any_port(ip)) >= 0
                                    : impl_->servidor.bind_to_port(ip, puerto);
  // Un fallo al atar a la interfaz no se tolera aunque el bind haya salido bien: el puerto
  // quedaria abierto en todas las interfaces que tengan esa IP.
  if (!impl_->error_socket.empty()) throw std::runtime_error("http: " + impl_->error_socket);
  if (!enlazado) {
    throw std::runtime_error("http: no se pudo escuchar en " + ip + ":" + std::to_string(puerto));
  }
  impl_->hilo = std::thread([this] {
    try {
      impl_->servidor.listen_after_bind();
    } catch (const std::exception& error) {
      // Por ejemplo, no se pudo crear un hilo del grupo: sin esto seria un std::terminate.
      registro::linea(kComponente, std::string("el servidor fallo: ") + error.what());
    } catch (...) {
      registro::linea(kComponente, "el servidor fallo por una excepcion desconocida");
    }
    impl_->hilo_termino = true;
  });
  // httplib no avisa de que ya atiende: se espera a que declare estar corriendo, con tope
  // (si el hilo terminara antes por un fallo, wait_until_ready esperaria para siempre).
  const auto limite = Reloj::now() + std::chrono::seconds(2);
  while (!impl_->servidor.is_running()) {
    if (impl_->hilo_termino || Reloj::now() > limite) {
      impl_->servidor.stop();
      impl_->hilo.join();
      throw std::runtime_error("http: el servidor no arranco en " + ip + ":" +
                               std::to_string(puerto));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  const std::string interfaz =
      impl_->config.escucha.interfaz.empty() ? "" : " interfaz=" + impl_->config.escucha.interfaz;
  registro::linea(kComponente, "escuchando en " + ip + ":" + std::to_string(puerto) + interfaz +
                                   " -> placa " + a_texto(impl_->config.upstream.ip) + ":" +
                                   std::to_string(impl_->config.upstream.puerto));
  return puerto;
}

bool RelayHttp::termino_inesperadamente() const {
  return impl_ && impl_->hilo_termino && !impl_->deteniendo;
}

void RelayHttp::detener() {
  if (!impl_) return;
  impl_->deteniendo = true;
  impl_->servidor.stop();
  if (impl_->hilo.joinable()) impl_->hilo.join();
}

}  // namespace puente_aula
