// Lógica de los actuadores de la XIAO: servo, LED RGB y buzzer.
// C++ puro, sin dependencias de Arduino: así se prueba en el host con g++
// (pruebas/test_logica_actuadores.cpp). El sketch (xiao.ino) hace la E/S:
// parsea el JSON, mueve el servo y enciende los pines con LEDC (actuadores.h)
// y publica el estado.
//
// Contrato: docs/contrato-mqtt.md, sección 3 (aula/actuador/cmd y
// aula/actuador/estado). Plan: docs/HIGH-LEVEL-PLAN.md, secciones 6.1 y 13.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace logica_actuadores {

// Qué pide un mensaje de aula/actuador/cmd respecto a cada actuador.
enum class PedidoServo {
  Ausente,   // el mensaje no trae "servo": no es para este módulo
  Invalido,  // trae "servo" pero no es "abrir": se ignora
  Abrir      // "servo": "abrir" (abre 90°, espera 3 s y se cierra solo)
};

enum class PedidoLed {
  Ausente,   // el mensaje no trae "led"
  Invalido,  // trae "led" pero no es uno de los cinco valores: se ignora
  Azul,
  Verde,
  Rojo,
  Ambar,
  Apagado
};

// SEMÁNTICA PROVISIONAL. El contrato (sección 3) deja un [decidir] abierto
// sobre el buzzer: "1" = pitido corto de éxito, "2" = doble pitido de error.
// Esa es la lectura que toma el plan (sección 6.1) y la que se implementa
// aquí: 0 = silencio, 1 = un pitido corto, 2 = dos pitidos. Si el contrato
// cambia, solo hay que tocar este enum y los tiempos de abajo.
enum class PedidoBuzzer {
  Ausente,   // el mensaje no trae "buzzer"
  Invalido,  // trae "buzzer" pero no es 0, 1 ni 2: se ignora
  Silencio,  // "buzzer": 0 (corta cualquier pitido en curso)
  Exito,     // "buzzer": 1 (un pitido corto)
  Error      // "buzzer": 2 (doble pitido)
};

// Estado del servo que se publica en el estado.
enum class EstadoServo {
  Cerrado,
  Abierto
};

// Color del LED RGB que se publica en el estado.
enum class ColorLed {
  Azul,
  Verde,
  Rojo,
  Ambar,
  Apagado
};

// Tiempo que el servo queda abierto antes de cerrarse solo (contrato: 3 s).
const uint32_t kDuracionServoAbiertoMs = 3000;

// Tiempo que se ve un color de retroalimentación (verde, rojo o ámbar) con la
// sesión activa, antes de que el LED vuelva al azul de la sesión.
const uint32_t kRetroSesionMs = 3000;

// Tiempos del buzzer (semántica provisional, ver PedidoBuzzer).
const uint32_t kPitidoBuzzerMs = 100;    // duración de cada pitido
const uint32_t kPausaBuzzerMs = 120;     // separación entre los dos pitidos

// Interpretación de los campos del cmd. `presente`: la clave existe y no es
// null. `esCadena` / `esEntero`: el valor es del tipo JSON esperado (no
// booleano, ni cadena, ni decimal). `valor`: el texto o el número.
//
// El servo solo acepta "abrir": no existe "cerrar" (el servo se cierra solo).
// Cualquier otro valor se ignora, sin cambiar nada.
PedidoServo interpretarServo(bool presente, bool esCadena, const char* valor);
PedidoLed interpretarLed(bool presente, bool esCadena, const char* valor);
PedidoBuzzer interpretarBuzzer(bool presente, bool esEntero, long valor);

// true si el pedido es una orden válida (no Ausente ni Invalido). Toda orden
// válida se responde republicando el estado, aunque no cambie nada.
bool esOrden(PedidoServo pedido);
bool esOrden(PedidoLed pedido);
bool esOrden(PedidoBuzzer pedido);

// Nombres del contrato, para el payload de aula/actuador/estado.
const char* nombreEstadoServo(EstadoServo estado);
const char* nombreColor(ColorLed color);

// Estado de los tres actuadores y sus temporizados, sin bloquear.
//
// Convención del repo (como en logica_luz): este módulo no llama a millis(),
// quien lo usa le pasa el tiempo. Los tiempos son los de millis() (uint32_t,
// con desbordamiento cada ~49 días): las restas son sin signo, así que el
// desbordamiento no rompe el temporizado del servo.
//
// Los métodos que devuelven bool indican si el estado cambió de verdad: solo
// entonces hay que escribir los pines y republicar el estado.
class ControlActuadores {
 public:
  // --- Servo ---------------------------------------------------------------

  // Aplica la orden del servo. "Abrir" abre el servo y reinicia la ventana de
  // kDuracionServoAbiertoMs: si ya estaba abierto, la ventana se alarga pero el
  // estado no cambia (devuelve false, no hay que republicar).
  bool aplicarServo(PedidoServo pedido, uint32_t ahora_ms);

  // Cierra el servo si lleva abierto kDuracionServoAbiertoMs o más. Devuelve
  // true si se acaba de cerrar (hay que escribir el pin y republicar).
  bool revisarServo(uint32_t ahora_ms);

  EstadoServo estadoServo() const;
  bool servoAbierto() const;

  // --- LED RGB -------------------------------------------------------------

  // Aplica el color de retroalimentación pedido. Devuelve true si el color
  // EFECTIVO cambia. Con la sesión activa, verde, rojo y ámbar se muestran
  // kRetroSesionMs y luego el LED vuelve al azul (revisarLed); un color nuevo
  // reinicia esa ventana. "azul" y "apagado" no destellan: con la sesión
  // activa el LED se queda azul (y cancelan una ventana en curso).
  bool aplicarLed(PedidoLed pedido, uint32_t ahora_ms);

  // Cierra la ventana de retroalimentación si lleva kRetroSesionMs o más.
  // Devuelve true si el color efectivo cambió (vuelve al azul, o se apaga si la
  // sesión ya terminó).
  bool revisarLed(uint32_t ahora_ms);

  // Aplica el estado de la sesión (aula/sesion/estado, tópico retenido). Con
  // la sesión activa el LED muestra azul (salvo la ventana de
  // retroalimentación); al terminar la sesión el LED se apaga y se olvida el
  // color recordado. Si termina con una ventana abierta, ese color se ve hasta
  // que venza (revisarLed) y entonces el LED se apaga. Devuelve true si el
  // color efectivo cambia.
  bool aplicarSesionActiva(bool activa);

  // Color que se está mostrando (el efectivo).
  ColorLed colorLed() const;

  // Último color de retroalimentación pedido por el cmd (el que se recuerda
  // mientras la sesión está activa; se olvida al terminar la sesión).
  ColorLed colorRetroalimentacion() const;

  bool sesionActiva() const;

  // --- Buzzer --------------------------------------------------------------

  // Aplica la orden del buzzer. Devuelve true si la salida del pin cambió
  // (hay que escribirlo). Un pitido pedido mientras ya suena reinicia el
  // patrón sin cambiar la salida.
  bool aplicarBuzzer(PedidoBuzzer pedido, uint32_t ahora_ms);

  // Avanza el patrón del buzzer. Devuelve true si la salida del pin cambió.
  bool revisarBuzzer(uint32_t ahora_ms);

  // Estado de la salida del buzzer (el pin está sonando o no).
  bool buzzerEncendido() const;

 private:
  enum class FaseBuzzer {
    Inactivo,  // no suena
    Sonando,   // pitido en curso
    Pausa      // separación entre los dos pitidos del error
  };

  bool servo_abierto_ = false;
  uint32_t servo_desde_ms_ = 0;
  ColorLed color_retro_ = ColorLed::Apagado;
  bool sesion_activa_ = false;
  bool retro_visible_ = false;  // ventana de retroalimentación abierta (solo con sesión)
  uint32_t retro_desde_ms_ = 0;
  FaseBuzzer fase_buzzer_ = FaseBuzzer::Inactivo;
  uint8_t pitidos_restantes_ = 0;
  uint32_t buzzer_desde_ms_ = 0;
  bool buzzer_salida_ = false;
};

// Escribe en `destino` el payload de aula/actuador/estado, p. ej.
// {"servo":"cerrado","led":"azul","luz":0}, con terminador nulo. Devuelve su
// longitud, o 0 si no cabe en `capacidad` (entonces deja el búfer vacío).
size_t construirEstado(char* destino, size_t capacidad, EstadoServo servo, ColorLed led,
                       bool luz_encendida);

// Lo mismo, tomando el estado del control.
size_t construirEstado(char* destino, size_t capacidad, const ControlActuadores& control,
                       bool luz_encendida);

}  // namespace logica_actuadores
