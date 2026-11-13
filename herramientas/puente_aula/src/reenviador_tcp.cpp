#include "puente_aula/reenviador_tcp.hpp"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <list>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "puente_aula/registro.hpp"
#include "puente_aula/sistema.hpp"

namespace puente_aula {

namespace {

using Reloj = std::chrono::steady_clock;

constexpr const char* kComponente = "mqtt";
constexpr std::size_t kTamBuffer = 16 * 1024;
// Cuanto se espera a entregar lo ya leido cuando una punta cierra.
constexpr int kVaciadoMs = 2000;

// Descriptor con dueno unico: se cierra al salir de ambito.
class Descriptor {
 public:
  Descriptor() = default;
  explicit Descriptor(int fd) : fd_(fd) {}
  Descriptor(Descriptor&& otro) noexcept : fd_(otro.liberar()) {}
  Descriptor& operator=(Descriptor&& otro) noexcept {
    if (this != &otro) {
      cerrar();
      fd_ = otro.liberar();
    }
    return *this;
  }
  Descriptor(const Descriptor&) = delete;
  Descriptor& operator=(const Descriptor&) = delete;
  ~Descriptor() { cerrar(); }

  int get() const { return fd_; }
  bool valido() const { return fd_ >= 0; }
  int liberar() {
    const int fd = fd_;
    fd_ = -1;
    return fd;
  }
  void cerrar() {
    if (fd_ >= 0) {
      ::shutdown(fd_, SHUT_RDWR);
      ::close(fd_);
      fd_ = -1;
    }
  }

 private:
  int fd_ = -1;
};

std::string texto_errno(int codigo) { return std::strerror(codigo); }

std::int64_t ms_desde(Reloj::time_point inicio) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(Reloj::now() - inicio).count();
}

// Keepalive corto, TCP_USER_TIMEOUT con el mismo plazo total (el keepalive solo no detecta
// datos sin confirmar) y TCP_NODELAY (MQTT son mensajes pequenos y sensibles a la latencia).
bool ajustar_socket(int fd, const Keepalive& keepalive, std::string& error) {
  const int uno = 1;
  const int inactividad = keepalive.inactividad_s;
  const int intervalo = keepalive.intervalo_s;
  const int sondeos = keepalive.sondeos;
  const unsigned int usuario_ms =
      static_cast<unsigned int>(inactividad + intervalo * sondeos) * 1000u;
  const struct {
    int nivel;
    int opcion;
    const void* valor;
    socklen_t longitud;
    const char* nombre;
  } opciones[] = {
      {SOL_SOCKET, SO_KEEPALIVE, &uno, sizeof uno, "SO_KEEPALIVE"},
      {IPPROTO_TCP, TCP_KEEPIDLE, &inactividad, sizeof inactividad, "TCP_KEEPIDLE"},
      {IPPROTO_TCP, TCP_KEEPINTVL, &intervalo, sizeof intervalo, "TCP_KEEPINTVL"},
      {IPPROTO_TCP, TCP_KEEPCNT, &sondeos, sizeof sondeos, "TCP_KEEPCNT"},
      {IPPROTO_TCP, TCP_USER_TIMEOUT, &usuario_ms, sizeof usuario_ms, "TCP_USER_TIMEOUT"},
      {IPPROTO_TCP, TCP_NODELAY, &uno, sizeof uno, "TCP_NODELAY"},
  };
  for (const auto& opcion : opciones) {
    if (::setsockopt(fd, opcion.nivel, opcion.opcion, opcion.valor, opcion.longitud) != 0) {
      error = std::string(opcion.nombre) + ": " + texto_errno(errno);
      return false;
    }
  }
  return true;
}

// Conecta (sin bloquear mas que `timeout_ms`) y devuelve el socket ya en modo no bloqueante.
// Mientras conecta vigila tambien al cliente: si se va (FIN o error) o si la conexion se
// expulsa o el puente se detiene, abandona en vez de agotar el plazo.
Descriptor conectar(const sockaddr_storage& destino, socklen_t longitud, int timeout_ms,
                    int cliente_fd, int cierre_fd, int parada_fd, std::string& error) {
  Descriptor socket_fd(::socket(destino.ss_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
  if (!socket_fd.valido()) {
    error = "socket: " + texto_errno(errno);
    return Descriptor();
  }
  if (::connect(socket_fd.get(), reinterpret_cast<const sockaddr*>(&destino), longitud) != 0 &&
      errno != EINPROGRESS) {
    error = "connect: " + texto_errno(errno);
    return Descriptor();
  }
  pollfd esperas[4] = {{socket_fd.get(), POLLOUT, 0},
                       {parada_fd, POLLIN, 0},
                       {cierre_fd, POLLIN, 0},
                       {cliente_fd, POLLRDHUP, 0}};
  const auto limite = Reloj::now() + std::chrono::milliseconds(timeout_ms);
  for (;;) {
    const auto restante =
        std::chrono::duration_cast<std::chrono::milliseconds>(limite - Reloj::now()).count();
    if (restante <= 0) {
      error = "tiempo de conexion agotado";
      return Descriptor();
    }
    const int listo = ::poll(esperas, 4, static_cast<int>(restante));
    if (listo < 0 && errno == EINTR) continue;
    if (listo < 0) {
      error = "poll: " + texto_errno(errno);
      return Descriptor();
    }
    if (esperas[1].revents != 0 || esperas[2].revents != 0) {
      error = "parada o expulsion solicitada";
      return Descriptor();
    }
    if (esperas[3].revents != 0) {
      error = "el cliente se fue durante la conexion";
      return Descriptor();
    }
    if (esperas[0].revents != 0) break;
  }
  int fallo = 0;
  socklen_t largo = sizeof fallo;
  if (::getsockopt(socket_fd.get(), SOL_SOCKET, SO_ERROR, &fallo, &largo) != 0 || fallo != 0) {
    error = "connect: " + texto_errno(fallo != 0 ? fallo : errno);
    return Descriptor();
  }
  return socket_fd;
}

// Un sentido de la bomba: lo leido de una punta y aun no entregado a la otra.
struct Canal {
  char datos[kTamBuffer];
  std::size_t inicio = 0;
  std::size_t fin = 0;
  bool eof = false;  // la punta de origen cerro
  std::uint64_t total = 0;

  bool vacio() const { return inicio == fin; }
  std::size_t libre() const { return kTamBuffer - fin; }
  // Los datos pueden ser la contrasena MQTT: no se dejan en memoria liberada.
  ~Canal() { explicit_bzero(datos, sizeof datos); }
};

enum class Motivo { ClienteCerro, UpstreamCerro, Error, Parada, Expulsada, VaciadoAgotado };

const char* texto_motivo(Motivo motivo) {
  switch (motivo) {
    case Motivo::ClienteCerro: return "cliente_cerro";
    case Motivo::UpstreamCerro: return "upstream_cerro";
    case Motivo::Error: return "error";
    case Motivo::Parada: return "parada";
    case Motivo::Expulsada: return "expulsada_por_reconexion";
    case Motivo::VaciadoAgotado: return "vaciado_agotado";
  }
  return "desconocido";
}

struct ResultadoBomba {
  std::uint64_t subida = 0;   // cliente -> upstream
  std::uint64_t bajada = 0;   // upstream -> cliente
  Motivo motivo = Motivo::Error;
};

// Lee de `origen` a `canal`. false si la lectura fallo (no es EOF).
bool leer(int origen, Canal& canal) {
  const ssize_t leidos = ::recv(origen, canal.datos + canal.fin, canal.libre(), 0);
  if (leidos > 0) {
    canal.fin += static_cast<std::size_t>(leidos);
    canal.total += static_cast<std::uint64_t>(leidos);
    return true;
  }
  if (leidos == 0) {
    canal.eof = true;
    return true;
  }
  return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
}

// Escribe lo pendiente de `canal` en `destino`. false si la escritura fallo.
bool escribir(int destino, Canal& canal) {
  while (!canal.vacio()) {
    const ssize_t escritos =
        ::send(destino, canal.datos + canal.inicio, canal.fin - canal.inicio, MSG_NOSIGNAL);
    if (escritos > 0) {
      canal.inicio += static_cast<std::size_t>(escritos);
      continue;
    }
    if (escritos < 0 && errno == EINTR) continue;
    if (escritos < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
    return false;
  }
  if (canal.vacio()) canal.inicio = canal.fin = 0;
  return true;
}

ResultadoBomba bombear(int cliente, int upstream, int parada, int cierre) {
  auto subida = std::make_unique<Canal>();  // cliente -> upstream
  auto bajada = std::make_unique<Canal>();  // upstream -> cliente
  ResultadoBomba resultado;
  bool vaciando = false;
  Reloj::time_point limite_vaciado;

  const auto cerrar_con = [&](Motivo motivo) {
    resultado.motivo = motivo;
    resultado.subida = subida->total;
    resultado.bajada = bajada->total;
  };

  for (;;) {
    pollfd esperas[4] = {{parada, POLLIN, 0}, {cliente, 0, 0}, {upstream, 0, 0}, {cierre, POLLIN, 0}};
    const bool lee_cliente = !subida->eof && subida->libre() > 0;
    const bool lee_upstream = !bajada->eof && bajada->libre() > 0;
    if (lee_cliente) esperas[1].events |= POLLIN;
    if (!bajada->vacio()) esperas[1].events |= POLLOUT;
    if (lee_upstream) esperas[2].events |= POLLIN;
    if (!subida->vacio()) esperas[2].events |= POLLOUT;

    int espera_ms = -1;
    if (vaciando) {
      const auto restante =
          std::chrono::duration_cast<std::chrono::milliseconds>(limite_vaciado - Reloj::now())
              .count();
      espera_ms = restante > 0 ? static_cast<int>(restante) : 0;
    }
    const int listo = ::poll(esperas, 4, espera_ms);
    if (listo < 0) {
      if (errno == EINTR) continue;
      cerrar_con(Motivo::Error);
      return resultado;
    }
    if (esperas[0].revents != 0) {
      cerrar_con(Motivo::Parada);
      return resultado;
    }
    if (esperas[3].revents != 0) {
      cerrar_con(Motivo::Expulsada);
      return resultado;
    }

    // Una punta con POLLERR, o con POLLHUP sin que la estemos leyendo, ya no sirve.
    for (int i = 1; i <= 2; ++i) {
      const short eventos = esperas[i].events;
      const short vistos = esperas[i].revents;
      if ((vistos & (POLLERR | POLLNVAL)) != 0 ||
          ((vistos & POLLHUP) != 0 && (eventos & POLLIN) == 0)) {
        cerrar_con(Motivo::Error);
        return resultado;
      }
    }

    // Primero se entrega y despues se lee: se vacia antes de pedir mas.
    if ((esperas[2].revents & POLLOUT) != 0 && !escribir(upstream, *subida)) {
      cerrar_con(Motivo::Error);
      return resultado;
    }
    if ((esperas[1].revents & POLLOUT) != 0 && !escribir(cliente, *bajada)) {
      cerrar_con(Motivo::Error);
      return resultado;
    }
    if ((esperas[1].events & POLLIN) != 0 && esperas[1].revents != 0 && !leer(cliente, *subida)) {
      cerrar_con(Motivo::Error);
      return resultado;
    }
    if ((esperas[2].events & POLLIN) != 0 && esperas[2].revents != 0 && !leer(upstream, *bajada)) {
      cerrar_con(Motivo::Error);
      return resultado;
    }
    // Lo recien leido se intenta entregar ya, sin esperar otra vuelta de poll.
    if (!escribir(upstream, *subida) || !escribir(cliente, *bajada)) {
      cerrar_con(Motivo::Error);
      return resultado;
    }

    if (subida->eof && subida->vacio()) {
      cerrar_con(Motivo::ClienteCerro);
      return resultado;
    }
    if (bajada->eof && bajada->vacio()) {
      cerrar_con(Motivo::UpstreamCerro);
      return resultado;
    }
    if ((subida->eof || bajada->eof) && !vaciando) {
      vaciando = true;
      limite_vaciado = Reloj::now() + std::chrono::milliseconds(kVaciadoMs);
    }
    if (vaciando && Reloj::now() >= limite_vaciado) {
      cerrar_con(Motivo::VaciadoAgotado);
      return resultado;
    }
  }
}

}  // namespace

struct ReenviadorTcp::Impl {
  // Una conexion en curso. El nodo de la lista se crea ANTES de lanzar el hilo (si crear el
  // hilo falla, no queda un hilo sin dueno ni un std::terminate: se retira el nodo y se cierra
  // el socket).
  struct Conexion {
    std::thread hilo;
    std::atomic<bool> terminada{false};
    std::atomic<bool> expulsada{false};
    Descriptor cierre;  // eventfd propio: una escritura expulsa esta conexion
    DireccionIp origen;
    Reloj::time_point inicio = Reloj::now();
  };

  explicit Impl(ConfigMqtt configuracion) : config(std::move(configuracion)) {}

  ConfigMqtt config;
  Descriptor escucha;
  Descriptor parada;  // eventfd: una vez escrito, queda legible para todos los poll()
  std::thread aceptador;
  std::atomic<std::size_t> activas{0};
  std::mutex mutex;  // protege `conexiones`
  std::list<std::unique_ptr<Conexion>> conexiones;
  // Un limite de avisos por motivo: un escaneo no tapa el aviso de otra cosa.
  registro::LimiteAvisos avisos_no_permitida;
  registro::LimiteAvisos avisos_tope;
  registro::LimiteAvisos avisos_expulsion;
  bool iniciado = false;
  std::atomic<bool> aceptador_termino{false};  // el hilo que acepta acabo (por detener() o no)
  std::atomic<bool> deteniendo{false};         // alguien llamo a detener()

  // El hilo que acepta: si algo lo tumba (sin memoria, un error de poll), se avisa al proceso
  // para que salga con error en vez de seguir "vivo" sin aceptar a nadie.
  void aceptar_protegido() {
    try {
      aceptar_en_bucle();
    } catch (const std::exception& error) {
      registro::linea(kComponente, std::string("el aceptador fallo: ") + error.what());
    } catch (...) {
      registro::linea(kComponente, "el aceptador fallo por una excepcion desconocida");
    }
    aceptador_termino = true;
  }

  void aceptar_en_bucle() {
    pollfd esperas[2] = {{escucha.get(), POLLIN, 0}, {parada.get(), POLLIN, 0}};
    for (;;) {
      const int listo = ::poll(esperas, 2, 1000);
      segar_terminadas();
      if (listo < 0 && errno == EINTR) continue;
      if (listo < 0) {
        registro::linea(kComponente, "poll del aceptador fallo: " + texto_errno(errno));
        return;
      }
      if (esperas[1].revents != 0) return;
      if (esperas[0].revents == 0) continue;

      sockaddr_storage origen{};
      socklen_t longitud = sizeof origen;
      Descriptor cliente(::accept4(escucha.get(), reinterpret_cast<sockaddr*>(&origen), &longitud,
                                   SOCK_NONBLOCK | SOCK_CLOEXEC));
      if (!cliente.valido()) {
        const int codigo = errno;
        if (codigo == EAGAIN || codigo == EWOULDBLOCK || codigo == EINTR ||
            codigo == ECONNABORTED) {
          continue;
        }
        registro::linea(kComponente, "accept fallo: " + texto_errno(codigo));
        // Sin descriptores libres, poll devolveria "listo" sin parar: se frena el bucle.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        continue;
      }
      try {
        atender(std::move(cliente),
                ip_de_sockaddr(reinterpret_cast<const sockaddr*>(&origen), longitud));
      } catch (const std::exception& error) {
        // Sin memoria o sin hilos: se pierde esa conexion, no el puente.
        registro::linea(kComponente, std::string("no se pudo atender una conexion: ") + error.what());
      }
    }
  }

  // El tope de conexiones. La XIAO es un unico cliente: si ya hay `max_conexiones` y llega otra
  // de la MISMA IP, la mas nueva es la buena (la vieja es una sesion zombi que la placa ya
  // abandono), asi que se expulsa la mas antigua de esa IP. De otra IP no se desaloja a nadie.
  enum class Lugar { No, Si, SiExpulsando };

  // No registra nada: se llama con el mutex tomado y el registro se escribe fuera de el.
  Lugar hacer_lugar(const DireccionIp& origen) {
    std::lock_guard<std::mutex> cerrojo(mutex);
    std::size_t vivas = 0;
    std::size_t hilos = 0;
    Conexion* mas_antigua = nullptr;
    for (auto& conexion : conexiones) {
      if (conexion->terminada) continue;
      ++hilos;
      if (conexion->expulsada) continue;
      ++vivas;
      if (conexion->origen == origen &&
          (mas_antigua == nullptr || conexion->inicio < mas_antigua->inicio)) {
        mas_antigua = conexion.get();
      }
    }
    // Las expulsadas tardan unos milisegundos en irse: hasta el doble del tope, y no mas.
    if (hilos >= 2 * config.max_conexiones) return Lugar::No;
    if (vivas < config.max_conexiones) return Lugar::Si;
    if (mas_antigua == nullptr) return Lugar::No;
    mas_antigua->expulsada = true;
    const std::uint64_t uno = 1;
    const ssize_t escritos = ::write(mas_antigua->cierre.get(), &uno, sizeof uno);
    (void)escritos;
    return Lugar::SiExpulsando;
  }

  void atender(Descriptor cliente, const std::optional<DireccionIp>& origen) {
    if (!origen || !ip_permitida(*origen, config.permitidos)) {
      avisar_rechazo(avisos_no_permitida, origen, "no_permitida");
      return;  // el Descriptor cierra el socket
    }

    // Todo lo que puede fallar por falta de recursos se reserva ANTES de decidir el lugar: si no
    // hay memoria o descriptores, no se expulsa a una conexion buena por una que no se puede
    // atender.
    auto conexion = std::make_unique<Conexion>();
    conexion->origen = *origen;
    conexion->cierre = Descriptor(::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK));
    if (!conexion->cierre.valido()) {
      registro::linea(kComponente, "eventfd fallo: " + texto_errno(errno));
      return;
    }

    const Lugar lugar = hacer_lugar(*origen);
    if (lugar == Lugar::No) {
      avisar_rechazo(avisos_tope, origen, "tope_de_conexiones");
      return;
    }
    if (lugar == Lugar::SiExpulsando) {
      std::uint64_t omitidos = 0;
      if (avisos_expulsion.permitir(omitidos)) {
        registro::linea(kComponente, "tope alcanzado: se expulsa la conexion mas antigua de origen=" +
                                         a_texto(*origen) + " para aceptar la nueva" +
                                         (omitidos > 0 ? " omitidos=" + std::to_string(omitidos) : ""));
      }
    }

    Conexion* puntero = conexion.get();
    {
      // Se reserva el nodo antes de lanzar el hilo.
      std::lock_guard<std::mutex> cerrojo(mutex);
      conexiones.push_back(std::move(conexion));
    }
    ++activas;
    try {
      puntero->hilo = std::thread([this, puntero, cliente = std::move(cliente)]() mutable {
        // Pase lo que pase en servir() (incluida una excepcion), la conexion se da por terminada y
        // el contador baja: si no, el hueco se perderia para siempre.
        struct AlSalir {
          Impl& impl;
          Conexion* conexion;
          ~AlSalir() {
            --impl.activas;
            conexion->terminada = true;
          }
        } al_salir{*this, puntero};
        try {
          servir(std::move(cliente), *puntero);
        } catch (const std::exception& error) {
          registro::linea(kComponente, std::string("la conexion fallo: ") + error.what());
        } catch (...) {
          registro::linea(kComponente, "la conexion fallo por una excepcion desconocida");
        }
      });
    } catch (...) {
      --activas;
      {
        std::lock_guard<std::mutex> cerrojo(mutex);
        conexiones.remove_if([puntero](const std::unique_ptr<Conexion>& c) { return c.get() == puntero; });
      }
      registro::linea(kComponente, "no se pudo crear el hilo de la conexion; se descarta");
    }
  }

  void avisar_rechazo(registro::LimiteAvisos& limite, const std::optional<DireccionIp>& origen,
                      const char* motivo) {
    std::uint64_t omitidos = 0;
    if (!limite.permitir(omitidos)) return;
    std::string texto = "origen=" + (origen ? a_texto(*origen) : std::string("desconocido")) +
                        " rechazada=" + motivo;
    if (omitidos > 0) texto += " omitidos=" + std::to_string(omitidos);
    registro::linea(kComponente, texto);
  }

  // Une los hilos de las conexiones que ya terminaron.
  void segar_terminadas() {
    std::list<std::unique_ptr<Conexion>> terminadas;
    {
      std::lock_guard<std::mutex> cerrojo(mutex);
      for (auto it = conexiones.begin(); it != conexiones.end();) {
        if ((*it)->terminada) {
          terminadas.push_back(std::move(*it));
          it = conexiones.erase(it);
        } else {
          ++it;
        }
      }
    }
    for (auto& conexion : terminadas) {
      if (conexion->hilo.joinable()) conexion->hilo.join();
    }
  }

  void servir(Descriptor cliente, Conexion& conexion) {
    const auto inicio = conexion.inicio;
    const std::string texto_origen = a_texto(conexion.origen);
    const std::string texto_destino =
        a_texto(config.upstream.ip) + ":" + std::to_string(config.upstream.puerto);

    std::string error;
    sockaddr_storage destino;
    const socklen_t longitud = a_sockaddr(config.upstream.ip, config.upstream.puerto, destino);
    Descriptor upstream = conectar(destino, longitud, config.connect_timeout_ms, cliente.get(),
                                   conexion.cierre.get(), parada.get(), error);
    if (!upstream.valido()) {
      registro::linea(kComponente, "origen=" + texto_origen + " upstream=" + texto_destino +
                                       " rechazada=upstream_inalcanzable detalle=\"" + error +
                                       "\" ms=" + std::to_string(ms_desde(inicio)));
      return;
    }
    if (!ajustar_socket(cliente.get(), config.keepalive, error) ||
        !ajustar_socket(upstream.get(), config.keepalive, error)) {
      registro::linea(kComponente, "origen=" + texto_origen + " rechazada=opciones_de_socket" +
                                       " detalle=\"" + error + "\"");
      return;
    }

    registro::linea(kComponente, "conexion origen=" + texto_origen + " upstream=" + texto_destino);
    const ResultadoBomba resultado =
        bombear(cliente.get(), upstream.get(), parada.get(), conexion.cierre.get());
    upstream.cerrar();
    cliente.cerrar();
    registro::linea(kComponente, "cierre origen=" + texto_origen + " upstream=" + texto_destino +
                                     " subida_bytes=" + std::to_string(resultado.subida) +
                                     " bajada_bytes=" + std::to_string(resultado.bajada) +
                                     " duracion_ms=" + std::to_string(ms_desde(inicio)) +
                                     " motivo=" + texto_motivo(resultado.motivo));
  }
};

ReenviadorTcp::ReenviadorTcp(ConfigMqtt config) : impl_(std::make_unique<Impl>(std::move(config))) {}

ReenviadorTcp::~ReenviadorTcp() { detener(); }

int ReenviadorTcp::iniciar() {
  Impl& impl = *impl_;
  if (impl.iniciado) throw std::logic_error("mqtt: ya iniciado");
  const std::string texto_ip = a_texto(impl.config.escucha.ip);

  sockaddr_storage direccion;
  const socklen_t longitud =
      a_sockaddr(impl.config.escucha.ip, impl.config.escucha.puerto, direccion);
  Descriptor escucha(::socket(direccion.ss_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
  if (!escucha.valido()) throw std::runtime_error("mqtt: socket: " + texto_errno(errno));
  const int uno = 1;
  ::setsockopt(escucha.get(), SOL_SOCKET, SO_REUSEADDR, &uno, sizeof uno);
  std::string error_interfaz;
  if (!atar_a_interfaz(escucha.get(), impl.config.escucha.interfaz, error_interfaz)) {
    throw std::runtime_error("mqtt: " + error_interfaz);
  }
  if (::bind(escucha.get(), reinterpret_cast<const sockaddr*>(&direccion), longitud) != 0) {
    throw std::runtime_error("mqtt: no se pudo escuchar en " + texto_ip + ":" +
                             std::to_string(impl.config.escucha.puerto) + ": " +
                             texto_errno(errno));
  }
  if (::listen(escucha.get(), 8) != 0) {
    throw std::runtime_error("mqtt: listen: " + texto_errno(errno));
  }
  sockaddr_storage real{};
  socklen_t largo_real = sizeof real;
  ::getsockname(escucha.get(), reinterpret_cast<sockaddr*>(&real), &largo_real);
  const int puerto = real.ss_family == AF_INET6
                         ? ntohs(reinterpret_cast<sockaddr_in6*>(&real)->sin6_port)
                         : ntohs(reinterpret_cast<sockaddr_in*>(&real)->sin_port);

  Descriptor parada(::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK));
  if (!parada.valido()) throw std::runtime_error("mqtt: eventfd: " + texto_errno(errno));

  impl.escucha = std::move(escucha);
  impl.parada = std::move(parada);
  impl.iniciado = true;
  impl.aceptador = std::thread([&impl] { impl.aceptar_protegido(); });
  const std::string interfaz =
      impl.config.escucha.interfaz.empty() ? "" : " interfaz=" + impl.config.escucha.interfaz;
  registro::linea(kComponente, "escuchando en " + texto_ip + ":" + std::to_string(puerto) +
                                   interfaz + " -> " + a_texto(impl.config.upstream.ip) + ":" +
                                   std::to_string(impl.config.upstream.puerto));
  return puerto;
}

void ReenviadorTcp::detener() {
  if (!impl_ || !impl_->iniciado) return;
  Impl& impl = *impl_;
  impl.iniciado = false;
  impl.deteniendo = true;
  const std::uint64_t uno = 1;
  const ssize_t escritos = ::write(impl.parada.get(), &uno, sizeof uno);
  (void)escritos;  // un eventfd recien creado no se llena
  if (impl.aceptador.joinable()) impl.aceptador.join();
  std::list<std::unique_ptr<Impl::Conexion>> restantes;
  {
    std::lock_guard<std::mutex> cerrojo(impl.mutex);
    restantes.swap(impl.conexiones);
  }
  for (auto& conexion : restantes) {
    if (conexion->hilo.joinable()) conexion->hilo.join();
  }
  impl.escucha.cerrar();
}

bool ReenviadorTcp::termino_inesperadamente() const {
  return impl_->iniciado && impl_->aceptador_termino && !impl_->deteniendo;
}

std::size_t ReenviadorTcp::conexiones_activas() const { return impl_->activas.load(); }

}  // namespace puente_aula
