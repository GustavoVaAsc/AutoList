#pragma once

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
  virtual void sleep_for(Millis duration) = 0;
};

class SystemClock final : public Clock {
 public:
  TimePoint now() const override { return std::chrono::steady_clock::now(); }
  void sleep_for(Millis duration) override { std::this_thread::sleep_for(duration); }
};

}  // namespace vision
