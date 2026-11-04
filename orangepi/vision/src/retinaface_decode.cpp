#include "vision/retinaface_decode.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <string>

namespace vision {

namespace {

// Las varianzas estandar de RetinaFace: el peso de cada distancia (dx, dy, dw, dh).
constexpr float kVarBox[4] = {0.1f, 0.2f, 0.3f, 0.3f};

// Relacion de la segunda ancla de cada posicion (la misma area que la cuadrada).
constexpr float kRatioAncla = 1.3f;

// true si los 10 offsets de landmarks son finitos.
bool landmarks_finitos(const float* offsets) {
  for (std::size_t i = 0; i < 10; ++i) {
    if (!std::isfinite(offsets[i])) return false;
  }
  return true;
}

// La entrada de referencia del RetinaFace clasico, para la que valen los tamanos base.
constexpr float kEntradaDeReferencia = 640.0f;

}  // namespace

int values_per_anchor(const DetectorLayout& layout) {
  int valores = 4;  // cx, cy, w, h
  if (layout.score_in_boxes_tensor) ++valores;
  if (layout.landmarks_in_boxes_tensor) valores += 10;
  return valores;
}

int posiciones_mapa(int tamano, int stride) {
  // El techo de tamano/stride, sin sumar antes de dividir: con un tamano cercano a
  // INT_MAX, "tamano + stride - 1" desborda con signo.
  if (tamano <= 0 || stride <= 0) {
    throw std::invalid_argument("posiciones_mapa: el tamano " + std::to_string(tamano) +
                                " o el stride " + std::to_string(stride) + " no son validos");
  }
  return tamano / stride + (tamano % stride == 0 ? 0 : 1);
}

std::vector<Anchor> generate_anchors(const DetectorLayout& layout, int input_width,
                                      int input_height) {
  if (layout.strides.size() != layout.anchors_per_stride.size() ||
      layout.strides.size() != layout.base_anchor_sizes.size()) {
    throw std::invalid_argument(
        "generate_anchors: strides, anchors_per_stride y base_anchor_sizes deben tener el "
        "mismo tamaño");
  }
  if (input_width <= 0 || input_height <= 0) {
    throw std::invalid_argument("generate_anchors: la entrada " + std::to_string(input_width) +
                                "x" + std::to_string(input_height) + " no es valida");
  }
  const float escala_x = static_cast<float>(input_width) / kEntradaDeReferencia;
  const float escala_y = static_cast<float>(input_height) / kEntradaDeReferencia;

  std::vector<Anchor> anclas;
  for (std::size_t i = 0; i < layout.strides.size(); ++i) {
    const int stride = layout.strides[i];
    if (stride <= 0) {
      throw std::invalid_argument("generate_anchors: el stride " + std::to_string(stride) +
                                  " no es valido");
    }
    const int anclas_por_posicion = layout.anchors_per_stride[i];
    const int ancho = posiciones_mapa(input_width, stride);
    const int alto = posiciones_mapa(input_height, stride);
    const float base_ancho = layout.base_anchor_sizes[i].first * escala_x;
    const float base_alto = layout.base_anchor_sizes[i].second * escala_y;
    for (int y = 0; y < alto; ++y) {
      for (int x = 0; x < ancho; ++x) {
        // El centro del ancla cae en la esquina de la celda (la convencion clasica de
        // RetinaFace): el stride solo fija la rejilla de posiciones.
        const float cx = static_cast<float>(x * stride);
        const float cy = static_cast<float>(y * stride);
        for (int a = 0; a < anclas_por_posicion; ++a) {
          Anchor ancla;
          ancla.cx = cx;
          ancla.cy = cy;
          ancla.stride = stride;
          if (a % 2 == 0) {
            // Ancla cuadrada: el tamano base tal cual.
            ancla.w = base_ancho;
            ancla.h = base_alto;
          } else {
            // Ancla de relacion 1.3: la misma area, mas ancha y mas baja.
            ancla.w = base_ancho * kRatioAncla;
            ancla.h = base_alto / kRatioAncla;
          }
          anclas.push_back(ancla);
        }
      }
    }
  }
  return anclas;
}

cv::Rect2f decode_box(const Anchor& anchor, const float* box) {
  // Convencion clasica de RetinaFace (la de insightface): las varianzas se aplican al
  // valor predicho, que ya esta en unidades de la imagen. El stride NO multiplica: el
  // stride solo fija la rejilla de posiciones y el tamano del ancla.
  //
  // PROVISIONAL: hay que contrastar esta convencion con el .nb real cuando exista.
  for (int i = 0; i < 4; ++i) {
    if (!std::isfinite(box[i])) {
      throw std::invalid_argument(
          "decode_box: la salida del modelo trae un valor no finito en la caja");
    }
  }
  const float dx = box[0] * kVarBox[0];
  const float dy = box[1] * kVarBox[1];
  const float dw = box[2] * kVarBox[2];
  const float dh = box[3] * kVarBox[3];
  const float cx = anchor.cx + dx;
  const float cy = anchor.cy + dy;
  const float w = anchor.w * std::exp(dw);
  const float h = anchor.h * std::exp(dh);
  return cv::Rect2f(cx - w / 2.0f, cy - h / 2.0f, w, h);
}

std::array<cv::Point2f, 5> decode_landmarks(const cv::Rect2f& bbox, const float* landmarks) {
  const float cx = bbox.x + bbox.width / 2.0f;
  const float cy = bbox.y + bbox.height / 2.0f;
  std::array<cv::Point2f, 5> puntos{};
  for (std::size_t i = 0; i < puntos.size(); ++i) {
    puntos[i] = cv::Point2f(cx + landmarks[2 * i] * bbox.width,
                            cy + landmarks[2 * i + 1] * bbox.height);
  }
  return puntos;
}

float iou(const cv::Rect2f& a, const cv::Rect2f& b) {
  const float x1 = std::max(a.x, b.x);
  const float y1 = std::max(a.y, b.y);
  const float x2 = std::min(a.x + a.width, b.x + b.width);
  const float y2 = std::min(a.y + a.height, b.y + b.height);
  const float ancho = x2 - x1;
  const float alto = y2 - y1;
  if (ancho <= 0.0f || alto <= 0.0f) return 0.0f;
  const float interseccion = ancho * alto;
  const float area_a = a.width * a.height;
  const float area_b = b.width * b.height;
  const float union_ = area_a + area_b - interseccion;
  if (union_ <= 0.0f) return 0.0f;
  return interseccion / union_;
}

std::vector<int> nms(const std::vector<cv::Rect2f>& boxes, const std::vector<float>& scores,
                     float iou_threshold) {
  if (boxes.size() != scores.size()) {
    throw std::invalid_argument("nms: hay " + std::to_string(boxes.size()) + " cajas y " +
                                std::to_string(scores.size()) + " scores");
  }
  if (!(iou_threshold >= 0.0f && iou_threshold <= 1.0f)) {
    throw std::invalid_argument("nms: el umbral de IoU " + std::to_string(iou_threshold) +
                                " esta fuera de [0, 1]");
  }
  // Un score no finito haria que el comparador del orden no sea una relacion de orden
  // estricto debil (comportamiento indefinido segun el estandar): se rechaza antes.
  for (std::size_t i = 0; i < scores.size(); ++i) {
    if (!std::isfinite(scores[i])) {
      throw std::invalid_argument("nms: el score " + std::to_string(i) +
                                  " no es finito (NaN o infinito)");
    }
  }
  std::vector<int> indices(boxes.size());
  std::iota(indices.begin(), indices.end(), 0);
  // De mayor a menor score; a igualdad de score, el indice menor va primero (estable).
  std::stable_sort(indices.begin(), indices.end(), [&scores](int a, int b) {
    return scores[a] > scores[b];
  });

  std::vector<bool> descartada(boxes.size(), false);
  std::vector<int> conservados;
  for (std::size_t i = 0; i < indices.size(); ++i) {
    const int actual = indices[i];
    if (descartada[actual]) continue;
    conservados.push_back(actual);
    for (std::size_t j = i + 1; j < indices.size(); ++j) {
      const int otro = indices[j];
      if (descartada[otro]) continue;
      if (iou(boxes[actual], boxes[otro]) > iou_threshold) descartada[otro] = true;
    }
  }
  return conservados;
}

std::vector<FaceDetection> decode_detector_outputs(const std::vector<DetectorTensor>& tensors,
                                                    const DetectorLayout& layout,
                                                    int input_width, int input_height) {
  return decode_detector_outputs(tensors, layout,
                                  generate_anchors(layout, input_width, input_height));
}

std::vector<FaceDetection> decode_detector_outputs(const std::vector<DetectorTensor>& tensors,
                                                    const DetectorLayout& layout,
                                                    const std::vector<Anchor>& anchors) {
  const DetectorTensor* cajas = nullptr;
  const DetectorTensor* scores = nullptr;
  const DetectorTensor* landmarks = nullptr;
  for (const DetectorTensor& tensor : tensors) {
    if (tensor.index == layout.boxes_output_index && cajas == nullptr) cajas = &tensor;
    if (!layout.score_in_boxes_tensor && tensor.index == layout.scores_output_index &&
        scores == nullptr) {
      scores = &tensor;
    }
    if (!layout.landmarks_in_boxes_tensor && tensor.index == layout.landmarks_output_index &&
        landmarks == nullptr) {
      landmarks = &tensor;
    }
  }
  if (cajas == nullptr) {
    throw std::invalid_argument("decode_detector_outputs: falta el tensor de salida " +
                                std::to_string(layout.boxes_output_index));
  }
  // Los tensores que el layout pide aparte tienen que venir en la lista: sin esta
  // comprobacion el puntero queda en nullptr y la lectura es comportamiento
  // indefinido (un SIGSEGV en pleno fotograma, no una excepcion).
  if (!layout.score_in_boxes_tensor && scores == nullptr) {
    throw std::invalid_argument("decode_detector_outputs: el layout pide el score en un "
                                "tensor aparte (salida " +
                                std::to_string(layout.scores_output_index) +
                                ") y ese tensor no viene");
  }
  if (!layout.landmarks_in_boxes_tensor && landmarks == nullptr) {
    throw std::invalid_argument("decode_detector_outputs: el layout pide los landmarks en "
                                "un tensor aparte (salida " +
                                std::to_string(layout.landmarks_output_index) +
                                ") y ese tensor no viene");
  }

  const int valores_por_ancla = values_per_anchor(layout);
  const std::size_t esperados = anchors.size() * static_cast<std::size_t>(valores_por_ancla);
  if (cajas->values.size() != esperados) {
    throw std::invalid_argument("decode_detector_outputs: el tensor de cajas trae " +
                                std::to_string(cajas->values.size()) + " valores y se esperan " +
                                std::to_string(esperados) + " (" + std::to_string(anchors.size()) +
                                " anclas por " + std::to_string(valores_por_ancla) + " valores)");
  }
  if (scores != nullptr && scores->values.size() != anchors.size()) {
    throw std::invalid_argument("decode_detector_outputs: el tensor de scores trae " +
                                std::to_string(scores->values.size()) + " valores y se esperan " +
                                std::to_string(anchors.size()));
  }
  if (landmarks != nullptr && landmarks->values.size() != anchors.size() * 10) {
    throw std::invalid_argument("decode_detector_outputs: el tensor de landmarks trae " +
                                std::to_string(landmarks->values.size()) +
                                " valores y se esperan " + std::to_string(anchors.size() * 10));
  }

  // Recorre las anclas en el mismo orden que las trae el tensor.
  std::vector<cv::Rect2f> cajas_decodificadas;
  std::vector<float> valores_score;
  std::vector<std::array<cv::Point2f, 5>> puntos;
  for (std::size_t i = 0; i < anchors.size(); ++i) {
    const float* base = cajas->values.data() + i * static_cast<std::size_t>(valores_por_ancla);
    float score = 0.0f;
    const float* offsets = nullptr;
    if (layout.score_in_boxes_tensor) {
      score = base[4];
    } else {
      score = scores->values[i];
    }
    if (layout.landmarks_in_boxes_tensor) {
      offsets = base + (layout.score_in_boxes_tensor ? 5 : 4);
    } else {
      offsets = landmarks->values.data() + i * 10;
    }

    // Un score no finito (NaN) pasa el filtro "score < umbral" (la comparacion con NaN
    // es falsa) y llegaria al NMS con un comparador que no es una relacion de orden
    // estricto debil: se descarta el ancla.
    if (!std::isfinite(score)) continue;
    if (score < layout.score_threshold) continue;
    // Los offsets de landmarks tambien tienen que ser finitos: si no, el punto sale
    // NaN y align_face lanza en cada fotograma.
    if (!landmarks_finitos(offsets)) continue;

    const cv::Rect2f bbox = decode_box(anchors[i], base);
    cajas_decodificadas.push_back(bbox);
    valores_score.push_back(score);
    puntos.push_back(decode_landmarks(bbox, offsets));
  }

  // Preseleccion: al NMS solo pasan las K cajas de mayor score (K = 500 por omision,
  // como el "nms top 500" del RetinaFace de referencia). Sin este tope, el NMS seria
  // O(n^2) sobre las 16 800 anclas de una entrada de 640.
  std::vector<int> orden(cajas_decodificadas.size());
  std::iota(orden.begin(), orden.end(), 0);
  const std::size_t tope = std::min<std::size_t>(layout.nms_top_k, orden.size());
  std::partial_sort(orden.begin(), orden.begin() + static_cast<std::ptrdiff_t>(tope),
                    orden.end(), [&valores_score](int a, int b) {
                      if (valores_score[a] != valores_score[b]) {
                        return valores_score[a] > valores_score[b];
                      }
                      return a < b;
                    });
  std::vector<cv::Rect2f> cajas_top;
  std::vector<float> scores_top;
  cajas_top.reserve(tope);
  scores_top.reserve(tope);
  for (std::size_t i = 0; i < tope; ++i) {
    const int indice = orden[i];
    cajas_top.push_back(cajas_decodificadas[indice]);
    scores_top.push_back(valores_score[indice]);
  }

  const std::vector<int> conservados = nms(cajas_top, scores_top, layout.nms_iou_threshold);
  std::vector<FaceDetection> detecciones;
  detecciones.reserve(conservados.size());
  for (int indice : conservados) {
    const std::size_t original = orden[static_cast<std::size_t>(indice)];
    FaceDetection deteccion;
    deteccion.bbox = cajas_decodificadas[original];
    deteccion.score = valores_score[original];
    deteccion.landmarks = puntos[original];
    detecciones.push_back(deteccion);
  }
  return detecciones;
}

}  // namespace vision
