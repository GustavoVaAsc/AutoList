// probar_captura: firma y manda una o varias peticiones GET /capture, para probar la placa en
// la LAN (o a traves del puente) sin el servicio de vision.
//
// El secreto sale SOLO de un archivo (--secreto) con permisos 600 y del usuario que ejecuta;
// no se acepta por argumento ni por entorno, y no se imprime nunca.
//
// Codigos de salida: ver include/puente_aula/salida_probar.hpp (0, 1, 2, 4, 5, 6, 78).
//
// El 200 de la placa va cifrado (AES-256-GCM, clave derivada del secreto, ver respuesta_cifrada.hpp):
// se descifra y se verifica la etiqueta, atada al ts y al nonce de ESTA peticion. Un 200 que no se
// descifra no es de fiar y no se guarda.
//
// --sin-firma prueba que la placa RECHAZA lo que no va firmado: un 401 es el resultado correcto
// (sale 0); un 200 es un fallo grave (ACEPTO SIN FIRMA, sale 6) y su cuerpo no se guarda.

#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <exception>
#include <iostream>
#include <string>

#include "puente_aula/cliente_http.hpp"
#include "puente_aula/firma_captura.hpp"
#include "puente_aula/jpeg.hpp"
#include "puente_aula/respuesta_cifrada.hpp"
#include "puente_aula/salida_probar.hpp"
#include "puente_aula/secreto.hpp"
#include "puente_aula/url_placa.hpp"

namespace {

using namespace puente_aula;

// Primer epoch (2026-01-01T00:00:00Z) que se acepta como hora real: el mismo piso que el servicio de
// vision (vision::kMinEpochPlausible). Un reloj anterior (sin NTP, de fabrica) no firma.
constexpr std::int64_t kMinEpochPlausible = 1767225600;
constexpr std::size_t kTopeCuerpo = 8 * 1024 * 1024;

void uso(std::ostream& salida) {
  salida << "uso: probar_captura --url http://<ip>[:puerto] --secreto <archivo> [opciones]\n"
            "  --url <url>        la placa (o el puente): http://IP[:puerto]. El camino es\n"
            "                     siempre /capture (es el que se firma)\n"
            "  --secreto <ruta>   archivo con el secreto (modo 600, del usuario). Obligatorio\n"
            "  --res <nombre>     resolucion, p. ej. SVGA (por omision SVGA)\n"
            "  --veces <n>        cuantas peticiones (por omision 1)\n"
            "  --reusar-nonce     todas usan el nonce de la primera: de la segunda en adelante\n"
            "                     la placa debe contestar 403 (pide --veces >= 2)\n"
            "  --sin-firma        no manda X-Captura-HMAC: la placa debe contestar 401\n"
            "  --salida <ruta>    guarda la JPEG (modo 600); con varias, <ruta>.<n>\n"
            "  --timeout-ms <n>   tiempo de conexion y de lectura (por omision 5000)\n";
}

bool res_valida(const std::string& res) {
  if (res.empty() || res.size() > 16) return false;
  for (const char c : res) {
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return false;
  }
  return true;
}

// Escribe la JPEG con modo 0600 (tambien si el archivo ya existia con otro modo).
bool guardar(const std::string& ruta, const std::string& cuerpo, std::string& error) {
  const int fd = ::open(ruta.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (fd < 0) {
    error = std::strerror(errno);
    return false;
  }
  bool bien = ::fchmod(fd, 0600) == 0;
  std::size_t escritos = 0;
  while (bien && escritos < cuerpo.size()) {
    const ssize_t n = ::write(fd, cuerpo.data() + escritos, cuerpo.size() - escritos);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) bien = false;
    else escritos += static_cast<std::size_t>(n);
  }
  if (!bien) error = std::strerror(errno);
  ::close(fd);
  return bien;
}

// Texto corto del cuerpo de un error de la placa ("ocupada", "temperatura"...): solo ASCII
// imprimible y recortado. Nunca el cuerpo de una foto.
std::string vista_previa(const std::string& cuerpo) {
  std::string vista;
  for (const char c : cuerpo) {
    if (vista.size() >= 80) break;
    vista += (c >= 0x20 && c < 0x7f) ? c : '?';
  }
  return vista;
}

}  // namespace

// Borra el secreto de la memoria al salir, por cualquier camino.
struct BorrarAlSalir {
  std::string& texto;
  ~BorrarAlSalir() { explicit_bzero(texto.data(), texto.size()); }
};

int main(int argc, char** argv) {
  prctl(PR_SET_DUMPABLE, 0);  // sin volcados ni /proc/<pid>/mem legible: la memoria lleva el secreto
  std::string url, ruta_secreto, res = "SVGA", salida;
  int veces = 1;
  int timeout_ms = 5000;
  bool reusar_nonce = false;
  bool sin_firma = false;

  for (int i = 1; i < argc; ++i) {
    const std::string argumento = argv[i];
    const auto valor = [&](std::string& destino) {
      if (i + 1 >= argc) return false;
      destino = argv[++i];
      return true;
    };
    std::string texto;
    if (argumento == "--help" || argumento == "-h") {
      uso(std::cout);
      return 0;
    } else if (argumento == "--url" && valor(url)) {
    } else if (argumento == "--secreto" && valor(ruta_secreto)) {
    } else if (argumento == "--res" && valor(res)) {
    } else if (argumento == "--salida" && valor(salida)) {
    } else if (argumento == "--veces" && valor(texto)) {
      if (texto.empty() || texto.size() > 4 || texto.find_first_not_of("0123456789") != std::string::npos ||
          std::stoi(texto) < 1) {
        std::cerr << "--veces: se esperaba un entero entre 1 y 9999\n";
        return kSalidaUso;
      }
      veces = std::stoi(texto);
    } else if (argumento == "--timeout-ms" && valor(texto)) {
      if (texto.empty() || texto.size() > 6 || texto.find_first_not_of("0123456789") != std::string::npos ||
          std::stoi(texto) < 1) {
        std::cerr << "--timeout-ms: se esperaba un entero positivo\n";
        return kSalidaUso;
      }
      timeout_ms = std::stoi(texto);
    } else if (argumento == "--reusar-nonce") {
      reusar_nonce = true;
    } else if (argumento == "--sin-firma") {
      sin_firma = true;
    } else {
      std::cerr << "argumento desconocido o sin valor: " << argumento << "\n";
      uso(std::cerr);
      return kSalidaUso;
    }
  }

  if (url.empty() || (ruta_secreto.empty() && !sin_firma)) {
    std::cerr << "faltan --url y --secreto\n";
    uso(std::cerr);
    return kSalidaUso;
  }
  if (!res_valida(res)) {
    std::cerr << "--res: solo letras y digitos, hasta 16\n";
    return kSalidaUso;
  }
  if (reusar_nonce && veces < 2) {
    std::cerr << "--reusar-nonce necesita --veces >= 2 (el repetido es el segundo)\n";
    return kSalidaUso;
  }
  if (reusar_nonce && sin_firma) {
    std::cerr << "--reusar-nonce y --sin-firma no se combinan\n";
    return kSalidaUso;
  }

  OpcionesCliente opciones;
  std::string error;
  if (!analizar_url_placa(url, opciones.host, opciones.puerto, error)) {
    std::cerr << "--url: " << error << "\n";
    return kSalidaUso;
  }
  opciones.connect_timeout_ms = timeout_ms;
  opciones.read_timeout_ms = timeout_ms;
  opciones.total_timeout_ms = timeout_ms;
  opciones.max_body_bytes = kTopeCuerpo;

  // El secreto se lee aunque vaya --sin-firma, si se dio: asi un archivo mal protegido se
  // detecta siempre. Sin firma y sin archivo, no hace falta.
  std::string secreto;
  const BorrarAlSalir borrar{secreto};
  if (!ruta_secreto.empty()) {
    try {
      secreto = leer_secreto(ruta_secreto);
    } catch (const ErrorSecreto& fallo) {
      std::cerr << "secreto: " << fallo.what() << "\n";
      return kSalidaConfig;
    }
  }

  const std::string objetivo = std::string(kCaminoFirmado) + "?res=" + res;
  std::optional<std::uint64_t> nonce_fijo;
  ResultadosPrueba resultados;

  for (int n = 1; n <= veces; ++n) {
    const std::int64_t ts = static_cast<std::int64_t>(std::time(nullptr));
    if (ts < kMinEpochPlausible) {
      std::cerr << "el reloj de este equipo esta antes de 2026-01-01: no se firma\n";
      return kSalidaConfig;
    }
    std::optional<std::uint64_t> nonce = (reusar_nonce && nonce_fijo) ? nonce_fijo : nonce_aleatorio();
    if (!nonce) {
      std::cerr << "el kernel no dio un nonce aleatorio\n";
      return kSalidaTransporte;
    }
    if (reusar_nonce && !nonce_fijo) nonce_fijo = nonce;

    const Cabeceras cabeceras = cabeceras_captura(secreto, ts, *nonce, !sin_firma);
    const auto inicio = std::chrono::steady_clock::now();
    const RespuestaCaptura respuesta = pedir(opciones, objetivo, cabeceras);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - inicio)
                        .count();

    std::printf("#%d ", n);
    if (respuesta.falla != Falla::Ninguna) {
      static const char* const nombres[] = {"",
                                            "sin_conexion",
                                            "tiempo_de_conexion",
                                            "tiempo_de_lectura",
                                            "tiempo_total",
                                            "cuerpo_demasiado_grande",
                                            "error"};
      std::printf("sin respuesta: %s ms=%lld\n", nombres[static_cast<int>(respuesta.falla)],
                  static_cast<long long>(ms));
      resultados.transporte_fallido = true;
      continue;
    }
    std::printf("estado=%d bytes=%zu ms=%lld", respuesta.estado, respuesta.cuerpo.size(),
                static_cast<long long>(ms));
    if (respuesta.estado != 200) {
      // Con --sin-firma el 401 es el resultado correcto; cualquier otro estado es inesperado.
      if (!estado_esperado(sin_firma, respuesta.estado)) resultados.no_200 = true;
      std::printf(" respuesta=\"%s\"", vista_previa(respuesta.cuerpo).c_str());
      for (const auto& [nombre, valor_cabecera] : respuesta.cabeceras) {
        if (nombre == "Retry-After") std::printf(" retry-after=%s", valor_cabecera.c_str());
      }
      std::printf("\n");
      continue;
    }
    // Con --sin-firma un 200 es un FALLO: la placa acepto una peticion sin firma. No se guarda nada.
    if (sin_firma) {
      std::printf("\n    ACEPTO SIN FIRMA: la placa contesto 200 a una peticion sin X-Captura-HMAC\n");
      resultados.acepto_sin_firma = true;
      continue;
    }
    // El 200 viene cifrado: se descifra aqui.
    RespuestaDescifrada descifrada = descifrar_respuesta(secreto, ts, *nonce, respuesta);
    const char* texto_resp = descifrada.estado == EstadoRespuesta::Ok        ? "ok"
                             : descifrada.estado == EstadoRespuesta::Ausente ? "ausente"
                                                                             : "invalida";
    const bool autentica = descifrada.estado == EstadoRespuesta::Ok;
    if (autentica) {
      const auto dimensiones =
          dimensiones_jpeg(reinterpret_cast<const std::uint8_t*>(descifrada.texto_plano.data()),
                           descifrada.texto_plano.size());
      if (dimensiones) {
        std::printf(" jpeg=%dx%d", dimensiones->ancho, dimensiones->alto);
      } else {
        std::printf(" jpeg=no_reconocida");
      }
    }
    std::printf(" resp=%s", texto_resp);
    std::printf("\n");
    if (!autentica) {
      resultados.no_autentica = true;
      std::printf("    la respuesta NO es autentica: no se guarda\n");
    } else if (!salida.empty()) {
      const std::string destino = veces == 1 ? salida : salida + "." + std::to_string(n);
      if (!guardar(destino, descifrada.texto_plano, error)) {
        std::cerr << destino << ": no se pudo guardar: " << error << "\n";
        explicit_bzero(descifrada.texto_plano.data(), descifrada.texto_plano.size());
        return kSalidaTransporte;
      }
      std::printf("    guardada en %s (modo 600)\n", destino.c_str());
    }
    explicit_bzero(descifrada.texto_plano.data(), descifrada.texto_plano.size());
  }

  return codigo_de_salida(resultados);
}
