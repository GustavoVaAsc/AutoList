#pragma once

#include <time.h>

#include <chrono>
#include <thread>

namespace vision {

using TimePoint = std::chrono::steady_clock::time_point;
using Millis = std::chrono::milliseconds;

// Reloj y espera inyectables: el servicio no llama a steady_clock ni a
// std::this_thread directamente, para poder probarlo sin esperas reales. now() se llama
// tambien desde el hilo del bus (al encolar un evento), asi que debe ser seguro entre hilos.
class Clock {
 public:
  virtual ~Clock() = default;
  virtual TimePoint now() const = 0;
  // CLOCK_BOOTTIME: lo que lleva el sistema encendido, INCLUIDA la suspension (now() es
  // monotonico y no la cuenta). Lo usa la guardia del reloj de pared: un reloj de pared que
  // avanza lo mismo que BOOTTIME no ha saltado, aunque el equipo haya estado suspendido.
  virtual Millis boottime() const = 0;
  virtual void sleep_for(Millis duration) = 0;
};

class SystemClock final : public Clock {
 public:
  TimePoint now() const override { return std::chrono::steady_clock::now(); }
  Millis boottime() const override {
    timespec ahora{};
    clock_gettime(CLOCK_BOOTTIME, &ahora);
    return Millis(static_cast<long long>(ahora.tv_sec) * 1000 + ahora.tv_nsec / 1000000);
  }
  void sleep_for(Millis duration) override { std::this_thread::sleep_for(duration); }
};

}  // namespace vision
