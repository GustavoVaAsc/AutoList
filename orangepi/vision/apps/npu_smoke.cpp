// Carga un .nb, le da un archivo de entrada crudo y mide la inferencia en la NPU.
// Uso: npu_smoke <modelo.nb> <entrada.dat> [iteraciones=100]
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>

#include "vision/npu.hpp"

namespace {

using namespace vision;

void print_tensor(const char* kind, std::size_t index, const TensorInfo& t) {
  // Las dimensiones en el orden de VIPLite, de la mas rapida a la mas lenta: en una
  // entrada intercalada la primera es el canal (ver vision::input_shape).
  std::cout << kind << "[" << index << "] name=" << t.name << " dims=";
  for (std::size_t i = 0; i < t.dims.size(); ++i) std::cout << (i ? "x" : "") << t.dims[i];
  std::cout << " format=" << to_string(t.format) << " quant=" << to_string(t.quant)
            << " scale=" << t.scale << " zero_point=" << t.zero_point
            << " fixed_point_pos=" << t.fixed_point_pos << " bytes=" << t.size_bytes << '\n';
}

// Lee el archivo completo, que debe medir exactamente `expected_bytes`; se comprueba el
// tamano antes de leer para no cargar en memoria un archivo cualquiera.
std::vector<std::uint8_t> read_file(const char* path, std::size_t expected_bytes) {
  std::error_code error;
  const std::uintmax_t size = std::filesystem::file_size(path, error);
  if (error) throw std::runtime_error(std::string("no se pudo leer ") + path + ": " + error.message());
  if (size != expected_bytes) {
    throw std::runtime_error("entrada 0: se esperaban " + std::to_string(expected_bytes) +
                             " bytes y " + path + " tiene " + std::to_string(size));
  }
  std::ifstream file(path, std::ios::binary);
  std::vector<std::uint8_t> data(expected_bytes);
  if (!file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()))) {
    throw std::runtime_error(std::string("no se pudo leer ") + path);
  }
  return data;
}

double mean(const std::vector<double>& v) {
  return std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3 || argc > 4) {
    std::cerr << "uso: " << argv[0] << " <modelo.nb> <entrada.dat> [iteraciones=100]\n";
    return 2;
  }
  long iterations = 100;
  if (argc == 4) {
    char* end = nullptr;
    errno = 0;
    iterations = std::strtol(argv[3], &end, 10);
    if (errno != 0 || end == argv[3] || *end != '\0' || iterations < 1 || iterations > 1000000) {
      std::cerr << "las iteraciones deben ser un entero entre 1 y 1000000: " << argv[3] << '\n';
      return 2;
    }
  }

  try {
    npu::Driver driver;
    npu::Network network(driver, argv[1]);
    std::cout << "VIPLite driver version=0x" << std::hex << driver.version() << std::dec << '\n';
    for (std::size_t i = 0; i < network.input_count(); ++i) print_tensor("input", i, network.input_info(i));
    for (std::size_t i = 0; i < network.output_count(); ++i) print_tensor("output", i, network.output_info(i));
    if (network.input_count() != 1) {
      std::cerr << "npu_smoke solo maneja redes con una entrada\n";
      return 1;
    }
    const std::vector<std::uint8_t> input = read_file(argv[2], network.input_info(0).size_bytes);

    std::vector<double> input_copy, run, output_read;
    std::vector<float> last_output;
    for (long i = 0; i < iterations; ++i) {
      network.set_input(0, input);
      network.run();
      last_output = network.read_output(0);
      input_copy.push_back(network.timings().input_copy_ms);
      run.push_back(network.timings().run_ms);
      output_read.push_back(network.timings().output_read_ms);
    }

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "load: create=" << network.timings().create_ms
              << " ms, prepare=" << network.timings().prepare_ms << " ms\n";
    std::cout << "iterations=" << iterations << "\n"
              << "avg input_copy=" << mean(input_copy) << " ms\n"
              << "avg run=" << mean(run) << " ms (min=" << *std::min_element(run.begin(), run.end())
              << ", max=" << *std::max_element(run.begin(), run.end()) << ")\n"
              << "avg output_read=" << mean(output_read) << " ms\n"
              << "avg total=" << mean(input_copy) + mean(run) + mean(output_read) << " ms\n";
    std::cout << "output[0] dequantizado (primeros " << std::min<std::size_t>(last_output.size(), 8)
              << " de " << last_output.size() << "):";
    for (std::size_t i = 0; i < std::min<std::size_t>(last_output.size(), 8); ++i) {
      std::cout << ' ' << last_output[i];
    }
    std::cout << '\n';
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
