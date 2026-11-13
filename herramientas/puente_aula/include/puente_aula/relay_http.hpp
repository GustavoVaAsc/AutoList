#pragma once

// Relevo HTTP de GET /capture: la Orange Pi (por ZeroTier) pide la foto aqui y el puente la
// pide a la placa (en la LAN).
//
// Lo que hace, y solo eso:
//   - atiende unicamente a los origenes de la lista de permitidos; a los demas les contesta
//     403 en cuanto llega la cabecera de la peticion (sin leer el cuerpo, sin enrutar y sin
//     tocar la placa);
//   - reenvia solo GET /capture, con la query tal cual y las tres cabeceras X-Captura-*
//     sin tocar. Ninguna otra cabecera del cliente llega a la placa. Otro metodo o camino
//     se contesta aqui (405 / 404) y nunca se reenvia;
//   - devuelve el codigo, el Content-Type (application/octet-stream para la foto cifrada,
//     text/plain para los errores; todo lo demas se normaliza a octet-stream) y el cuerpo de
//     la placa, mas Retry-After y X-Captura-IV si los trae y cumplen su gramatica (ninguna
//     otra cabecera de la placa). Sin cache (Cache-Control: no-store) y sin escribir
//     nada a disco;
//   - una sola peticion a la placa a la vez (la placa atiende una captura a la vez): una
//     segunda recibe 503 al instante (con Retry-After: 1 y X-Puente: ocupado), no se encola;
//   - cuerpo mayor que el tope -> 502; placa caida -> 502; sin contestar a tiempo, o pasado el
//     plazo total (total_timeout_ms) -> 504.
//
// El puente no conoce el secreto: no firma ni verifica nada. Reenvia tal cual la foto
// CIFRADA y su IV (X-Captura-IV); la descifra y verifica quien tiene el secreto (el servicio de
// vision, probar_captura).
//
// Defensas antes de la decision por IP: se ata a una interfaz (opcional, SO_BINDTODEVICE), el
// servidor limita hilos, cola y tamano de cabeceras (ver CMakeLists: CPPHTTPLIB_HEADER_MAX_*), y
// el firewall (puente_aula.nft) deja pasar solo lo permitido.

#include <memory>

#include "puente_aula/config.hpp"

namespace puente_aula {

class RelayHttp {
 public:
  explicit RelayHttp(ConfigHttp config);
  ~RelayHttp();
  RelayHttp(const RelayHttp&) = delete;
  RelayHttp& operator=(const RelayHttp&) = delete;

  // Abre el puerto y empieza a atender en un hilo propio. Devuelve el puerto real (util con
  // puerto 0, en las pruebas). Lanza std::runtime_error si no puede escuchar.
  int iniciar();

  // Deja de aceptar y espera a que terminen las peticiones en curso. Idempotente.
  void detener();

  // true si el hilo de escucha termino sin que nadie llamara a detener() (p. ej. no se pudo crear
  // un hilo). El proceso debe salir con error para que systemd lo reinicie.
  bool termino_inesperadamente() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace puente_aula
