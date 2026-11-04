// Pruebas del decodificador de RetinaFace: son funciones puras, asi que se prueban
// con tensores sinteticos, sin NPU ni modelo.
#include <chrono>
#include <cmath>
#include <climits>
#include <iostream>
#include <limits>
#include <vector>

#include "minitest.hpp"
#include "vision/npu_preprocess.hpp"
#include "vision/retinaface_decode.hpp"

namespace {

using namespace vision;

// El numero de anclas del RetinaFace clasico con una entrada de 640:
// (80*80 + 40*40 + 20*20) posiciones por 2 anclas.
constexpr std::size_t kAnclas640 = (80 * 80 + 40 * 40 + 20 * 20) * 2;
// El bloque del stride 8 son 80*80 posiciones por 2 anclas.
constexpr std::size_t kBloque8 = 80 * 80 * 2;
constexpr std::size_t kBloque16 = 40 * 40 * 2;

}  // namespace

TEST(las_anclas_de_640_tienen_el_numero_y_las_posiciones_del_retinaface_clasico) {
  const DetectorLayout layout;
  const std::vector<Anchor> anclas = generate_anchors(layout, 640, 640);
  CHECK_EQ(anclas.size(), kAnclas640);

  // La primera: stride 8, posicion (0,0), ancla cuadrada de 16x16 centrada en (0,0) (el
  // centro del ancla cae en la esquina de la celda).
  CHECK_EQ(anclas[0].stride, 8);
  CHECK_NEAR(anclas[0].cx, 0.0, 1e-6);
  CHECK_NEAR(anclas[0].cy, 0.0, 1e-6);
  CHECK_NEAR(anclas[0].w, 16.0, 1e-6);
  CHECK_NEAR(anclas[0].h, 16.0, 1e-6);

  // La segunda: la de relacion 1.3 (la misma area, mas ancha y mas baja).
  CHECK_NEAR(anclas[1].cx, 0.0, 1e-6);
  CHECK_NEAR(anclas[1].w, 16.0 * 1.3, 1e-6);
  CHECK_NEAR(anclas[1].h, 16.0 / 1.3, 1e-6);

  // La tercera: la posicion (1,0) del mismo stride (centro 8).
  CHECK_NEAR(anclas[2].cx, 8.0, 1e-6);
  CHECK_NEAR(anclas[2].cy, 0.0, 1e-6);

  // El primer bloque es el del stride 8; luego el del 16 (ancho 32) y el del 32 (64).
  CHECK_EQ(anclas[kBloque8].stride, 16);
  CHECK_NEAR(anclas[kBloque8].cx, 0.0, 1e-6);
  CHECK_NEAR(anclas[kBloque8].w, 32.0, 1e-6);
  CHECK_NEAR(anclas[kBloque8].h, 32.0, 1e-6);

  CHECK_EQ(anclas[kBloque8 + kBloque16].stride, 32);
  CHECK_NEAR(anclas[kBloque8 + kBloque16].cx, 0.0, 1e-6);
  CHECK_NEAR(anclas[kBloque8 + kBloque16].w, 64.0, 1e-6);
}

TEST(las_anclas_se_escalan_con_el_tamano_de_la_entrada) {
  const DetectorLayout layout;
  const std::vector<Anchor> anclas = generate_anchors(layout, 320, 320);
  CHECK_EQ(anclas.size(), kAnclas640 / 4);  // la mitad de ancho y de alto
  // Los tamanos base bajan a la mitad y los centros se mantienen en el mapa.
  CHECK_NEAR(anclas[0].w, 8.0, 1e-6);
  CHECK_NEAR(anclas[0].h, 8.0, 1e-6);
  CHECK_NEAR(anclas[0].cx, 0.0, 1e-6);
}

TEST(el_generador_de_anclas_rechaza_un_layout_incoherente) {
  DetectorLayout layout;
  layout.anchors_per_stride.push_back(2);
  CHECK_THROWS(generate_anchors(layout, 640, 640), std::invalid_argument);
  CHECK_THROWS(generate_anchors(DetectorLayout(), 0, 640), std::invalid_argument);
}

// El numero de posiciones se calcula sin sumar antes de dividir: con un tamano cercano
// a INT_MAX, "tamano + stride - 1" desborda con signo.
TEST(el_numero_de_posiciones_no_desborda_con_un_tamano_enorme) {
  CHECK_EQ(posiciones_mapa(640, 8), 80);
  CHECK_EQ(posiciones_mapa(640, 16), 40);
  CHECK_EQ(posiciones_mapa(641, 8), 81);
  CHECK_EQ(posiciones_mapa(INT_MAX, 8), 268435456);
  CHECK(posiciones_mapa(INT_MAX, 8) > 0);
  CHECK_THROWS(posiciones_mapa(0, 8), std::invalid_argument);
  CHECK_THROWS(posiciones_mapa(640, 0), std::invalid_argument);
}

// La convencion clasica de RetinaFace: las varianzas se aplican al valor predicho (que
// ya esta en unidades de la imagen) y el stride NO multiplica.
TEST(la_decodificacion_de_cajas_aplica_las_varianzas_de_retinaface) {
  const Anchor ancla{0.0f, 0.0f, 16.0f, 16.0f, 8};

  // Sin desplazamiento: la caja es la del ancla.
  const float sin_desplazamiento[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  const cv::Rect2f caja = decode_box(ancla, sin_desplazamiento);
  CHECK_NEAR(caja.x, -8.0, 1e-6);
  CHECK_NEAR(caja.y, -8.0, 1e-6);
  CHECK_NEAR(caja.width, 16.0, 1e-6);
  CHECK_NEAR(caja.height, 16.0, 1e-6);

  // Con 1 en las cuatro salidas: dx = 0.1, dy = 0.2, dw = dh = 0.3. El centro se mueve
  // 0.1 y 0.2 pixeles (no 0.1 * stride) y el tamano escala por exp(0.3).
  const float desplazamientos[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  const cv::Rect2f movida = decode_box(ancla, desplazamientos);
  CHECK_NEAR(movida.x + movida.width / 2.0, 0.0 + 0.1, 1e-4);
  CHECK_NEAR(movida.y + movida.height / 2.0, 0.0 + 0.2, 1e-4);
  CHECK_NEAR(movida.width, 16.0 * std::exp(0.3), 1e-3);
  CHECK_NEAR(movida.height, 16.0 * std::exp(0.3), 1e-3);
}

TEST(la_decodificacion_de_cajas_rechaza_un_valor_no_finito) {
  const Anchor ancla{0.0f, 0.0f, 16.0f, 16.0f, 8};
  const float con_nan[4] = {std::nanf(""), 0.0f, 0.0f, 0.0f};
  CHECK_THROWS(decode_box(ancla, con_nan), std::invalid_argument);
}

TEST(la_decodificacion_de_landmarks_escala_con_el_tamano_de_la_caja) {
  const cv::Rect2f caja(0.0f, 0.0f, 10.0f, 20.0f);
  const float offsets[10] = {0.1f, 0.2f, -0.1f, 0.0f, 0.0f, 0.5f, 0.25f, -0.25f, 0.0f, 0.1f};
  const std::array<cv::Point2f, 5> puntos = decode_landmarks(caja, offsets);

  // El centro de la caja es (5,10); cada offset se multiplica por el tamano de la caja.
  CHECK_NEAR(puntos[0].x, 5.0 + 0.1 * 10.0, 1e-6);
  CHECK_NEAR(puntos[0].y, 10.0 + 0.2 * 20.0, 1e-6);
  CHECK_NEAR(puntos[1].x, 5.0 - 0.1 * 10.0, 1e-6);
  CHECK_NEAR(puntos[1].y, 10.0 + 0.0 * 20.0, 1e-6);
  CHECK_NEAR(puntos[2].x, 5.0, 1e-6);
  CHECK_NEAR(puntos[2].y, 10.0 + 0.5 * 20.0, 1e-6);
  CHECK_NEAR(puntos[3].x, 5.0 + 0.25 * 10.0, 1e-6);
  CHECK_NEAR(puntos[3].y, 10.0 - 0.25 * 20.0, 1e-6);
  CHECK_NEAR(puntos[4].x, 5.0 + 0.0 * 10.0, 1e-6);
  CHECK_NEAR(puntos[4].y, 10.0 + 0.1 * 20.0, 1e-6);
}

TEST(el_nms_funde_las_cajas_solapadas_y_conserva_las_separadas) {
  const std::vector<cv::Rect2f> cajas = {
      cv::Rect2f(0.0f, 0.0f, 10.0f, 10.0f),
      cv::Rect2f(1.0f, 1.0f, 10.0f, 10.0f),    // solapada con la anterior (IoU ~0.68)
      cv::Rect2f(100.0f, 100.0f, 10.0f, 10.0f),  // separada
  };
  const std::vector<float> scores = {0.9f, 0.8f, 0.7f};

  const std::vector<int> conservados = nms(cajas, scores, 0.4f);
  CHECK_EQ(conservados.size(), std::size_t{2});
  CHECK_EQ(conservados[0], 0);  // la de mayor score
  CHECK_EQ(conservados[1], 2);

  // Con un umbral alto las dos cajas solapadas sobreviven.
  const std::vector<int> sin_fusion = nms(cajas, scores, 0.9f);
  CHECK_EQ(sin_fusion.size(), std::size_t{3});
}

// El NMS es una API publica: si no hay un score por caja, el comparador leeria fuera de
// rango, y un umbral de IoU fuera de [0,1] no tiene sentido.
TEST(el_nms_rechaza_que_falten_scores_y_un_umbral_fuera_de_rango) {
  const std::vector<cv::Rect2f> cajas = {cv::Rect2f(0.0f, 0.0f, 10.0f, 10.0f)};
  CHECK_THROWS(nms(cajas, std::vector<float>(), 0.4f), std::invalid_argument);
  CHECK_THROWS(nms(cajas, std::vector<float>{0.5f, 0.6f}, 0.4f), std::invalid_argument);
  CHECK_THROWS(nms(cajas, std::vector<float>{0.5f}, -0.1f), std::invalid_argument);
  CHECK_THROWS(nms(cajas, std::vector<float>{0.5f}, 1.1f), std::invalid_argument);
}

TEST(el_pipeline_completo_devuelve_las_detecciones_sobre_el_umbral) {
  DetectorLayout layout;
  layout.score_threshold = 0.5f;
  const std::vector<Anchor> anclas = generate_anchors(layout, 640, 640);
  const int valores = values_per_anchor(layout);
  CHECK_EQ(valores, 15);

  // Tensor sintetico: todo a cero (sin score) y dos detecciones.
  std::vector<float> tensor(anclas.size() * static_cast<std::size_t>(valores), 0.0f);
  tensor[4] = 0.9f;   // score de la primera ancla
  tensor[100 * valores + 4] = 0.8f;  // score de la ancla 100 (posicion 50 del stride 8)

  const std::vector<DetectorTensor> tensores = {DetectorTensor{0, tensor}};
  const std::vector<FaceDetection> detecciones =
      decode_detector_outputs(tensores, layout, 640, 640);
  CHECK_EQ(detecciones.size(), std::size_t{2});

  // La de mayor score va primero.
  CHECK_NEAR(detecciones[0].score, 0.9, 1e-6);
  // La primera ancla esta en la esquina (0,0) y mide 16x16.
  CHECK_NEAR(detecciones[0].bbox.x, -8.0, 1e-4);
  CHECK_NEAR(detecciones[0].bbox.y, -8.0, 1e-4);
  CHECK_NEAR(detecciones[0].bbox.width, 16.0, 1e-4);
  CHECK_NEAR(detecciones[0].bbox.height, 16.0, 1e-4);

  CHECK_NEAR(detecciones[1].score, 0.8, 1e-6);
  // La ancla 100 es la posicion (50,0) del stride 8: centro (400,0).
  CHECK_NEAR(detecciones[1].bbox.x, 400.0 - 8.0, 1e-4);
  CHECK_NEAR(detecciones[1].bbox.y, -8.0, 1e-4);

  // Los landmarks van en el orden de FaceDetection::landmarks.
  CHECK_EQ(detecciones[0].landmarks.size(), std::size_t{5});
  CHECK_NEAR(detecciones[0].landmarks[0].x, 0.0, 1e-4);
  CHECK_NEAR(detecciones[0].landmarks[0].y, 0.0, 1e-4);
}

TEST(el_pipeline_descarta_lo_que_no_llega_al_umbral_de_score) {
  DetectorLayout layout;
  layout.score_threshold = 0.5f;
  const std::vector<Anchor> anclas = generate_anchors(layout, 640, 640);
  const int valores = values_per_anchor(layout);
  std::vector<float> tensor(anclas.size() * static_cast<std::size_t>(valores), 0.0f);
  tensor[4] = 0.49f;  // por debajo del umbral

  const std::vector<DetectorTensor> tensores = {DetectorTensor{0, tensor}};
  CHECK_EQ(decode_detector_outputs(tensores, layout, 640, 640).size(), std::size_t{0});
}

TEST(el_pipeline_acepta_el_score_y_los_landmarks_en_tensores_apartados) {
  DetectorLayout layout;
  layout.score_threshold = 0.5f;
  layout.score_in_boxes_tensor = false;
  layout.landmarks_in_boxes_tensor = false;
  layout.scores_output_index = 1;
  layout.landmarks_output_index = 2;
  CHECK_EQ(values_per_anchor(layout), 4);

  const std::vector<Anchor> anclas = generate_anchors(layout, 640, 640);
  std::vector<float> cajas(anclas.size() * 4, 0.0f);
  std::vector<float> scores(anclas.size(), 0.0f);
  std::vector<float> landmarks(anclas.size() * 10, 0.0f);
  scores[3] = 0.77f;
  landmarks[3 * 10] = 0.25f;  // el desplazamiento x del primer landmark

  const std::vector<DetectorTensor> tensores = {
      DetectorTensor{0, cajas}, DetectorTensor{1, scores}, DetectorTensor{2, landmarks}};
  const std::vector<FaceDetection> detecciones =
      decode_detector_outputs(tensores, layout, 640, 640);
  CHECK_EQ(detecciones.size(), std::size_t{1});
  CHECK_NEAR(detecciones[0].score, 0.77, 1e-6);
  // El ancla 3 es la de relacion 1.3 de la posicion (1,0) del stride 8: centro (8,0) y
  // tamano 20.8 x 12.3077.
  CHECK_NEAR(detecciones[0].bbox.x, 8.0 - 16.0 * 1.3 / 2.0, 1e-4);
  CHECK_NEAR(detecciones[0].bbox.width, 16.0 * 1.3, 1e-4);
  // El landmark se desplaza 0.25 * 20.8 = 5.2 a la derecha del centro de la caja.
  CHECK_NEAR(detecciones[0].landmarks[0].x, 8.0 + 0.25 * 16.0 * 1.3, 1e-4);
  CHECK_NEAR(detecciones[0].landmarks[0].y, 0.0, 1e-4);
}

// Si el layout pide el score o los landmarks en un tensor aparte y ese tensor no
// viene, la lectura seria fuera de rango (comportamiento indefinido): tiene que ser una
// excepcion clara.
TEST(el_pipeline_rechaza_un_layout_que_pide_un_tensor_que_no_viene) {
  DetectorLayout layout;
  layout.score_in_boxes_tensor = false;
  layout.scores_output_index = 1;
  const std::vector<Anchor> anclas = generate_anchors(layout, 640, 640);
  std::vector<float> cajas(anclas.size() * 4, 0.0f);
  // Solo viene el tensor de cajas: falta el de scores.
  const std::vector<DetectorTensor> tensores = {DetectorTensor{0, cajas}};
  CHECK_THROWS(decode_detector_outputs(tensores, layout, 640, 640), std::invalid_argument);

  DetectorLayout con_landmarks;
  con_landmarks.landmarks_in_boxes_tensor = false;
  con_landmarks.landmarks_output_index = 2;
  // Con los landmarks aparte, el tensor de cajas trae 4 valores y el score.
  std::vector<float> cajas_con_score(anclas.size() * 5, 0.0f);
  const std::vector<DetectorTensor> sin_landmarks = {DetectorTensor{0, cajas_con_score}};
  CHECK_THROWS(decode_detector_outputs(sin_landmarks, con_landmarks, 640, 640),
               std::invalid_argument);
}

// Solo pasan al NMS las K cajas de mejor score (K = 500): si no, el NMS seria O(n^2)
// sobre las 16 800 anclas.
TEST(el_nms_solo_recibe_las_k_cajas_con_mejor_score) {
  DetectorLayout layout;
  layout.strides = {8};
  layout.anchors_per_stride = {1};
  layout.base_anchor_sizes = {{1.0f, 1.0f}};  // cajas de 1x1: no se solapan
  layout.score_threshold = 0.5f;
  CHECK_EQ(layout.nms_top_k, std::size_t{500});

  const std::vector<Anchor> anclas = generate_anchors(layout, 640, 640);
  CHECK_EQ(anclas.size(), std::size_t{6400});  // mas de K
  const int valores = values_per_anchor(layout);
  std::vector<float> tensor(anclas.size() * static_cast<std::size_t>(valores), 0.0f);
  for (std::size_t i = 0; i < anclas.size(); ++i) {
    tensor[i * static_cast<std::size_t>(valores) + 4] = 0.9f;  // todas sobre el umbral
  }
  const std::vector<DetectorTensor> tensores = {DetectorTensor{0, tensor}};
  const std::vector<FaceDetection> detecciones =
      decode_detector_outputs(tensores, layout, 640, 640);
  // Sin la preseleccion serian 12 800.
  CHECK_EQ(detecciones.size(), std::size_t{500});
}

// El peor caso del fotograma: 16 800 anclas, todas sobre el umbral. El decode tiene que
// ser de milisegundos, no de segundos (el hilo de trabajo no puede bloquear el servicio).
TEST(el_decode_de_16800_anclas_termina_en_milisegundos) {
  DetectorLayout layout;
  const auto inicio_anclas = std::chrono::steady_clock::now();
  const std::vector<Anchor> anclas = generate_anchors(layout, 640, 640);
  const auto fin_anclas = std::chrono::steady_clock::now();
  CHECK_EQ(anclas.size(), kAnclas640);
  const int valores = values_per_anchor(layout);
  std::vector<float> tensor(anclas.size() * static_cast<std::size_t>(valores), 0.0f);
  for (std::size_t i = 0; i < anclas.size(); ++i) {
    tensor[i * static_cast<std::size_t>(valores) + 4] = 0.9f;
  }
  const std::vector<DetectorTensor> tensores = {DetectorTensor{0, tensor}};

  const auto inicio = std::chrono::steady_clock::now();
  const std::vector<FaceDetection> detecciones = decode_detector_outputs(tensores, layout, anclas);
  const auto fin = std::chrono::steady_clock::now();
  const double ms = std::chrono::duration<double, std::milli>(fin - inicio).count();
  const double ms_anclas =
      std::chrono::duration<double, std::milli>(fin_anclas - inicio_anclas).count();
  std::cout << "[retinaface] decode de " << anclas.size() << " anclas: " << ms
            << " ms (" << detecciones.size() << " detecciones); las anclas (una vez, se "
               "cachean) tardan "
            << ms_anclas << " ms\n";
  CHECK(ms < 1000.0);

  // El preproceso de la entrada (resize a 640x640 y cuantizacion) es la otra mitad del
  // trabajo en CPU del fotograma.
  TensorInfo entrada;
  entrada.format = DataFormat::Uint8;
  entrada.quant = QuantFormat::None;
  entrada.dims = {640, 640, 3, 1};
  entrada.element_count = 640 * 640 * 3;
  entrada.size_bytes = entrada.element_count;
  const cv::Mat imagen(480, 640, CV_8UC3, cv::Scalar(30, 60, 90));
  const auto inicio_pre = std::chrono::steady_clock::now();
  const std::vector<std::uint8_t> bytes = preprocess_to_bytes(imagen, entrada, PreprocessOptions{});
  const auto fin_pre = std::chrono::steady_clock::now();
  const double ms_pre = std::chrono::duration<double, std::milli>(fin_pre - inicio_pre).count();
  std::cout << "[retinaface] preproceso a 640x640: " << ms_pre << " ms (" << bytes.size()
            << " bytes)\n";
  CHECK(ms_pre < 1000.0);
}

TEST(el_pipeline_rechaza_un_tensor_que_no_cuadra_con_el_numero_de_anclas) {
  DetectorLayout layout;
  const std::vector<DetectorTensor> tensores = {DetectorTensor{0, std::vector<float>(10, 0.0f)}};
  CHECK_THROWS(decode_detector_outputs(tensores, layout, 640, 640), std::invalid_argument);
}

// --- Tercera ronda: los valores no finitos se descartan --------------------------
//
// Un score NaN pasa el filtro "score < umbral" (la comparacion con NaN es falsa) y
// llegaba al partial_sort y al stable_sort con un comparador que, con un NaN, no es una
// relacion de orden estricto debil (comportamiento indefinido segun el estandar).
TEST(el_pipeline_descarta_el_ancla_con_un_score_no_finito) {
  DetectorLayout layout;
  layout.score_threshold = 0.5f;
  const std::vector<Anchor> anclas = generate_anchors(layout, 640, 640);
  const int valores = values_per_anchor(layout);
  std::vector<float> tensor(anclas.size() * static_cast<std::size_t>(valores), 0.0f);
  tensor[4] = std::nanf("");                    // score NaN de la primera ancla
  tensor[100 * valores + 4] = 0.8f;             // score bueno de la ancla 100

  const std::vector<DetectorTensor> tensores = {DetectorTensor{0, tensor}};
  const std::vector<FaceDetection> detecciones =
      decode_detector_outputs(tensores, layout, 640, 640);
  // La ancla con el score NaN se descarta: solo pasa la que tiene un score bueno.
  CHECK_EQ(detecciones.size(), std::size_t{1});
  CHECK_NEAR(detecciones[0].score, 0.8, 1e-6);
}

// Un landmark NaN haria que align_face lanzara en cada fotograma: el ancla se
// descarta antes de decodificar los offsets.
TEST(el_pipeline_descarta_el_ancla_con_landmarks_no_finitos) {
  DetectorLayout layout;
  layout.score_threshold = 0.5f;
  const std::vector<Anchor> anclas = generate_anchors(layout, 640, 640);
  const int valores = values_per_anchor(layout);
  std::vector<float> tensor(anclas.size() * static_cast<std::size_t>(valores), 0.0f);
  tensor[4] = 0.9f;          // score bueno de la primera ancla
  tensor[5] = std::nanf(""); // el primer offset de landmark es NaN
  tensor[100 * valores + 4] = 0.8f;

  const std::vector<DetectorTensor> tensores = {DetectorTensor{0, tensor}};
  const std::vector<FaceDetection> detecciones =
      decode_detector_outputs(tensores, layout, 640, 640);
  CHECK_EQ(detecciones.size(), std::size_t{1});
  CHECK_NEAR(detecciones[0].score, 0.8, 1e-6);
  for (const cv::Point2f& punto : detecciones[0].landmarks) CHECK(std::isfinite(punto.x));
}

// Lo mismo, con los landmarks en un tensor aparte.
TEST(el_pipeline_descarta_el_ancla_con_landmarks_no_finitos_en_tensor_apartado) {
  DetectorLayout layout;
  layout.score_threshold = 0.5f;
  layout.landmarks_in_boxes_tensor = false;
  layout.landmarks_output_index = 1;
  const std::vector<Anchor> anclas = generate_anchors(layout, 640, 640);
  std::vector<float> cajas(anclas.size() * 5, 0.0f);
  std::vector<float> landmarks(anclas.size() * 10, 0.0f);
  cajas[4] = 0.9f;
  landmarks[3] = std::nanf("");  // el offset x del primer landmark de la ancla 0
  cajas[100 * 5 + 4] = 0.8f;

  const std::vector<DetectorTensor> tensores = {DetectorTensor{0, cajas},
                                                 DetectorTensor{1, landmarks}};
  const std::vector<FaceDetection> detecciones =
      decode_detector_outputs(tensores, layout, 640, 640);
  CHECK_EQ(detecciones.size(), std::size_t{1});
  CHECK_NEAR(detecciones[0].score, 0.8, 1e-6);
}

// El NMS es una API publica: un score NaN en el comparador del orden no seria una
// relacion de orden estricto debil, asi que se rechaza con una excepcion clara.
TEST(el_nms_rechaza_un_score_no_finito) {
  const std::vector<cv::Rect2f> cajas = {cv::Rect2f(0.0f, 0.0f, 10.0f, 10.0f),
                                         cv::Rect2f(50.0f, 50.0f, 10.0f, 10.0f)};
  const std::vector<float> scores = {0.9f, std::nanf("")};
  CHECK_THROWS(nms(cajas, scores, 0.4f), std::invalid_argument);
  const std::vector<float> infinitos = {0.9f, std::numeric_limits<float>::infinity()};
  CHECK_THROWS(nms(cajas, infinitos, 0.4f), std::invalid_argument);
}
