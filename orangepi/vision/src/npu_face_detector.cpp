#include "vision/npu_face_detector.hpp"

#include <stdexcept>
#include <string>

namespace vision {

namespace {

// Una salida del layout tiene que existir en el modelo.
void comprobar_indice(std::size_t indice, std::size_t salidas, const char* nombre) {
  if (indice >= salidas) {
    throw std::invalid_argument("NpuFaceDetector: el layout pide " + std::string(nombre) +
                                " en la salida " + std::to_string(indice) +
                                " y el modelo solo tiene " + std::to_string(salidas) +
                                " salidas");
  }
}

}  // namespace

NpuFaceDetector::NpuFaceDetector(const npu::Driver& driver, const std::filesystem::path& nb_file,
                                 const NpuDetectorOptions& options)
    : network_(driver, nb_file), options_(options) {
  // El orden de color no tiene valor por omision: no se deduce del modelo.
  if (!options_.color_order.has_value()) {
    throw std::invalid_argument(
        "NpuFaceDetector: falta el orden de color de la entrada (npu.color; el RetinaFace "
        "del fabricante lleva \"bgr\")");
  }
  // Un modelo de rostro tiene una sola entrada: con mas de una, solo se rellenaria la
  // primera y la red correria con las demas sin inicializar.
  if (network_.input_count() != 1) {
    throw std::invalid_argument("NpuFaceDetector: el modelo debe tener una sola entrada y "
                                "tiene " + std::to_string(network_.input_count()));
  }
  const std::size_t salidas = network_.output_count();
  comprobar_indice(options_.layout.boxes_output_index, salidas, "las cajas");
  comprobar_indice(options_.layout.scores_output_index, salidas, "los scores");
  comprobar_indice(options_.layout.landmarks_output_index, salidas, "los landmarks");
  input_info_ = network_.input_info(0);
  // El tensor de ENTRADA se valida al arrancar: formato, dimensiones, modo de escala y
  // rango representable. Un .nb con otra disposicion tiene que hacer fallar el proceso
  // aqui, no en el primer fotograma con el alumno delante de la camara.
  PreprocessOptions preproceso;
  preproceso.color_order = *options_.color_order;
  preproceso.scale_mode = options_.scale_mode;
  validar_tensor_entrada(input_info_, preproceso, "NpuFaceDetector");
  ajustar_tamano_entrada(options_.input_width, options_.input_height, input_info_,
                         "NpuFaceDetector");
  // Las anclas se generan una vez: dependen solo del tamano de entrada. Antes de
  // generarlas, las dimensiones se acotan: sin este techo, un .nb que declarara
  // 60000x60000 pediria cientos de millones de anclas antes de que nada lo rechazara.
  comprobar_tamano_entrada(options_.input_width, options_.input_height, "NpuFaceDetector");
  anchors_ = generate_anchors(options_.layout, options_.input_width, options_.input_height);
  // El formato, el numero de elementos y la disposicion de las tres salidas se
  // comprueban al arrancar, no en cada fotograma.
  comprobar_salida_detector(network_.output_info(options_.layout.boxes_output_index),
                            "las cajas", anchors_.size(), kValoresCaja);
  comprobar_salida_detector(network_.output_info(options_.layout.scores_output_index),
                            "los scores", anchors_.size(), kValoresScore);
  comprobar_salida_detector(network_.output_info(options_.layout.landmarks_output_index),
                            "los landmarks", anchors_.size(), kValoresLandmarks);
}

std::vector<FaceDetection> NpuFaceDetector::detect(const cv::Mat& bgr) {
  PreprocessOptions preproceso;
  preproceso.color_order = *options_.color_order;
  preproceso.scale_mode = options_.scale_mode;

  // Letterbox: la imagen se reduce sin deformarla y se centra sobre gris 114, como la
  // demo del fabricante. El preproceso recibe ya el tamano de la entrada, asi que no
  // vuelve a redimensionar.
  if (bgr.empty()) {
    throw std::invalid_argument("NpuFaceDetector: la imagen esta vacia");
  }
  const Letterbox letterbox =
      calcular_letterbox(bgr.cols, bgr.rows, options_.input_width, options_.input_height);
  const cv::Mat entrada =
      aplicar_letterbox(bgr, letterbox, options_.input_width, options_.input_height);
  const std::vector<std::uint8_t> bytes = preprocess_to_bytes(entrada, input_info_, preproceso);
  network_.set_input(0, bytes);
  network_.run();

  const std::vector<DetectorTensor> tensores = {
      DetectorTensor{options_.layout.boxes_output_index,
                     network_.read_output(options_.layout.boxes_output_index)},
      DetectorTensor{options_.layout.scores_output_index,
                     network_.read_output(options_.layout.scores_output_index)},
      DetectorTensor{options_.layout.landmarks_output_index,
                     network_.read_output(options_.layout.landmarks_output_index)}};

  // Las anclas van cacheadas: no se regeneran en cada fotograma.
  std::vector<FaceDetection> detecciones =
      decode_detector_outputs(tensores, options_.layout, anchors_);
  // De la entrada del modelo a la imagen original.
  deshacer_letterbox_detecciones(letterbox, detecciones, bgr.cols, bgr.rows);
  return detecciones;
}

}  // namespace vision
