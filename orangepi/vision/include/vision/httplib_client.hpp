#pragma once

// Adaptador de HttpClient sobre cpp-httplib.
//
// El plazo de `get` es el de la peticion ENTERA (conexion, cabeceras y cuerpo): un
// servidor que gotea bytes no puede alargar la peticion. cpp-httplib solo da plazos por
// operacion, asi que el adaptador acota el plazo de socket y, ademas, vigila el reloj
// desde el hilo de la peticion: en cuanto se pasa el plazo total, el llamador cierra el
// socket (stop) y espera a que el hilo termine (join). Nunca queda un hilo colgado.

#include <cstddef>
#include <string>
#include <vector>

#include "vision/http_client.hpp"

namespace vision {

struct HttpClientOptions {
  // El certificado de Node-RED es autofirmado: con true se acepta cualquier
  // certificado. RIESGO CONOCIDO: la comunicacion con Node-RED por HTTPS necesita
  // true hasta que haya un certificado valido (README, seccion "Limitaciones
  // conocidas"). Por omision es false: no se acepta ningun certificado invalido.
  bool allow_invalid_certificates = false;
  // Ruta a un CA (o al certificado autofirmado de Node-RED) con el que se valida el
  // servidor. Con esta ruta se puede confiar en el certificado CONCRETO en vez de
  // apagar la validacion: es la opcion recomendada frente a
  // allow_invalid_certificates. Vacia = sin CA adicional (se usan los del sistema).
  std::string ca_file;
  int connect_timeout_ms = 1000;
  int read_timeout_ms = 1000;
  int write_timeout_ms = 1000;
};

class HttplibClient final : public HttpClient {
 public:
  explicit HttplibClient(HttpClientOptions options = {});

  // Nunca lanza: cualquier fallo de red, un estado distinto de 200 o una excepcion de
  // la biblioteca devuelven status 0 y cuerpo vacio.
  HttpResponse get(const std::string& url, Millis timeout,
                   std::size_t max_body_bytes) override;

  // Igual, pero con cabeceras adicionales ("Nombre: valor"), como el token de la
  // galeria. No sigue redirecciones: una redireccion a otro host seria una fuga de la
  // peticion.
  HttpResponse get(const std::string& url, Millis timeout, std::size_t max_body_bytes,
                   const std::vector<std::string>& headers) override;

  [[nodiscard]] bool supports_request_headers() const noexcept override { return true; }

 private:
  // Cuerpo de get(): puede lanzar; get() lo envuelve y nunca deja escapar la excepcion.
  HttpResponse get_impl(const std::string& url, Millis timeout,
                         std::size_t max_body_bytes,
                         const std::vector<std::string>& headers);

  HttpClientOptions options_;
};

}  // namespace vision
