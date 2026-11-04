#include "vision/npu_face_detector.hpp"

#include <stdexcept>
#include <string>

namespace vision {

namespace {

// El tensor de entrada tiene que ser el que el modelo espera (640x640 por omision).
// Si las opciones no dicen nada (0 o menos), se toma el del propio .nb.
void ajustar_tamano(NpuDetectorOptions& options, const TensorInfo& info) {
  const InputShape shape = input_shape(info);
  if (options.input_width <= 0) options.input_width = shape.width;
  if (options.input_height <= 0) options.input_height = shape.height;
  if (options.input_width != shape.width || options.input_height != shape.height) {
    throw std::invalid_argument(
        "NpuFaceDetector: el modelo espera una entrada de " + std::to_string(shape.width) + "x" +
        std::to_string(shape.height) + " y se pidio " + std::to_string(options.input_width) +
        "x" + std::to_string(options.input_height));
  }
}

}  // namespace

NpuFaceDetector::NpuFaceDetector(const npu::Driver& driver, const std::filesystem::path& nb_file,
                                 const NpuDetectorOptions& options)
    : network_(driver, nb_file), options_(options) {
  if (network_.input_count() == 0) {
    throw std::invalid_argument("NpuFaceDetector: el modelo no tiene entradas");
  }
  // El tensor de salida que se lee es el de las cajas; el resto lo apunta el layout.
  options_.layout.boxes_output_index = options_.output_index;
  if (options_.output_index >= network_.output_count()) {
    throw std::invalid_argument("NpuFaceDetector: el modelo tiene " +
                                std::to_string(network_.output_count()) +
                                " salidas y se pidio la " + std::to_string(options_.output_index));
  }
  input_info_ = network_.input_info(0);
  ajustar_tamano(options_, input_info_);
}

std::vector<FaceDetection> NpuFaceDetector::detect(const cv::Mat& bgr) {
  PreprocessOptions preproceso;
  preproceso.color_order = options_.color_order;
  preproceso.scale_mode = options_.scale_mode;

  const std::vector<std::uint8_t> bytes = preprocess_to_bytes(bgr, input_info_, preproceso);
  network_.set_input(0, bytes);
  network_.run();

  // Se leen los tensores que dice el layout: el de las cajas y, si van aparte, el del
  // score y el de los landmarks.
  std::vector<DetectorTensor> tensores;
  tensores.push_back(
      DetectorTensor{options_.layout.boxes_output_index, network_.read_output(options_.layout.boxes_output_index)});
  if (!options_.layout.score_in_boxes_tensor) {
    tensores.push_back(DetectorTensor{options_.layout.scores_output_index,
                                      network_.read_output(options_.layout.scores_output_index)});
  }
  if (!options_.layout.landmarks_in_boxes_tensor) {
    tensores.push_back(DetectorTensor{options_.layout.landmarks_output_index,
                                      network_.read_output(options_.layout.landmarks_output_index)});
  }

  std::vector<FaceDetection> detecciones =
      decode_detector_outputs(tensores, options_.layout, options_.input_width,
                              options_.input_height);

  // Del modelo a la imagen original: la escala se aplica al final.
  const float escala_x = static_cast<float>(bgr.cols) / static_cast<float>(options_.input_width);
  const float escala_y = static_cast<float>(bgr.rows) / static_cast<float>(options_.input_height);
  for (FaceDetection& deteccion : detecciones) {
    deteccion.bbox.x *= escala_x;
    deteccion.bbox.y *= escala_y;
    deteccion.bbox.width *= escala_x;
    deteccion.bbox.height *= escala_y;
    for (cv::Point2f& punto : deteccion.landmarks) {
      punto.x *= escala_x;
      punto.y *= escala_y;
    }
  }
  return detecciones;
}

}  // namespace vision
