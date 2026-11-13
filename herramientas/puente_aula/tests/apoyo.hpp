#pragma once

// Ayudas de las pruebas con sockets en loopback (nada sale de 127.0.0.1).

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <cerrno>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <functional>
#include <mutex>
#include <thread>
#include <string>
#include <string_view>
#include <vector>

#include "puente_aula/ip.hpp"
#include "puente_aula/registro.hpp"

namespace apoyo {

inline puente_aula::DireccionIp ip(const char* texto) {
  return *puente_aula::parsear_ip(texto);
}

// Escucha en 127.0.0.1 con puerto libre; devuelve el fd y rellena el puerto.
inline int escuchar_loopback(int& puerto) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in direccion{};
  direccion.sin_family = AF_INET;
  direccion.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  direccion.sin_port = 0;
  ::bind(fd, reinterpret_cast<sockaddr*>(&direccion), sizeof direccion);
  ::listen(fd, 8);
  socklen_t largo = sizeof direccion;
  ::getsockname(fd, reinterpret_cast<sockaddr*>(&direccion), &largo);
  puerto = ntohs(direccion.sin_port);
  return fd;
}

// Un puerto de loopback en el que NO escucha nadie y que nadie mas puede tomar mientras viva el
// objeto: se reserva con bind() sin listen(), asi que connect() recibe ECONNREFUSED y ningun
// otro proceso (ni otro test) puede quedarse con ese numero entre medias.
class PuertoCerrado {
 public:
  PuertoCerrado() {
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in direccion{};
    direccion.sin_family = AF_INET;
    direccion.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(fd_, reinterpret_cast<sockaddr*>(&direccion), sizeof direccion);
    socklen_t largo = sizeof direccion;
    ::getsockname(fd_, reinterpret_cast<sockaddr*>(&direccion), &largo);
    puerto = ntohs(direccion.sin_port);
  }
  ~PuertoCerrado() { ::close(fd_); }
  PuertoCerrado(const PuertoCerrado&) = delete;
  PuertoCerrado& operator=(const PuertoCerrado&) = delete;
  int puerto = 0;

 private:
  int fd_ = -1;
};

// Acepta una conexion (o -1 si no llega en `ms`).
inline int aceptar(int escucha, int ms) {
  pollfd espera{escucha, POLLIN, 0};
  if (::poll(&espera, 1, ms) <= 0) return -1;
  return ::accept(escucha, nullptr, nullptr);
}

inline int conectar_loopback(int puerto) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in direccion{};
  direccion.sin_family = AF_INET;
  direccion.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  direccion.sin_port = htons(static_cast<std::uint16_t>(puerto));
  if (::connect(fd, reinterpret_cast<sockaddr*>(&direccion), sizeof direccion) != 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}

// Conecta a 127.0.0.1:puerto con una IP de origen distinta (todo 127.0.0.0/8 es loopback).
inline int conectar_desde(const char* origen, int puerto) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in local{};
  local.sin_family = AF_INET;
  ::inet_pton(AF_INET, origen, &local.sin_addr);
  if (::bind(fd, reinterpret_cast<sockaddr*>(&local), sizeof local) != 0) {
    ::close(fd);
    return -1;
  }
  sockaddr_in direccion{};
  direccion.sin_family = AF_INET;
  direccion.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  direccion.sin_port = htons(static_cast<std::uint16_t>(puerto));
  if (::connect(fd, reinterpret_cast<sockaddr*>(&direccion), sizeof direccion) != 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}

inline bool enviar_todo(int fd, std::string_view datos) {
  std::size_t enviados = 0;
  while (enviados < datos.size()) {
    const ssize_t n = ::send(fd, datos.data() + enviados, datos.size() - enviados, MSG_NOSIGNAL);
    if (n <= 0) return false;
    enviados += static_cast<std::size_t>(n);
  }
  return true;
}

// Lee hasta `n` bytes o hasta que pase `ms` sin recibir nada o la punta cierre.
inline std::string leer_hasta(int fd, std::size_t n, int ms) {
  std::string datos;
  char buffer[8192];
  while (datos.size() < n) {
    pollfd espera{fd, POLLIN, 0};
    if (::poll(&espera, 1, ms) <= 0) break;
    const ssize_t leidos = ::recv(fd, buffer, std::min(sizeof buffer, n - datos.size()), 0);
    if (leidos <= 0) break;
    datos.append(buffer, static_cast<std::size_t>(leidos));
  }
  return datos;
}

// true si la punta cierra (EOF o RST) antes de `ms`; lo que llegue antes se descarta.
inline bool espera_cierre(int fd, int ms) {
  char buffer[4096];
  const auto limite = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  for (;;) {
    const auto restante = std::chrono::duration_cast<std::chrono::milliseconds>(
                              limite - std::chrono::steady_clock::now())
                              .count();
    if (restante <= 0) return false;
    pollfd espera{fd, POLLIN, 0};
    if (::poll(&espera, 1, static_cast<int>(restante)) <= 0) return false;
    const ssize_t leidos = ::recv(fd, buffer, sizeof buffer, 0);
    if (leidos == 0) return true;
    if (leidos < 0) return errno != EAGAIN && errno != EINTR;
  }
}

// Espera a que `condicion` se cumpla (hasta `ms`).
inline bool esperar_hasta(const std::function<bool()>& condicion, int ms) {
  const auto limite = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < limite) {
    if (condicion()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return condicion();
}

// Recoge las lineas de registro mientras vive.
class CapturaRegistro {
 public:
  CapturaRegistro() {
    puente_aula::registro::fijar_salida([this](const std::string& linea) {
      std::lock_guard<std::mutex> cerrojo(mutex_);
      lineas_.push_back(linea);
    });
  }
  ~CapturaRegistro() { puente_aula::registro::fijar_salida(nullptr); }
  std::vector<std::string> lineas() {
    std::lock_guard<std::mutex> cerrojo(mutex_);
    return lineas_;
  }
  std::string todo() {
    std::string texto;
    for (const auto& linea : lineas()) texto += linea + "\n";
    return texto;
  }

 private:
  std::mutex mutex_;
  std::vector<std::string> lineas_;
};

}  // namespace apoyo
