#pragma once

// Reintento con backoff de la recarga de la galeria (lo usa main.cpp). Vive aparte para
// poder probar la decision sin el proceso.
//
// Una galeria "de fiar" es la que se cargo sin descartar ninguna fila (GalleryStore::
// loaded_ok()): mientras no lo sea, los registros estan cerrados, asi que hay que seguir
// reintentando tanto si la recarga FALLO como si cargo pero descarto filas.

#include <algorithm>
#include <optional>

#include "vision/clock.hpp"

namespace vision {

class GalleryRetry {
 public:
  GalleryRetry(Millis base, int max_factor)
      : base_(base), max_(base * max_factor), interval_(base) {}

  // Anota el resultado de un intento de recarga. Con una galeria de fiar se cancela el
  // reintento y el intervalo vuelve al base. Si no, se programa el siguiente intento dentro
  // del intervalo actual (que se duplica para el que siga, con tope) y se devuelve esa espera.
  Millis record(bool trustworthy, TimePoint now) {
    if (trustworthy) {
      next_.reset();
      interval_ = base_;
      return Millis::zero();
    }
    const Millis wait = interval_;
    next_ = now + wait;
    interval_ = std::min(interval_ * 2, max_);
    return wait;
  }

  // Toca reintentar (hay un reintento programado y ya llego su hora).
  bool due(TimePoint now) const { return next_.has_value() && now >= *next_; }

 private:
  Millis base_;
  Millis max_;
  Millis interval_;
  std::optional<TimePoint> next_;
};

}  // namespace vision
