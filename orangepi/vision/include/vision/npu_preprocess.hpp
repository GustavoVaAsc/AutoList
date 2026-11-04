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
// (x-127.5)/127.5). Por eso el modo de escala se DERIVA del tensor (ScaleMode::Auto, el
// valor por omision) en vez de dejarlo a mano: elegirlo mal satura la entrada en
// silencio (el byte 255 sobre un tensor con escala 1/255 da 255 en casi todos los
// pixeles) y deja la decision biometrica en fallo abierto.
//
// La derivacion (modo_escala_efectivo) es:
//  - tensor de 8 bits (uint8/int8) cuantificado de forma afin con escala 1/255 y
//    zero_point 0: el dominio real es [0,1], luego el modo es Unit (x/255).
//  - tensor de 8 bits SIN cuantizacion: el valor real es el byte, luego el modo es None.
//  - tensor de 8 bits con cuantizacion afin que no sea 1/255 con zero_point 0: no se
//    puede deducir, falla cerrado (std::invalid_argument) pidiendo confirmar la
//    convencion con el .nb.
//  - tensor de punto flotante (fp32/fp16): no se puede deducir, falla cerrado
//    (std::invalid_argument) pidiendo elegir el modo a mano.
//
// Los modos explicitos (None, Unit, ArcFace) siguen existiendo para forzar a mano, pero
// si no son compatibles con la cuantizacion del tensor, tambien falla cerrado.

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
  // Se deriva del tensor de entrada (ver modo_escala_efectivo). Es el valor por
  // omision: deducirlo es mas seguro que dejarlo a mano.
  Auto,
  // El valor real es el byte de la imagen (0..255): la normalizacion va DENTRO del
  // modelo cuantizado.
  None,
  // (x - 127.5) / 127.5, la de w600k_mbf/MobileFaceNet: el dominio real es [-1, 1].
  ArcFace,
  // x / 255: el dominio real es [0, 1]. Es el dominio de un tensor de entrada
  // cuantizado con escala 1/255, como el de la red de ejemplo del fabricante.
  Unit,
};

// Techo de cada lado del tensor de entrada. Las dimensiones vienen del .nb: sin un
// techo, un .nb corrupto (por ejemplo [60000, 60000, 3, 1]) haria que el resize
// pidiera decenas de gigabytes y matara el proceso por el OOM killer.
constexpr int kMaxLadoEntrada = 4096;

struct PreprocessOptions {
  // PROVISIONAL: depende del .nb, que todavia no existe. Por omision RGB (la
  // convencion de los modelos de reconocimiento tipo ArcFace). Los .nb de RetinaFace
  // suelen esperar BGR: hay que cambiarlo cuando exista el modelo real.
  ColorOrder color_order = ColorOrder::Rgb;
  // El modo de escala se deriva del tensor de entrada (Auto) salvo que se elija a mano.
  ScaleMode scale_mode = ScaleMode::Auto;
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

// Modo de escala efectivo: si `modo` es Auto, se deriva del tensor; si es explicito,
// se comprueba que sea compatible con su cuantizacion.
//
// Lanza std::invalid_argument si el modo es Auto y no se puede deducir el dominio real
// (tensor de 8 bits con una cuantizacion afin que no sea 1/255 con zero_point 0, o
// tensor de punto flotante), y tambien si un modo explicito no es compatible con la
// cuantizacion del tensor.
ScaleMode modo_escala_efectivo(ScaleMode modo, const TensorInfo& info);

// Ajusta el tamano de entrada de las opciones al del tensor (si el llamador lo deja en 0
// o menos) y comprueba que el tamano pedido cuadre con el del tensor.
//
// `quien` es el nombre del adaptador, para el mensaje de error. Lanza
// std::invalid_argument si los tamanos no cuadran.
void ajustar_tamano_entrada(int& ancho, int& alto, const TensorInfo& info, const char* quien);

// Convierte la imagen BGR a los valores float del tensor, en el orden [w, h, c, n]
// (canal a canal).
//
// Lanza std::invalid_argument si la imagen esta vacia, no es de 8 bits con 3 canales,
// si el tensor no describe 3 canales, si sus dimensiones no cuadran, si el tensor
// declara un lote distinto de 1, si algun lado pasa de kMaxLadoEntrada o si el driver
// reservo menos bytes de los que necesita el tensor (eso se comprueba ANTES de
// reservar memoria).
std::vector<float> preprocess_to_floats(const cv::Mat& bgr, const TensorInfo& info,
                                         const PreprocessOptions& options);

// Lo mismo, pero devuelve los bytes listos para npu::Network::set_input: el tensor con
// su cuantizacion (vision::quantize) y relleno con ceros hasta el tamano que reserva el
// driver (TensorInfo::size_bytes), que puede ser mayor que el logico.
//
// Lanza std::invalid_argument con las mismas condiciones que preprocess_to_floats, y
// tambien si la entrada satura al cuantizar (ver vision::quantize).
std::vector<std::uint8_t> preprocess_to_bytes(const cv::Mat& bgr, const TensorInfo& info,
                                               const PreprocessOptions& options);

}  // namespace vision
