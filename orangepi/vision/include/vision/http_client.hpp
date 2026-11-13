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
  // Solo para el log: el codigo HTTP de una respuesta que la implementacion descarto por
  // no ser 200 (entonces `status` queda en 0 y el cuerpo vacio). 0 si no hubo respuesta
  // o si el fallo fue otro (red, plazo, tope del cuerpo). No cambia el contrato de
  // `status`: quien solo mira `status` sigue viendo 0 ante cualquier fallo.
  int rejected_status = 0;
  // Solo en un rechazo: el Retry-After de la respuesta, en segundos enteros. 0 si no vino
  // o no es un entero (la fecha HTTP no se interpreta). Lo usa el servicio para esperar
  // antes de repetir una captura que la placa rechazo con 429.
  int retry_after_s = 0;
  // Solo en un 200: el valor de X-Captura-IV (el IV del AES-GCM con que la placa cifra la
  // foto, vision/captura_firma.hpp). Vacio si la respuesta no lo traia.
  std::string resp_iv{};
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

  // Igual que get(), pero con cabeceras HTTP adicionales ("Nombre: valor"). La
  // implementacion por defecto las ignora: un cliente que no las admite no puede
  // enviar el token de la galeria (supports_request_headers() lo dice).
  virtual HttpResponse get(const std::string& url, Millis timeout, std::size_t max_body_bytes,
                           const std::vector<std::string>& headers) {
    (void)headers;
    return get(url, timeout, max_body_bytes);
  }

  // true si el cliente honor las cabeceras de la sobrecarga anterior.
  [[nodiscard]] virtual bool supports_request_headers() const noexcept { return false; }
};

}  // namespace vision
