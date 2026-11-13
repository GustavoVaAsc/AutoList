#pragma once

// La configuracion del puente: un archivo JSON (con comentarios // y /* */ permitidos),
// validado de forma estricta al arrancar. Cualquier clave desconocida, tipo erroneo o valor
// fuera de rango es un error: una errata no debe dejar el puente corriendo con valores por
// omision que nadie eligio.
//
// El archivo NO lleva secretos: el puente reenvia las firmas sin conocer el secreto, y la
// contrasena MQTT la ve solo el broker. Se puede dejar legible.
//
// Las direcciones son IPs literales (nunca nombres): no hay resolucion DNS que pueda
// bloquear o apuntar a otro sitio.

#include <cstddef>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "puente_aula/ip.hpp"

namespace puente_aula {

// Configuracion invalida: el mensaje dice la clave y el motivo.
class ErrorConfig : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

struct Extremo {
  DireccionIp ip;
  int puerto = 0;
  // Solo en "escucha": la interfaz de red a la que se ata el socket (SO_BINDTODEVICE) antes de
  // enlazar, p. ej. "zt7nnjxxxx" o "wlan0". Vacio = sin atar. Cierra el puerto a lo que llegue
  // por otra interfaz aunque el equipo enrute.
  std::string interfaz;
};

// Valores por omision y limites (los usa el analizador y los documentan las pruebas).
inline constexpr std::size_t kMaxBodyBytesOmision = 2 * 1024 * 1024;
inline constexpr std::size_t kMaxBodyBytesLimite = 64 * 1024 * 1024;
inline constexpr int kConnectHttpMsOmision = 2000;
inline constexpr int kReadHttpMsOmision = 3000;
// El plazo total del relevo va por debajo del timeout del cliente (el servicio de vision usa
// 5000 ms): el relevo contesta 504 y suelta el cerrojo de la placa antes de que el cliente se
// rinda, y su siguiente peticion no encuentra el relevo todavia "ocupado".
inline constexpr int kTotalHttpMsOmision = 4000;
inline constexpr int kPuertoMqttOmision = 1883;
inline constexpr int kConnectMqttMsOmision = 3000;
inline constexpr std::size_t kMaxConexionesOmision = 2;
// Tope del tope: cada conexion es un hilo y 3 descriptores; con las expulsadas en vuelo (hasta el
// doble) el TasksMax y el LimitNOFILE de puente_aula.service estan calculados con este 8.
inline constexpr std::size_t kMaxConexionesLimite = 8;

// Relevo de GET /capture hacia la placa.
struct ConfigHttp {
  Extremo escucha;                          // la IP de ZeroTier de este equipo
  std::vector<DireccionIp> permitidos;      // la IP de ZeroTier de la Orange Pi
  Extremo upstream;                         // la IP de la placa en la LAN
  std::size_t max_body_bytes = kMaxBodyBytesOmision;
  int connect_timeout_ms = kConnectHttpMsOmision;
  int read_timeout_ms = kReadHttpMsOmision;   // sin recibir nada de la placa durante tanto
  int total_timeout_ms = kTotalHttpMsOmision; // tope de toda la llamada a la placa
};

// TCP keepalive de las dos puntas de cada conexion del reenviador.
struct Keepalive {
  int inactividad_s = 10;  // silencio antes del primer sondeo
  int intervalo_s = 5;     // entre sondeos
  int sondeos = 3;         // sondeos sin respuesta antes de dar la conexion por muerta
};

// Reenviador TCP del MQTT.
struct ConfigMqtt {
  Extremo escucha;                          // la IP de la LAN de este equipo
  std::vector<DireccionIp> permitidos;      // la IP de la placa en la LAN
  Extremo upstream;                         // la IP de ZeroTier de la Orange Pi
  std::size_t max_conexiones = kMaxConexionesOmision;
  int connect_timeout_ms = kConnectMqttMsOmision;
  Keepalive keepalive;
};

struct Config {
  // Permite escuchar en 0.0.0.0 o ::. Ver el nombre largo en el JSON.
  bool permitir_bind_comodin = false;
  std::optional<ConfigHttp> http;
  std::optional<ConfigMqtt> mqtt;
};

// Un aviso por cada escucha SIN interfaz (el limite documentado la exige). Se escriben al arrancar.
std::vector<std::string> avisos_de_escucha(const Config& config);

// Lanza ErrorConfig con un mensaje claro si algo no vale.
Config analizar_config(std::string_view texto);
Config cargar_config(const std::filesystem::path& ruta);

}  // namespace puente_aula
