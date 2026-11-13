#pragma once

// Un GET a la placa (XIAO) con el cuerpo acotado, un plazo total y los fallos clasificados.
// Lo usan el relevo (reenvia lo que pide la Orange Pi) y probar_captura (firma y pide): asi
// la herramienta de prueba recorre el mismo camino de red que el servicio de vision.
//
// No sigue redirecciones, no recomprime (pide "identity" y no descomprime: el cuerpo es el que
// mando la placa, byte a byte), no reutiliza la conexion y no codifica de nuevo el camino (el
// objetivo se manda tal cual). La placa responde la foto con Transfer-Encoding: chunked y sin
// Content-Length: el tope se aplica a lo recibido, no solo a lo declarado.

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace puente_aula {

using Cabeceras = std::vector<std::pair<std::string, std::string>>;

struct OpcionesCliente {
  std::string host;  // IP literal
  int puerto = 80;
  int connect_timeout_ms = 2000;
  int read_timeout_ms = 3000;   // sin recibir nada durante tanto
  int total_timeout_ms = 4000;  // desde que empieza hasta el ultimo byte
  std::size_t max_body_bytes = 2 * 1024 * 1024;
};

enum class Falla {
  Ninguna,
  Conexion,         // rechazada, inalcanzable...
  TiempoConexion,   // la conexion no se establecio a tiempo
  TiempoLectura,    // la placa no contesto (o dejo de contestar) a tiempo
  TiempoTotal,      // la respuesta tardo mas del plazo total
  CuerpoExcedido,   // el cuerpo pasa del tope (por Content-Length o por lo recibido)
  Otra
};

// Las unicas cabeceras de RESPUESTA que se conservan (el relevo las reenvia):
//   Retry-After          la placa lo manda con 429 y el servicio de vision lo respeta
//   X-Captura-IV         el IV (12 bytes en 24 hex minuscula) con que la placa cifro la foto
//                        (AES-256-GCM; ver respuesta_cifrada.hpp)
// Cualquier otra cabecera de la placa se descarta, y las dos que se conservan solo si
// cumplen su gramatica (ver cabecera_respuesta_valida).
inline constexpr const char* kCabecerasRespuestaConservadas[] = {"Retry-After", "X-Captura-IV"};

struct RespuestaCaptura {
  Falla falla = Falla::Ninguna;
  int estado = 0;
  // Solo "application/octet-stream" (la foto cifrada), "image/jpeg" o "text/plain"; cualquier
  // otra cosa se normaliza a "application/octet-stream" (y vacio si la placa no mando
  // Content-Type).
  std::string content_type;
  std::string cuerpo;
  Cabeceras cabeceras;  // solo las conservadas y validas, con el valor tal cual
  // X-Captura-IV llego pero no cumple la gramatica (se descarto): para quien descifra, es una
  // respuesta invalida y no una sin cifrar.
  bool iv_malformado = false;
};

// `objetivo` es el camino y la query tal como van en la peticion ("/capture?res=SVGA").
RespuestaCaptura pedir(const OpcionesCliente& opciones, const std::string& objetivo,
                       const Cabeceras& cabeceras);

// Un valor de cabecera apto para reenviar: sin controles y de longitud acotada.
bool valor_cabecera_valido(const std::string& valor);

// La gramatica de las cabeceras de respuesta que se conservan:
//   Retry-After          ^[0-9]{1,6}$
//   X-Captura-IV         ^[0-9a-f]{24}$
bool cabecera_respuesta_valida(std::string_view nombre, std::string_view valor);

// "application/octet-stream", "image/jpeg" o "text/plain" (sin parametros, sin importar
// mayusculas) se conservan; todo lo demas es "application/octet-stream".
std::string tipo_contenido_permitido(std::string_view content_type);

}  // namespace puente_aula
