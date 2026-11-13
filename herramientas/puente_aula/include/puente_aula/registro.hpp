#pragma once

// El registro del puente: una linea por evento, a stderr (el journal de systemd le pone
// la hora). Convencion del servicio de vision: prefijo "[nombre]".
//
// REGLA: nunca se registran cuerpos, valores de cabeceras, firmas ni datos de las
// conexiones MQTT (llevan la contrasena del broker). Solo IPs, caminos sin query, codigos,
// cuentas de bytes y tiempos.

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>

namespace puente_aula::registro {

// Escribe "[puente_aula] <componente> <texto>". Seguro entre hilos.
void linea(std::string_view componente, std::string_view texto);

// Cambia el destino de las lineas (las pruebas recogen lo registrado). nullptr vuelve a
// stderr.
void fijar_salida(std::function<void(const std::string&)> salida);

// Deja un texto externo apto para una linea de registro: solo ASCII imprimible, sin
// espacios, recortado a `maximo` caracteres (lo demas pasa a '?'). Un camino con saltos de
// linea no puede fabricar lineas falsas.
std::string limpiar(std::string_view texto, std::size_t maximo);

// Limita los avisos de rechazo (un escaneo de puertos no debe llenar el journal): deja
// pasar uno por intervalo y cuenta los que se callaron.
class LimiteAvisos {
 public:
  explicit LimiteAvisos(std::chrono::milliseconds intervalo = std::chrono::seconds(5))
      : intervalo_(intervalo) {}

  // true si toca avisar ahora; `omitidos` recibe cuantos avisos se callaron desde el ultimo.
  bool permitir(std::uint64_t& omitidos);

 private:
  std::mutex mutex_;
  std::chrono::milliseconds intervalo_;
  std::chrono::steady_clock::time_point ultimo_{};
  bool hubo_aviso_ = false;
  std::uint64_t callados_ = 0;
};

}  // namespace puente_aula::registro
