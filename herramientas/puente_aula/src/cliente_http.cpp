#include "puente_aula/cliente_http.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>

#include <httplib.h>

namespace puente_aula {

namespace {

using Reloj = std::chrono::steady_clock;

constexpr std::size_t kLongitudMaxValorCabecera = 256;

bool es_digito(char c) { return c >= '0' && c <= '9'; }
bool es_hex_minuscula(char c) { return es_digito(c) || (c >= 'a' && c <= 'f'); }

}  // namespace

bool valor_cabecera_valido(const std::string& valor) {
  if (valor.size() > kLongitudMaxValorCabecera) return false;
  for (const char caracter : valor) {
    const auto byte = static_cast<unsigned char>(caracter);
    if (byte < 0x20 || byte == 0x7f) return false;
  }
  return true;
}

bool cabecera_respuesta_valida(std::string_view nombre, std::string_view valor) {
  if (nombre == "Retry-After") {
    return !valor.empty() && valor.size() <= 6 && std::all_of(valor.begin(), valor.end(), es_digito);
  }
  if (nombre == "X-Captura-IV") {
    return valor.size() == 24 && std::all_of(valor.begin(), valor.end(), es_hex_minuscula);
  }
  return false;
}

std::string tipo_contenido_permitido(std::string_view content_type) {
  const std::size_t fin = content_type.find(';');
  std::string tipo(content_type.substr(0, fin));
  tipo.erase(std::remove_if(tipo.begin(), tipo.end(),
                            [](char c) { return c == ' ' || c == '\t'; }),
             tipo.end());
  std::transform(tipo.begin(), tipo.end(), tipo.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (tipo == "image/jpeg" || tipo == "text/plain" || tipo == "application/octet-stream") return tipo;
  return "application/octet-stream";
}

RespuestaCaptura pedir(const OpcionesCliente& opciones, const std::string& objetivo,
                       const Cabeceras& cabeceras) {
  RespuestaCaptura salida;
  const auto inicio = Reloj::now();
  const auto plazo = inicio + std::chrono::milliseconds(opciones.total_timeout_ms);

  httplib::Client cliente(opciones.host, opciones.puerto);
  cliente.set_connection_timeout(std::chrono::milliseconds(opciones.connect_timeout_ms));
  cliente.set_read_timeout(std::chrono::milliseconds(opciones.read_timeout_ms));
  cliente.set_write_timeout(std::chrono::milliseconds(opciones.read_timeout_ms));
  // El plazo total lo hace cumplir httplib (aborta lecturas lentas) y, ademas, el receptor
  // de abajo: asi tampoco se alarga con una placa que gotea bytes.
  cliente.set_max_timeout(std::chrono::milliseconds(opciones.total_timeout_ms));
  cliente.set_keep_alive(false);
  cliente.set_follow_location(false);
  cliente.set_path_encode(false);
  cliente.set_decompress(false);

  httplib::Headers enviar;
  for (const auto& [nombre, valor] : cabeceras) enviar.emplace(nombre, valor);
  enviar.emplace("Accept-Encoding", "identity");

  bool excedido = false;
  bool fuera_de_plazo = false;
  auto ultimo_avance = inicio;  // el ultimo momento en que llego algo de la placa
  const auto al_recibir_cabeceras = [&](const httplib::Response& respuesta) {
    ultimo_avance = Reloj::now();
    salida.estado = respuesta.status;
    const std::string tipo = respuesta.get_header_value("Content-Type");
    salida.content_type = tipo.empty() ? std::string() : tipo_contenido_permitido(tipo);
    for (const char* nombre : kCabecerasRespuestaConservadas) {
      if (respuesta.has_header(nombre)) {
        const std::string valor = respuesta.get_header_value(nombre);
        if (cabecera_respuesta_valida(nombre, valor)) {
          salida.cabeceras.emplace_back(nombre, valor);
        } else if (std::string_view(nombre) == "X-Captura-IV") {
          salida.iv_malformado = true;
        }
      }
    }
    const std::string largo = respuesta.get_header_value("Content-Length");
    if (!largo.empty()) {
      char* fin = nullptr;
      const unsigned long long declarado = std::strtoull(largo.c_str(), &fin, 10);
      if (fin != largo.c_str() && declarado > opciones.max_body_bytes) {
        excedido = true;
        return false;
      }
    }
    return true;
  };
  const auto recibir = [&](const char* datos, std::size_t longitud) {
    ultimo_avance = Reloj::now();
    if (Reloj::now() > plazo) {
      fuera_de_plazo = true;
      return false;
    }
    if (salida.cuerpo.size() + longitud > opciones.max_body_bytes) {
      excedido = true;
      return false;
    }
    salida.cuerpo.append(datos, longitud);
    return true;
  };

  const auto resultado = cliente.Get(objetivo, enviar, al_recibir_cabeceras, recibir);

  if (excedido || fuera_de_plazo) {
    salida.falla = excedido ? Falla::CuerpoExcedido : Falla::TiempoTotal;
    salida.cuerpo.clear();
    return salida;
  }
  if (!resultado) {
    salida.cuerpo.clear();
    switch (resultado.error()) {
      case httplib::Error::ConnectionTimeout:
        salida.falla = Falla::TiempoConexion;
        break;
      case httplib::Error::Timeout:
        // Con plazo total activo, httplib no distingue el total del de lectura: si ya paso el
        // plazo total, fue ese.
        salida.falla = Reloj::now() >= plazo ? Falla::TiempoTotal : Falla::TiempoLectura;
        break;
      case httplib::Error::Connection:
        salida.falla = Falla::Conexion;
        break;
      case httplib::Error::Read: {
        // httplib informa del tiempo de lectura vencido como "Read", igual que un cierre
        // brusco: si la placa lleva callada al menos el tiempo de lectura, fue el tiempo.
        const auto callada = Reloj::now() - ultimo_avance;
        if (Reloj::now() >= plazo) {
          salida.falla = Falla::TiempoTotal;
        } else {
          salida.falla = callada >= std::chrono::milliseconds(opciones.read_timeout_ms)
                             ? Falla::TiempoLectura
                             : Falla::Otra;
        }
        break;
      }
      default:
        salida.falla = Falla::Otra;
        break;
    }
    return salida;
  }
  salida.estado = resultado->status;
  return salida;
}

}  // namespace puente_aula
