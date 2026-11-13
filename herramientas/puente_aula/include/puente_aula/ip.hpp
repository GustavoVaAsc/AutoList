#pragma once

// Direcciones IP como valores comparables, y la decision de la lista de permitidos.
//
// Todo lo que entra al puente se decide por la IP de origen del socket, asi que esta
// pieza es pura (sin red): se prueba sin abrir un solo socket. Una direccion IPv4 que
// llega como "::ffff:a.b.c.d" (socket IPv6 de pila doble) se normaliza a IPv4, para que
// la lista de permitidos se escriba una sola vez.

#include <sys/socket.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace puente_aula {

struct DireccionIp {
  bool v6 = false;
  // IPv4: los 4 primeros bytes (el resto en cero). IPv6: los 16.
  std::array<std::uint8_t, 16> bytes{};

  friend bool operator==(const DireccionIp&, const DireccionIp&) = default;
};

// Solo direcciones literales ("192.0.2.1", "::1"). No resuelve nombres, no admite zona
// ("fe80::1%eth0"), ni espacios, ni ceros a la izquierda. std::nullopt si no es valida.
std::optional<DireccionIp> parsear_ip(std::string_view texto);

std::string a_texto(const DireccionIp& ip);

// 0.0.0.0 y :: (escuchar en todas las interfaces; como destino, "esta maquina").
bool es_comodin(const DireccionIp& ip);

// true si `origen` esta en la lista. Una lista vacia no permite a nadie (falla cerrado).
bool ip_permitida(const DireccionIp& origen, const std::vector<DireccionIp>& permitidos);

// La IP de un sockaddr (AF_INET o AF_INET6); std::nullopt para cualquier otra familia.
std::optional<DireccionIp> ip_de_sockaddr(const sockaddr* direccion, socklen_t longitud);

// Rellena un sockaddr para bind() o connect(). Devuelve la longitud util.
socklen_t a_sockaddr(const DireccionIp& ip, int puerto, sockaddr_storage& salida);

}  // namespace puente_aula
