#pragma once

// Reenviador TCP transparente para el MQTT de la placa: la XIAO (en la LAN) se conecta aqui
// y el puente abre una conexion igual hacia el Mosquitto de la Orange Pi (por ZeroTier).
//
// Es una bomba de bytes: NO interpreta MQTT. La autenticacion, la ACL y el LWT siguen siendo
// del broker, que ve los mismos bytes que veria sin el puente.
//
// Modelo de hilos: un hilo acepta; cada conexion tiene UN hilo que mueve los dos sentidos
// con poll() sobre los dos sockets (no hace falta un hilo por sentido). Con el tope de
// conexiones (2 por omision) hay como mucho 3 hilos: no se justifica un reactor ni un
// pool. Un hilo por conexion hace la bomba lineal y facil de razonar, y un hilo bloqueado
// en una conexion (p. ej. conectando a la Orange Pi) no frena a la otra ni al aceptador.
//
// Cierre: cuando una punta cierra o falla, se cierra la otra de inmediato, para que la XIAO
// note la caida y reconecte (apaga la tira al perder el enlace) y para que Mosquitto
// dispare el LWT. Lo que ya se habia leido de la punta que cerro se entrega antes de cerrar
// (con un tope de 2 s): un DISCONNECT seguido del cierre llega entero al broker.
// NO es transparente a medias-cierres TCP: un FIN del cliente termina la sesion (es la
// semantica de MQTT: el cliente que cierra su lado ya no espera nada). No se promete
// shutdown(SHUT_WR) hacia el broker ni seguir entregando respuestas tras un FIN.
//
// Tope de conexiones y reconexion: la XIAO es un unico cliente. Si llega una conexion de una IP
// permitida con el tope lleno, se expulsa la MAS ANTIGUA de esa misma IP (la sesion zombi que la
// placa ya abandono) y se acepta la nueva; de otra IP no se desaloja a nadie.
//
// Mientras el puente conecta con el broker (hasta connect_timeout_ms) vigila al cliente: si se va,
// abandona en vez de agotar el plazo.
//
// TCP keepalive (y TCP_USER_TIMEOUT con el mismo plazo) en las dos puntas: un camino de
// ZeroTier muerto o una placa que desaparecio sin cerrar se detectan y se propagan.

#include <cstddef>
#include <memory>

#include "puente_aula/config.hpp"

namespace puente_aula {

class ReenviadorTcp {
 public:
  explicit ReenviadorTcp(ConfigMqtt config);
  ~ReenviadorTcp();
  ReenviadorTcp(const ReenviadorTcp&) = delete;
  ReenviadorTcp& operator=(const ReenviadorTcp&) = delete;

  // Abre el puerto y empieza a aceptar. Devuelve el puerto real (util con puerto 0, en las
  // pruebas). Lanza std::runtime_error si no puede escuchar.
  int iniciar();

  // Deja de aceptar, cierra todas las conexiones y espera a sus hilos. Idempotente.
  void detener();

  // true si el hilo que acepta termino sin que nadie llamara a detener(). El proceso debe salir
  // con error para que systemd lo reinicie.
  bool termino_inesperadamente() const;

  // Conexiones en curso (para las pruebas).
  std::size_t conexiones_activas() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace puente_aula
