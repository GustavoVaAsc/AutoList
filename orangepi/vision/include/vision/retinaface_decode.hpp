#pragma once

// Decodificador de RetinaFace en CPU: anclas, cajas, landmarks y NMS.
//
// Son funciones PURAS (sin NPU y sin mas OpenCV que cv::Point/cv::Rect): se prueban
// con tensores sinteticos.
//
// La convencion es la del RetinaFace de biubug6 (Pytorch_Retinaface) que el fabricante
// convierte y decodifica en su demo (awnpu_model_zoo, examples/retinaface,
// retinaface_post.cpp). El R50 a 320 (D1) esta verificado en la placa contra la demo; el
// MobileNet 0.25 a 320 (D3) carga, corre y detecta la cara de la foto de la demo, sin
// una referencia del fabricante con la que comparar:
//  - Tres tensores de salida fp32, en este orden: cajas (4 valores por ancla), scores
//    (2 por ancla: [fondo, cara], con el softmax dentro del modelo) y landmarks (10 por
//    ancla: 5 pares x, y). VIPLite los reporta como 4x4200x1, 2x4200x1 y 10x4200x1: los
//    valores de una misma ancla van seguidos. (retinaface_post.cpp:172-174, 227-229)
//  - Anclas cuadradas de lado fijo en pixeles de la entrada del modelo (NO se escalan
//    con la entrada): {16, 32}, {64, 128} y {256, 512} para los strides 8, 16 y 32, con
//    el centro en el MEDIO de la celda, ((k + 0.5) * stride, (j + 0.5) * stride). En el
//    tensor van por stride, luego por fila, por columna y por tamano.
//    (retinaface_post.cpp:130-164)
//  - Varianzas 0.1 (centro y landmarks) y 0.2 (tamano), multiplicadas por el tamano del
//    ancla. Los landmarks son relativos al ANCLA, no a la caja decodificada.
//    (retinaface_post.cpp:188, 202-205, 218-219)

#include <array>
#include <cstddef>
#include <vector>

#include <opencv2/core.hpp>

#include "vision/face_detector.hpp"
#include "vision/npu_preprocess.hpp"
#include "vision/tensor.hpp"

namespace vision {

// Varianzas de RetinaFace (retinaface_post.cpp:188): el desplazamiento del centro y de
// los landmarks se multiplica por 0.1 y el logaritmo del tamano por 0.2, siempre en
// unidades del tamano del ancla.
inline constexpr float kVarianzaCentro = 0.1f;
inline constexpr float kVarianzaTamano = 0.2f;

// Valores por ancla de cada tensor de salida.
inline constexpr std::size_t kValoresCaja = 4;       // dx, dy, dw, dh
inline constexpr std::size_t kValoresScore = 2;      // [fondo, cara]
inline constexpr std::size_t kValoresLandmarks = 10;  // 5 pares x, y
// Columna del score que es la probabilidad de cara (retinaface_post.cpp:200, 211).
inline constexpr std::size_t kColumnaCara = 1;

// Una ancla del detector: centro y tamano, en pixeles de la imagen de entrada del modelo.
// Lleva el stride del mapa de caracteristicas del que sale.
struct Anchor {
  float cx = 0.0f;
  float cy = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
  int stride = 8;
};

// Disposicion de la salida del detector. Los valores por omision son los del modelo
// real (ver el comentario de cabecera y retinaface_post.cpp / model_config.h).
struct DetectorLayout {
  // Strides de los mapas de caracteristicas.
  std::vector<int> strides{8, 16, 32};
  // Lado (en pixeles de la entrada del modelo) de las anclas de cada stride: una ancla
  // cuadrada por tamano y posicion. Mismo numero de entradas que `strides`.
  std::vector<std::vector<float>> anchor_sizes{{16.0f, 32.0f}, {64.0f, 128.0f}, {256.0f, 512.0f}};
  // Los dos umbrales siguen a la demo del fabricante por paridad (model_config.h:19-20);
  // no estan afinados para el aula: afinarlos es parte de la calibracion con LFW.
  // Umbral de IoU de la supresion de no maximos (NMS_THRESHOLD).
  float nms_iou_threshold = 0.45f;
  // Una deteccion necesita un score de cara MAYOR que este (SCORE_THRESHOLD;
  // retinaface_post.cpp:200 compara con ">").
  float score_threshold = 0.4f;
  // Cuantas cajas pasan al NMS: solo las K de mayor score. La demo no lo acota; aqui se
  // acota para que el NMS, que es O(n^2), no bloquee el hilo de trabajo del servicio si
  // muchas anclas pasan el umbral.
  std::size_t nms_top_k = 500;

  // Indices de los tres tensores de salida del .nb (retinaface_post.cpp:172-174).
  std::size_t boxes_output_index = 0;
  std::size_t scores_output_index = 1;
  std::size_t landmarks_output_index = 2;
};

// Numero de posiciones del mapa de caracteristicas en una dimension: el techo de
// tamano/stride, calculado sin sumar antes de dividir (para no desbordar con un
// tamano cercano a INT_MAX).
int posiciones_mapa(int tamano, int stride);

// Genera las anclas del detector para una entrada de input_width x input_height, en el
// orden del tensor: por stride, por fila (j), por columna (k) y por tamano. El mapa de
// cada stride tiene ceil(alto/stride) filas y ceil(ancho/stride) columnas, el centro de
// cada ancla es ((k + 0.5) * stride, (j + 0.5) * stride) y su lado es el de
// anchor_sizes, sin escalar con la entrada (retinaface_post.cpp:130-164).
//
// Lanza std::invalid_argument si strides y anchor_sizes no tienen el mismo tamano, si un
// stride no es positivo, si un stride no tiene tamanos o uno no es positivo, o si la
// entrada no es valida.
std::vector<Anchor> generate_anchors(const DetectorLayout& layout, int input_width,
                                      int input_height);

// Precondicion comun de decode_box y decode_landmarks: los valores crudos tienen que ser
// finitos (los 4 de la caja, los 10 de los landmarks). Las dos lo comprueban con la
// misma funcion y lanzan std::invalid_argument si no; decode_detector_outputs la usa
// antes de llamarlas para descartar el ancla en vez de lanzar. Un valor finito puede
// DESBORDAR al decodificar (exp(1000 * 0.2) es infinito en float): por eso
// decode_detector_outputs comprueba tambien el resultado.

// Decodifica los valores crudos (dx, dy, dw, dh) de una ancla a una caja en pixeles de
// la imagen de entrada del modelo (retinaface_post.cpp:202-210):
//   cx = ancla.cx + dx * 0.1 * ancla.w      w = ancla.w * exp(dw * 0.2)
//   cy = ancla.cy + dy * 0.1 * ancla.h      h = ancla.h * exp(dh * 0.2)
//
// Lanza std::invalid_argument si algun valor crudo no es finito.
cv::Rect2f decode_box(const Anchor& anchor, const float* box);

// Decodifica los 10 offsets de landmarks (5 pares x, y) a 5 puntos en pixeles de la
// entrada del modelo. Son relativos al ANCLA (retinaface_post.cpp:218-219):
//   x = ancla.cx + lx * 0.1 * ancla.w       y = ancla.cy + ly * 0.1 * ancla.h
// El orden es el de FaceDetection::landmarks: ojo izquierdo, ojo derecho, nariz, boca
// izquierda y boca derecha.
//
// Lanza std::invalid_argument si algun offset no es finito.
std::array<cv::Point2f, 5> decode_landmarks(const Anchor& anchor, const float* landmarks);

// IoU de dos cajas (0 si no se tocan).
float iou(const cv::Rect2f& a, const cv::Rect2f& b);

// Supresion de no maximos por IoU. Devuelve los indices conservados, del score mas
// alto al mas bajo.
//
// Lanza std::invalid_argument si no hay un score por caja, si el umbral de IoU esta
// fuera de [0, 1] o si algun score no es finito (un NaN en el comparador del orden no
// seria una relacion de orden estricto debil: comportamiento indefinido).
std::vector<int> nms(const std::vector<cv::Rect2f>& boxes, const std::vector<float>& scores,
                     float iou_threshold);

// Un tensor de salida del detector, ya desquantizado a float.
struct DetectorTensor {
  std::size_t index = 0;
  std::vector<float> values;
};

// Interpreta los tres tensores de salida del detector segun el layout y devuelve las
// detecciones en pixeles de la imagen de ENTRADA del modelo (deshacer el letterbox lo
// hace el adaptador, que es quien conoce la imagen original). El score de cada
// deteccion es la columna de cara del tensor de scores.
//
// Las anclas se generan aqui (a partir del tamano de entrada). El adaptador usa la
// variante de abajo, que las recibe ya generadas: dependen solo del tamano de entrada,
// que es fijo, asi que no hay que regenerarlas en cada fotograma.
//
// Lanza std::invalid_argument si falta alguno de los tres tensores o si alguno no trae
// exactamente sus valores por ancla (4, 2 y 10) por el numero de anclas.
//
// El ancla se descarta (no es una deteccion) si su score no es finito o es mayor que 1
// (la salida de un softmax no lo es: es un tensor mal interpretado; la demo tampoco
// dibuja esas detecciones, retinaface_post.cpp:317-321), si sus valores crudos de caja
// o de landmarks no son finitos, o si la caja o los landmarks decodificados no lo son
// (desbordamiento). Un NaN en el score pasaria a un comparador que no es una relacion de
// orden estricto debil, y un NaN en un landmark haria que align_face lanzara en cada
// fotograma.
std::vector<FaceDetection> decode_detector_outputs(const std::vector<DetectorTensor>& tensors,
                                                    const DetectorLayout& layout,
                                                    int input_width, int input_height);

// Lo mismo, pero con las anclas ya generadas (las cachea el adaptador).
std::vector<FaceDetection> decode_detector_outputs(const std::vector<DetectorTensor>& tensors,
                                                    const DetectorLayout& layout,
                                                    const std::vector<Anchor>& anchors);

// Comprueba una salida del detector al arrancar: que dequantize la sepa leer, que traiga
// `por_ancla` valores por cada una de las `anclas` y que los de una misma ancla vayan
// seguidos, es decir, que VIPLite la reporte como [por_ancla, anclas] o
// [por_ancla, anclas, 1, ...] (de la dimension mas rapida a la mas lenta). Con los
// valores ordenados de otra forma ([anclas, por_ancla]) y el mismo numero de elementos,
// el decodificador leeria basura sin fallar.
//
// `nombre` es la salida ("las cajas", "los scores"...), para el mensaje. Lanza
// std::invalid_argument si la salida no vale.
void comprobar_salida_detector(const TensorInfo& info, const char* nombre, std::size_t anclas,
                               std::size_t por_ancla);

// Devuelve las detecciones de la entrada del modelo a la imagen original de
// ancho_imagen x alto_imagen: deshace el letterbox en la caja (recortada a la imagen) y
// en los landmarks (sin recortar: pueden caer fuera de la imagen, como en la demo,
// retinaface_post.cpp:263-285).
void deshacer_letterbox_detecciones(const Letterbox& letterbox,
                                    std::vector<FaceDetection>& detecciones, int ancho_imagen,
                                    int alto_imagen);

}  // namespace vision
