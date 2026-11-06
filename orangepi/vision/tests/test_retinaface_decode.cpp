// Pruebas del decodificador de RetinaFace: son funciones puras, asi que se prueban
// con tensores sinteticos, sin NPU ni modelo. Los valores esperados salen de la
// decodificacion de la demo del fabricante (retinaface_post.cpp).
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

// Anclas del modelo real a 320: (40*40 + 20*20 + 10*10) posiciones por 2 tamanos.
constexpr std::size_t kAnclas320 = (40 * 40 + 20 * 20 + 10 * 10) * 2;
// El bloque del stride 8 son 40*40 posiciones por 2 tamanos; el del 16, 20*20 por 2.
constexpr std::size_t kBloque8de320 = 40 * 40 * 2;
constexpr std::size_t kBloque16de320 = 20 * 20 * 2;
// A 640: (80*80 + 40*40 + 20*20) posiciones por 2 tamanos.
constexpr std::size_t kAnclas640 = (80 * 80 + 40 * 40 + 20 * 20) * 2;

// Los tres tensores de salida del detector, todo a cero, para `anclas` anclas.
struct Salidas {
  std::vector<float> cajas;
  std::vector<float> scores;
  std::vector<float> landmarks;

  explicit Salidas(std::size_t anclas)
      : cajas(anclas * kValoresCaja, 0.0f),
        scores(anclas * kValoresScore, 0.0f),
        landmarks(anclas * kValoresLandmarks, 0.0f) {}

  // [fondo, cara] del ancla i.
  void score(std::size_t i, float fondo, float cara) {
    scores[i * kValoresScore] = fondo;
    scores[i * kValoresScore + 1] = cara;
  }

  std::vector<DetectorTensor> tensores() const {
    return {DetectorTensor{0, cajas}, DetectorTensor{1, scores}, DetectorTensor{2, landmarks}};
  }
};

}  // namespace

// --- Anclas -------------------------------------------------------------------------

TEST(las_anclas_de_320_son_las_de_la_referencia) {
  const std::vector<Anchor> anclas = generate_anchors(DetectorLayout{}, 320, 320);
  CHECK_EQ(anclas.size(), kAnclas320);
  CHECK_EQ(anclas.size(), std::size_t{4200});  // las 4200 de 4x4200x1 en la placa

  // La primera: stride 8, fila 0, columna 0, centro en el medio de la celda (4, 4), lado 16.
  CHECK_EQ(anclas[0].stride, 8);
  CHECK_NEAR(anclas[0].cx, 4.0, 1e-6);
  CHECK_NEAR(anclas[0].cy, 4.0, 1e-6);
  CHECK_NEAR(anclas[0].w, 16.0, 1e-6);
  CHECK_NEAR(anclas[0].h, 16.0, 1e-6);
  // La segunda: la misma posicion, el segundo tamano (32), tambien cuadrada.
  CHECK_NEAR(anclas[1].cx, 4.0, 1e-6);
  CHECK_NEAR(anclas[1].w, 32.0, 1e-6);
  CHECK_NEAR(anclas[1].h, 32.0, 1e-6);
  // La tercera: la columna siguiente (centro x = 12).
  CHECK_NEAR(anclas[2].cx, 12.0, 1e-6);
  CHECK_NEAR(anclas[2].cy, 4.0, 1e-6);
  // La fila siguiente empieza tras 40 columnas por 2 tamanos.
  CHECK_NEAR(anclas[80].cx, 4.0, 1e-6);
  CHECK_NEAR(anclas[80].cy, 12.0, 1e-6);

  // El bloque del stride 16: centro (8, 8), lados 64 y 128.
  CHECK_EQ(anclas[kBloque8de320].stride, 16);
  CHECK_NEAR(anclas[kBloque8de320].cx, 8.0, 1e-6);
  CHECK_NEAR(anclas[kBloque8de320].cy, 8.0, 1e-6);
  CHECK_NEAR(anclas[kBloque8de320].w, 64.0, 1e-6);
  CHECK_NEAR(anclas[kBloque8de320 + 1].w, 128.0, 1e-6);

  // El bloque del stride 32: centro (16, 16), lados 256 y 512.
  const std::size_t bloque32 = kBloque8de320 + kBloque16de320;
  CHECK_EQ(anclas[bloque32].stride, 32);
  CHECK_NEAR(anclas[bloque32].cx, 16.0, 1e-6);
  CHECK_NEAR(anclas[bloque32].w, 256.0, 1e-6);
  CHECK_NEAR(anclas[bloque32 + 1].w, 512.0, 1e-6);

  // La ultima: stride 32, fila 9, columna 9 (centro 9.5 * 32 = 304), lado 512.
  CHECK_NEAR(anclas.back().cx, 304.0, 1e-6);
  CHECK_NEAR(anclas.back().cy, 304.0, 1e-6);
  CHECK_NEAR(anclas.back().w, 512.0, 1e-6);
}

// Los lados de las anclas son pixeles de la entrada: NO se escalan con su tamano.
TEST(las_anclas_no_se_escalan_con_la_entrada) {
  const std::vector<Anchor> anclas = generate_anchors(DetectorLayout{}, 640, 640);
  CHECK_EQ(anclas.size(), kAnclas640);
  CHECK_NEAR(anclas[0].w, 16.0, 1e-6);
  CHECK_NEAR(anclas[0].cx, 4.0, 1e-6);
}

// Con una entrada no cuadrada, las columnas salen del ancho y las filas del alto.
TEST(las_anclas_de_una_entrada_no_cuadrada_recorren_filas_de_ancho_columnas) {
  // 320 de ancho y 192 de alto: el stride 8 tiene 40 columnas y 24 filas.
  const std::vector<Anchor> anclas = generate_anchors(DetectorLayout{}, 320, 192);
  CHECK_EQ(anclas.size(), std::size_t{(40 * 24 + 20 * 12 + 10 * 6) * 2});
  CHECK_NEAR(anclas[78].cx, 316.0, 1e-6);  // la ultima columna de la fila 0
  CHECK_NEAR(anclas[78].cy, 4.0, 1e-6);
  CHECK_NEAR(anclas[80].cx, 4.0, 1e-6);    // la primera de la fila 1
  CHECK_NEAR(anclas[80].cy, 12.0, 1e-6);
}

TEST(el_generador_de_anclas_rechaza_un_layout_incoherente) {
  DetectorLayout sobra_un_stride;
  sobra_un_stride.strides.push_back(64);
  CHECK_THROWS(generate_anchors(sobra_un_stride, 320, 320), std::invalid_argument);

  DetectorLayout sin_tamanos;
  sin_tamanos.anchor_sizes[1].clear();
  CHECK_THROWS(generate_anchors(sin_tamanos, 320, 320), std::invalid_argument);

  DetectorLayout tamano_negativo;
  tamano_negativo.anchor_sizes[0][0] = -16.0f;
  CHECK_THROWS(generate_anchors(tamano_negativo, 320, 320), std::invalid_argument);

  DetectorLayout stride_cero;
  stride_cero.strides[0] = 0;
  CHECK_THROWS(generate_anchors(stride_cero, 320, 320), std::invalid_argument);

  CHECK_THROWS(generate_anchors(DetectorLayout(), 0, 320), std::invalid_argument);
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

// --- Cajas y landmarks --------------------------------------------------------------

// retinaface_post.cpp:202-205: el centro se mueve d * 0.1 * lado del ancla y el lado
// escala por exp(d * 0.2).
TEST(la_caja_se_decodifica_con_las_varianzas_por_el_tamano_del_ancla) {
  const Anchor ancla{100.0f, 50.0f, 16.0f, 16.0f, 8};

  // Sin desplazamiento: la caja es la del ancla.
  const float cero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  const cv::Rect2f caja = decode_box(ancla, cero);
  CHECK_NEAR(caja.x, 92.0, 1e-5);
  CHECK_NEAR(caja.y, 42.0, 1e-5);
  CHECK_NEAR(caja.width, 16.0, 1e-5);
  CHECK_NEAR(caja.height, 16.0, 1e-5);

  // Con 1 en las cuatro salidas: el centro se mueve 0.1 * 16 = 1.6 px y el lado escala
  // por exp(0.2).
  const float uno[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  const cv::Rect2f movida = decode_box(ancla, uno);
  CHECK_NEAR(movida.x + movida.width / 2.0, 101.6, 1e-4);
  CHECK_NEAR(movida.y + movida.height / 2.0, 51.6, 1e-4);
  CHECK_NEAR(movida.width, 16.0 * std::exp(0.2), 1e-3);
  CHECK_NEAR(movida.height, 16.0 * std::exp(0.2), 1e-3);

  // El desplazamiento escala con el lado del ancla: con un ancla de 256, dx = -2 mueve el
  // centro 0.1 * 256 * 2 = 51.2 px a la izquierda; dh = -1 encoge el alto por exp(-0.2).
  const Anchor grande{160.0f, 160.0f, 256.0f, 256.0f, 32};
  const float valores[4] = {-2.0f, 0.5f, 0.0f, -1.0f};
  const cv::Rect2f otra = decode_box(grande, valores);
  CHECK_NEAR(otra.x + otra.width / 2.0, 160.0 - 51.2, 1e-3);
  CHECK_NEAR(otra.y + otra.height / 2.0, 160.0 + 12.8, 1e-3);
  CHECK_NEAR(otra.width, 256.0, 1e-3);
  CHECK_NEAR(otra.height, 256.0 * std::exp(-0.2), 1e-3);
}

// La misma precondicion en las dos: los valores crudos tienen que ser finitos.
TEST(la_decodificacion_de_cajas_y_landmarks_rechaza_un_valor_no_finito) {
  const Anchor ancla{0.0f, 0.0f, 16.0f, 16.0f, 8};
  const float con_nan[4] = {std::nanf(""), 0.0f, 0.0f, 0.0f};
  CHECK_THROWS(decode_box(ancla, con_nan), std::invalid_argument);
  float landmarks[10] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  landmarks[7] = std::numeric_limits<float>::infinity();
  CHECK_THROWS(decode_landmarks(ancla, landmarks), std::invalid_argument);
}

// retinaface_post.cpp:218-219: los landmarks son relativos al ANCLA (su centro y su
// lado), con la varianza 0.1; la caja decodificada no interviene.
TEST(los_landmarks_se_decodifican_relativos_al_ancla) {
  const Anchor ancla{100.0f, 50.0f, 32.0f, 32.0f, 8};
  const float offsets[10] = {1.0f, -1.0f, 0.0f, 0.0f, 2.5f, 0.5f, -3.0f, 1.0f, 0.25f, -0.5f};
  const std::array<cv::Point2f, 5> puntos = decode_landmarks(ancla, offsets);
  // 0.1 * 32 = 3.2 px por unidad.
  CHECK_NEAR(puntos[0].x, 100.0 + 3.2, 1e-4);
  CHECK_NEAR(puntos[0].y, 50.0 - 3.2, 1e-4);
  CHECK_NEAR(puntos[1].x, 100.0, 1e-4);
  CHECK_NEAR(puntos[1].y, 50.0, 1e-4);
  CHECK_NEAR(puntos[2].x, 100.0 + 8.0, 1e-4);
  CHECK_NEAR(puntos[2].y, 50.0 + 1.6, 1e-4);
  CHECK_NEAR(puntos[3].x, 100.0 - 9.6, 1e-4);
  CHECK_NEAR(puntos[3].y, 50.0 + 3.2, 1e-4);
  CHECK_NEAR(puntos[4].x, 100.0 + 0.8, 1e-4);
  CHECK_NEAR(puntos[4].y, 50.0 - 1.6, 1e-4);
}

// --- NMS ----------------------------------------------------------------------------

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

// --- Las tres salidas ---------------------------------------------------------------

// Los valores por omision son los del modelo real (model_config.h y
// retinaface_post.cpp:172-174).
TEST(el_layout_por_omision_es_el_del_modelo_del_fabricante) {
  const DetectorLayout layout;
  CHECK_NEAR(layout.score_threshold, 0.4, 1e-6);
  CHECK_NEAR(layout.nms_iou_threshold, 0.45, 1e-6);
  CHECK_EQ(layout.boxes_output_index, std::size_t{0});
  CHECK_EQ(layout.scores_output_index, std::size_t{1});
  CHECK_EQ(layout.landmarks_output_index, std::size_t{2});
  CHECK_EQ(layout.strides.size(), std::size_t{3});
}

// El score es la columna 1 ([fondo, cara]) y tiene que ser MAYOR que el umbral.
TEST(el_score_es_la_columna_de_cara_y_tiene_que_superar_el_umbral) {
  const DetectorLayout layout;  // umbral 0.4
  const std::vector<Anchor> anclas = generate_anchors(layout, 320, 320);
  Salidas salidas(anclas.size());
  salidas.score(0, 0.1f, 0.9f);     // cara: se detecta
  salidas.score(500, 0.9f, 0.1f);   // fondo alto: NO es cara
  salidas.score(1000, 0.6f, 0.4f);  // justo en el umbral: NO pasa (">", no ">=")
  salidas.score(3000, 0.59f, 0.41f);  // por encima del umbral

  const std::vector<FaceDetection> detecciones =
      decode_detector_outputs(salidas.tensores(), layout, anclas);
  CHECK_EQ(detecciones.size(), std::size_t{2});
  CHECK_NEAR(detecciones[0].score, 0.9, 1e-6);
  CHECK_NEAR(detecciones[1].score, 0.41, 1e-6);
}

// De punta a punta con valores no triviales: la caja y los landmarks se decodifican
// contra su ancla, en pixeles de la entrada del modelo.
TEST(el_pipeline_decodifica_caja_y_landmarks_contra_su_ancla) {
  const DetectorLayout layout;
  const std::vector<Anchor> anclas = generate_anchors(layout, 320, 320);
  Salidas salidas(anclas.size());
  // El ancla 3201 es la de 128 de la primera posicion del stride 16: centro (8, 8).
  const std::size_t i = kBloque8de320 + 1;
  CHECK_NEAR(anclas[i].w, 128.0, 1e-6);
  salidas.score(i, 0.05f, 0.95f);
  salidas.cajas[i * 4 + 0] = 2.0f;   // centro x: 8 + 2 * 0.1 * 128 = 33.6
  salidas.cajas[i * 4 + 1] = 1.0f;   // centro y: 8 + 12.8 = 20.8
  salidas.cajas[i * 4 + 2] = 0.0f;   // ancho: 128
  salidas.cajas[i * 4 + 3] = -1.0f;  // alto: 128 * exp(-0.2)
  salidas.landmarks[i * 10 + 0] = 1.0f;   // x del ojo izquierdo: 8 + 12.8
  salidas.landmarks[i * 10 + 9] = -0.5f;  // y de la boca derecha: 8 - 6.4

  const std::vector<FaceDetection> detecciones =
      decode_detector_outputs(salidas.tensores(), layout, 320, 320);
  CHECK_EQ(detecciones.size(), std::size_t{1});
  const FaceDetection& d = detecciones[0];
  CHECK_NEAR(d.score, 0.95, 1e-6);
  const double alto = 128.0 * std::exp(-0.2);
  CHECK_NEAR(d.bbox.x, 33.6 - 64.0, 1e-3);
  CHECK_NEAR(d.bbox.y, 20.8 - alto / 2.0, 1e-3);
  CHECK_NEAR(d.bbox.width, 128.0, 1e-3);
  CHECK_NEAR(d.bbox.height, alto, 1e-3);
  CHECK_NEAR(d.landmarks[0].x, 8.0 + 12.8, 1e-4);
  CHECK_NEAR(d.landmarks[0].y, 8.0, 1e-4);
  CHECK_NEAR(d.landmarks[4].x, 8.0, 1e-4);
  CHECK_NEAR(d.landmarks[4].y, 8.0 - 6.4, 1e-4);
}

// Si falta un tensor, la lectura seria fuera de rango (comportamiento indefinido): tiene
// que ser una excepcion clara. Y cada tensor tiene que traer sus valores por ancla.
TEST(el_pipeline_rechaza_tensores_que_faltan_o_que_no_cuadran) {
  const DetectorLayout layout;
  const std::vector<Anchor> anclas = generate_anchors(layout, 320, 320);
  const Salidas salidas(anclas.size());

  // Falta el de scores.
  const std::vector<DetectorTensor> sin_scores = {DetectorTensor{0, salidas.cajas},
                                                   DetectorTensor{2, salidas.landmarks}};
  CHECK_THROWS(decode_detector_outputs(sin_scores, layout, anclas), std::invalid_argument);

  // Un score por ancla (la convencion de una columna) no vale: son dos.
  const std::vector<DetectorTensor> una_columna = {
      DetectorTensor{0, salidas.cajas}, DetectorTensor{1, std::vector<float>(anclas.size(), 0.0f)},
      DetectorTensor{2, salidas.landmarks}};
  CHECK_THROWS(decode_detector_outputs(una_columna, layout, anclas), std::invalid_argument);

  // Las cajas con 15 valores por ancla (score y landmarks dentro) tampoco.
  const std::vector<DetectorTensor> quince = {
      DetectorTensor{0, std::vector<float>(anclas.size() * 15, 0.0f)},
      DetectorTensor{1, salidas.scores}, DetectorTensor{2, salidas.landmarks}};
  CHECK_THROWS(decode_detector_outputs(quince, layout, anclas), std::invalid_argument);

  // Un tensor de 10 valores no cuadra con ningun numero de anclas.
  const std::vector<DetectorTensor> corto = {DetectorTensor{0, std::vector<float>(10, 0.0f)},
                                             DetectorTensor{1, salidas.scores},
                                             DetectorTensor{2, salidas.landmarks}};
  CHECK_THROWS(decode_detector_outputs(corto, layout, anclas), std::invalid_argument);
}

// Solo pasan al NMS las K cajas de mejor score (K = 500): si no, el NMS seria O(n^2)
// sobre todas las anclas que pasen el umbral.
TEST(el_nms_solo_recibe_las_k_cajas_con_mejor_score) {
  DetectorLayout layout;
  layout.strides = {8};
  layout.anchor_sizes = {{1.0f}};  // cajas de 1x1 separadas 8 px: no se solapan
  CHECK_EQ(layout.nms_top_k, std::size_t{500});

  const std::vector<Anchor> anclas = generate_anchors(layout, 640, 640);
  CHECK_EQ(anclas.size(), std::size_t{6400});  // mas de K
  Salidas salidas(anclas.size());
  for (std::size_t i = 0; i < anclas.size(); ++i) salidas.score(i, 0.1f, 0.9f);
  const std::vector<FaceDetection> detecciones =
      decode_detector_outputs(salidas.tensores(), layout, anclas);
  CHECK_EQ(detecciones.size(), std::size_t{500});
}

// El peor caso del fotograma: todas las anclas sobre el umbral. El decode tiene que ser
// de milisegundos, no de segundos (el hilo de trabajo no puede bloquear el servicio).
TEST(el_decode_de_todas_las_anclas_termina_en_milisegundos) {
  const DetectorLayout layout;
  const auto inicio_anclas = std::chrono::steady_clock::now();
  const std::vector<Anchor> anclas = generate_anchors(layout, 640, 640);
  const auto fin_anclas = std::chrono::steady_clock::now();
  CHECK_EQ(anclas.size(), kAnclas640);
  Salidas salidas(anclas.size());
  for (std::size_t i = 0; i < anclas.size(); ++i) salidas.score(i, 0.1f, 0.9f);
  const std::vector<DetectorTensor> tensores = salidas.tensores();

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

  // El preproceso del detector real (letterbox de una foto de 640x480 a 320x320 y la
  // entrada intercalada uint8) es la otra mitad del trabajo en CPU del fotograma.
  TensorInfo entrada;
  entrada.format = DataFormat::Uint8;
  entrada.quant = QuantFormat::None;
  entrada.dims = {3, 320, 320, 1};
  entrada.element_count = 320 * 320 * 3;
  entrada.size_bytes = entrada.element_count;
  const cv::Mat imagen(480, 640, CV_8UC3, cv::Scalar(30, 60, 90));
  PreprocessOptions bgr;
  bgr.color_order = ColorOrder::Bgr;
  const auto inicio_pre = std::chrono::steady_clock::now();
  const Letterbox letterbox = calcular_letterbox(imagen.cols, imagen.rows, 320, 320);
  const std::vector<std::uint8_t> bytes =
      preprocess_to_bytes(aplicar_letterbox(imagen, letterbox, 320, 320), entrada, bgr);
  const auto fin_pre = std::chrono::steady_clock::now();
  const double ms_pre = std::chrono::duration<double, std::milli>(fin_pre - inicio_pre).count();
  std::cout << "[retinaface] letterbox y preproceso a 320x320: " << ms_pre << " ms ("
            << bytes.size() << " bytes)\n";
  CHECK(ms_pre < 1000.0);
}

// --- Los valores no finitos se descartan --------------------------------------------
//
// Un score NaN no pasa el umbral ("score > umbral" es falso con NaN) y un infinito se
// descarta aparte: los dos llegarian al partial_sort y al stable_sort con un comparador
// que no es una relacion de orden estricto debil (comportamiento indefinido).
TEST(el_pipeline_descarta_el_ancla_con_un_score_no_finito) {
  const DetectorLayout layout;
  const std::vector<Anchor> anclas = generate_anchors(layout, 320, 320);
  Salidas salidas(anclas.size());
  salidas.score(0, 0.0f, std::nanf(""));
  salidas.score(1, 0.0f, std::numeric_limits<float>::infinity());
  salidas.score(100, 0.2f, 0.8f);

  const std::vector<FaceDetection> detecciones =
      decode_detector_outputs(salidas.tensores(), layout, anclas);
  CHECK_EQ(detecciones.size(), std::size_t{1});
  CHECK_NEAR(detecciones[0].score, 0.8, 1e-6);
}

// Un landmark NaN haria que align_face lanzara en cada fotograma, y una caja NaN
// abortaria el fotograma entero: el ancla se descarta.
TEST(el_pipeline_descarta_el_ancla_con_landmarks_o_caja_no_finitos) {
  const DetectorLayout layout;
  const std::vector<Anchor> anclas = generate_anchors(layout, 320, 320);
  Salidas salidas(anclas.size());
  salidas.score(0, 0.1f, 0.9f);
  salidas.landmarks[3] = std::nanf("");  // la y del segundo landmark del ancla 0
  salidas.score(50, 0.1f, 0.85f);
  salidas.cajas[50 * 4 + 2] = std::nanf("");  // el ancho del ancla 50
  salidas.score(100, 0.2f, 0.8f);

  const std::vector<FaceDetection> detecciones =
      decode_detector_outputs(salidas.tensores(), layout, anclas);
  CHECK_EQ(detecciones.size(), std::size_t{1});
  CHECK_NEAR(detecciones[0].score, 0.8, 1e-6);
  for (const cv::Point2f& punto : detecciones[0].landmarks) CHECK(std::isfinite(punto.x));
}

// Valores crudos finitos pero enormes desbordan al decodificar: exp(1000 * 0.2) es
// infinito en float. La caja saldria con un ancho infinito: el ancla se descarta.
TEST(el_pipeline_descarta_el_ancla_cuya_caja_desborda_al_decodificar) {
  const DetectorLayout layout;
  const std::vector<Anchor> anclas = generate_anchors(layout, 320, 320);
  Salidas salidas(anclas.size());
  salidas.score(0, 0.1f, 0.9f);
  salidas.cajas[0 * 4 + 2] = 1000.0f;  // dw: el ancho desborda a infinito
  salidas.score(100, 0.2f, 0.8f);

  const std::vector<FaceDetection> detecciones =
      decode_detector_outputs(salidas.tensores(), layout, anclas);
  CHECK_EQ(detecciones.size(), std::size_t{1});
  CHECK_NEAR(detecciones[0].score, 0.8, 1e-6);
  CHECK(std::isfinite(detecciones[0].bbox.width));
}

// El score es la salida de un softmax: mayor que 1 no es una probabilidad, sino un
// tensor mal interpretado. La demo no dibuja esas detecciones ("score is illegal",
// retinaface_post.cpp:317-321); aqui se descartan.
TEST(el_pipeline_descarta_un_score_mayor_que_uno) {
  const DetectorLayout layout;
  const std::vector<Anchor> anclas = generate_anchors(layout, 320, 320);
  Salidas salidas(anclas.size());
  salidas.score(0, 0.0f, 1.5f);
  salidas.score(100, 0.0f, 1.0f);  // 1 exacto si vale
  salidas.score(2000, 0.2f, 0.8f);

  const std::vector<FaceDetection> detecciones =
      decode_detector_outputs(salidas.tensores(), layout, anclas);
  CHECK_EQ(detecciones.size(), std::size_t{2});
  CHECK_NEAR(detecciones[0].score, 1.0, 1e-6);
  CHECK_NEAR(detecciones[1].score, 0.8, 1e-6);
}

// --- Las salidas al arrancar y la vuelta a la imagen original ----------------------

namespace {

TensorInfo salida_fp32(std::vector<std::uint32_t> dims) {
  TensorInfo info;
  info.name = "salida";
  info.format = DataFormat::Fp32;
  info.quant = QuantFormat::None;
  info.dims = std::move(dims);
  std::size_t elementos = 1;
  for (const std::uint32_t d : info.dims) elementos *= d;
  info.element_count = elementos;
  info.size_bytes = elementos * 4;
  return info;
}

}  // namespace

// Cada salida tiene que ser [valores por ancla, anclas] (con un 1 al final, o sin el):
// con los valores de una misma ancla seguidos. Otra forma con el mismo numero de
// elementos haria leer basura sin fallar.
TEST(la_salida_del_detector_tiene_que_tener_los_valores_de_cada_ancla_seguidos) {
  comprobar_salida_detector(salida_fp32({4, 4200, 1}), "las cajas", 4200, 4);
  comprobar_salida_detector(salida_fp32({4, 4200}), "las cajas", 4200, 4);

  // Traspuesta: el mismo numero de elementos, otro orden.
  const std::string mensaje = [] {
    try {
      comprobar_salida_detector(salida_fp32({4200, 4, 1}), "las cajas", 4200, 4);
    } catch (const std::invalid_argument& error) {
      return std::string(error.what());
    }
    return std::string();
  }();
  CHECK(mensaje.find("las cajas") != std::string::npos);
  CHECK(mensaje.find("4200x4x1") != std::string::npos);
  // Una dimension extra que no es 1, con mas elementos o con los mismos.
  CHECK_THROWS(comprobar_salida_detector(salida_fp32({4, 4200, 2}), "las cajas", 4200, 4),
               std::invalid_argument);
  CHECK_THROWS(comprobar_salida_detector(salida_fp32({4, 2100, 2}), "las cajas", 4200, 4),
               std::invalid_argument);
  // Un formato que dequantize no sabe leer.
  TensorInfo fp16 = salida_fp32({4, 4200, 1});
  fp16.format = DataFormat::Fp16;
  CHECK_THROWS(comprobar_salida_detector(fp16, "las cajas", 4200, 4), std::invalid_argument);
  TensorInfo desconocido = salida_fp32({4, 4200, 1});
  desconocido.format = DataFormat::Other;
  CHECK_THROWS(comprobar_salida_detector(desconocido, "las cajas", 4200, 4),
               std::invalid_argument);
}

// Las detecciones vuelven a la imagen original: la caja se recorta a la imagen y los
// landmarks no (un landmark en el relleno queda fuera de la imagen, como en la demo).
TEST(deshacer_el_letterbox_de_las_detecciones_recorta_la_caja_y_no_los_landmarks) {
  // La foto de la demo: 592x503 sobre 320x320, 24 filas de relleno arriba.
  const Letterbox lb = calcular_letterbox(592, 503, 320, 320);
  FaceDetection d;
  d.score = 0.9f;
  // Una caja que empieza en el relleno de arriba (y = 10) y llega a x = 330.
  d.bbox = cv::Rect2f(300.0f, 10.0f, 30.0f, 40.0f);
  d.landmarks = {cv::Point2f(310.0f, 12.0f), cv::Point2f(320.0f, 30.0f),
                 cv::Point2f(315.0f, 40.0f), cv::Point2f(310.0f, 45.0f),
                 cv::Point2f(320.0f, 45.0f)};
  std::vector<FaceDetection> detecciones = {d};
  deshacer_letterbox_detecciones(lb, detecciones, 592, 503);
  CHECK_EQ(detecciones.size(), std::size_t{1});
  const FaceDetection& r = detecciones[0];
  CHECK_NEAR(r.score, 0.9, 1e-6);
  // La caja: x0 = 300 * 1.85 = 555, x1 = 330 * 1.85 = 610.5 -> 591; y0 < 0 -> 0.
  CHECK_NEAR(r.bbox.x, 555.0, 1e-3);
  CHECK_NEAR(r.bbox.x + r.bbox.width, 591.0, 1e-3);
  CHECK_NEAR(r.bbox.y, 0.0, 1e-6);
  CHECK_NEAR(r.bbox.y + r.bbox.height, (50.0 - 24.0) * 503.0 / 272.0, 1e-3);
  // El primer landmark esta en el relleno de arriba: queda por encima de la imagen.
  CHECK(r.landmarks[0].y < 0.0f);
  CHECK_NEAR(r.landmarks[0].y, (12.0 - 24.0) * 503.0 / 272.0, 1e-3);
  // Y el segundo, a la derecha de la imagen (320 * 1.85 = 592 > 591): no se recorta.
  CHECK(r.landmarks[1].x > 591.0f);
}

// La preseleccion del NMS se queda con las K de MAYOR score aunque lleguen barajadas,
// y el resultado sale en orden descendente.
TEST(el_top_k_se_queda_con_las_mejores_y_en_orden_descendente) {
  DetectorLayout layout;
  layout.strides = {8};
  layout.anchor_sizes = {{1.0f}};  // cajas de 1x1 separadas 8 px: no se solapan
  const std::vector<Anchor> anclas = generate_anchors(layout, 640, 640);
  const std::size_t n = anclas.size();  // 6400
  Salidas salidas(n);
  // Scores distintos entre 0.41 y 0.99, repartidos por las anclas con una permutacion
  // (1777 y 6400 son coprimos): el ancla i recibe el rango (i * 1777) % 6400.
  for (std::size_t i = 0; i < n; ++i) {
    const std::size_t rango = (i * 1777) % n;
    const float cara = 0.41f + 0.58f * static_cast<float>(rango) / static_cast<float>(n - 1);
    salidas.score(i, 1.0f - cara, cara);
  }
  const std::vector<FaceDetection> detecciones =
      decode_detector_outputs(salidas.tensores(), layout, anclas);
  CHECK_EQ(detecciones.size(), layout.nms_top_k);
  // Las 500 mejores son los rangos 5900..6399: la peor que queda es la del rango 5900.
  const float peor_esperada = 0.41f + 0.58f * 5900.0f / static_cast<float>(n - 1);
  CHECK_NEAR(detecciones.front().score, 0.99, 1e-5);
  CHECK_NEAR(detecciones.back().score, peor_esperada, 1e-5);
  for (std::size_t i = 1; i < detecciones.size(); ++i) {
    CHECK(detecciones[i - 1].score > detecciones[i].score);
  }
}
