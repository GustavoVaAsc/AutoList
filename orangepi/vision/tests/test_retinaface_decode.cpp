// Pruebas del decodificador de RetinaFace: son funciones puras, asi que se prueban
// con tensores sinteticos, sin NPU ni modelo.
#include <cmath>
#include <vector>

#include "minitest.hpp"
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

  // La primera: stride 8, posicion (0,0), ancla cuadrada de 16x16 centrada en (4,4).
  CHECK_EQ(anclas[0].stride, 8);
  CHECK_NEAR(anclas[0].cx, 4.0, 1e-6);
  CHECK_NEAR(anclas[0].cy, 4.0, 1e-6);
  CHECK_NEAR(anclas[0].w, 16.0, 1e-6);
  CHECK_NEAR(anclas[0].h, 16.0, 1e-6);

  // La segunda: la de relacion 1.3 (la misma area, mas ancha y mas baja).
  CHECK_NEAR(anclas[1].cx, 4.0, 1e-6);
  CHECK_NEAR(anclas[1].w, 16.0 * 1.3, 1e-6);
  CHECK_NEAR(anclas[1].h, 16.0 / 1.3, 1e-6);

  // La tercera: la posicion (1,0) del mismo stride (centro 12).
  CHECK_NEAR(anclas[2].cx, 12.0, 1e-6);
  CHECK_NEAR(anclas[2].cy, 4.0, 1e-6);

  // El primer bloque es el del stride 8; luego el del 16 (ancho 32) y el del 32 (64).
  CHECK_EQ(anclas[kBloque8].stride, 16);
  CHECK_NEAR(anclas[kBloque8].cx, 8.0, 1e-6);
  CHECK_NEAR(anclas[kBloque8].w, 32.0, 1e-6);
  CHECK_NEAR(anclas[kBloque8].h, 32.0, 1e-6);

  CHECK_EQ(anclas[kBloque8 + kBloque16].stride, 32);
  CHECK_NEAR(anclas[kBloque8 + kBloque16].cx, 16.0, 1e-6);
  CHECK_NEAR(anclas[kBloque8 + kBloque16].w, 64.0, 1e-6);
}

TEST(las_anclas_se_escalan_con_el_tamano_de_la_entrada) {
  const DetectorLayout layout;
  const std::vector<Anchor> anclas = generate_anchors(layout, 320, 320);
  CHECK_EQ(anclas.size(), kAnclas640 / 4);  // la mitad de ancho y de alto
  // Los tamanos base bajan a la mitad y los centros se mantienen en el mapa.
  CHECK_NEAR(anclas[0].w, 8.0, 1e-6);
  CHECK_NEAR(anclas[0].h, 8.0, 1e-6);
  CHECK_NEAR(anclas[0].cx, 4.0, 1e-6);
}

TEST(el_generador_de_anclas_rechaza_un_layout_incoherente) {
  DetectorLayout layout;
  layout.anchors_per_stride.push_back(2);
  CHECK_THROWS(generate_anchors(layout, 640, 640), std::invalid_argument);
  CHECK_THROWS(generate_anchors(DetectorLayout(), 0, 640), std::invalid_argument);
}

TEST(la_decodificacion_de_cajas_aplica_las_varianzas_de_retinaface) {
  const Anchor ancla{4.0f, 4.0f, 16.0f, 16.0f, 8};

  // Sin desplazamiento: la caja es la del ancla.
  const float sin_desplazamiento[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  const cv::Rect2f caja = decode_box(ancla, sin_desplazamiento, 8);
  CHECK_NEAR(caja.x, -4.0, 1e-6);
  CHECK_NEAR(caja.y, -4.0, 1e-6);
  CHECK_NEAR(caja.width, 16.0, 1e-6);
  CHECK_NEAR(caja.height, 16.0, 1e-6);

  // Con 1 en las cuatro salidas: dx = 0.1, dy = 0.2, dw = dh = 0.3, escalados por el
  // stride 8.
  const float desplazamientos[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  const cv::Rect2f movida = decode_box(ancla, desplazamientos, 8);
  CHECK_NEAR(movida.x + movida.width / 2.0, 4.0 + 0.1 * 8.0, 1e-4);
  CHECK_NEAR(movida.y + movida.height / 2.0, 4.0 + 0.2 * 8.0, 1e-4);
  CHECK_NEAR(movida.width, 16.0 * std::exp(0.3 * 8.0), 1e-3);
  CHECK_NEAR(movida.height, 16.0 * std::exp(0.3 * 8.0), 1e-3);
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

TEST(el_iou_de_cajas_que_no_se_tocan_es_cero) {
  CHECK_NEAR(iou(cv::Rect2f(0.0f, 0.0f, 10.0f, 10.0f), cv::Rect2f(50.0f, 50.0f, 10.0f, 10.0f)), 0.0,
             1e-9);
  CHECK_NEAR(iou(cv::Rect2f(0.0f, 0.0f, 10.0f, 10.0f), cv::Rect2f(0.0f, 0.0f, 10.0f, 10.0f)), 1.0,
             1e-9);
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
  CHECK_NEAR(detecciones[0].bbox.x, -4.0, 1e-4);
  CHECK_NEAR(detecciones[0].bbox.y, -4.0, 1e-4);
  CHECK_NEAR(detecciones[0].bbox.width, 16.0, 1e-4);
  CHECK_NEAR(detecciones[0].bbox.height, 16.0, 1e-4);

  CHECK_NEAR(detecciones[1].score, 0.8, 1e-6);
  CHECK_NEAR(detecciones[1].bbox.x, 404.0 - 8.0, 1e-4);

  // Los landmarks van en el orden de FaceDetection::landmarks.
  CHECK_EQ(detecciones[0].landmarks.size(), std::size_t{5});
  CHECK_NEAR(detecciones[0].landmarks[0].x, 4.0, 1e-4);
  CHECK_NEAR(detecciones[0].landmarks[0].y, 4.0, 1e-4);
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
  // El ancla 3 es la de relacion 1.3 de la posicion (1,0) del stride 8: centro (12,4) y
  // tamano 20.8 x 12.3077.
  CHECK_NEAR(detecciones[0].bbox.x, 12.0 - 16.0 * 1.3 / 2.0, 1e-4);
  CHECK_NEAR(detecciones[0].bbox.width, 16.0 * 1.3, 1e-4);
  // El landmark se desplaza 0.25 * 20.8 = 5.2 a la derecha del centro de la caja.
  CHECK_NEAR(detecciones[0].landmarks[0].x, 12.0 + 0.25 * 16.0 * 1.3, 1e-4);
  CHECK_NEAR(detecciones[0].landmarks[0].y, 4.0, 1e-4);
}

TEST(el_pipeline_rechaza_un_tensor_que_no_cuadra_con_el_numero_de_anclas) {
  DetectorLayout layout;
  const std::vector<DetectorTensor> tensores = {DetectorTensor{0, std::vector<float>(10, 0.0f)}};
  CHECK_THROWS(decode_detector_outputs(tensores, layout, 640, 640), std::invalid_argument);
}
