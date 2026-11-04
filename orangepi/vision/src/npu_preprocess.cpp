#include "vision/npu_preprocess.hpp"

#include <stdexcept>
#include <string>

#include <opencv2/imgproc.hpp>

namespace vision {

namespace {

// El valor real de un byte de la imagen, ya con la escala del modo elegido.
float escala(ScaleMode modo, float byte) {
  switch (modo) {
    case ScaleMode::None: return byte;
    case ScaleMode::ArcFace: return (byte - 127.5f) / 127.5f;
    case ScaleMode::Unit: return byte / 255.0f;
  }
  return byte;
}

}  // namespace

InputShape input_shape(const TensorInfo& info) {
  if (info.dims.size() < 3) {
    throw std::invalid_argument("input_shape: el tensor de entrada necesita al menos 3 "
                                "dimensiones [w, h, c]: el tensor declara " +
                                std::to_string(info.dims.size()));
  }
  InputShape shape;
  shape.width = static_cast<int>(info.dims[0]);
  shape.height = static_cast<int>(info.dims[1]);
  shape.channels = static_cast<int>(info.dims[2]);
  shape.batch = info.dims.size() >= 4 ? static_cast<int>(info.dims[3]) : 1;
  return shape;
}

std::vector<float> preprocess_to_floats(const cv::Mat& bgr, const TensorInfo& info,
                                         const PreprocessOptions& options) {
  if (bgr.empty()) {
    throw std::invalid_argument("preprocess: la imagen esta vacia");
  }
  if (bgr.type() != CV_8UC3) {
    throw std::invalid_argument("preprocess: la imagen debe ser BGR de 8 bits con 3 canales "
                                "(CV_8UC3)");
  }
  const InputShape shape = input_shape(info);
  if (shape.width <= 0 || shape.height <= 0) {
    throw std::invalid_argument("preprocess: el tensor tiene dimensiones no validas");
  }
  if (shape.channels != 3) {
    throw std::invalid_argument("preprocess: el tensor de entrada declara " +
                                std::to_string(shape.channels) +
                                " canales y solo se soportan 3 (BGR o RGB)");
  }
  if (shape.batch <= 0) {
    throw std::invalid_argument("preprocess: el tensor de entrada declara un lote de " +
                                std::to_string(shape.batch));
  }
  if (options.scale_mode != ScaleMode::None &&
      (info.format == DataFormat::Uint8 || info.format == DataFormat::Int8) &&
      info.quant != QuantFormat::Affine) {
    throw std::invalid_argument(
        "preprocess: la escala elegida sobre un tensor de 8 bits sin cuantizacion afín "
        "perderia el rango; usa ScaleMode::None (el byte tal cual) o un tensor float");
  }

  // El resize: el tensor manda sobre el tamano de la imagen.
  cv::Mat resized;
  if (bgr.cols == shape.width && bgr.rows == shape.height) {
    resized = bgr;
  } else {
    cv::resize(bgr, resized, cv::Size(shape.width, shape.height), 0.0, 0.0, cv::INTER_LINEAR);
  }

  // Escritura en el orden [w, h, c, n]: canal a canal, no intercalado.
  const std::size_t por_canal =
      static_cast<std::size_t>(shape.width) * static_cast<std::size_t>(shape.height);
  std::vector<float> valores(por_canal * static_cast<std::size_t>(shape.channels) *
                             static_cast<std::size_t>(shape.batch));
  std::size_t escritura = 0;
  for (int n = 0; n < shape.batch; ++n) {
    for (int canal = 0; canal < shape.channels; ++canal) {
      // Con RGB el canal 0 del tensor es el ultimo de la imagen BGR.
      const int canal_imagen = options.color_order == ColorOrder::Rgb ? 2 - canal : canal;
      for (int y = 0; y < shape.height; ++y) {
        const cv::Vec3b* fila = resized.ptr<cv::Vec3b>(y);
        for (int x = 0; x < shape.width; ++x) {
          valores[escritura++] =
              escala(options.scale_mode, static_cast<float>(fila[x][canal_imagen]));
        }
      }
    }
  }
  return valores;
}

std::vector<std::uint8_t> preprocess_to_bytes(const cv::Mat& bgr, const TensorInfo& info,
                                               const PreprocessOptions& options) {
  const std::vector<float> valores = preprocess_to_floats(bgr, info, options);
  std::vector<std::uint8_t> bytes = quantize(info, valores);
  if (info.size_bytes != 0) {
    if (info.size_bytes < bytes.size()) {
      throw std::invalid_argument("preprocess: el driver reservo " +
                                  std::to_string(info.size_bytes) + " bytes y el tensor "
                                  "necesita " + std::to_string(bytes.size()));
    }
    // El driver puede haber reservado mas de lo logico: el relleno va en ceros.
    bytes.resize(info.size_bytes, 0);
  }
  return bytes;
}

}  // namespace vision
