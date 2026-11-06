#include "vision/retinaface_decode.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <string>

namespace vision {

namespace {

// La precondicion comun de decode_box y decode_landmarks (ver retinaface_decode.hpp):
// los `n` valores crudos son finitos. La usan las dos (para lanzar) y el pipeline (para
// descartar el ancla antes de llamarlas).
bool valores_finitos(const float* valores, std::size_t n) {
  for (std::size_t i = 0; i < n; ++i) {
    if (!std::isfinite(valores[i])) return false;
  }
  return true;
}

// true si la caja decodificada es finita: un valor crudo finito puede desbordar al
// decodificar (exp(1000 * 0.2) es infinito en float).
bool caja_decodificada_finita(const cv::Rect2f& caja) {
  return std::isfinite(caja.x) && std::isfinite(caja.y) && std::isfinite(caja.width) &&
         std::isfinite(caja.height);
}

// Lo mismo con los landmarks decodificados.
bool puntos_finitos(const std::array<cv::Point2f, 5>& puntos) {
  for (const cv::Point2f& punto : puntos) {
    if (!std::isfinite(punto.x) || !std::isfinite(punto.y)) return false;
  }
  return true;
}

// El score de cara es la salida de un softmax: tiene que estar en (umbral, 1]. Escrito
// asi, un NaN tampoco pasa (la comparacion con NaN es falsa), ni un infinito.
bool score_valido(float score, float umbral) {
  return score > umbral && score <= 1.0f;
}

// El tensor tiene que traer `por_ancla` valores por cada una de las `anclas`.
void comprobar_valores(const DetectorTensor& tensor, const char* nombre, std::size_t anclas,
                       std::size_t por_ancla) {
  const std::size_t esperados = anclas * por_ancla;
  if (tensor.values.size() != esperados) {
    throw std::invalid_argument("decode_detector_outputs: el tensor de " + std::string(nombre) +
                                " (salida " + std::to_string(tensor.index) + ") trae " +
                                std::to_string(tensor.values.size()) + " valores y se esperan " +
                                std::to_string(esperados) + " (" + std::to_string(anclas) +
                                " anclas por " + std::to_string(por_ancla) + " valores)");
  }
}

}  // namespace

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
  if (layout.strides.size() != layout.anchor_sizes.size()) {
    throw std::invalid_argument(
        "generate_anchors: strides y anchor_sizes deben tener el mismo tamano");
  }
  if (input_width <= 0 || input_height <= 0) {
    throw std::invalid_argument("generate_anchors: la entrada " + std::to_string(input_width) +
                                "x" + std::to_string(input_height) + " no es valida");
  }
  std::vector<Anchor> anclas;
  for (std::size_t i = 0; i < layout.strides.size(); ++i) {
    const int stride = layout.strides[i];
    if (stride <= 0) {
      throw std::invalid_argument("generate_anchors: el stride " + std::to_string(stride) +
                                  " no es valido");
    }
    const std::vector<float>& tamanos = layout.anchor_sizes[i];
    if (tamanos.empty()) {
      throw std::invalid_argument("generate_anchors: el stride " + std::to_string(stride) +
                                  " no tiene tamanos de ancla");
    }
    for (const float tamano : tamanos) {
      if (!(tamano > 0.0f) || !std::isfinite(tamano)) {
        throw std::invalid_argument("generate_anchors: un tamano de ancla del stride " +
                                    std::to_string(stride) + " no es positivo");
      }
    }
    const int filas = posiciones_mapa(input_height, stride);
    const int columnas = posiciones_mapa(input_width, stride);
    const float paso = static_cast<float>(stride);
    // El orden del tensor: fila (j), columna (k) y tamano (retinaface_post.cpp:146-158).
    for (int j = 0; j < filas; ++j) {
      for (int k = 0; k < columnas; ++k) {
        // El centro del ancla cae en el medio de la celda.
        const float cx = (static_cast<float>(k) + 0.5f) * paso;
        const float cy = (static_cast<float>(j) + 0.5f) * paso;
        for (const float tamano : tamanos) {
          Anchor ancla;
          ancla.cx = cx;
          ancla.cy = cy;
          ancla.w = tamano;
          ancla.h = tamano;
          ancla.stride = stride;
          anclas.push_back(ancla);
        }
      }
    }
  }
  return anclas;
}

cv::Rect2f decode_box(const Anchor& anchor, const float* box) {
  if (!valores_finitos(box, kValoresCaja)) {
    throw std::invalid_argument(
        "decode_box: la salida del modelo trae un valor no finito en la caja");
  }
  const float cx = anchor.cx + box[0] * kVarianzaCentro * anchor.w;
  const float cy = anchor.cy + box[1] * kVarianzaCentro * anchor.h;
  const float w = anchor.w * std::exp(box[2] * kVarianzaTamano);
  const float h = anchor.h * std::exp(box[3] * kVarianzaTamano);
  return cv::Rect2f(cx - w * 0.5f, cy - h * 0.5f, w, h);
}

std::array<cv::Point2f, 5> decode_landmarks(const Anchor& anchor, const float* landmarks) {
  if (!valores_finitos(landmarks, kValoresLandmarks)) {
    throw std::invalid_argument(
        "decode_landmarks: la salida del modelo trae un valor no finito en los landmarks");
  }
  std::array<cv::Point2f, 5> puntos{};
  for (std::size_t i = 0; i < puntos.size(); ++i) {
    puntos[i] = cv::Point2f(anchor.cx + landmarks[2 * i] * kVarianzaCentro * anchor.w,
                            anchor.cy + landmarks[2 * i + 1] * kVarianzaCentro * anchor.h);
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
    if (tensor.index == layout.scores_output_index && scores == nullptr) scores = &tensor;
    if (tensor.index == layout.landmarks_output_index && landmarks == nullptr) {
      landmarks = &tensor;
    }
  }
  // Los tres tensores tienen que venir: sin esta comprobacion el puntero queda en
  // nullptr y la lectura es comportamiento indefinido (un SIGSEGV en pleno fotograma,
  // no una excepcion).
  if (cajas == nullptr || scores == nullptr || landmarks == nullptr) {
    throw std::invalid_argument(
        "decode_detector_outputs: faltan tensores de salida: se necesitan las cajas (salida " +
        std::to_string(layout.boxes_output_index) + "), los scores (salida " +
        std::to_string(layout.scores_output_index) + ") y los landmarks (salida " +
        std::to_string(layout.landmarks_output_index) + ")");
  }
  comprobar_valores(*cajas, "cajas", anchors.size(), kValoresCaja);
  comprobar_valores(*scores, "scores", anchors.size(), kValoresScore);
  comprobar_valores(*landmarks, "landmarks", anchors.size(), kValoresLandmarks);

  // Recorre las anclas en el mismo orden que las trae el tensor. Con una cara pasan el
  // umbral unas decenas de anclas: la reserva cubre el tope del NMS sin realojar.
  const std::size_t reserva = std::min(anchors.size(), layout.nms_top_k);
  std::vector<cv::Rect2f> cajas_decodificadas;
  std::vector<float> valores_score;
  std::vector<std::array<cv::Point2f, 5>> puntos;
  cajas_decodificadas.reserve(reserva);
  valores_score.reserve(reserva);
  puntos.reserve(reserva);
  for (std::size_t i = 0; i < anchors.size(); ++i) {
    const float* caja = cajas->values.data() + i * kValoresCaja;
    const float score = scores->values[i * kValoresScore + kColumnaCara];
    const float* offsets = landmarks->values.data() + i * kValoresLandmarks;

    // "score > umbral", como la demo (retinaface_post.cpp:200), y como mucho 1 (la
    // salida de un softmax; la demo no dibuja las que pasan de 1, retinaface_post.cpp:
    // 317-321). Un NaN o un infinito llegarian al NMS con un comparador que no es una
    // relacion de orden estricto debil.
    if (!score_valido(score, layout.score_threshold)) continue;
    // La precondicion de decode_box y decode_landmarks: si no se cumple, se descarta el
    // ancla en vez de abortar el fotograma entero (un NaN en un landmark haria ademas
    // que align_face lanzara).
    if (!valores_finitos(caja, kValoresCaja) || !valores_finitos(offsets, kValoresLandmarks)) {
      continue;
    }
    const cv::Rect2f bbox = decode_box(anchors[i], caja);
    const std::array<cv::Point2f, 5> puntos_ancla = decode_landmarks(anchors[i], offsets);
    // Un valor finito pero enorme desborda al decodificar: tambien se descarta.
    if (!caja_decodificada_finita(bbox) || !puntos_finitos(puntos_ancla)) continue;

    cajas_decodificadas.push_back(bbox);
    valores_score.push_back(score);
    puntos.push_back(puntos_ancla);
  }

  // Preseleccion: al NMS solo pasan las K cajas de mayor score (K = 500 por omision).
  // Sin este tope, el NMS seria O(n^2) sobre todas las anclas que pasen el umbral.
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

void comprobar_salida_detector(const TensorInfo& info, const char* nombre, std::size_t anclas,
                               std::size_t por_ancla) {
  const std::string prefijo = std::string("NpuFaceDetector: la salida de ") + nombre + " (" +
                              info.name + ", " + texto_dims(info.dims) + ")";
  exigir_salida_descuantizable(info, "NpuFaceDetector");
  if (info.element_count != anclas * por_ancla) {
    throw std::invalid_argument(prefijo + " trae " + std::to_string(info.element_count) +
                                " elementos y el layout espera " +
                                std::to_string(anclas * por_ancla) + " (" +
                                std::to_string(anclas) + " anclas por " +
                                std::to_string(por_ancla) + " valores)");
  }
  bool disposicion_ok = info.dims.size() >= 2 && info.dims[0] == por_ancla &&
                        info.dims[1] == anclas;
  for (std::size_t i = 2; disposicion_ok && i < info.dims.size(); ++i) {
    if (info.dims[i] != 1) disposicion_ok = false;
  }
  if (!disposicion_ok) {
    throw std::invalid_argument(prefijo + " no tiene la forma [" + std::to_string(por_ancla) +
                                ", " + std::to_string(anclas) +
                                ", 1]: los valores de cada ancla tienen que ir seguidos");
  }
}

void deshacer_letterbox_detecciones(const Letterbox& letterbox,
                                    std::vector<FaceDetection>& detecciones, int ancho_imagen,
                                    int alto_imagen) {
  for (FaceDetection& deteccion : detecciones) {
    deteccion.bbox = deshacer_letterbox(letterbox, deteccion.bbox, ancho_imagen, alto_imagen);
    for (cv::Point2f& punto : deteccion.landmarks) {
      punto = deshacer_letterbox(letterbox, punto);
    }
  }
}

}  // namespace vision
