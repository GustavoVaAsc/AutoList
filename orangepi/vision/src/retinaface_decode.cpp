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

// La entrada de referencia del RetinaFace clasico, para la que valen los tamanos base.
constexpr float kEntradaDeReferencia = 640.0f;

}  // namespace

int values_per_anchor(const DetectorLayout& layout) {
  int valores = 4;  // cx, cy, w, h
  if (layout.score_in_boxes_tensor) ++valores;
  if (layout.landmarks_in_boxes_tensor) valores += 10;
  return valores;
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
    const int ancho = (input_width + stride - 1) / stride;
    const int alto = (input_height + stride - 1) / stride;
    const float base_ancho = layout.base_anchor_sizes[i].first * escala_x;
    const float base_alto = layout.base_anchor_sizes[i].second * escala_y;
    for (int y = 0; y < alto; ++y) {
      for (int x = 0; x < ancho; ++x) {
        const float cx = (static_cast<float>(x) + 0.5f) * static_cast<float>(stride);
        const float cy = (static_cast<float>(y) + 0.5f) * static_cast<float>(stride);
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

cv::Rect2f decode_box(const Anchor& anchor, const float* box, int stride) {
  // Las distancias del modelo se escalan con la varianza de RetinaFace y con el stride.
  const float dx = box[0] * kVarBox[0];
  const float dy = box[1] * kVarBox[1];
  const float dw = box[2] * kVarBox[2];
  const float dh = box[3] * kVarBox[3];
  const float escala = static_cast<float>(stride);
  const float cx = anchor.cx + dx * escala;
  const float cy = anchor.cy + dy * escala;
  const float w = anchor.w * std::exp(dw * escala);
  const float h = anchor.h * std::exp(dh * escala);
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

  const std::vector<Anchor> anclas = generate_anchors(layout, input_width, input_height);
  const int valores_por_ancla = values_per_anchor(layout);
  const std::size_t esperados = anclas.size() * static_cast<std::size_t>(valores_por_ancla);
  if (cajas->values.size() != esperados) {
    throw std::invalid_argument("decode_detector_outputs: el tensor de cajas trae " +
                                std::to_string(cajas->values.size()) + " valores y se esperan " +
                                std::to_string(esperados) + " (" + std::to_string(anclas.size()) +
                                " anclas por " + std::to_string(valores_por_ancla) + " valores)");
  }
  if (scores != nullptr && scores->values.size() != anclas.size()) {
    throw std::invalid_argument("decode_detector_outputs: el tensor de scores trae " +
                                std::to_string(scores->values.size()) + " valores y se esperan " +
                                std::to_string(anclas.size()));
  }
  if (landmarks != nullptr && landmarks->values.size() != anclas.size() * 10) {
    throw std::invalid_argument("decode_detector_outputs: el tensor de landmarks trae " +
                                std::to_string(landmarks->values.size()) +
                                " valores y se esperan " + std::to_string(anclas.size() * 10));
  }

  // Recorre las anclas en el mismo orden que las trae el tensor.
  std::vector<cv::Rect2f> cajas_decodificadas;
  std::vector<float> valores_score;
  std::vector<std::array<cv::Point2f, 5>> puntos;
  for (std::size_t i = 0; i < anclas.size(); ++i) {
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

    if (score < layout.score_threshold) continue;

    const cv::Rect2f bbox = decode_box(anclas[i], base, anclas[i].stride);
    cajas_decodificadas.push_back(bbox);
    valores_score.push_back(score);
    puntos.push_back(decode_landmarks(bbox, offsets));
  }

  const std::vector<int> conservados = nms(cajas_decodificadas, valores_score,
                                            layout.nms_iou_threshold);
  std::vector<FaceDetection> detecciones;
  detecciones.reserve(conservados.size());
  for (int indice : conservados) {
    FaceDetection deteccion;
    deteccion.bbox = cajas_decodificadas[indice];
    deteccion.score = valores_score[indice];
    deteccion.landmarks = puntos[indice];
    detecciones.push_back(deteccion);
  }
  return detecciones;
}

}  // namespace vision
