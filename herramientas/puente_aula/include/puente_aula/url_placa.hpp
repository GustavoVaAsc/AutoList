#pragma once

// La URL que se da a probar_captura: "http://IP[:puerto]" y nada mas. El camino es siempre
// /capture (es el que se firma) y la query la pone --res. IPs literales, como en el puente.

#include <string>

namespace puente_aula {

// true y rellena host (IP normalizada) y puerto; false y `error` si no vale.
bool analizar_url_placa(const std::string& url, std::string& host, int& puerto, std::string& error);

}  // namespace puente_aula
