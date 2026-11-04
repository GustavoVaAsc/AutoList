#include "vision/npu_face_detector.hpp"

#include <stdexcept>
#include <string>

namespace vision {

namespace {

// El tensor de salida tiene que traer los elementos que el layout espera: si no, el
// fallo aparece en cada fotograma, con el alumno posando y la luz encendida.
void comprobar_salida(const TensorInfo& info, std::size_t esperados, const char* quien) {
  exigir_salida_descuantizable(info, quien);
  if (info.element_count != esperados) {
    throw std::invalid_argument(std::string(quien) + ": la salida del modelo trae " +
                                std::to_string(info.element_count) + " elementos y el layout "
                                "espera " + std::to_string(esperados));
  }
}

}  // namespace

NpuFaceDetector::NpuFaceDetector(const npu::Driver& driver, const std::filesystem::path& nb_file,
                                 const NpuDetectorOptions& options)
    : network_(driver, nb_file), options_(options) {
  // Un modelo de rostro tiene una sola entrada: con mas de una, solo se rellenaria la
  // primera y la red correria con las demas sin inicializar.
  if (network_.input_count() != 1) {
    throw std::invalid_argument("NpuFaceDetector: el modelo debe tener una sola entrada y "
                                "tiene " + std::to_string(network_.input_count()));
  }
  // El tensor de salida que se lee es el de las cajas; el resto lo apunta el layout.
  options_.layout.boxes_output_index = options_.output_index;
  const std::size_t salidas = network_.output_count();
  if (options_.output_index >= salidas) {
    throw std::invalid_argument("NpuFaceDetector: el modelo tiene " +
                                std::to_string(salidas) +
                                " salidas y se pidio la " + std::to_string(options_.output_index));
  }
  // Los indices de salida del layout tambien tienen que estar dentro de las salidas.
  if (!options_.layout.score_in_boxes_tensor &&
      options_.layout.scores_output_index >= salidas) {
    throw std::invalid_argument("NpuFaceDetector: el layout pide el score en la salida " +
                                std::to_string(options_.layout.scores_output_index) +
                                " y el modelo solo tiene " + std::to_string(salidas) + " salidas");
  }
  if (!options_.layout.landmarks_in_boxes_tensor &&
      options_.layout.landmarks_output_index >= salidas) {
    throw std::invalid_argument("NpuFaceDetector: el layout pide los landmarks en la salida " +
                                std::to_string(options_.layout.landmarks_output_index) +
                                " y el modelo solo tiene " + std::to_string(salidas) + " salidas");
  }
  input_info_ = network_.input_info(0);
  ajustar_tamano_entrada(options_.input_width, options_.input_height, input_info_,
                         "NpuFaceDetector");
  // Las anclas se generan una vez: dependen solo del tamano de entrada.
  anchors_ = generate_anchors(options_.layout, options_.input_width, options_.input_height);
  // El formato y el numero de elementos de las salidas se comprueban al arrancar, no en
  // cada fotograma.
  const std::size_t por_ancla = static_cast<std::size_t>(values_per_anchor(options_.layout));
  comprobar_salida(network_.output_info(options_.layout.boxes_output_index),
                   anchors_.size() * por_ancla, "NpuFaceDetector");
  if (!options_.layout.score_in_boxes_tensor) {
    comprobar_salida(network_.output_info(options_.layout.scores_output_index), anchors_.size(),
                     "NpuFaceDetector");
  }
  if (!options_.layout.landmarks_in_boxes_tensor) {
    comprobar_salida(network_.output_info(options_.layout.landmarks_output_index),
                     anchors_.size() * 10, "NpuFaceDetector");
  }
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

  // Las anclas van cacheadas: no se regeneran en cada fotograma.
  std::vector<FaceDetection> detecciones =
      decode_detector_outputs(tensores, options_.layout, anchors_);

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
