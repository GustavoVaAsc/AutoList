#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "vision/clock.hpp"

namespace vision {

struct HttpResponse {
  int status = 0;  // 0 = no hubo respuesta HTTP (error de red o de conexion)
  std::vector<std::uint8_t> body;
};

// La libreria HTTP aun no esta decidida; solo existe esta interfaz y un fake en memoria.
// El cuerpo viene de la red y no es de fiar: quien lo use debe acotarlo y validarlo.
class HttpClient {
 public:
  virtual ~HttpClient() = default;
  // `timeout` es el plazo de la peticion ENTERA (conexion, cabeceras y cuerpo), no de cada
  // lectura: un servidor que gotea bytes no puede alargarla. `max_body_bytes` acota el
  // cuerpo: en cuanto lo supera (por Content-Length o por lo recibido) la implementacion
  // debe abortar la transferencia, sin acumular el resto. En ambos casos, y ante cualquier
  // otro error de red, devuelve status 0; no lanza por fallos de red.
  virtual HttpResponse get(const std::string& url, Millis timeout, std::size_t max_body_bytes) = 0;
};

}  // namespace vision
