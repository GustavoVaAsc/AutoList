#include "puente_aula/url_placa.hpp"

#include "puente_aula/ip.hpp"

namespace puente_aula {

// http://IP[:puerto] -> host y puerto. IPs literales, como en el puente.
bool analizar_url_placa(const std::string& url, std::string& host, int& puerto, std::string& error) {
  const std::string prefijo = "http://";
  if (url.compare(0, prefijo.size(), prefijo) != 0) {
    error = "la URL debe empezar con http://";
    return false;
  }
  std::string resto = url.substr(prefijo.size());
  if (!resto.empty() && resto.back() == '/') resto.pop_back();
  if (resto.empty() || resto.find_first_of("/?#@") != std::string::npos) {
    error = "la URL es solo http://IP[:puerto]: el camino es siempre /capture y la query la pone --res";
    return false;
  }
  puerto = 80;
  std::string texto_host = resto;
  if (resto.front() == '[') {  // [::1]:puerto
    const std::size_t cierre = resto.find(']');
    if (cierre == std::string::npos) {
      error = "IPv6 sin ']'";
      return false;
    }
    texto_host = resto.substr(1, cierre - 1);
    const std::string cola = resto.substr(cierre + 1);
    if (!cola.empty()) {
      if (cola.front() != ':') {
        error = "sobra texto tras la IPv6";
        return false;
      }
      resto = cola.substr(1);
      puerto = -1;
    }
  } else {
    const std::size_t dos_puntos = resto.rfind(':');
    if (dos_puntos != std::string::npos && resto.find(':') == dos_puntos) {
      texto_host = resto.substr(0, dos_puntos);
      resto = resto.substr(dos_puntos + 1);
      puerto = -1;
    }
  }
  if (puerto == -1) {
    if (resto.empty() || resto.size() > 5 || resto.find_first_not_of("0123456789") != std::string::npos) {
      error = "puerto invalido";
      return false;
    }
    puerto = std::stoi(resto);
    if (puerto < 1 || puerto > 65535) {
      error = "puerto fuera de rango";
      return false;
    }
  }
  const auto ip = parsear_ip(texto_host);
  if (!ip) {
    error = "el host debe ser una IP literal (no se resuelven nombres)";
    return false;
  }
  host = a_texto(*ip);
  return true;
}

}  // namespace puente_aula
