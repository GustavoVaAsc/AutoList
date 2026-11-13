#include "vision/httplib_client.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <httplib.h>

#include "vision/captura_firma.hpp"

namespace vision {

namespace {

using SteadyClock = std::chrono::steady_clock;

// URL partida en esquema, host, puerto y camino.
struct ParsedUrl {
  bool valid = false;
  bool https = false;
  std::string host;
  int port = 80;
  std::string path = "/";
};

ParsedUrl parse_url(const std::string& url) {
  ParsedUrl parsed;
  const std::size_t scheme_end = url.find("://");
  if (scheme_end == std::string::npos) return parsed;
  const std::string scheme = url.substr(0, scheme_end);
  if (scheme == "http") {
    parsed.https = false;
    parsed.port = 80;
  } else if (scheme == "https") {
    parsed.https = true;
    parsed.port = 443;
  } else {
    return parsed;
  }

  const std::size_t authority_start = scheme_end + 3;
  std::size_t authority_end = url.find('/', authority_start);
  if (authority_end == std::string::npos) authority_end = url.size();
  std::string authority = url.substr(authority_start, authority_end - authority_start);
  if (authority.empty()) return parsed;

  const std::size_t colon = authority.rfind(':');
  if (colon != std::string::npos && authority.find(']') == std::string::npos) {
    const std::string port_text = authority.substr(colon + 1);
    if (!port_text.empty() &&
        port_text.find_first_not_of("0123456789") == std::string::npos) {
      parsed.port = std::atoi(port_text.c_str());
      authority.erase(colon);  // el host es lo que queda antes de los dos puntos
    }
  }
  if (authority.empty()) return parsed;
  parsed.host = authority;
  parsed.path = (authority_end < url.size()) ? url.substr(authority_end) : "/";
  if (parsed.path.empty()) parsed.path.assign(1, '/');
  parsed.valid = true;
  return parsed;
}

// Plazos por operacion de cpp-httplib, acotados por lo que queda del plazo total.
template <typename Client>
void apply_timeouts(Client& client, const HttpClientOptions& options, int remaining) {
  const int connect = std::max(1, std::min(options.connect_timeout_ms, remaining));
  const int read = std::max(1, std::min(options.read_timeout_ms, remaining));
  const int write = std::max(1, std::min(options.write_timeout_ms, remaining));
  client.set_connection_timeout(connect / 1000, (connect % 1000) * 1000);
  client.set_read_timeout(read / 1000, (read % 1000) * 1000);
  client.set_write_timeout(write / 1000, (write % 1000) * 1000);
}

// Cuanto queda del plazo total, en milisegundos (nunca negativo).
int remaining_ms(const SteadyClock::time_point& deadline) {
  const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - SteadyClock::now());
  return static_cast<int>(std::max<long long>(left.count(), 0));
}

// El cliente HTTP se construye en el llamador y se comparte con el hilo de la
// peticion. Al vencer el plazo, el llamador llama a stop(): cpp-httplib cierra el
// socket y el hilo termina solo (se espera con join). Antes el hilo se desacoplaba
// (detach) y se quedaba colgado para siempre con un servidor que gotea bytes, porque
// los plazos de cpp-httplib son por operacion: con rafaga 4 quedaban cuatro hilos
// colgados por intento, sin cota.
struct ClienteCompartido {
  std::shared_ptr<httplib::Client> http;
  std::shared_ptr<httplib::SSLClient> https;

  void stop() {
    if (http) http->stop();
    if (https) https->stop();
  }
};

// Peticion en un cliente concreto (http o https). Devuelve la respuesta; status 0
// significa que no hubo respuesta valida.
template <typename Client>
HttpResponse run_request(Client& client, const ParsedUrl& url,
                         const std::vector<std::string>& headers, std::size_t max_body_bytes,
                         const SteadyClock::time_point& deadline) {
  HttpResponse response;

  httplib::Headers request_headers;
  for (const std::string& header : headers) {
    const std::size_t colon = header.find(':');
    if (colon == std::string::npos) continue;
    const std::string name = header.substr(0, colon);
    const std::string value = header.substr(colon + 1);
    request_headers.emplace(name, value);
  }

  std::vector<std::uint8_t> body;
  bool rejected = false;
  int rejected_status = 0;
  int retry_after_s = 0;
  std::string resp_iv;

  // Las cabeceras llegan antes que el cuerpo: aqui se puede abortar sin leerlo.
  const auto response_handler = [&](const httplib::Response& received) -> bool {
    if (received.status != 200) {
      rejected = true;
      rejected_status = received.status;  // solo para el log del llamador
      // Retry-After en segundos enteros (hasta 5 cifras); cualquier otra forma se ignora.
      const std::string retry = received.get_header_value("Retry-After");
      if (!retry.empty() && retry.size() <= 5 &&
          retry.find_first_not_of("0123456789") == std::string::npos) {
        retry_after_s = std::atoi(retry.c_str());
      }
      return false;
    }
    resp_iv = received.get_header_value(kHeaderCapturaIv);
    const std::string content_length = received.get_header_value("Content-Length");
    if (!content_length.empty()) {
      const long long declared = std::atoll(content_length.c_str());
      if (declared > static_cast<long long>(max_body_bytes)) {
        rejected = true;
        return false;
      }
    }
    if (SteadyClock::now() >= deadline) {
      rejected = true;
      return false;
    }
    return true;
  };

  // El cuerpo llega por partes: en cuanto supera el maximo, se aborta la transferencia
  // sin acumular el resto.
  const auto content_receiver = [&](const char* data, std::size_t length) -> bool {
    if (SteadyClock::now() >= deadline) {
      rejected = true;
      return false;
    }
    if (body.size() + length > max_body_bytes) {
      rejected = true;
      return false;
    }
    body.insert(body.end(), data, data + length);
    return true;
  };

  const httplib::Result result =
      client.Get(url.path, request_headers, response_handler, content_receiver);
  if (!result) {
    if (!rejected) {
      std::cerr << "[vision] fallo la peticion HTTP: " << httplib::to_string(result.error())
                << '\n';
    }
    response.rejected_status = rejected_status;
    response.retry_after_s = retry_after_s;
    return response;  // status 0 y cuerpo vacio
  }
  if (rejected || result->status != 200) {
    response.rejected_status = result->status != 200 ? result->status : rejected_status;
    response.retry_after_s = retry_after_s;
    return response;
  }

  response.status = result->status;
  response.body = std::move(body);
  response.resp_iv = std::move(resp_iv);
  return response;
}

}  // namespace

HttplibClient::HttplibClient(HttpClientOptions options) : options_(options) {
  if (options_.allow_invalid_certificates) {
    // Que su activacion sea visible: no es el estado normal.
    std::cerr << "[vision] ATENCION: se aceptan certificados invalidos"
              << (options_.ca_file.empty()
                      ? ""
                      : " (el CA configurado no se usa: la validacion esta desactivada)")
              << '\n';
  }
}

HttpResponse HttplibClient::get(const std::string& url, Millis timeout,
                                 std::size_t max_body_bytes) {
  return get(url, timeout, max_body_bytes, {});
}

HttpResponse HttplibClient::get(const std::string& url, Millis timeout,
                                 std::size_t max_body_bytes,
                                 const std::vector<std::string>& headers) {
  // get NO lanza nunca: cualquier fallo (de red, de memoria o de la biblioteca) sale
  // como status 0 y cuerpo vacio.
  try {
    return get_impl(url, timeout, max_body_bytes, headers);
  } catch (const std::exception& error) {
    std::cerr << "[vision] fallo inesperado en la peticion HTTP: " << error.what() << '\n';
  } catch (...) {
    std::cerr << "[vision] fallo inesperado en la peticion HTTP\n";
  }
  return HttpResponse{};
}

HttpResponse HttplibClient::get_impl(const std::string& url, Millis timeout,
                                      std::size_t max_body_bytes,
                                      const std::vector<std::string>& headers) {
  HttpResponse response;
  const ParsedUrl parsed = parse_url(url);
  if (!parsed.valid) {
    std::cerr << "[vision] URL invalida: se pidio una pagina que no es http ni https\n";
    return response;
  }
  if (timeout.count() <= 0) {
    std::cerr << "[vision] el plazo de la peticion es cero o negativo\n";
    return response;
  }

  const SteadyClock::time_point deadline = SteadyClock::now() + timeout;
  const HttpClientOptions options = options_;

  // La peticion corre en un hilo aparte: el plazo total es del llamador, no de cada
  // lectura del socket. El cliente lo construye el llamador y lo comparte con el
  // hilo, para poder cerrar el socket (stop) cuando se pasa el plazo.
  auto compartido = std::make_shared<ClienteCompartido>();
  try {
    if (parsed.https) {
      compartido->https = std::make_shared<httplib::SSLClient>(parsed.host, parsed.port);
      httplib::SSLClient& client = *compartido->https;
      client.enable_server_certificate_verification(!options.allow_invalid_certificates);
      if (!options.ca_file.empty()) {
        // Se confía en el CA (o en el certificado autofirmado) concreto, no en
        // cualquier certificado.
        client.set_ca_cert_path(options.ca_file);
      }
      client.set_follow_location(false);
      apply_timeouts(client, options, remaining_ms(deadline));
    } else {
      compartido->http = std::make_shared<httplib::Client>(parsed.host, parsed.port);
      httplib::Client& client = *compartido->http;
      client.set_follow_location(false);
      apply_timeouts(client, options, remaining_ms(deadline));
    }
  } catch (const std::exception& error) {
    std::cerr << "[vision] no se pudo preparar la peticion HTTP: " << error.what() << '\n';
    return response;
  } catch (...) {
    std::cerr << "[vision] no se pudo preparar la peticion HTTP\n";
    return response;
  }

  auto promise = std::make_shared<std::promise<HttpResponse>>();
  std::future<HttpResponse> future = promise->get_future();
  std::thread worker;
  try {
    worker = std::thread([promise, compartido, parsed, max_body_bytes, headers, deadline]() {
    HttpResponse result;
    try {
      if (compartido->https) {
        result = run_request(*compartido->https, parsed, headers, max_body_bytes, deadline);
      } else {
        result = run_request(*compartido->http, parsed, headers, max_body_bytes, deadline);
      }
    } catch (const std::exception& error) {
      std::cerr << "[vision] excepcion en la peticion HTTP: " << error.what() << '\n';
      result = HttpResponse{};
    } catch (...) {
      std::cerr << "[vision] excepcion desconocida en la peticion HTTP\n";
      result = HttpResponse{};
    }
    try {
      promise->set_value(std::move(result));
    } catch (...) {
      // El llamador ya se fue (plazo vencido): no hay a quien avisarle.
    }
    });
  } catch (const std::exception& error) {
    std::cerr << "[vision] no se pudo arrancar el hilo de la peticion HTTP: " << error.what()
              << '\n';
    return response;
  }

  const int left = remaining_ms(deadline);
  if (left <= 0 || future.wait_for(std::chrono::milliseconds(left)) !=
                       std::future_status::ready) {
    std::cerr << "[vision] la peticion HTTP supero el plazo total (" << timeout.count()
              << " ms)\n";
    // Se cierra el socket: el hilo termina solo y se espera a que lo haga. No queda
    // ningun hilo colgado.
    compartido->stop();
    worker.join();
    return response;  // status 0 y cuerpo vacio
  }
  worker.join();
  return future.get();
}

}  // namespace vision
