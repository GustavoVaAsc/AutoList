#include "puente_aula/config.hpp"

#include <fstream>
#include <initializer_list>
#include <limits>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

namespace puente_aula {

namespace {

using nlohmann::json;

// El nombre es largo y feo a proposito: quien lo escribe sabe que abre el puerto en todas
// las interfaces (LAN incluida) en vez de solo en la IP de ZeroTier o de la LAN.
constexpr const char* kClaveComodin = "inseguro_permitir_escuchar_en_todas_las_interfaces";

[[noreturn]] void fallar(const std::string& ruta, const std::string& motivo) {
  throw ErrorConfig(ruta + ": " + motivo);
}

std::string ruta_de(const std::string& padre, const char* clave) {
  return padre.empty() ? std::string(clave) : padre + "." + clave;
}

// Un nombre de interfaz de Linux (IFNAMSIZ = 16 con el cero): letras, digitos y "_.:-".
std::string interfaz_de(const json& valor, const std::string& ruta) {
  if (!valor.is_string()) fallar(ruta, "se esperaba un texto con el nombre de una interfaz");
  const std::string nombre = valor.get<std::string>();
  if (nombre.empty() || nombre.size() > 15) fallar(ruta, "el nombre debe tener de 1 a 15 caracteres");
  for (const char c : nombre) {
    const bool bueno = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                       c == '_' || c == '.' || c == ':' || c == '-';
    if (!bueno) fallar(ruta, "el nombre de la interfaz solo admite letras, digitos y _ . : -");
  }
  return nombre;
}

// Una clave que no existe es un error (una errata no debe pasar callada).
void solo_claves(const json& objeto, std::initializer_list<const char*> permitidas,
                 const std::string& ruta) {
  for (const auto& [clave, valor] : objeto.items()) {
    (void)valor;
    bool conocida = false;
    for (const char* permitida : permitidas) {
      if (clave == permitida) conocida = true;
    }
    if (!conocida) fallar(ruta_de(ruta, clave.c_str()), "clave desconocida");
  }
}

const json& objeto_de(const json& padre, const std::string& ruta_padre, const char* clave,
                      bool requerido, const json*& hallado) {
  static const json vacio = json::object();
  const std::string ruta = ruta_de(ruta_padre, clave);
  const auto it = padre.find(clave);
  if (it == padre.end()) {
    if (requerido) fallar(ruta, "falta (obligatoria)");
    hallado = nullptr;
    return vacio;
  }
  if (!it->is_object()) fallar(ruta, "se esperaba un objeto");
  hallado = &*it;
  return *it;
}

// Un entero en [minimo, maximo]. Sin `omision` la clave es obligatoria.
long long entero(const json& objeto, const std::string& ruta_objeto, const char* clave,
                 std::optional<long long> omision, long long minimo, long long maximo) {
  const std::string ruta = ruta_de(ruta_objeto, clave);
  const auto it = objeto.find(clave);
  if (it == objeto.end()) {
    if (!omision) fallar(ruta, "falta (obligatoria)");
    return *omision;
  }
  // is_number_integer admite true/false? No: nlohmann los distingue (is_boolean).
  if (!it->is_number_integer()) fallar(ruta, "se esperaba un entero");
  if (it->is_number_unsigned() && it->get<unsigned long long>() >
                                      static_cast<unsigned long long>(
                                          std::numeric_limits<long long>::max())) {
    fallar(ruta, "fuera de rango");
  }
  const long long valor = it->get<long long>();
  if (valor < minimo || valor > maximo) {
    fallar(ruta, "debe estar entre " + std::to_string(minimo) + " y " + std::to_string(maximo));
  }
  return valor;
}

DireccionIp ip_de(const json& valor, const std::string& ruta, bool permitir_comodin,
                  const char* que) {
  if (!valor.is_string()) fallar(ruta, "se esperaba un texto con una direccion IP");
  const std::string texto = valor.get<std::string>();
  const auto ip = parsear_ip(texto);
  if (!ip) {
    fallar(ruta, std::string("no es una direccion IP literal valida (") + que +
                     "; no se resuelven nombres)");
  }
  if (!permitir_comodin && es_comodin(*ip)) {
    fallar(ruta, "no puede ser 0.0.0.0 ni ::");
  }
  return *ip;
}

DireccionIp ip_requerida(const json& objeto, const std::string& ruta_objeto, const char* clave,
                         bool permitir_comodin, const char* que) {
  const std::string ruta = ruta_de(ruta_objeto, clave);
  const auto it = objeto.find(clave);
  if (it == objeto.end()) fallar(ruta, "falta (obligatoria)");
  return ip_de(*it, ruta, permitir_comodin, que);
}

std::vector<DireccionIp> lista_permitidos(const json& objeto, const std::string& ruta_objeto) {
  const std::string ruta = ruta_de(ruta_objeto, "permitidos");
  const auto it = objeto.find("permitidos");
  if (it == objeto.end()) fallar(ruta, "falta (obligatoria)");
  if (!it->is_array()) fallar(ruta, "se esperaba una lista de IPs");
  if (it->empty()) fallar(ruta, "no puede estar vacia: sin permitidos el puente no acepta a nadie");
  std::vector<DireccionIp> permitidos;
  for (std::size_t i = 0; i < it->size(); ++i) {
    permitidos.push_back(ip_de((*it)[i], ruta + "[" + std::to_string(i) + "]", false,
                               "IP de origen permitida"));
  }
  return permitidos;
}

// {"ip": ..., "puerto": ...}. `puerto_omision` vacio hace obligatorio el puerto.
Extremo extremo(const json& padre, const std::string& ruta_padre, const char* clave,
                bool es_escucha, bool permitir_comodin, std::optional<int> puerto_omision,
                const char* que) {
  const json* hallado = nullptr;
  const std::string ruta = ruta_de(ruta_padre, clave);
  const json& objeto = objeto_de(padre, ruta_padre, clave, true, hallado);
  if (es_escucha) {
    solo_claves(objeto, {"ip", "puerto", "interfaz"}, ruta);
  } else {
    solo_claves(objeto, {"ip", "puerto"}, ruta);
  }
  Extremo salida;
  if (es_escucha && objeto.find("interfaz") != objeto.end()) {
    salida.interfaz = interfaz_de(objeto.at("interfaz"), ruta_de(ruta, "interfaz"));
  }
  salida.ip = ip_requerida(objeto, ruta, "ip", permitir_comodin, que);
  std::optional<long long> omision;
  if (puerto_omision) omision = *puerto_omision;
  salida.puerto = static_cast<int>(entero(objeto, ruta, "puerto", omision, 1, 65535));
  return salida;
}

std::string extremo_a_texto(const Extremo& e) {
  return a_texto(e.ip) + ":" + std::to_string(e.puerto);
}

ConfigHttp leer_http(const json& objeto, bool permitir_comodin) {
  const std::string ruta = "http";
  solo_claves(objeto, {"escucha", "permitidos", "upstream", "max_body_bytes",
                       "connect_timeout_ms", "read_timeout_ms", "total_timeout_ms"},
              ruta);
  ConfigHttp http;
  http.escucha = extremo(objeto, ruta, "escucha", true, permitir_comodin, std::nullopt,
                         "IP de ZeroTier de este equipo");
  http.permitidos = lista_permitidos(objeto, ruta);
  http.upstream =
      extremo(objeto, ruta, "upstream", false, false, 80, "IP de la placa en la LAN");
  http.max_body_bytes = static_cast<std::size_t>(
      entero(objeto, ruta, "max_body_bytes", static_cast<long long>(kMaxBodyBytesOmision), 1,
             static_cast<long long>(kMaxBodyBytesLimite)));
  http.connect_timeout_ms = static_cast<int>(
      entero(objeto, ruta, "connect_timeout_ms", kConnectHttpMsOmision, 1, 60000));
  http.read_timeout_ms =
      static_cast<int>(entero(objeto, ruta, "read_timeout_ms", kReadHttpMsOmision, 1, 120000));
  http.total_timeout_ms = static_cast<int>(
      entero(objeto, ruta, "total_timeout_ms", kTotalHttpMsOmision, 1, 120000));
  if (extremo_a_texto(http.escucha) == extremo_a_texto(http.upstream)) {
    fallar("http.upstream", "es la misma direccion en la que escucha el puente (un bucle)");
  }
  return http;
}

ConfigMqtt leer_mqtt(const json& objeto, bool permitir_comodin) {
  const std::string ruta = "mqtt";
  solo_claves(objeto, {"escucha", "permitidos", "upstream", "max_conexiones",
                       "connect_timeout_ms", "keepalive"},
              ruta);
  ConfigMqtt mqtt;
  mqtt.escucha = extremo(objeto, ruta, "escucha", true, permitir_comodin, kPuertoMqttOmision,
                         "IP de la LAN de este equipo");
  mqtt.permitidos = lista_permitidos(objeto, ruta);
  mqtt.upstream = extremo(objeto, ruta, "upstream", false, false, kPuertoMqttOmision,
                          "IP de ZeroTier de la Orange Pi");
  mqtt.max_conexiones = static_cast<std::size_t>(
      entero(objeto, ruta, "max_conexiones", static_cast<long long>(kMaxConexionesOmision), 1,
             static_cast<long long>(kMaxConexionesLimite)));
  mqtt.connect_timeout_ms = static_cast<int>(
      entero(objeto, ruta, "connect_timeout_ms", kConnectMqttMsOmision, 1, 60000));

  const json* hallado = nullptr;
  const json& ka = objeto_de(objeto, ruta, "keepalive", false, hallado);
  const std::string ruta_ka = ruta_de(ruta, "keepalive");
  solo_claves(ka, {"inactividad_s", "intervalo_s", "sondeos"}, ruta_ka);
  mqtt.keepalive.inactividad_s =
      static_cast<int>(entero(ka, ruta_ka, "inactividad_s", mqtt.keepalive.inactividad_s, 1, 3600));
  mqtt.keepalive.intervalo_s =
      static_cast<int>(entero(ka, ruta_ka, "intervalo_s", mqtt.keepalive.intervalo_s, 1, 600));
  mqtt.keepalive.sondeos =
      static_cast<int>(entero(ka, ruta_ka, "sondeos", mqtt.keepalive.sondeos, 1, 20));

  if (extremo_a_texto(mqtt.escucha) == extremo_a_texto(mqtt.upstream)) {
    fallar("mqtt.upstream", "es la misma direccion en la que escucha el puente (un bucle)");
  }
  return mqtt;
}

}  // namespace

Config analizar_config(std::string_view texto) {
  json raiz;
  try {
    // Comentarios permitidos: el archivo de ejemplo los usa para explicar cada IP.
    raiz = json::parse(texto, nullptr, true, true);
  } catch (const json::parse_error& error) {
    throw ErrorConfig(std::string("JSON invalido: ") + error.what());
  }
  if (!raiz.is_object()) fallar("(raiz)", "se esperaba un objeto JSON");
  solo_claves(raiz, {kClaveComodin, "http", "mqtt"}, "");

  Config config;
  const auto comodin = raiz.find(kClaveComodin);
  if (comodin != raiz.end()) {
    if (!comodin->is_boolean()) fallar(kClaveComodin, "se esperaba true o false");
    config.permitir_bind_comodin = comodin->get<bool>();
  }

  const json* seccion = nullptr;
  objeto_de(raiz, "", "http", false, seccion);
  if (seccion != nullptr) config.http = leer_http(*seccion, config.permitir_bind_comodin);
  objeto_de(raiz, "", "mqtt", false, seccion);
  if (seccion != nullptr) config.mqtt = leer_mqtt(*seccion, config.permitir_bind_comodin);

  if (!config.http && !config.mqtt) {
    fallar("(raiz)", "hace falta al menos una seccion: \"http\" o \"mqtt\"");
  }
  if (config.http && config.mqtt) {
    const auto igual = [](const Extremo& a, const Extremo& b) {
      return a.ip == b.ip && a.puerto == b.puerto;
    };
    if (igual(config.http->escucha, config.mqtt->escucha)) {
      fallar("mqtt.escucha", "es la misma direccion en la que escucha http");
    }
    // Un destino que es el puerto de escucha del otro componente encadenaria el puente consigo
    // mismo (y saltaria sus filtros).
    if (igual(config.http->upstream, config.mqtt->escucha)) {
      fallar("http.upstream", "es la direccion en la que escucha mqtt (un bucle)");
    }
    if (igual(config.mqtt->upstream, config.http->escucha)) {
      fallar("mqtt.upstream", "es la direccion en la que escucha http (un bucle)");
    }
  }
  return config;
}

Config cargar_config(const std::filesystem::path& ruta) {
  std::ifstream archivo(ruta, std::ios::binary);
  if (!archivo) throw ErrorConfig(ruta.string() + ": no se puede abrir el archivo de configuracion");
  std::ostringstream contenido;
  contenido << archivo.rdbuf();
  if (archivo.bad()) throw ErrorConfig(ruta.string() + ": error leyendo el archivo");
  try {
    return analizar_config(contenido.str());
  } catch (const ErrorConfig& error) {
    throw ErrorConfig(ruta.string() + ": " + error.what());
  }
}

}  // namespace puente_aula

namespace puente_aula {

std::vector<std::string> avisos_de_escucha(const Config& config) {
  std::vector<std::string> avisos;
  const auto aviso = [&](const char* seccion, const char* interfaz) {
    avisos.push_back(std::string("AVISO: ") + seccion +
                     ".escucha.interfaz no esta definida: el puerto queda abierto en cualquier "
                     "interfaz que tenga esa IP y la decision por IP de origen llega DESPUES de "
                     "aceptar la conexion. El limite documentado exige escucha.interfaz (" +
                     interfaz + ") y las reglas de puente_aula.nft.");
  };
  if (config.http && config.http->escucha.interfaz.empty()) aviso("http", "la de ZeroTier");
  if (config.mqtt && config.mqtt->escucha.interfaz.empty()) aviso("mqtt", "la de la LAN");
  return avisos;
}

}  // namespace puente_aula
