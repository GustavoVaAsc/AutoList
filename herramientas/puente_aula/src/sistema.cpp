#include "puente_aula/sistema.hpp"

#include <sys/socket.h>

#include <cerrno>
#include <cstring>
#include <fstream>

namespace puente_aula {

namespace {

bool reenvia(const std::filesystem::path& ruta) {
  std::ifstream archivo(ruta);
  char caracter = '0';
  archivo.get(caracter);
  return archivo && caracter == '1';
}

}  // namespace

EstadoReenvio leer_reenvio(const std::filesystem::path& ruta_ipv4,
                           const std::filesystem::path& ruta_ipv6) {
  return {reenvia(ruta_ipv4), reenvia(ruta_ipv6)};
}

std::vector<std::string> avisos_de_reenvio(const EstadoReenvio& estado) {
  std::vector<std::string> avisos;
  const auto aviso = [&](const char* que, const char* como) {
    avisos.push_back(std::string("AVISO: ") + que +
                     "=1: este equipo ENRUTA entre interfaces. Un host de la LAN podria llegar a "
                     "ZeroTier (y al reves) sin pasar por el puente, y las listas de permitidos no "
                     "protegen nada. Desactivalo (" + como +
                     ") o bloquea el reenvio con el firewall (ver puente_aula.nft). Docker lo "
                     "activa por omision.");
  };
  if (estado.ipv4) aviso("net.ipv4.ip_forward", "sysctl -w net.ipv4.ip_forward=0");
  if (estado.ipv6) aviso("net.ipv6.conf.all.forwarding", "sysctl -w net.ipv6.conf.all.forwarding=0");
  return avisos;
}

bool atar_a_interfaz(int fd, const std::string& interfaz, std::string& error) {
  if (interfaz.empty()) return true;
  if (::setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, interfaz.c_str(),
                   static_cast<socklen_t>(interfaz.size())) == 0) {
    return true;
  }
  const int codigo = errno;
  error = "no se pudo atar el socket a la interfaz \"" + interfaz + "\": " + std::strerror(codigo);
  if (codigo == ENODEV) {
    error += " (esa interfaz no existe; revisa 'ip link' y escucha.interfaz)";
  } else if (codigo == EPERM || codigo == EACCES) {
    error += " (SO_BINDTODEVICE sin privilegios necesita Linux >= 5.7)";
  }
  return false;
}

}  // namespace puente_aula
