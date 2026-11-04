#pragma once

// Decodificador de RetinaFace en CPU: anclas, cajas, landmarks y NMS.
//
// Son funciones PURAS (sin NPU y sin mas OpenCV que cv::Point/cv::Rect): se prueban
// con tensores sinteticos. El .nb del detector todavia NO existe, asi que la
// disposicion de su salida esta parametrizada (DetectorLayout) y marcada como
// PROVISIONAL: cuando exista el modelo real habra que confirmar los valores.
//
// Lo que se asume del modelo (PROVISIONAL, hasta ver el .nb):
//  - La salida del detector trae, por ancla y en este orden: cx, cy, w, h, score y los
//    10 offsets de los 5 landmarks (5 pares x, y). Los valores de caja y de landmark son
//    "distancias" que se decodifican con las varianzas estandar de RetinaFace
//    (0.1, 0.2, 0.3, 0.3) y la escala del stride.
//  - Las anclas del tensor salen en el mismo orden que generate_anchors(): por stride,
//    luego por fila y columna, y dentro de cada posicion las anclas del stride.
//  - El score es una probabilidad en [0, 1] (la sigmoid esta dentro del modelo).

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

#include <opencv2/core.hpp>

#include "vision/face_detector.hpp"

namespace vision {

// Una ancla del detector: centro y tamano, en pixeles de la imagen de entrada del modelo.
// Lleva el stride del mapa de caracteristicas del que sale (lo necesita la decodificacion).
struct Anchor {
  float cx = 0.0f;
  float cy = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
  int stride = 8;
};

// Disposicion de la salida del detector.
//
// PROVISIONAL: el .nb real todavia no existe y puede obligar a cambiar estos valores
// (sobre todo el reparto de los tensores de salida y el numero de valores por ancla).
// Los valores por omision son los del RetinaFace clasico con entrada de 640.
struct DetectorLayout {
  // Strides de los mapas de caracteristicas: 8, 16 y 32.
  std::vector<int> strides{8, 16, 32};
  // Anclas por posicion: 2 en cada stride.
  std::vector<int> anchors_per_stride{2, 2, 2};
  // Tamano base de las anclas para una entrada de 640: (16,16), (32,32) y (64,64).
  std::vector<std::pair<float, float>> base_anchor_sizes{
      {16.0f, 16.0f}, {32.0f, 32.0f}, {64.0f, 64.0f}};
  // Umbral de IoU de la supresion de no maximos.
  float nms_iou_threshold = 0.4f;
  // Score minimo para considerar una deteccion.
  float score_threshold = 0.5f;

  // Reparto de los tensores de salida del .nb. El tensor de las cajas es el 0; si el
  // .nb expone el score y los landmarks en tensores aparte, se apuntan aqui (y se
  // apaga el flag correspondiente).
  std::size_t boxes_output_index = 0;
  std::size_t scores_output_index = 0;
  std::size_t landmarks_output_index = 0;
  // true si el score va dentro del tensor de cajas (un solo tensor por ancla).
  bool score_in_boxes_tensor = true;
  // true si los 10 offsets de landmarks van dentro del tensor de cajas.
  bool landmarks_in_boxes_tensor = true;
};

// Valores por ancla en el tensor de cajas: 4 de caja, mas el score y los 10 offsets
// de landmarks si van dentro del mismo tensor.
int values_per_anchor(const DetectorLayout& layout);

// Genera las anclas del detector para una entrada de input_width x input_height.
//
// Por cada stride: el mapa de caracteristicas tiene ceil(ancho/stride) x
// ceil(alto/stride) posiciones y el centro de cada ancla es ((x+0.5)*stride,
// (y+0.5)*stride). El tamano base de cada stride se escala con la entrada sobre 640.
// Hay dos anclas por posicion: la cuadrada y la de relacion 1.3 (la misma area, la
// anchura 1.3 veces mayor y la altura 1.3 veces menor).
//
// Lanza std::invalid_argument si los tres vectores del layout no tienen el mismo
// tamano, si un stride no es positivo o si la entrada no es valida.
std::vector<Anchor> generate_anchors(const DetectorLayout& layout, int input_width,
                                      int input_height);

// Decodifica los valores crudos (cx, cy, w, h) de una ancla a una caja en pixeles de la
// imagen de entrada del modelo, con las varianzas estandar de RetinaFace (0.1, 0.2,
// 0.3, 0.3) y la escala del stride.
cv::Rect2f decode_box(const Anchor& anchor, const float* box, int stride);

// Decodifica los 10 offsets de landmarks (5 pares x, y) a 5 puntos en pixeles: cada
// offset se multiplica por el tamano de la caja y se suma al centro de la caja.
// El orden es el de FaceDetection::landmarks: ojo izquierdo, ojo derecho, nariz, boca
// izquierda y boca derecha.
std::array<cv::Point2f, 5> decode_landmarks(const cv::Rect2f& bbox, const float* landmarks);

// IoU de dos cajas (0 si no se tocan).
float iou(const cv::Rect2f& a, const cv::Rect2f& b);

// Supresion de no maximos por IoU. Devuelve los indices conservados, del score mas
// alto al mas bajo.
std::vector<int> nms(const std::vector<cv::Rect2f>& boxes, const std::vector<float>& scores,
                     float iou_threshold);

// Un tensor de salida del detector, ya desquantizado a float.
struct DetectorTensor {
  std::size_t index = 0;
  std::vector<float> values;
};

// Interpreta los tensores de salida del detector segun el layout y devuelve las
// detecciones en pixeles de la imagen de ENTRADA del modelo (el reescalado a la imagen
// original lo hace el adaptador, que es quien conoce el tamano de la original).
//
// Lanza std::invalid_argument si falta el tensor de cajas, si el numero de valores no
// cuadra con el numero de anclas o si un tensor apartado no trae un valor por ancla.
std::vector<FaceDetection> decode_detector_outputs(const std::vector<DetectorTensor>& tensors,
                                                    const DetectorLayout& layout,
                                                    int input_width, int input_height);

}  // namespace vision
