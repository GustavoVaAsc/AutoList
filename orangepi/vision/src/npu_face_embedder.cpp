#include "vision/npu_face_embedder.hpp"

#include <stdexcept>
#include <string>

namespace vision {

namespace {

// El tensor de entrada tiene que ser el que el modelo espera (112x112 por omision).
// Si las opciones no dicen nada (0 o menos), se toma el del propio .nb.
void ajustar_tamano(NpuEmbedderOptions& options, const TensorInfo& info) {
  const InputShape shape = input_shape(info);
  if (options.input_width <= 0) options.input_width = shape.width;
  if (options.input_height <= 0) options.input_height = shape.height;
  if (options.input_width != shape.width || options.input_height != shape.height) {
    throw std::invalid_argument(
        "NpuFaceEmbedder: el modelo espera una entrada de " + std::to_string(shape.width) + "x" +
        std::to_string(shape.height) + " y se pidio " + std::to_string(options.input_width) + "x" +
        std::to_string(options.input_height));
  }
}

}  // namespace

NpuFaceEmbedder::NpuFaceEmbedder(const npu::Driver& driver, const std::filesystem::path& nb_file,
                                 const NpuEmbedderOptions& options)
    : network_(driver, nb_file), options_(options) {
  if (network_.input_count() == 0) {
    throw std::invalid_argument("NpuFaceEmbedder: el modelo no tiene entradas");
  }
  if (options_.output_index >= network_.output_count()) {
    throw std::invalid_argument("NpuFaceEmbedder: el modelo tiene " +
                                std::to_string(network_.output_count()) +
                                " salidas y se pidio la " + std::to_string(options_.output_index));
  }
  input_info_ = network_.input_info(0);
  ajustar_tamano(options_, input_info_);
}

std::vector<float> NpuFaceEmbedder::embed(const cv::Mat& aligned_bgr_112) {
  PreprocessOptions preproceso;
  preproceso.color_order = options_.color_order;
  preproceso.scale_mode = options_.scale_mode;

  const std::vector<std::uint8_t> bytes =
      preprocess_to_bytes(aligned_bgr_112, input_info_, preproceso);
  network_.set_input(0, bytes);
  network_.run();
  // La salida se devuelve tal cual: la dimension la fija el modelo y la normalizacion
  // L2 la hace el servicio al final.
  return network_.read_output(options_.output_index);
}

}  // namespace vision
