#pragma once

// Reloj de pared inyectable: estampa la hora en los mensajes que publica el servicio.
// El reloj de milisegundos (vision/clock.hpp) no sirve para esto: no lleva fecha ni
// zona horaria.

#include <string>
#include <string_view>

namespace vision {

// La hora local, en ISO 8601 con zona: "2026-11-04T09:12:33.120-06:00".
class WallClock {
 public:
  virtual ~WallClock() = default;
  virtual std::string now_iso8601() const = 0;
};

// Reloj del sistema: la hora local de la placa (que tiene RTC). La zona sale de la
// configuracion del sistema (variable TZ), no del reloj.
class SystemWallClock final : public WallClock {
 public:
  std::string now_iso8601() const override;
};

// Comprueba el formato de la hora anterior: "AAAA-MM-DDTHH:MM:SS", con milisegundos
// opcionales, seguido de la zona "+HH:MM" o "-HH:MM".
bool is_iso8601_with_zone(std::string_view text);

}  // namespace vision
