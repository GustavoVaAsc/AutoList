#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

#include "vision/tensor.hpp"

namespace vision::npu {

class NpuError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// Inicializa el driver VIPLite (vip_init) y lo apaga al destruirse (vip_destroy).
// Debe vivir mas que cualquier Network. Requiere acceso a /dev/vipcore.
class Driver {
 public:
  Driver();
  ~Driver();
  Driver(const Driver&) = delete;
  Driver& operator=(const Driver&) = delete;

  std::uint32_t version() const;
};

// Duracion en ms de cada paso; cada campo se actualiza con la llamada que le corresponde.
struct Timings {
  double create_ms = 0.0;        // cargar el .nb
  double prepare_ms = 0.0;       // preparar la red y crear los buffers
  double input_copy_ms = 0.0;    // set_input
  double run_ms = 0.0;           // run
  double output_read_ms = 0.0;   // read_output_raw / read_output
};

struct NetworkOptions {
  // Tiempo maximo de una inferencia (VIP_NETWORK_PROP_SET_TIME_OUT). Valor provisional:
  // hay que ajustarlo al modelo real una vez medido; no puede ser 0.
  std::uint32_t timeout_ms = 2000;
};

// Red .nb cargada y preparada, con sus buffers de entrada y salida ya enlazados.
// No es segura entre hilos.
class Network {
 public:
  // Lanza NpuError si el archivo no existe o el driver rechaza la red, y
  // std::invalid_argument si options.timeout_ms es 0.
  Network(const Driver& driver, const std::filesystem::path& nb_file,
          const NetworkOptions& options = {});
  ~Network();
  Network(const Network&) = delete;
  Network& operator=(const Network&) = delete;

  std::size_t input_count() const;
  std::size_t output_count() const;
  const TensorInfo& input_info(std::size_t index) const;
  const TensorInfo& output_info(std::size_t index) const;

  // Copia los bytes crudos al buffer de entrada; deben ocupar exactamente size_bytes.
  void set_input(std::size_t index, std::span<const std::uint8_t> data);

  // Ejecuta la red y espera a que termine.
  void run();

  // Bytes crudos de la salida (todos los que reserva el driver, size_bytes) y los elementos
  // logicos de la salida (element_count) dequantizados a float.
  std::vector<std::uint8_t> read_output_raw(std::size_t index);
  std::vector<float> read_output(std::size_t index);

  const Timings& timings() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace vision::npu
