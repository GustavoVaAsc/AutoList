// Lógica de decisión de la tira LED de 12 V (campo "luz" de aula/actuador/cmd).
// C++ puro, sin dependencias de Arduino: así se prueba en el host con g++
// (pruebas/test_logica_luz.cpp). El sketch (xiao.ino) hace la E/S: parsea el JSON,
// escribe el pin y publica.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace logica_luz {

// Qué pide un mensaje de aula/actuador/cmd respecto a la luz.
enum class PedidoLuz {
  Ausente,   // el mensaje no trae "luz": no es para este módulo
  Invalido,  // trae "luz" pero no es el entero 0 ni el 1: se ignora
  Apagar,    // "luz": 0
  Encender   // "luz": 1
};

// Tamaño del búfer de PubSubClient (por defecto, 256 bytes). Incluye la cabecera
// y el tópico, así que el payload de un cmd tiene que ser menor.
const size_t kMaxCmdBytes = 256;

// true si el payload de un cmd cabe en el búfer; si no, se descarta antes de
// parsearlo.
bool cabeCmd(size_t longitud);

// Contrato, sección 1: se ignora todo mensaje con retain o con payload vacío en
// los tópicos con Retain = no. `posibleRetenido` es true cuando el mensaje pudo
// llegar como retenido (PubSubClient 2.8 no expone la bandera; ver xiao.ino).
bool debeDescartar(bool posibleRetenido, size_t longitud);

// `presente`: la clave "luz" existe y no es null. `esEntero`: su valor es un
// entero JSON (no booleano, ni cadena, ni decimal). Solo 0 y 1 son válidos.
PedidoLuz interpretarLuz(bool presente, bool esEntero, long valor);

// Tiempo máximo que la tira queda encendida sin que llegue otro luz:1. 360 s
// cubren con margen el vencimiento de 5 min del registro en Node-RED: la visión
// publica luz:1 una sola vez al iniciar el registro (5 poses) y luz:0 al terminar.
// Quien necesite la luz más tiempo debe reenviar luz:1 antes de que se cumpla. Si
// la visión agrega un reenvío periódico, este plazo se puede bajar.
const uint32_t kApagadoAutomaticoMs = 360000;

// true si el pedido es una orden válida de luz (Apagar o Encender), aunque no
// cambie el estado: toda orden válida se responde republicando el estado.
bool esOrden(PedidoLuz pedido);

// Estado de la luz y su apagado automático. Arranca apagada. Los tiempos son los
// de millis() (uint32_t, con desbordamiento cada ~49 días): las restas son sin signo.
class ControlLuz {
 public:
  explicit ControlLuz(uint32_t limite_ms = kApagadoAutomaticoMs) : limite_ms_(limite_ms) {}

  bool encendida() const { return encendida_; }

  // Aplica el pedido y devuelve true si el estado de la luz cambió (entonces hay
  // que escribir el pin y publicar el estado). Cada Encender, aunque la luz ya
  // esté encendida, reinicia el plazo del apagado automático. Ausente e Invalido
  // no cambian nada.
  bool aplicar(PedidoLuz pedido, uint32_t ahora_ms);

  // Apaga la luz si lleva encendida `limite_ms` o más desde el último Encender.
  // Devuelve true si la apagó (hay que escribir el pin y publicar el estado).
  bool revisarVencimiento(uint32_t ahora_ms);

 private:
  uint32_t limite_ms_;
  uint32_t encendida_desde_ms_ = 0;
  bool encendida_ = false;
};

// Escribe en `destino` el payload de aula/actuador/estado, p. ej. {"luz":1}, con
// terminador nulo. Devuelve su longitud, o 0 si no cabe en `capacidad`.
size_t construirEstado(char* destino, size_t capacidad, bool encendida);

}  // namespace logica_luz
