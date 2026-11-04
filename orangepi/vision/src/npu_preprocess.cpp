#include "vision/npu_preprocess.hpp"

#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
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

// Margen admisible al containment del rango: el 1% del ancho del dominio. Absorbe el
// redondeo de las convenciones habituales (con escala 1/127.5 y zero_point 128, el
// uint8 representa [-1.0039, 0.9961] y el dominio [-1,1] no llega al extremo) sin
// dejar pasar un tensor que de verdad no cubre el dominio (un int8 con escala 1/255
// representa [-0.502, 0.498] y no cubre ni de lejos el [0,1]).
constexpr float kToleranciaRango = 0.01f;

bool es_ocho_bits(DataFormat formato) {
  return formato == DataFormat::Uint8 || formato == DataFormat::Int8;
}

bool escala_igual(float a, float b) {
  return std::fabs(a - b) <= kToleranciaEscala;
}

// El tensor de entrada tiene que caber en el buffer que reservo el driver: si no,
// se falla ANTES de reservar memoria (el .nb manda en las dimensiones).
void comprobar_tamano(const TensorInfo& info, const char* quien) {
  const std::string prefijo = std::string(quien) + ": ";
  const std::size_t elemento = element_size(info.format);
  if (elemento == 0) {
    throw std::invalid_argument(prefijo + "el tensor de entrada tiene un formato no "
                                "soportado (" + std::string(to_string(info.format)) + ")");
  }
  const std::size_t necesarios = info.element_count * elemento;
  if (info.size_bytes != 0 && info.size_bytes < necesarios) {
    throw std::invalid_argument(prefijo + "el driver reservo " +
                                std::to_string(info.size_bytes) + " bytes y el tensor "
                                "necesita " + std::to_string(necesarios));
  }
}

// Bytes que el preproceso reserva en un fotograma: los float del tensor, la imagen
// redimensionada y el buffer de bytes del tensor. Se comprueba ANTES de reservar.
std::size_t bytes_del_preproceso(const InputShape& shape, const TensorInfo& info) {
  const std::size_t elementos = static_cast<std::size_t>(shape.width) *
                                static_cast<std::size_t>(shape.height) *
                                static_cast<std::size_t>(shape.channels) *
                                static_cast<std::size_t>(shape.batch);
  const std::size_t bytes_float = elementos * sizeof(float);
  const std::size_t bytes_tensor = elementos * element_size(info.format);
  const std::size_t bytes_imagen = static_cast<std::size_t>(shape.width) *
                                   static_cast<std::size_t>(shape.height) * 3u;
  return bytes_float + bytes_tensor + bytes_imagen;
}

// Nombre del modo de escala, para los mensajes de error.
std::string nombre_modo(ScaleMode modo) {
  switch (modo) {
    case ScaleMode::Auto: return "auto";
    case ScaleMode::None: return "ninguna";
    case ScaleMode::ArcFace: return "arcface";
    case ScaleMode::Unit: return "unidad";
  }
  return "auto";
}

// Un real en los mensajes: sin el relleno de decimales de std::to_string.
std::string formato_real(float valor) {
  std::ostringstream out;
  out << std::setprecision(6) << valor;
  return out.str();
}

// true si el rango del tensor contiene el dominio (con el margen de redondeo).
bool rango_contiene(const RangoReal& contenedor, const RangoReal& contenido) {
  const float margen = kToleranciaRango * (contenido.maximo - contenido.minimo);
  return contenedor.minimo <= contenido.minimo + margen &&
         contenedor.maximo >= contenido.maximo - margen;
}

}  // namespace

ScaleMode modo_escala_efectivo(ScaleMode modo, const TensorInfo& info) {
  if (!es_ocho_bits(info.format)) {
    // Tensor de punto flotante o de un formato que no se deduce: el dominio real no se
    // puede deducir del tensor. El mensaje NOMBRA el formato real: no todos los
    // formatos que llegan aqui son de punto flotante.
    if (modo == ScaleMode::Auto) {
      const bool flotante =
          info.format == DataFormat::Fp32 || info.format == DataFormat::Fp16;
      const std::string descripcion =
          flotante ? "es de punto flotante (" + std::string(to_string(info.format)) + ")"
                   : "tiene el formato " + std::string(to_string(info.format));
      throw std::invalid_argument(
          "preprocess: el tensor de entrada " + descripcion +
          ": no se puede deducir el dominio real, elige el modo de escala a mano "
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

RangoReal rango_tensor(const TensorInfo& info) {
  const float infinito = std::numeric_limits<float>::infinity();
  // Un tensor de punto flotante representa cualquier valor: el rango es ilimitado.
  if (info.format == DataFormat::Fp32 || info.format == DataFormat::Fp16) {
    return RangoReal{-infinito, infinito};
  }
  if (info.format != DataFormat::Uint8 && info.format != DataFormat::Int8) {
    return RangoReal{-infinito, infinito};
  }
  const bool con_signo = info.format == DataFormat::Int8;
  const float minimo = con_signo ? -128.0f : 0.0f;
  const float maximo = con_signo ? 127.0f : 255.0f;
  if (info.quant == QuantFormat::Affine) {
    // real = (q - zero_point) * scale, con q entre minimo y maximo.
    const float zero_point = static_cast<float>(info.zero_point);
    return RangoReal{(minimo - zero_point) * info.scale,
                     (maximo - zero_point) * info.scale};
  }
  if (info.quant == QuantFormat::DynamicFixedPoint) {
    // real = q * 2^-pos.
    const float factor = std::ldexp(1.0f, -info.fixed_point_pos);
    return RangoReal{minimo * factor, maximo * factor};
  }
  // Sin cuantizacion el valor real es el del byte.
  return RangoReal{minimo, maximo};
}

RangoReal dominio_escala(ScaleMode modo) {
  switch (modo) {
    case ScaleMode::None: return RangoReal{0.0f, 255.0f};
    case ScaleMode::Unit: return RangoReal{0.0f, 1.0f};
    case ScaleMode::ArcFace: return RangoReal{-1.0f, 1.0f};
    case ScaleMode::Auto: break;
  }
  throw std::invalid_argument(
      "dominio_escala: el modo Auto no es un modo resuelto (usa modo_escala_efectivo)");
}

void comprobar_tamano_entrada(int ancho, int alto, const char* quien) {
  const std::string prefijo = std::string(quien) + ": ";
  if (ancho <= 0 || alto <= 0) {
    throw std::invalid_argument(prefijo + "la entrada " + std::to_string(ancho) + "x" +
                                std::to_string(alto) + " no es valida");
  }
  if (ancho > kMaxLadoEntrada || alto > kMaxLadoEntrada) {
    throw std::invalid_argument(prefijo + "la entrada " + std::to_string(ancho) + "x" +
                                std::to_string(alto) + " pasa del techo por lado (" +
                                std::to_string(kMaxLadoEntrada) + ")");
  }
  const std::size_t elementos =
      static_cast<std::size_t>(ancho) * static_cast<std::size_t>(alto) * 3u;
  if (elementos > kMaxElementosEntrada) {
    throw std::invalid_argument(prefijo + "la entrada " + std::to_string(ancho) + "x" +
                                std::to_string(alto) + "x3 son " +
                                std::to_string(elementos) +
                                " elementos y el techo es " +
                                std::to_string(kMaxElementosEntrada));
  }
}

void validar_tensor_entrada(const TensorInfo& info, const PreprocessOptions& options,
                            const char* quien) {
  const std::string prefijo = std::string(quien) + ": ";
  // 1. Formato del tensor de entrada: solo se sabe escribir en estos cuatro.
  switch (info.format) {
    case DataFormat::Fp32:
    case DataFormat::Fp16:
    case DataFormat::Uint8:
    case DataFormat::Int8:
      break;
    default:
      throw std::invalid_argument(prefijo + "el tensor de entrada tiene el formato " +
                                  std::string(to_string(info.format)) +
                                  ", que no se soporta (solo fp32, fp16, uint8 e int8)");
  }
  // 2. Dimensiones: tres canales y un lote de 1.
  const InputShape shape = input_shape(info);
  if (shape.width <= 0 || shape.height <= 0) {
    throw std::invalid_argument(prefijo + "el tensor de entrada tiene dimensiones no validas (" +
                                std::to_string(shape.width) + "x" +
                                std::to_string(shape.height) + ")");
  }
  if (shape.channels != 3) {
    throw std::invalid_argument(prefijo + "el tensor de entrada declara " +
                                std::to_string(shape.channels) +
                                " canales y solo se soportan 3 (BGR o RGB)");
  }
  if (shape.batch != 1) {
    throw std::invalid_argument(prefijo + "el tensor de entrada declara un lote de " +
                                std::to_string(shape.batch) +
                                " y un modelo de rostro necesita un lote de 1");
  }
  // 3. Techos de tamano: por lado y en numero total de elementos. Sin el total, un
  //    tensor de 4096x4096x3 reservaria 201 MB de float en cada fotograma.
  comprobar_tamano_entrada(shape.width, shape.height, quien);
  const std::size_t bytes = bytes_del_preproceso(shape, info);
  if (bytes > kMaxBytesPreproceso) {
    throw std::invalid_argument(prefijo + "el preproceso reservaria " +
                                std::to_string(bytes) + " bytes y el techo es " +
                                std::to_string(kMaxBytesPreproceso));
  }
  // 4. El buffer del driver tiene que alcanzar para el tensor.
  comprobar_tamano(info, quien);
  // 5. El modo de escala: si es Auto, se deriva del tensor; si es explicito, tiene que
  //    ser compatible. Aqui (al arrancar) es donde falla cerrado si no se puede deducir.
  const ScaleMode modo = modo_escala_efectivo(options.scale_mode, info);
  // 6. El rango que el tensor puede representar tiene que contener el dominio que
  //    implica el modo. Es una comprobacion ESTATICA (del .nb, no de la imagen): si no
  //    cabe, la entrada se recorta en silencio y la decision biometrica queda en fallo
  //    abierto.
  const RangoReal rango = rango_tensor(info);
  const RangoReal dominio = dominio_escala(modo);
  if (!rango_contiene(rango, dominio)) {
    throw std::invalid_argument(
        prefijo + "el tensor de entrada (" + std::string(to_string(info.format)) +
        (info.quant == QuantFormat::Affine
             ? " con escala " + formato_real(info.scale) + " y zero_point " +
                   std::to_string(info.zero_point)
             : " sin cuantizacion") +
        ") representa el rango [" + formato_real(rango.minimo) + ", " +
        formato_real(rango.maximo) + "] y el modo de escala " + nombre_modo(modo) +
        " necesita el rango [" + formato_real(dominio.minimo) + ", " +
        formato_real(dominio.maximo) + "]: el dominio no cabe en el tensor");
  }
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
  // La validacion del tensor de entrada es la MISMA que se hace al arrancar: aqui no se
  // puede aceptar un tensor que el adaptador habria rechazado (formato, dimensiones,
  // techos, modo de escala y rango representable).
  validar_tensor_entrada(info, options, "preprocess");
  const InputShape shape = input_shape(info);
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
