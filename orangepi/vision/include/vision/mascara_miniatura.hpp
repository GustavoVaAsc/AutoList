#pragma once

// Enmascara la "miniatura" de un JSON antes de imprimirlo. La miniatura (JPEG en base64,
// contrato MQTT v0.7 y v0.17) es una foto de una persona: las herramientas que muestran
// mensajes en la terminal (mqtt_probe) no deben sacarla. Solo cabecera y sin libmosquitto,
// para que las pruebas la usen sin el broker.
//
// Busca la clave LITERAL "miniatura" (la que emite la vision, que escribe JSON compacto con
// dump()); una clave escrita con escapes Unicode ("\u006diniatura") no se enmascara. No
// parsea el documento, asi que un mensaje roto o truncado tambien se enmascara.

#include <cstddef>
#include <string>

namespace vision {

namespace detail {
inline bool es_blanco_json(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
}  // namespace detail

// Reemplaza el valor de cada "miniatura": "<texto>" por "<N caracteres>", con N el largo del
// texto tal como viene (con sus escapes). Un valor que no es un string (null, un numero) se
// deja como esta; un string que no cierra se enmascara hasta el final; la palabra "miniatura"
// como valor de otra clave, o sin dos puntos detras, no se toca.
inline std::string enmascarar_miniatura(const std::string& cuerpo) {
  static const std::string clave = "\"miniatura\"";
  std::string salida;
  std::size_t desde = 0;
  while (true) {
    const std::size_t en = cuerpo.find(clave, desde);
    if (en == std::string::npos) break;
    std::size_t i = en + clave.size();
    while (i < cuerpo.size() && detail::es_blanco_json(cuerpo[i])) ++i;
    if (i >= cuerpo.size() || cuerpo[i] != ':') {
      salida.append(cuerpo, desde, i - desde);
      desde = i;
      continue;
    }
    ++i;
    while (i < cuerpo.size() && detail::es_blanco_json(cuerpo[i])) ++i;
    if (i >= cuerpo.size() || cuerpo[i] != '"') {
      salida.append(cuerpo, desde, i - desde);
      desde = i;
      continue;
    }
    const std::size_t inicio = i + 1;
    std::size_t fin = inicio;
    while (fin < cuerpo.size() && cuerpo[fin] != '"') fin += (cuerpo[fin] == '\\') ? 2 : 1;
    if (fin > cuerpo.size()) fin = cuerpo.size();
    salida.append(cuerpo, desde, inicio - desde);
    salida += "<" + std::to_string(fin - inicio) + " caracteres>";
    desde = fin;
  }
  salida.append(cuerpo, desde, std::string::npos);
  return salida;
}

}  // namespace vision
