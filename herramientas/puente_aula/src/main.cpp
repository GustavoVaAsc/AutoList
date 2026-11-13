// puente_aula: el relevo entre la LAN del aula (donde esta la XIAO) y ZeroTier (donde esta la
// Orange Pi). Dos componentes en un proceso, cada uno opcional segun el archivo de
// configuracion: el relevo HTTP de GET /capture y el reenviador TCP del MQTT.
//
// Codigos de salida (los del servicio de vision):
//   0   salida ordenada por SIGTERM o SIGINT
//   1   fallo transitorio: no se pudo abrir un puerto (p. ej. la IP de ZeroTier aun no existe)
//   2   uso incorrecto de la linea de comandos
//   78  configuracion invalida (EX_CONFIG)
// La unidad systemd lleva RestartPreventExitStatus=2 78: ninguno de los dos se arregla solo
// reintentando; tras corregir la linea de comandos o el archivo hace falta un reinicio manual.
#include <pthread.h>
#include <signal.h>
#include <sys/prctl.h>

#include <cstring>
#include <ctime>
#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "puente_aula/config.hpp"
#include "puente_aula/registro.hpp"
#include "puente_aula/reenviador_tcp.hpp"
#include "puente_aula/relay_http.hpp"
#include "puente_aula/sistema.hpp"

namespace {

constexpr int kSalidaFalloTransitorio = 1;
constexpr int kSalidaUso = 2;
constexpr int kSalidaConfig = 78;

void uso(std::ostream& salida) {
  salida << "uso: puente_aula --config <archivo.conf>\n"
            "  --config <ruta>   archivo de configuracion JSON (ver puente_aula.conf.example)\n"
            "  --help            esta ayuda\n"
            "No necesita root. Se detiene con SIGTERM o SIGINT.\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::string ruta_config;
  for (int i = 1; i < argc; ++i) {
    const std::string argumento = argv[i];
    if (argumento == "--help" || argumento == "-h") {
      uso(std::cout);
      return 0;
    }
    if (argumento == "--config" && i + 1 < argc) {
      ruta_config = argv[++i];
      continue;
    }
    std::cerr << "[puente_aula] argumento desconocido o sin valor: " << argumento << "\n";
    uso(std::cerr);
    return kSalidaUso;
  }
  if (ruta_config.empty()) {
    std::cerr << "[puente_aula] falta --config\n";
    uso(std::cerr);
    return kSalidaUso;
  }

  // Sin volcados de memoria ni /proc/<pid>/mem legible por otros procesos del usuario: la
  // memoria del reenviador MQTT pasa por la contrasena del broker.
  prctl(PR_SET_DUMPABLE, 0);

  puente_aula::Config config;
  try {
    config = puente_aula::cargar_config(ruta_config);
  } catch (const std::exception& error) {  // ErrorConfig, pero tambien fallos de nlohmann o de memoria
    std::cerr << "[puente_aula] configuracion invalida: " << error.what() << "\n";
    return kSalidaConfig;
  }

  // Las senales se bloquean ANTES de crear hilos (los hilos heredan la mascara) y las recoge
  // el hilo principal con sigwait: sin manejadores asincronos. SIGPIPE se ignora (los
  // sockets usan MSG_NOSIGNAL, pero una libreria no debe poder matar el proceso).
  sigset_t senales;
  sigemptyset(&senales);
  sigaddset(&senales, SIGTERM);
  sigaddset(&senales, SIGINT);
  pthread_sigmask(SIG_BLOCK, &senales, nullptr);
  signal(SIGPIPE, SIG_IGN);

  if (config.permitir_bind_comodin) {
    puente_aula::registro::linea(
        "config", "AVISO: inseguro_permitir_escuchar_en_todas_las_interfaces esta activo");
  }

  // Un equipo que enruta entre la LAN y ZeroTier salta las listas de permitidos: se avisa fuerte.
  for (const std::string& aviso :
       puente_aula::avisos_de_reenvio(puente_aula::leer_reenvio())) {
    puente_aula::registro::linea("config", aviso);
  }

  for (const std::string& aviso : puente_aula::avisos_de_escucha(config)) {
    puente_aula::registro::linea("config", aviso);
  }

  std::unique_ptr<puente_aula::RelayHttp> http;
  std::unique_ptr<puente_aula::ReenviadorTcp> mqtt;
  try {
    if (config.http) {
      http = std::make_unique<puente_aula::RelayHttp>(*config.http);
      http->iniciar();
    }
    if (config.mqtt) {
      mqtt = std::make_unique<puente_aula::ReenviadorTcp>(*config.mqtt);
      mqtt->iniciar();
    }
  } catch (const std::exception& error) {
    std::cerr << "[puente_aula] no arranco: " << error.what() << "\n";
    return kSalidaFalloTransitorio;
  }

  // Se espera una senal con un tope de medio segundo y, entre una y otra, se comprueba que los
  // hilos que aceptan siguen vivos: si uno murio (p. ej. no se pudo crear un hilo), el proceso
  // sale con error y systemd lo reinicia, en vez de quedar "vivo" sin atender a nadie.
  int resultado = 0;
  for (;;) {
    timespec espera{0, 500 * 1000 * 1000};
    const int senal = sigtimedwait(&senales, nullptr, &espera);
    if (senal > 0) {
      puente_aula::registro::linea("puente", std::string("senal ") + strsignal(senal) +
                                                 ": deteniendo");
      break;
    }
    if ((http && http->termino_inesperadamente()) || (mqtt && mqtt->termino_inesperadamente())) {
      puente_aula::registro::linea("puente", "un componente dejo de atender: se sale con error");
      resultado = kSalidaFalloTransitorio;
      break;
    }
  }
  if (mqtt) mqtt->detener();
  if (http) http->detener();
  return resultado;
}
