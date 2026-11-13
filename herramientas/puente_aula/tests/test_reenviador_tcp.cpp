// El reenviador TCP contra un "broker" falso en loopback. Las pruebas son sincronas: el
// broker falso es un socket de escucha y el test acepta y lee/escribe a mano.
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <thread>
#include <vector>

#include "apoyo.hpp"
#include "minitest.hpp"
#include "puente_aula/reenviador_tcp.hpp"

using namespace puente_aula;
using namespace apoyo;

namespace {

ConfigMqtt config_base(int puerto_broker) {
  ConfigMqtt config;
  config.escucha = {ip("127.0.0.1"), 0, ""};
  config.permitidos = {ip("127.0.0.1")};
  config.upstream = {ip("127.0.0.1"), puerto_broker, ""};
  config.max_conexiones = 2;
  config.connect_timeout_ms = 1000;
  return config;
}

// Un broker falso y el reenviador delante.
struct Escena {
  explicit Escena(const std::function<void(ConfigMqtt&)>& ajustar = nullptr) {
    broker = escuchar_loopback(puerto_broker);
    ConfigMqtt config = config_base(puerto_broker);
    if (ajustar) ajustar(config);
    reenviador = std::make_unique<ReenviadorTcp>(config);
    puerto = reenviador->iniciar();
  }
  ~Escena() {
    reenviador.reset();
    ::close(broker);
  }
  // Una conexion de la "XIAO" y la del reenviador que le llega al broker.
  bool conectar(int& cliente, int& lado_broker) {
    cliente = conectar_loopback(puerto);
    if (cliente < 0) return false;
    lado_broker = aceptar(broker, 1000);
    return lado_broker >= 0;
  }

  int puerto_broker = 0;
  int broker = -1;
  int puerto = 0;
  std::unique_ptr<ReenviadorTcp> reenviador;
};

struct Cierre {
  int fd;
  ~Cierre() {
    if (fd >= 0) ::close(fd);
  }
};

}  // namespace

TEST(tcp_pasa_bytes_en_los_dos_sentidos) {
  Escena escena;
  int cliente = -1, lado_broker = -1;
  CHECK(escena.conectar(cliente, lado_broker));
  Cierre c1{cliente}, c2{lado_broker};

  CHECK(enviar_todo(cliente, std::string("\x10\x0c\x00\x04MQTT", 8)));
  CHECK_EQ(leer_hasta(lado_broker, 8, 1000), std::string("\x10\x0c\x00\x04MQTT", 8));
  CHECK(enviar_todo(lado_broker, std::string("\x20\x02\x00\x00", 4)));
  CHECK_EQ(leer_hasta(cliente, 4, 1000), std::string("\x20\x02\x00\x00", 4));
  // Varios intercambios en la misma conexion.
  for (int i = 0; i < 20; ++i) {
    const std::string mensaje = "ping-" + std::to_string(i);
    CHECK(enviar_todo(cliente, mensaje));
    CHECK_EQ(leer_hasta(lado_broker, mensaje.size(), 1000), mensaje);
    CHECK(enviar_todo(lado_broker, mensaje));
    CHECK_EQ(leer_hasta(cliente, mensaje.size(), 1000), mensaje);
  }
}

TEST(tcp_mueve_un_volumen_grande_en_los_dos_sentidos_a_la_vez) {
  Escena escena;
  int cliente = -1, lado_broker = -1;
  CHECK(escena.conectar(cliente, lado_broker));
  Cierre c1{cliente}, c2{lado_broker};

  const std::size_t total = 2 * 1024 * 1024;
  std::string subida(total, '\0'), bajada(total, '\0');
  for (std::size_t i = 0; i < total; ++i) {
    subida[i] = static_cast<char>(i * 7 + 1);
    bajada[i] = static_cast<char>(i * 13 + 5);
  }
  std::thread envia_cliente([&] { enviar_todo(cliente, subida); });
  std::thread envia_broker([&] { enviar_todo(lado_broker, bajada); });
  const std::string en_broker = leer_hasta(lado_broker, total, 3000);
  const std::string en_cliente = leer_hasta(cliente, total, 3000);
  envia_cliente.join();
  envia_broker.join();
  CHECK(en_broker == subida);
  CHECK(en_cliente == bajada);
}

TEST(tcp_cierre_del_cliente_llega_al_broker_y_entrega_lo_pendiente) {
  Escena escena;
  int cliente = -1, lado_broker = -1;
  CHECK(escena.conectar(cliente, lado_broker));
  Cierre c2{lado_broker};

  // Un DISCONNECT seguido del cierre: el broker tiene que ver los dos.
  CHECK(enviar_todo(cliente, std::string("\xe0\x00", 2)));
  ::close(cliente);
  CHECK_EQ(leer_hasta(lado_broker, 2, 1000), std::string("\xe0\x00", 2));
  CHECK(espera_cierre(lado_broker, 1000));
  CHECK(esperar_hasta([&] { return escena.reenviador->conexiones_activas() == 0; }, 1000));
}

TEST(tcp_cierre_del_broker_llega_al_cliente) {
  Escena escena;
  int cliente = -1, lado_broker = -1;
  CHECK(escena.conectar(cliente, lado_broker));
  Cierre c1{cliente};

  CHECK(enviar_todo(lado_broker, std::string("\x20\x02\x00\x05", 4)));
  ::close(lado_broker);
  CHECK_EQ(leer_hasta(cliente, 4, 1000), std::string("\x20\x02\x00\x05", 4));
  CHECK(espera_cierre(cliente, 1000));
  CHECK(esperar_hasta([&] { return escena.reenviador->conexiones_activas() == 0; }, 1000));
}

TEST(tcp_cierre_abrupto_con_rst_tambien_se_propaga) {
  Escena escena;
  int cliente = -1, lado_broker = -1;
  CHECK(escena.conectar(cliente, lado_broker));
  Cierre c2{lado_broker};

  // SO_LINGER en cero: close() manda RST en vez de FIN (una placa que se cuelga).
  const linger abrupto{1, 0};
  ::setsockopt(cliente, SOL_SOCKET, SO_LINGER, &abrupto, sizeof abrupto);
  ::close(cliente);
  CHECK(espera_cierre(lado_broker, 1000));
}

// Con el tope lleno, una IP DISTINTA (permitida) no desaloja a nadie y se rechaza.
TEST(tcp_tope_de_conexiones_rechaza_a_otra_ip_sin_desalojar_a_nadie) {
  Escena escena([](ConfigMqtt& config) { config.permitidos = {ip("127.0.0.1"), ip("127.0.0.2")}; });
  int c1 = -1, b1 = -1, c2 = -1, b2 = -1;
  CHECK(escena.conectar(c1, b1));
  CHECK(escena.conectar(c2, b2));
  Cierre k1{c1}, k2{c2}, k3{b1}, k4{b2};
  CHECK_EQ(escena.reenviador->conexiones_activas(), static_cast<std::size_t>(2));

  const int c3 = conectar_desde("127.0.0.2", escena.puerto);
  Cierre k5{c3};
  CHECK(c3 >= 0);
  CHECK(espera_cierre(c3, 1000));
  CHECK_EQ(aceptar(escena.broker, 200), -1);  // no llego al broker
  // Las dos primeras siguen vivas, en los dos sentidos.
  CHECK(enviar_todo(c1, "a"));
  CHECK_EQ(leer_hasta(b1, 1, 1000), std::string("a"));
  CHECK(enviar_todo(b2, "b"));
  CHECK_EQ(leer_hasta(c2, 1, 1000), std::string("b"));
}

// La XIAO es un unico cliente: al reconectar con el tope lleno, la mas nueva es la buena.
TEST(tcp_tope_lleno_expulsa_la_conexion_mas_antigua_de_la_misma_ip) {
  Escena escena;  // tope 2, un solo origen permitido
  int c1 = -1, b1 = -1, c2 = -1, b2 = -1;
  CHECK(escena.conectar(c1, b1));
  CHECK(escena.conectar(c2, b2));
  Cierre k1{c1}, k2{c2}, k3{b1}, k4{b2};

  int c3 = -1, b3 = -1;
  CHECK(escena.conectar(c3, b3));  // el broker ve la tercera: se acepto
  Cierre k5{c3}, k6{b3};
  // La mas antigua (c1) se fue, en las dos puntas...
  CHECK(espera_cierre(c1, 1000));
  CHECK(espera_cierre(b1, 1000));
  // ... la del medio sigue y la nueva funciona.
  CHECK(enviar_todo(c2, "m"));
  CHECK_EQ(leer_hasta(b2, 1, 1000), std::string("m"));
  CHECK(enviar_todo(c3, "n"));
  CHECK_EQ(leer_hasta(b3, 1, 1000), std::string("n"));
  CHECK(esperar_hasta([&] { return escena.reenviador->conexiones_activas() == 2; }, 1000));

  // Y otra vez: ahora la mas antigua es c2.
  int c4 = -1, b4 = -1;
  CHECK(escena.conectar(c4, b4));
  Cierre k7{c4}, k8{b4};
  CHECK(espera_cierre(c2, 1000));
  CHECK(enviar_todo(c3, "o"));
  CHECK_EQ(leer_hasta(b3, 1, 1000), std::string("o"));
}

TEST(tcp_tope_configurable_con_una_sola_conexion) {
  Escena escena([](ConfigMqtt& config) {
    config.max_conexiones = 1;
    config.permitidos = {ip("127.0.0.1"), ip("127.0.0.2")};
  });
  int c1 = -1, b1 = -1;
  CHECK(escena.conectar(c1, b1));
  Cierre k1{c1}, k2{b1};
  const int c2 = conectar_desde("127.0.0.2", escena.puerto);
  Cierre k3{c2};
  CHECK(espera_cierre(c2, 1000));
  CHECK(enviar_todo(c1, "a"));
  CHECK_EQ(leer_hasta(b1, 1, 1000), std::string("a"));
}

// Cada motivo de rechazo tiene su propio limite de avisos: un escaneo de IPs no permitidas no
// calla el aviso del tope.
TEST(tcp_un_limite_de_avisos_por_motivo) {
  CapturaRegistro registro;
  Escena escena([](ConfigMqtt& config) {
    config.max_conexiones = 1;
    config.permitidos = {ip("127.0.0.1"), ip("127.0.0.2")};
  });
  int c1 = -1, b1 = -1;
  CHECK(escena.conectar(c1, b1));
  Cierre k1{c1}, k2{b1};

  for (int i = 0; i < 3; ++i) {
    const int intruso = conectar_desde("127.0.0.3", escena.puerto);  // no permitida
    Cierre k{intruso};
    CHECK(espera_cierre(intruso, 1000));
  }
  const int otra = conectar_desde("127.0.0.2", escena.puerto);  // permitida, con el tope lleno
  Cierre k3{otra};
  CHECK(espera_cierre(otra, 1000));

  const std::string todo = registro.todo();
  CHECK(todo.find("rechazada=no_permitida") != std::string::npos);
  CHECK(todo.find("rechazada=tope_de_conexiones") != std::string::npos);
  // Tres intrusos seguidos dentro de la misma ventana: un solo aviso.
  std::size_t avisos = 0;
  for (const auto& linea : registro.lineas()) {
    if (linea.find("rechazada=no_permitida") != std::string::npos) ++avisos;
  }
  CHECK_EQ(avisos, static_cast<std::size_t>(1));
}

// Mientras el puente conecta con el broker vigila al cliente: si este se va, no espera al plazo.
TEST(tcp_si_el_cliente_se_va_durante_la_conexion_al_broker_abandona_enseguida) {
  // Un "broker" cuya cola de conexiones pendientes esta llena: los SYN siguientes se descartan y
  // la conexion del reenviador se queda en SYN_SENT (en loopback, sin salir de la maquina).
  int puerto_broker = 0;
  const int llena = [&] {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in direccion{};
    direccion.sin_family = AF_INET;
    direccion.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(fd, reinterpret_cast<sockaddr*>(&direccion), sizeof direccion);
    ::listen(fd, 0);
    socklen_t largo = sizeof direccion;
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&direccion), &largo);
    puerto_broker = ntohs(direccion.sin_port);
    return fd;
  }();
  Cierre cierre_llena{llena};
  std::vector<int> rellenos;
  bool atascado = false;
  for (int i = 0; i < 10 && !atascado; ++i) {
    const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    sockaddr_in direccion{};
    direccion.sin_family = AF_INET;
    direccion.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    direccion.sin_port = htons(static_cast<std::uint16_t>(puerto_broker));
    ::connect(fd, reinterpret_cast<sockaddr*>(&direccion), sizeof direccion);
    pollfd espera{fd, POLLOUT, 0};
    if (::poll(&espera, 1, 150) == 0) atascado = true;
    rellenos.push_back(fd);
  }
  struct CerrarTodos {
    std::vector<int>& fds;
    ~CerrarTodos() {
      for (const int fd : fds) ::close(fd);
    }
  } cerrar_todos{rellenos};
  CHECK(atascado);  // si el kernel no atasca el SYN, la prueba no significa nada

  CapturaRegistro registro;
  ConfigMqtt config = config_base(puerto_broker);
  config.connect_timeout_ms = 10000;
  ReenviadorTcp reenviador(config);
  const int puerto = reenviador.iniciar();
  const int cliente = conectar_loopback(puerto);
  Cierre c{cliente};
  CHECK(cliente >= 0);
  CHECK(esperar_hasta([&] { return reenviador.conexiones_activas() == 1; }, 1000));
  const auto inicio = std::chrono::steady_clock::now();
  ::close(cliente);
  c.fd = -1;
  CHECK(esperar_hasta([&] { return reenviador.conexiones_activas() == 0; }, 3000));
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - inicio)
                      .count();
  CHECK(ms < 3000);  // el plazo de conexion era de 10 s
  CHECK(registro.todo().find("el cliente se fue durante la conexion") != std::string::npos);
}

TEST(tcp_escucha_atado_a_una_interfaz_y_rechaza_una_inexistente) {
  Escena escena([](ConfigMqtt& config) { config.escucha.interfaz = "lo"; });
  int cliente = -1, lado_broker = -1;
  CHECK(escena.conectar(cliente, lado_broker));
  Cierre c1{cliente}, c2{lado_broker};
  CHECK(enviar_todo(cliente, "x"));
  CHECK_EQ(leer_hasta(lado_broker, 1, 1000), std::string("x"));

  int puerto_broker = 0;
  const int broker = escuchar_loopback(puerto_broker);
  Cierre c3{broker};
  ConfigMqtt config = config_base(puerto_broker);
  config.escucha.interfaz = "noexiste0";
  ReenviadorTcp malo(config);
  try {
    malo.iniciar();
    CHECK(false);
  } catch (const std::runtime_error& error) {
    CHECK(std::string(error.what()).find("noexiste0") != std::string::npos);
  }
}

TEST(tcp_origen_no_permitido_se_cierra_sin_abrir_conexion_al_broker) {
  Escena escena([](ConfigMqtt& config) { config.permitidos = {ip("127.0.0.2")}; });
  const int cliente = conectar_loopback(escena.puerto);
  Cierre c{cliente};
  CHECK(cliente >= 0);
  CHECK(espera_cierre(cliente, 1000));
  CHECK_EQ(aceptar(escena.broker, 200), -1);
  CHECK_EQ(escena.reenviador->conexiones_activas(), static_cast<std::size_t>(0));
}

TEST(tcp_broker_inalcanzable_cierra_al_cliente_enseguida) {
  PuertoCerrado cerrado;
  Escena escena([&](ConfigMqtt& config) { config.upstream.puerto = cerrado.puerto; });
  const int cliente = conectar_loopback(escena.puerto);
  Cierre c{cliente};
  CHECK(cliente >= 0);
  CHECK(espera_cierre(cliente, 1000));
  CHECK(esperar_hasta([&] { return escena.reenviador->conexiones_activas() == 0; }, 1000));
}

TEST(tcp_detener_cierra_las_conexiones_abiertas) {
  Escena escena;
  int cliente = -1, lado_broker = -1;
  CHECK(escena.conectar(cliente, lado_broker));
  Cierre c1{cliente}, c2{lado_broker};
  escena.reenviador->detener();
  CHECK(espera_cierre(cliente, 1000));
  CHECK(espera_cierre(lado_broker, 1000));
  CHECK_EQ(conectar_loopback(escena.puerto), -1);  // ya no escucha
  escena.reenviador->detener();                    // idempotente
}

TEST(tcp_registra_conexion_y_cierre_con_bytes_sin_los_datos) {
  CapturaRegistro registro;
  Escena escena;
  int cliente = -1, lado_broker = -1;
  CHECK(escena.conectar(cliente, lado_broker));
  Cierre c2{lado_broker};

  const std::string conexion_mqtt = "CONNECT-usuario-xiao-CONTRASENA-MQTT-123";
  CHECK(enviar_todo(cliente, conexion_mqtt));
  CHECK_EQ(leer_hasta(lado_broker, conexion_mqtt.size(), 1000), conexion_mqtt);
  CHECK(enviar_todo(lado_broker, "CONNACK"));
  CHECK_EQ(leer_hasta(cliente, 7, 1000), std::string("CONNACK"));
  ::close(cliente);
  CHECK(esperar_hasta([&] { return registro.todo().find("cierre ") != std::string::npos; }, 2000));

  const std::string todo = registro.todo();
  CHECK(todo.find("conexion origen=127.0.0.1") != std::string::npos);
  CHECK(todo.find("subida_bytes=" + std::to_string(conexion_mqtt.size())) != std::string::npos);
  CHECK(todo.find("bajada_bytes=7") != std::string::npos);
  CHECK(todo.find("duracion_ms=") != std::string::npos);
  CHECK(todo.find("motivo=cliente_cerro") != std::string::npos);
  CHECK(todo.find("CONTRASENA") == std::string::npos);
  CHECK(todo.find("CONNACK") == std::string::npos);
}

TEST(tcp_no_termino_inesperadamente_ni_al_detenerlo) {
  Escena escena;
  CHECK(!escena.reenviador->termino_inesperadamente());
  escena.reenviador->detener();
  CHECK(!escena.reenviador->termino_inesperadamente());
}
