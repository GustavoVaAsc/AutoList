#pragma once

// Preproceso de imagen para los tensores de entrada de la NPU.
//
// La imagen entra en BGR (como la decodifica OpenCV) y sale como los bytes que espera
// el tensor de entrada: el resize a las dimensiones del tensor, el orden de color, la
// escala por canal y la cuantizacion del tensor.
//
// El tensor de la NPU describe sus dimensiones en el orden [w, h, c, n] (ver
// TensorInfo::dims) y su memoria es "canal a canal": primero todos los pixeles del
// canal 0, despues los del canal 1, y asi; NO es un intercalado (RGBRGB...).
//
// La normalizacion NO se aplica aqui por omision: el .nb cuantizado lleva la
// normalizacion DENTRO (el modelo se cuantiza con ella), asi que el byte que se
// escribe es el de la imagen. La escala "arcface" ((x-127.5)/127.5, la de
// w600k_mbf/MobileFaceNet) es para los .nb que NO la llevan dentro.
//
// OJO con la cuantizacion afin del tensor de ENTRADA: si el tensor de entrada esta
// cuantizado (scale y zero_point), el dominio real del modelo no es el byte de la
// imagen, sino el que da la escala del tensor (con scale 1/255 el dominio real es
// [0,1] y el byte es x/255; con scale 1/127.5 y zero_point 128 es [-1,1] y el byte es
// (x-127.5)/127.5). Por eso el modo de escala es una opcion y no una constante: hay que
// elegirlo con el .nb real a la vista.

#include <cstdint>
#include <vector>

#include <opencv2/core.hpp>

#include "vision/tensor.hpp"

namespace vision {

// Orden de los canales que espera el tensor de entrada.
enum class ColorOrder {
  Rgb,
  Bgr,
};

// Escala por canal que se aplica antes de cuantizar.
//
// El modo indica el DOMINIO REAL que espera el modelo (el valor que entra en la
// cuantizacion del tensor de entrada):
enum class ScaleMode {
  // El valor real es el byte de la imagen (0..255): la normalizacion va DENTRO del
  // modelo cuantizado. Es el valor por omision.
  None,
  // (x - 127.5) / 127.5, la de w600k_mbf/MobileFaceNet: el dominio real es [-1, 1].
  ArcFace,
  // x / 255: el dominio real es [0, 1]. Es el dominio de un tensor de entrada
  // cuantizado con escala 1/255, como el de la red de ejemplo del fabricante.
  Unit,
};

struct PreprocessOptions {
  // PROVISIONAL: depende del .nb, que todavia no existe. Por omision RGB (la
  // convencion de los modelos de reconocimiento tipo ArcFace). Los .nb de RetinaFace
  // suelen esperar BGR: hay que cambiarlo cuando exista el modelo real.
  ColorOrder color_order = ColorOrder::Rgb;
  // PROVISIONAL: por omision la normalizacion va dentro del modelo.
  ScaleMode scale_mode = ScaleMode::None;
};

// Dimensiones del tensor de entrada, en el orden [w, h, c, n] de VIPLite.
// Lanza std::invalid_argument si el tensor no tiene al menos 3 dimensiones.
struct InputShape {
  int width = 0;
  int height = 0;
  int channels = 0;
  int batch = 1;
};

InputShape input_shape(const TensorInfo& info);

// Convierte la imagen BGR a los valores float del tensor, en el orden [w, h, c, n]
// (canal a canal). El lote se replica: la misma imagen va a cada elemento del lote.
//
// Lanza std::invalid_argument si la imagen esta vacia, no es de 8 bits con 3 canales,
// si el tensor no describe 3 canales o si sus dimensiones no cuadran.
std::vector<float> preprocess_to_floats(const cv::Mat& bgr, const TensorInfo& info,
                                         const PreprocessOptions& options);

// Lo mismo, pero devuelve los bytes listos para npu::Network::set_input: el tensor con
// su cuantizacion (vision::quantize) y relleno con ceros hasta el tamano que reserva el
// driver (TensorInfo::size_bytes), que puede ser mayor que el logico.
//
// Lanza std::invalid_argument con las mismas condiciones que preprocess_to_floats, y
// tambien si el driver reservo menos bytes de los que necesita el tensor.
std::vector<std::uint8_t> preprocess_to_bytes(const cv::Mat& bgr, const TensorInfo& info,
                                               const PreprocessOptions& options);

}  // namespace vision
