#include "logica_pir.h"

#include <stdio.h>

namespace logica_pir {

bool esMovimiento(int lectura) { return lectura != 0; }

bool ControlPir::enCalentamiento(uint32_t ahora_ms) const {
  if (calentamiento_ms_ == 0) return false;
  // Resta sin signo: correcta aunque millis() haya dado la vuelta.
  return static_cast<uint32_t>(ahora_ms - arranque_ms_) < calentamiento_ms_;
}

bool ControlPir::dentroDeAntirrebote(uint32_t ahora_ms) const {
  if (!tiene_evento_) return false;
  return static_cast<uint32_t>(ahora_ms - ultimo_evento_ms_) < antirrebote_ms_;
}

bool ControlPir::procesarNivel(bool movimiento, uint32_t ahora_ms) {
  if (!movimiento) {
    // Flanco de bajada: no se publica nada, solo se recuerda el nivel.
    nivel_alto_ = false;
    return false;
  }
  if (nivel_alto_) return false;  // el nivel sigue en alto: no hay flanco de subida
  nivel_alto_ = true;

  // Flanco de subida. Todavía puede descartarse por calentamiento o antirrebote.
  if (enCalentamiento(ahora_ms)) return false;
  if (dentroDeAntirrebote(ahora_ms)) return false;

  tiene_evento_ = true;
  ultimo_evento_ms_ = ahora_ms;
  return true;
}

size_t construirMovimiento(char* destino, size_t capacidad) {
  if (destino == nullptr || capacidad == 0) return 0;
  const int n = snprintf(destino, capacidad, "{\"mov\":1}");
  if (n < 0 || static_cast<size_t>(n) >= capacidad) {
    destino[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n);
}

}  // namespace logica_pir
