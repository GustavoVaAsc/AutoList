#pragma once

// Comprobaciones y ajustes del host que no son de un componente en particular.

#include <filesystem>
#include <string>
#include <vector>

namespace puente_aula {

// Si el kernel esta reenviando paquetes entre interfaces. Un equipo que enruta entre la LAN y
// ZeroTier salta las listas de permitidos (un host de la LAN podria llegar a ZeroTier y al
// reves sin pasar por el puente): el puente debe correr en un equipo que NO enrute.
struct EstadoReenvio {
  bool ipv4 = false;
  bool ipv6 = false;
};

// Lee /proc/sys/net/ipv4/ip_forward y /proc/sys/net/ipv6/conf/all/forwarding (las rutas son
// parametros para las pruebas). Un archivo ausente o ilegible cuenta como "no reenvia".
EstadoReenvio leer_reenvio(
    const std::filesystem::path& ruta_ipv4 = "/proc/sys/net/ipv4/ip_forward",
    const std::filesystem::path& ruta_ipv6 = "/proc/sys/net/ipv6/conf/all/forwarding");

// Los avisos (uno por familia que reenvia); vacio si ninguna.
std::vector<std::string> avisos_de_reenvio(const EstadoReenvio& estado);

// Ata el socket a una interfaz (SO_BINDTODEVICE). Sin privilegios desde Linux 5.7. false y
// `error` (con el motivo y el remedio) si falla. `interfaz` vacia no hace nada y da true.
bool atar_a_interfaz(int fd, const std::string& interfaz, std::string& error);

}  // namespace puente_aula
