#include "puente_aula/ip.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>

#include <algorithm>
#include <cstring>

namespace puente_aula {

namespace {

// "::ffff:a.b.c.d" -> a.b.c.d
DireccionIp normalizar(const DireccionIp& ip) {
  if (!ip.v6) return ip;
  for (int i = 0; i < 10; ++i) {
    if (ip.bytes[i] != 0) return ip;
  }
  if (ip.bytes[10] != 0xff || ip.bytes[11] != 0xff) return ip;
  DireccionIp v4;
  std::copy_n(ip.bytes.begin() + 12, 4, v4.bytes.begin());
  return v4;
}

}  // namespace

std::optional<DireccionIp> parsear_ip(std::string_view texto) {
  // inet_pton necesita un texto terminado en cero; un NUL incrustado lo truncaria.
  if (texto.empty() || texto.size() > 45 || texto.find('\0') != std::string_view::npos) {
    return std::nullopt;
  }
  const std::string copia(texto);
  DireccionIp ip;
  in_addr v4;
  if (inet_pton(AF_INET, copia.c_str(), &v4) == 1) {
    std::memcpy(ip.bytes.data(), &v4, 4);
    return ip;
  }
  in6_addr v6;
  if (inet_pton(AF_INET6, copia.c_str(), &v6) == 1) {
    ip.v6 = true;
    std::memcpy(ip.bytes.data(), &v6, 16);
    return normalizar(ip);
  }
  return std::nullopt;
}

std::string a_texto(const DireccionIp& ip) {
  char salida[INET6_ADDRSTRLEN] = {0};
  if (ip.v6) {
    in6_addr v6;
    std::memcpy(&v6, ip.bytes.data(), 16);
    inet_ntop(AF_INET6, &v6, salida, sizeof salida);
  } else {
    in_addr v4;
    std::memcpy(&v4, ip.bytes.data(), 4);
    inet_ntop(AF_INET, &v4, salida, sizeof salida);
  }
  return salida;
}

bool es_comodin(const DireccionIp& ip) {
  const std::size_t longitud = ip.v6 ? 16 : 4;
  return std::all_of(ip.bytes.begin(), ip.bytes.begin() + longitud,
                     [](std::uint8_t byte) { return byte == 0; });
}

bool ip_permitida(const DireccionIp& origen, const std::vector<DireccionIp>& permitidos) {
  return std::find(permitidos.begin(), permitidos.end(), origen) != permitidos.end();
}

std::optional<DireccionIp> ip_de_sockaddr(const sockaddr* direccion, socklen_t longitud) {
  if (direccion == nullptr) return std::nullopt;
  DireccionIp ip;
  if (direccion->sa_family == AF_INET && longitud >= sizeof(sockaddr_in)) {
    const auto* v4 = reinterpret_cast<const sockaddr_in*>(direccion);
    std::memcpy(ip.bytes.data(), &v4->sin_addr, 4);
    return ip;
  }
  if (direccion->sa_family == AF_INET6 && longitud >= sizeof(sockaddr_in6)) {
    const auto* v6 = reinterpret_cast<const sockaddr_in6*>(direccion);
    ip.v6 = true;
    std::memcpy(ip.bytes.data(), &v6->sin6_addr, 16);
    return normalizar(ip);
  }
  return std::nullopt;
}

socklen_t a_sockaddr(const DireccionIp& ip, int puerto, sockaddr_storage& salida) {
  std::memset(&salida, 0, sizeof salida);
  if (ip.v6) {
    auto* v6 = reinterpret_cast<sockaddr_in6*>(&salida);
    v6->sin6_family = AF_INET6;
    v6->sin6_port = htons(static_cast<std::uint16_t>(puerto));
    std::memcpy(&v6->sin6_addr, ip.bytes.data(), 16);
    return sizeof(sockaddr_in6);
  }
  auto* v4 = reinterpret_cast<sockaddr_in*>(&salida);
  v4->sin_family = AF_INET;
  v4->sin_port = htons(static_cast<std::uint16_t>(puerto));
  std::memcpy(&v4->sin_addr, ip.bytes.data(), 4);
  return sizeof(sockaddr_in);
}

}  // namespace puente_aula
