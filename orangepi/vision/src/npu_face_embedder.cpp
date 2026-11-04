#include "vision/npu_face_embedder.hpp"

#include <stdexcept>
#include <string>

namespace vision {

NpuFaceEmbedder::NpuFaceEmbedder(const npu::Driver& driver, const std::filesystem::path& nb_file,
                                 const NpuEmbedderOptions& options)
    : network_(driver, nb_file), options_(options) {
  // Un modelo de rostro tiene una sola entrada: con mas de una, solo se rellenaria la
  // primera y la red correria con las demas sin inicializar.
  if (network_.input_count() != 1) {
    throw std::invalid_argument("NpuFaceEmbedder: el modelo debe tener una sola entrada y "
                                "tiene " + std::to_string(network_.input_count()));
  }
  if (options_.output_index >= network_.output_count()) {
    throw std::invalid_argument("NpuFaceEmbedder: el modelo tiene " +
                                std::to_string(network_.output_count()) +
                                " salidas y se pidio la " + std::to_string(options_.output_index));
  }
  // El formato de la salida se comprueba al arrancar: dequantize no sabe leer el fp16 y
  // el fallo tiene que salir antes de que un alumno este posando.
  const TensorInfo& salida = network_.output_info(options_.output_index);
  exigir_salida_descuantizable(salida, "NpuFaceEmbedder");
  if (salida.element_count == 0) {
    throw std::invalid_argument("NpuFaceEmbedder: la salida del modelo no declara elementos");
  }
  input_info_ = network_.input_info(0);
  // El tensor de ENTRADA se valida al arrancar: formato, dimensiones, modo de escala y
  // rango representable. Un .nb con otra disposicion tiene que hacer fallar el proceso
  // aqui, no en el primer fotograma con el alumno delante de la camara.
  PreprocessOptions preproceso;
  preproceso.color_order = options_.color_order;
  preproceso.scale_mode = options_.scale_mode;
  validar_tensor_entrada(input_info_, preproceso, "NpuFaceEmbedder");
  ajustar_tamano_entrada(options_.input_width, options_.input_height, input_info_,
                         "NpuFaceEmbedder");
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
