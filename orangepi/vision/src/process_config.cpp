// Analiza el archivo JSON de configuracion del proceso y lo mapea a ServiceConfig.
//
// El archivo NO es un secreto: las credenciales (contrasena del broker y token de la
// galeria) se leen de las variables de entorno que indica el propio archivo. Aqui solo
// se leen nombres de variables, nunca valores de credenciales.
#include "vision/process_config.hpp"

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <sstream>

#include <nlohmann/json.hpp>

namespace vision {

namespace {

using nlohmann::json;

// La ruta de una clave, para los mensajes de error: "captura.rafaga".
std::string ruta(const char* seccion, const char* clave) {
  if (seccion == nullptr) return clave;
  return std::string(seccion) + "." + clave;
}

bool leer_string(const json& objeto, const char* seccion, const char* clave,
                 std::string& valor, std::string& error) {
  const auto it = objeto.find(clave);
  if (it == objeto.end()) return true;
  if (!it->is_string()) {
    error = ruta(seccion, clave) + ": se esperaba un texto";
    return false;
  }
  valor = it->get<std::string>();
  return true;
}

bool leer_entero(const json& objeto, const char* seccion, const char* clave, int& valor,
                 std::string& error) {
  const auto it = objeto.find(clave);
  if (it == objeto.end()) return true;
  if (!it->is_number_integer()) {
    error = ruta(seccion, clave) + ": se esperaba un entero";
    return false;
  }
  const long long numero = it->get<long long>();
  if (numero < std::numeric_limits<int>::min() || numero > std::numeric_limits<int>::max()) {
    error = ruta(seccion, clave) + ": el entero esta fuera de rango";
    return false;
  }
  valor = static_cast<int>(numero);
  return true;
}

bool leer_tamano(const json& objeto, const char* seccion, const char* clave,
                 std::size_t& valor, std::string& error) {
  const auto it = objeto.find(clave);
  if (it == objeto.end()) return true;
  if (!it->is_number_integer()) {
    error = ruta(seccion, clave) + ": se esperaba un entero";
    return false;
  }
  const long long numero = it->get<long long>();
  if (numero < 0) {
    error = ruta(seccion, clave) + ": no puede ser negativo";
    return false;
  }
  valor = static_cast<std::size_t>(numero);
  return true;
}

bool leer_real(const json& objeto, const char* seccion, const char* clave, double& valor,
               std::string& error) {
  const auto it = objeto.find(clave);
  if (it == objeto.end()) return true;
  if (!it->is_number()) {
    error = ruta(seccion, clave) + ": se esperaba un numero";
    return false;
  }
  valor = it->get<double>();
  return true;
}

bool leer_logico(const json& objeto, const char* seccion, const char* clave, bool& valor,
                 std::string& error) {
  const auto it = objeto.find(clave);
  if (it == objeto.end()) return true;
  if (!it->is_boolean()) {
    error = ruta(seccion, clave) + ": se esperaba true o false";
    return false;
  }
  valor = it->get<bool>();
  return true;
}

// Una clave que no existe es un error: una errata ("rafagaa") pasaria desapercibida y
// el servicio arrancaria con los valores por omision sin avisar.
bool solo_claves(const json& objeto, std::initializer_list<const char*> permitidas,
                 const char* seccion, std::string& error) {
  for (const auto& [clave, valor] : objeto.items()) {
    (void)valor;
    bool conocida = false;
    for (const char* permitida : permitidas) {
      if (clave == permitida) {
        conocida = true;
        break;
      }
    }
    if (!conocida) {
      error = ruta(seccion, clave.c_str()) + ": clave desconocida";
      return false;
    }
  }
  return true;
}

// Devuelve el objeto de una seccion; si la seccion no esta, devuelve un objeto vacio.
bool seccion(const json& documento, const char* nombre, json& out, std::string& error) {
  const auto it = documento.find(nombre);
  if (it == documento.end()) {
    out = json::object();
    return true;
  }
  if (!it->is_object()) {
    error = std::string(nombre) + ": se esperaba un objeto";
    return false;
  }
  out = *it;
  return true;
}

bool exigir(const bool condicion, const char* seccion, const char* clave, const char* texto,
            std::string& error) {
  if (condicion) return true;
  error = ruta(seccion, clave) + ": " + texto;
  return false;
}

// La URL de la galeria, partida en esquema y host.
struct UrlPartes {
  std::string esquema;
  std::string host;
};

bool partir_url(const std::string& url, UrlPartes& out) {
  const std::size_t fin_esquema = url.find("://");
  if (fin_esquema == std::string::npos) return false;
  out.esquema = url.substr(0, fin_esquema);
  const std::size_t inicio_host = fin_esquema + 3;
  std::size_t fin_host = url.find('/', inicio_host);
  if (fin_host == std::string::npos) fin_host = url.size();
  std::string host = url.substr(inicio_host, fin_host - inicio_host);
  const std::size_t dos_puntos = host.rfind(':');
  if (dos_puntos != std::string::npos && host.find(']') == std::string::npos) {
    host.erase(dos_puntos);  // el puerto no forma parte del host
  }
  out.host = host;
  return true;
}

}  // namespace

ServiceConfig ProcessConfig::to_service_config() const {
  ServiceConfig config;
  config.capture_url = capture.url;
  config.burst_size = capture.burst_size;
  config.settle_time = Millis(capture.settle_ms);
  config.min_sharpness = thresholds.min_sharpness;
  config.min_recognized_score = thresholds.min_recognized_score;
  config.min_dudoso_score = thresholds.min_dudoso_score;
  config.http_timeout = Millis(capture.http_timeout_ms);
  config.max_body_bytes = capture.max_body_bytes;
  config.max_inbox_events = service.max_inbox_events;
  config.accept_png = service.accept_png;
  config.model_name = service.model_name;
  return config;
}

bool parse_process_config(const std::string& document, ProcessConfig& out, std::string& error) {
  json parsed;
  try {
    parsed = json::parse(document);
  } catch (const json::exception&) {
    error = "el documento no es JSON valido";
    return false;
  }
  if (!parsed.is_object()) {
    error = "el documento deberia ser un objeto";
    return false;
  }

  ProcessConfig config;
  error.clear();

  if (!solo_claves(parsed, {"mqtt", "captura", "galeria", "umbrales", "servicio", "http"},
                   nullptr, error)) {
    return false;
  }

  json bloque;
  if (!seccion(parsed, "mqtt", bloque, error)) return false;
  if (!solo_claves(bloque, {"host", "port", "usuario", "clave_env", "client_id"}, "mqtt", error)) {
    return false;
  }
  if (!leer_string(bloque, "mqtt", "host", config.mqtt.host, error)) return false;
  if (!leer_entero(bloque, "mqtt", "port", config.mqtt.port, error)) return false;
  if (!leer_string(bloque, "mqtt", "usuario", config.mqtt.user, error)) return false;
  if (!leer_string(bloque, "mqtt", "clave_env", config.mqtt.password_env, error)) return false;
  if (!leer_string(bloque, "mqtt", "client_id", config.mqtt.client_id, error)) return false;
  if (!exigir(config.mqtt.port >= 1 && config.mqtt.port <= 65535, "mqtt", "port",
              "el puerto debe estar entre 1 y 65535", error)) {
    return false;
  }
  if (!exigir(!config.mqtt.client_id.empty(), "mqtt", "client_id", "no puede estar vacio",
              error)) {
    return false;
  }

  if (!seccion(parsed, "captura", bloque, error)) return false;
  if (!solo_claves(bloque, {"url", "rafaga", "espera_luz_ms", "http_timeout_ms",
                            "max_body_bytes"},
                   "captura", error)) {
    return false;
  }
  if (!leer_string(bloque, "captura", "url", config.capture.url, error)) return false;
  if (!leer_entero(bloque, "captura", "rafaga", config.capture.burst_size, error)) return false;
  if (!leer_entero(bloque, "captura", "espera_luz_ms", config.capture.settle_ms, error)) {
    return false;
  }
  if (!leer_entero(bloque, "captura", "http_timeout_ms", config.capture.http_timeout_ms,
                    error)) {
    return false;
  }
  if (!leer_tamano(bloque, "captura", "max_body_bytes", config.capture.max_body_bytes, error)) {
    return false;
  }
  if (!exigir(!config.capture.url.empty(), "captura", "url", "no puede estar vacia", error)) {
    return false;
  }
  if (!exigir(config.capture.burst_size >= 1, "captura", "rafaga", "debe ser >= 1", error)) {
    return false;
  }
  if (!exigir(config.capture.settle_ms >= 0, "captura", "espera_luz_ms",
              "no puede ser negativa", error)) {
    return false;
  }
  if (!exigir(config.capture.http_timeout_ms > 0, "captura", "http_timeout_ms",
              "debe ser > 0", error)) {
    return false;
  }
  if (!exigir(config.capture.max_body_bytes > 0, "captura", "max_body_bytes", "debe ser > 0",
              error)) {
    return false;
  }

  if (!seccion(parsed, "galeria", bloque, error)) return false;
  if (!solo_claves(bloque, {"url", "token_env", "timeout_ms"}, "galeria", error)) return false;
  if (!leer_string(bloque, "galeria", "url", config.gallery.url, error)) return false;
  if (!leer_string(bloque, "galeria", "token_env", config.gallery.token_env, error)) return false;
  if (!leer_entero(bloque, "galeria", "timeout_ms", config.gallery.timeout_ms, error)) {
    return false;
  }
  if (!exigir(!config.gallery.url.empty(), "galeria", "url", "no puede estar vacia", error)) {
    return false;
  }
  if (!exigir(config.gallery.timeout_ms > 0, "galeria", "timeout_ms", "debe ser > 0", error)) {
    return false;
  }
  // Con token configurado, la galeria tiene que ir por https (o por http contra el
  // propio localhost): el token no puede viajar en claro.
  if (!config.gallery.token_env.empty() && !url_admite_token(config.gallery.url)) {
    error = ruta("galeria", "url") +
            ": con token la galeria necesita https (o http contra 127.0.0.1/localhost): "
            "el token no puede viajar en claro";
    return false;
  }

  if (!seccion(parsed, "umbrales", bloque, error)) return false;
  if (!solo_claves(bloque, {"min_sharpness", "min_recognized_score", "min_dudoso_score"},
                   "umbrales", error)) {
    return false;
  }
  if (!leer_real(bloque, "umbrales", "min_sharpness", config.thresholds.min_sharpness, error)) {
    return false;
  }
  double reconocido = config.thresholds.min_recognized_score;
  double dudoso = config.thresholds.min_dudoso_score;
  if (!leer_real(bloque, "umbrales", "min_recognized_score", reconocido, error)) return false;
  if (!leer_real(bloque, "umbrales", "min_dudoso_score", dudoso, error)) return false;
  config.thresholds.min_recognized_score = static_cast<float>(reconocido);
  config.thresholds.min_dudoso_score = static_cast<float>(dudoso);
  if (!exigir(config.thresholds.min_sharpness >= 0.0, "umbrales", "min_sharpness",
              "no puede ser negativo", error)) {
    return false;
  }
  if (!exigir(dudoso >= 0.0 && reconocido <= 1.0 && dudoso <= reconocido, "umbrales",
              "min_recognized_score",
              "los umbrales deben cumplir 0 <= min_dudoso_score <= min_recognized_score <= 1",
              error)) {
    return false;
  }

  if (!seccion(parsed, "servicio", bloque, error)) return false;
  if (!solo_claves(bloque, {"poll_ms", "max_inbox_events", "modelo", "acepta_png"}, "servicio",
                   error)) {
    return false;
  }
  if (!leer_entero(bloque, "servicio", "poll_ms", config.service.poll_ms, error)) return false;
  if (!leer_tamano(bloque, "servicio", "max_inbox_events", config.service.max_inbox_events,
                   error)) {
    return false;
  }
  if (!leer_string(bloque, "servicio", "modelo", config.service.model_name, error)) return false;
  if (!leer_logico(bloque, "servicio", "acepta_png", config.service.accept_png, error)) {
    return false;
  }
  if (!exigir(config.service.poll_ms >= 1, "servicio", "poll_ms", "debe ser >= 1", error)) {
    return false;
  }
  if (!exigir(config.service.max_inbox_events >= 4, "servicio", "max_inbox_events",
              "debe ser >= 4 (un PIR, un probar y un enrolar caben siempre)", error)) {
    return false;
  }
  if (!exigir(!config.service.model_name.empty(), "servicio", "modelo", "no puede estar vacio",
              error)) {
    return false;
  }

  if (!seccion(parsed, "http", bloque, error)) return false;
  if (!solo_claves(bloque, {"allow_invalid_certificates", "ca_file"}, "http", error)) {
    return false;
  }
  if (!leer_logico(bloque, "http", "allow_invalid_certificates",
                   config.http.allow_invalid_certificates, error)) {
    return false;
  }
  if (!leer_string(bloque, "http", "ca_file", config.http.ca_file, error)) return false;

  out = std::move(config);
  return true;
}

bool url_admite_token(const std::string& url) {
  UrlPartes partes;
  if (!partir_url(url, partes)) return false;
  if (partes.esquema == "https") return true;
  if (partes.esquema != "http") return false;
  return partes.host == "127.0.0.1" || partes.host == "localhost" || partes.host == "::1";
}

bool load_process_config(const std::string& path, ProcessConfig& out, bool& found,
                         std::string& error) {
  std::error_code code;
  if (!std::filesystem::exists(path, code)) {
    // El archivo no esta: se usan las omisiones y el proceso avisa por stderr.
    found = false;
    out = ProcessConfig{};
    error.clear();
    return true;
  }
  found = true;

  std::ifstream file(path);
  if (!file) {
    error = "no se pudo abrir " + path;
    return false;
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  if (file.bad()) {
    error = "no se pudo leer " + path;
    return false;
  }
  return parse_process_config(buffer.str(), out, error);
}

}  // namespace vision
