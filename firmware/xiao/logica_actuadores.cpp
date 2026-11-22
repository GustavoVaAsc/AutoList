#include "logica_actuadores.h"

#include <stdio.h>
#include <string.h>

namespace logica_actuadores {

PedidoServo interpretarServo(bool presente, bool esCadena, const char* valor) {
  if (!presente) return PedidoServo::Ausente;
  if (!esCadena || valor == nullptr) return PedidoServo::Invalido;
  if (strcmp(valor, "abrir") == 0) return PedidoServo::Abrir;
  return PedidoServo::Invalido;
}

PedidoLed interpretarLed(bool presente, bool esCadena, const char* valor) {
  if (!presente) return PedidoLed::Ausente;
  if (!esCadena || valor == nullptr) return PedidoLed::Invalido;
  if (strcmp(valor, "azul") == 0) return PedidoLed::Azul;
  if (strcmp(valor, "verde") == 0) return PedidoLed::Verde;
  if (strcmp(valor, "rojo") == 0) return PedidoLed::Rojo;
  if (strcmp(valor, "ambar") == 0) return PedidoLed::Ambar;
  if (strcmp(valor, "apagado") == 0) return PedidoLed::Apagado;
  return PedidoLed::Invalido;
}

PedidoBuzzer interpretarBuzzer(bool presente, bool esEntero, long valor) {
  if (!presente) return PedidoBuzzer::Ausente;
  if (!esEntero) return PedidoBuzzer::Invalido;
  if (valor == 0) return PedidoBuzzer::Silencio;
  if (valor == 1) return PedidoBuzzer::Exito;
  if (valor == 2) return PedidoBuzzer::Error;
  return PedidoBuzzer::Invalido;
}

bool esOrden(PedidoServo pedido) { return pedido == PedidoServo::Abrir; }

bool esOrden(PedidoLed pedido) {
  return pedido == PedidoLed::Azul || pedido == PedidoLed::Verde ||
         pedido == PedidoLed::Rojo || pedido == PedidoLed::Ambar ||
         pedido == PedidoLed::Apagado;
}

bool esOrden(PedidoBuzzer pedido) {
  return pedido == PedidoBuzzer::Silencio || pedido == PedidoBuzzer::Exito ||
         pedido == PedidoBuzzer::Error;
}

const char* nombreEstadoServo(EstadoServo estado) {
  return estado == EstadoServo::Abierto ? "abierto" : "cerrado";
}

const char* nombreColor(ColorLed color) {
  switch (color) {
    case ColorLed::Azul:
      return "azul";
    case ColorLed::Verde:
      return "verde";
    case ColorLed::Rojo:
      return "rojo";
    case ColorLed::Ambar:
      return "ambar";
    case ColorLed::Apagado:
      break;
  }
  return "apagado";
}

bool ControlActuadores::aplicarServo(PedidoServo pedido, uint32_t ahora_ms) {
  if (pedido != PedidoServo::Abrir) return false;
  servo_desde_ms_ = ahora_ms;  // cada "abrir" reinicia la ventana de 3 s
  if (servo_abierto_) return false;
  servo_abierto_ = true;
  return true;
}

bool ControlActuadores::revisarServo(uint32_t ahora_ms) {
  if (!servo_abierto_) return false;
  // Resta sin signo: correcta aunque millis() haya dado la vuelta.
  if (static_cast<uint32_t>(ahora_ms - servo_desde_ms_) < kDuracionServoAbiertoMs) {
    return false;
  }
  servo_abierto_ = false;
  return true;
}

EstadoServo ControlActuadores::estadoServo() const {
  return servo_abierto_ ? EstadoServo::Abierto : EstadoServo::Cerrado;
}

bool ControlActuadores::servoAbierto() const { return servo_abierto_; }

bool ControlActuadores::aplicarLed(PedidoLed pedido, uint32_t ahora_ms) {
  ColorLed nuevo;
  switch (pedido) {
    case PedidoLed::Azul:
      nuevo = ColorLed::Azul;
      break;
    case PedidoLed::Verde:
      nuevo = ColorLed::Verde;
      break;
    case PedidoLed::Rojo:
      nuevo = ColorLed::Rojo;
      break;
    case PedidoLed::Ambar:
      nuevo = ColorLed::Ambar;
      break;
    case PedidoLed::Apagado:
      nuevo = ColorLed::Apagado;
      break;
    default:  // Ausente e Invalido no cambian nada
      return false;
  }
  const ColorLed antes = colorLed();
  color_retro_ = nuevo;
  // Con la sesión activa solo verde, rojo y ámbar abren (o reinician) la
  // ventana; azul y apagado la cierran y el LED sigue azul.
  retro_visible_ = sesion_activa_ && (nuevo == ColorLed::Verde || nuevo == ColorLed::Rojo ||
                                      nuevo == ColorLed::Ambar);
  if (retro_visible_) retro_desde_ms_ = ahora_ms;
  return colorLed() != antes;
}

bool ControlActuadores::revisarLed(uint32_t ahora_ms) {
  if (!retro_visible_) return false;
  // Resta sin signo: correcta aunque millis() haya dado la vuelta.
  if (static_cast<uint32_t>(ahora_ms - retro_desde_ms_) < kRetroSesionMs) return false;
  retro_visible_ = false;
  // Con la sesión activa vuelve al azul; si la sesión ya terminó durante la
  // ventana, el color se olvida y el LED se apaga.
  if (!sesion_activa_) color_retro_ = ColorLed::Apagado;
  return true;  // el color efectivo cambió
}

bool ControlActuadores::aplicarSesionActiva(bool activa) {
  const ColorLed antes = colorLed();
  // Repetir el mismo estado (retenido, reconexión) no cambia nada ni acorta la
  // ventana. Si la sesión empieza, el azul manda y se cierra la ventana. Si
  // termina, el LED se apaga y el color recordado se olvida, salvo que haya una
  // ventana abierta: ese color se ve hasta que venza (revisarLed) y entonces
  // se apaga.
  if (activa != sesion_activa_) {
    if (activa) {
      retro_visible_ = false;
    } else if (!retro_visible_) {
      color_retro_ = ColorLed::Apagado;
    }
  }
  sesion_activa_ = activa;
  return colorLed() != antes;
}

ColorLed ControlActuadores::colorLed() const {
  // Con la sesión activa el azul manda (plan 6.1: azul = sesión activa),
  // salvo durante la ventana de retroalimentación de kRetroSesionMs, que puede
  // seguir abierta unos instantes tras terminar la sesión.
  if (sesion_activa_ && !retro_visible_) return ColorLed::Azul;
  return color_retro_;
}

ColorLed ControlActuadores::colorRetroalimentacion() const { return color_retro_; }

bool ControlActuadores::sesionActiva() const { return sesion_activa_; }

bool ControlActuadores::aplicarBuzzer(PedidoBuzzer pedido, uint32_t ahora_ms) {
  switch (pedido) {
    case PedidoBuzzer::Silencio: {
      if (fase_buzzer_ == FaseBuzzer::Inactivo) return false;
      const bool antes = buzzer_salida_;
      fase_buzzer_ = FaseBuzzer::Inactivo;
      pitidos_restantes_ = 0;
      buzzer_salida_ = false;
      return buzzer_salida_ != antes;
    }
    case PedidoBuzzer::Exito:
    case PedidoBuzzer::Error: {
      const bool antes = buzzer_salida_;
      pitidos_restantes_ = pedido == PedidoBuzzer::Error ? 2 : 1;
      fase_buzzer_ = FaseBuzzer::Sonando;
      buzzer_desde_ms_ = ahora_ms;
      buzzer_salida_ = true;
      return buzzer_salida_ != antes;
    }
    default:  // Ausente e Invalido no cambian nada
      return false;
  }
}

bool ControlActuadores::revisarBuzzer(uint32_t ahora_ms) {
  if (fase_buzzer_ == FaseBuzzer::Inactivo) return false;
  const uint32_t elapsed = static_cast<uint32_t>(ahora_ms - buzzer_desde_ms_);
  if (fase_buzzer_ == FaseBuzzer::Sonando) {
    if (elapsed < kPitidoBuzzerMs) return false;
    const bool antes = buzzer_salida_;
    if (pitidos_restantes_ > 1) {
      pitidos_restantes_--;
      fase_buzzer_ = FaseBuzzer::Pausa;
      buzzer_desde_ms_ = ahora_ms;
      buzzer_salida_ = false;
    } else {
      pitidos_restantes_ = 0;
      fase_buzzer_ = FaseBuzzer::Inactivo;
      buzzer_salida_ = false;
    }
    return buzzer_salida_ != antes;
  }
  // FaseBuzzer::Pausa: separación entre los dos pitidos del error.
  if (elapsed < kPausaBuzzerMs) return false;
  fase_buzzer_ = FaseBuzzer::Sonando;
  buzzer_desde_ms_ = ahora_ms;
  buzzer_salida_ = true;
  return true;
}

bool ControlActuadores::buzzerEncendido() const { return buzzer_salida_; }

size_t construirEstado(char* destino, size_t capacidad, EstadoServo servo, ColorLed led,
                       bool luz_encendida) {
  if (destino == nullptr || capacidad == 0) return 0;
  const int n = snprintf(destino, capacidad, "{\"servo\":\"%s\",\"led\":\"%s\",\"luz\":%d}",
                         nombreEstadoServo(servo), nombreColor(led), luz_encendida ? 1 : 0);
  if (n < 0 || static_cast<size_t>(n) >= capacidad) {
    destino[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n);
}

size_t construirEstado(char* destino, size_t capacidad, const ControlActuadores& control,
                       bool luz_encendida) {
  return construirEstado(destino, capacidad, control.estadoServo(), control.colorLed(),
                         luz_encendida);
}

}  // namespace logica_actuadores
