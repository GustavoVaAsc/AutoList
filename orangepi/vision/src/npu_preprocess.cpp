#include "vision/npu_preprocess.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

#include <opencv2/imgproc.hpp>

namespace vision {

namespace {

// El valor real de un byte de la imagen, ya con la escala del modo elegido.
float escala(ScaleMode modo, float byte) {
  switch (modo) {
    case ScaleMode::Auto: return byte;  // nunca llega aqui: el modo se resuelve antes
    case ScaleMode::None: return byte;
    case ScaleMode::ArcFace: return (byte - 127.5f) / 127.5f;
    case ScaleMode::Unit: return byte / 255.0f;
  }
  return byte;
}

// La escala 1/255 (dominio real [0,1]) y la 1/127.5 con zero_point 128 (dominio real
// [-1,1]) son las dos convenciones de entrada cuantizada que se reconocen.
constexpr float kEscalaUnidad = 1.0f / 255.0f;
constexpr float kEscalaArcFace = 1.0f / 127.5f;

// Tolerancia para comparar la escala del tensor (viene de un .nb, en punto flotante).
constexpr float kToleranciaEscala = 1e-6f;

bool es_ocho_bits(DataFormat formato) {
  return formato == DataFormat::Uint8 || formato == DataFormat::Int8;
}

bool escala_igual(float a, float b) {
  return std::fabs(a - b) <= kToleranciaEscala;
}

// El tensor de entrada tiene que caber en el buffer que reservo el driver: si no,
// se falla ANTES de reservar memoria (el .nb manda en las dimensiones).
void comprobar_tamano(const TensorInfo& info) {
  const std::size_t elemento = element_size(info.format);
  if (elemento == 0) {
    throw std::invalid_argument("preprocess: el tensor de entrada tiene un formato no "
                                "soportado (" + std::string(to_string(info.format)) + ")");
  }
  const std::size_t necesarios = info.element_count * elemento;
  if (info.size_bytes != 0 && info.size_bytes < necesarios) {
    throw std::invalid_argument("preprocess: el driver reservo " +
                                std::to_string(info.size_bytes) + " bytes y el tensor "
                                "necesita " + std::to_string(necesarios));
  }
}

}  // namespace

ScaleMode modo_escala_efectivo(ScaleMode modo, const TensorInfo& info) {
  if (!es_ocho_bits(info.format)) {
    // Tensor de punto flotante: el dominio real no se puede deducir del tensor.
    if (modo == ScaleMode::Auto) {
      throw std::invalid_argument(
          "preprocess: el tensor de entrada es de punto flotante (" +
          std::string(to_string(info.format)) +
          "): no se puede deducir el dominio real, elige el modo de escala a mano "
          "(ScaleMode::None, ScaleMode::Unit o ScaleMode::ArcFace)");
    }
    return modo;
  }

  if (info.quant == QuantFormat::None) {
    // Sin cuantizacion, el valor real del tensor es el byte de la imagen.
    if (modo == ScaleMode::Auto) return ScaleMode::None;
    if (modo != ScaleMode::None) {
      throw std::invalid_argument(
          "preprocess: el tensor de entrada es de 8 bits sin cuantizacion: el valor real "
          "es el byte de la imagen (ScaleMode::None); el modo elegido no es compatible");
    }
    return ScaleMode::None;
  }

  if (info.quant == QuantFormat::DynamicFixedPoint) {
    // El punto fijo dinamico no dice cual es el dominio real (puede ser [0,1] o
    // [-1,1] segun la posicion del punto): no se deduce, falla cerrado.
    throw std::invalid_argument(
        "preprocess: el tensor de entrada tiene cuantizacion de punto fijo dinamico: no "
        "se puede deducir el dominio real, confirma la convencion con el .nb");
  }

  // Cuantizacion afin.
  if (escala_igual(info.scale, kEscalaUnidad) && info.zero_point == 0) {
    // Dominio real [0,1]: el byte es x/255.
    if (modo == ScaleMode::Auto) return ScaleMode::Unit;
    if (modo != ScaleMode::Unit) {
      throw std::invalid_argument(
          "preprocess: el tensor de entrada esta cuantificado con escala 1/255: su "
          "dominio real es [0,1] (ScaleMode::Unit); el modo elegido no es compatible "
          "(con ScaleMode::None la entrada saturaria)");
    }
    return ScaleMode::Unit;
  }

  if (escala_igual(info.scale, kEscalaArcFace) && info.zero_point == 128) {
    // Dominio real [-1,1]: el byte es (x-127.5)/127.5.
    if (modo == ScaleMode::Auto) {
      throw std::invalid_argument(
          "preprocess: el tensor de entrada esta cuantificado con escala 1/127.5 y "
          "zero_point 128: no se puede deducir el dominio real, confirma la convencion "
          "con el .nb");
    }
    if (modo != ScaleMode::ArcFace) {
      throw std::invalid_argument(
          "preprocess: el tensor de entrada esta cuantificado con escala 1/127.5 y "
          "zero_point 128: su dominio real es [-1,1] (ScaleMode::ArcFace); el modo "
          "elegido no es compatible");
    }
    return ScaleMode::ArcFace;
  }

  if (modo == ScaleMode::Auto) {
    throw std::invalid_argument(
        "preprocess: el tensor de entrada tiene cuantizacion afin con escala " +
        std::to_string(info.scale) + " y zero_point " + std::to_string(info.zero_point) +
        ": no se puede deducir el dominio real, confirma la convencion con el .nb");
  }
  // Modo explicito con una cuantizacion que no se reconoce: se respeta (el llamador
  // confirmo la convencion) y la saturacion la detecta vision::quantize.
  return modo;
}

void ajustar_tamano_entrada(int& ancho, int& alto, const TensorInfo& info, const char* quien) {
  const InputShape shape = input_shape(info);
  if (ancho <= 0) ancho = shape.width;
  if (alto <= 0) alto = shape.height;
  if (ancho != shape.width || alto != shape.height) {
    throw std::invalid_argument(
        std::string(quien) + ": el modelo espera una entrada de " +
        std::to_string(shape.width) + "x" + std::to_string(shape.height) + " y se pidio " +
        std::to_string(ancho) + "x" + std::to_string(alto));
  }
}

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
  if (shape.width > kMaxLadoEntrada || shape.height > kMaxLadoEntrada) {
    throw std::invalid_argument("preprocess: el tensor de entrada declara " +
                                std::to_string(shape.width) + "x" +
                                std::to_string(shape.height) + " y el techo por lado es " +
                                std::to_string(kMaxLadoEntrada));
  }
  if (shape.channels != 3) {
    throw std::invalid_argument("preprocess: el tensor de entrada declara " +
                                std::to_string(shape.channels) +
                                " canales y solo se soportan 3 (BGR o RGB)");
  }
  if (shape.batch != 1) {
    throw std::invalid_argument("preprocess: el tensor de entrada declara un lote de " +
                                std::to_string(shape.batch) +
                                " y un modelo de rostro necesita un lote de 1");
  }
  // El tamano del tensor se comprueba contra el buffer del driver ANTES de reservar
  // memoria: las dimensiones vienen del .nb.
  comprobar_tamano(info);
  // El modo de escala: si es Auto, se deriva del tensor; si no, tiene que ser compatible.
  const ScaleMode modo = modo_escala_efectivo(options.scale_mode, info);

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
              escala(modo, static_cast<float>(fila[x][canal_imagen]));
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
