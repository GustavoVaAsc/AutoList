#include "puente_aula/registro.hpp"

#include <algorithm>
#include <cstdio>

namespace puente_aula::registro {

namespace {

std::mutex& mutex_salida() {
  static std::mutex mutex;
  return mutex;
}

std::function<void(const std::string&)>& salida_actual() {
  static std::function<void(const std::string&)> salida;
  return salida;
}

}  // namespace

void linea(std::string_view componente, std::string_view texto) {
  std::string completa = "[puente_aula] ";
  completa += componente;
  completa += ' ';
  completa += texto;
  std::lock_guard<std::mutex> cerrojo(mutex_salida());
  if (salida_actual()) {
    salida_actual()(completa);
    return;
  }
  completa += '\n';
  std::fwrite(completa.data(), 1, completa.size(), stderr);
  std::fflush(stderr);
}

void fijar_salida(std::function<void(const std::string&)> salida) {
  std::lock_guard<std::mutex> cerrojo(mutex_salida());
  salida_actual() = std::move(salida);
}

std::string limpiar(std::string_view texto, std::size_t maximo) {
  std::string limpio;
  limpio.reserve(std::min(texto.size(), maximo));
  for (const char caracter : texto) {
    if (limpio.size() >= maximo) break;
    const auto byte = static_cast<unsigned char>(caracter);
    limpio += (byte > 0x20 && byte < 0x7f) ? caracter : '?';
  }
  return limpio;
}

bool LimiteAvisos::permitir(std::uint64_t& omitidos) {
  std::lock_guard<std::mutex> cerrojo(mutex_);
  const auto ahora = std::chrono::steady_clock::now();
  if (hubo_aviso_ && ahora - ultimo_ < intervalo_) {
    ++callados_;
    return false;
  }
  hubo_aviso_ = true;
  ultimo_ = ahora;
  omitidos = callados_;
  callados_ = 0;
  return true;
}

}  // namespace puente_aula::registro
