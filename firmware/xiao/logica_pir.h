// Lógica del sensor PIR de la XIAO (nodo de campo, contrato MQTT sección 3).
// C++ puro, sin dependencias de Arduino: así se prueba en el host con g++
// (pruebas/test_logica_pir.cpp). El sketch (xiao.ino) hace la E/S: lee el pin
// del PIR, decide con este módulo y publica el evento.
//
// Este módulo no consume ningún tópico: solo produce el evento de movimiento.
// La obligación del contrato (sección 1) de descartar los mensajes con retain
// y los de payload vacío es de los consumidores, y en la XIAO la cumple el
// sketch (la ventana de retenidos) para los tópicos a los que se suscribe.
// Si más adelante este módulo llegara a interpretar un mensaje recibido, ese
// descarte debe seguir fuera del módulo: un payload retenido no puede
// confundirse con uno en vivo.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace logica_pir {

// Tópico del evento de movimiento (contrato, sección 3): QoS 0, sin retención.
const char kTopicoPir[] = "aula/camara/pir";

// Antirrebote entre eventos publicados. El contrato pide "al menos 2 s".
// Se fija en 2000 ms: un alumno que pasa por delante genera un solo flanco de
// subida útil, pero el PIR puede emitir varios cuando hay calor o una fuente
// de movimiento cercana; 2 s evita ráfagas de eventos y, a la vez, deja pasar
// dos movimientos seguidos de verdad (por ejemplo, dos alumnos que pasan uno
// tras otro a más de 2 s).
const uint32_t kAntirreboteMs = 2000;

// Tiempo de calentamiento del PIR HC-SR501: el datasheet pide del orden de un
// minuto antes de que la salida sea fiable. El primer disparo tras encenderlo
// es el calentamiento del sensor, no una persona en el aula, así que los
// flances que llegan dentro de esta ventana se ignoran.
const uint32_t kCalentamientoMs = 60000;

// Longitud del payload {"mov":1} sin el terminador nulo (9 bytes). El búfer de
// PubSubClient son 256 bytes: el payload cabe de sobra.
const size_t kMovimientoBytes = 9;

// true si la lectura del pin significa "movimiento detectado" (nivel alto del
// HC-SR501). digitalRead devuelve HIGH (1) o LOW (0).
bool esMovimiento(int lectura);

// Estado del PIR: detecta el flanco de subida, aplica el calentamiento y el
// antirrebote, y dice cuándo hay que publicar. Los tiempos son los de
// millis() (uint32_t, con desbordamiento cada ~49 días): las restas son sin
// signo, igual que en logica_luz.
//
// Lo que se pierde durante el calentamiento: si el PIR se dispara al powering
// y se queda en alto más allá del fin del calentamiento, ese movimiento no
// se avisa (no hay flanco de subida nuevo). El siguiente flanco de subida sí
// se publica.
class ControlPir {
 public:
  // `arranque_ms`: el instante en que la placa arrancó (millis() en setup()).
  // Desde ahí corre el calentamiento. `antirrebote_ms` y `calentamiento_ms`
  // tienen los valores del contrato y del sensor; se pueden cambiar en las
  // pruebas.
  explicit ControlPir(uint32_t arranque_ms = 0,
                       uint32_t antirrebote_ms = kAntirreboteMs,
                       uint32_t calentamiento_ms = kCalentamientoMs)
      : arranque_ms_(arranque_ms),
        antirrebote_ms_(antirrebote_ms),
        calentamiento_ms_(calentamiento_ms) {}

  // Registra el nivel actual del PIR y devuelve true si hay que publicar un
  // evento de movimiento, es decir, si este nivel es un flanco de subida que
  // pasa el calentamiento y el antirrebote. El flanco de bajada nunca
  // publica: el fin del movimiento no se avisa (no existe {"mov":0}).
  bool procesarNivel(bool movimiento, uint32_t ahora_ms);

  // true mientras la placa está en la ventana de calentamiento: los flances
  // de subida de esa ventana no se publican.
  bool enCalentamiento(uint32_t ahora_ms) const;

  // true si el evento quedaría dentro de la ventana de antirrebote del último
  // evento publicado, es decir, si ahora mismo un flanco se descartaría.
  bool dentroDeAntirrebote(uint32_t ahora_ms) const;

 private:
  uint32_t arranque_ms_;
  uint32_t antirrebote_ms_;
  uint32_t calentamiento_ms_;
  uint32_t ultimo_evento_ms_ = 0;
  bool nivel_alto_ = false;    // último nivel visto del PIR
  bool tiene_evento_ = false;  // si ya se publicó algún evento
};

// Escribe en `destino` el payload de aula/camara/pir, {"mov":1}, con
// terminador nulo. Devuelve su longitud, o 0 si no cabe en `capacidad`.
size_t construirMovimiento(char* destino, size_t capacidad);

}  // namespace logica_pir
