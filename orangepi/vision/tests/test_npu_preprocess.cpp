// Pruebas de la cuantizacion de la entrada y del preproceso de imagen: son funciones
// puras, asi que no necesitan NPU ni modelo.
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include <opencv2/core.hpp>

#include "minitest.hpp"
#include "vision/npu_preprocess.hpp"
#include "vision/tensor.hpp"

namespace {

using namespace vision;

TensorInfo tensor(DataFormat format, QuantFormat quant, std::vector<std::uint32_t> dims) {
  TensorInfo info;
  info.format = format;
  info.quant = quant;
  info.dims = std::move(dims);
  std::size_t elementos = 1;
  for (std::uint32_t d : info.dims) elementos *= d;
  info.element_count = elementos;
  info.size_bytes = elementos * element_size(format);
  return info;
}

// Imagen 2x2 con pixeles BGR consecutivos: (1,2,3), (4,5,6), (7,8,9), (10,11,12).
cv::Mat imagen_de_prueba() {
  cv::Mat imagen(2, 2, CV_8UC3);
  const std::uint8_t pixeles[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
  std::size_t i = 0;
  for (int y = 0; y < 2; ++y) {
    for (int x = 0; x < 2; ++x) {
      for (int c = 0; c < 3; ++c) imagen.at<cv::Vec3b>(y, x)[c] = pixeles[i++];
    }
  }
  return imagen;
}

// Devuelve el mensaje de la excepcion, o una cadena vacia si no hubo ninguna.
template <typename Accion>
std::string mensaje_de_error(Accion&& accion) {
  try {
    accion();
  } catch (const std::exception& error) {
    return error.what();
  }
  return "";
}

}  // namespace

TEST(quantize_uint8_sin_cuantizacion_es_el_valor_redondeado) {
  const TensorInfo info = tensor(DataFormat::Uint8, QuantFormat::None, {3});
  const std::vector<float> valores = {0.0f, 127.5f, 255.0f};
  const std::vector<std::uint8_t> bytes = quantize(info, valores);
  CHECK_EQ(bytes.size(), std::size_t{3});
  CHECK_EQ(static_cast<int>(bytes[0]), 0);
  CHECK_EQ(static_cast<int>(bytes[1]), 128);
  CHECK_EQ(static_cast<int>(bytes[2]), 255);
}

TEST(quantize_uint8_afin_es_el_inverso_de_dequantize) {
  TensorInfo info = tensor(DataFormat::Uint8, QuantFormat::Affine, {3});
  info.scale = 0.5f;
  info.zero_point = 128;
  const std::vector<float> valores = {0.0f, 1.0f, -4.0f};
  const std::vector<std::uint8_t> bytes = quantize(info, valores);
  CHECK_EQ(static_cast<int>(bytes[0]), 128);
  CHECK_EQ(static_cast<int>(bytes[1]), 130);
  CHECK_EQ(static_cast<int>(bytes[2]), 120);

  const std::vector<float> vuelta = dequantize(info, bytes);
  CHECK_EQ(vuelta.size(), std::size_t{3});
  CHECK_NEAR(vuelta[0], 0.0, 1e-6);
  CHECK_NEAR(vuelta[1], 1.0, 1e-6);
  CHECK_NEAR(vuelta[2], -4.0, 1e-6);
}

TEST(quantize_int8_afin_acepta_el_zero_point_negativo) {
  TensorInfo info = tensor(DataFormat::Int8, QuantFormat::Affine, {2});
  info.scale = 0.25f;
  info.zero_point = -10;
  const std::vector<std::uint8_t> bytes = quantize(info, std::vector<float>{0.0f, 2.5f});
  CHECK_EQ(static_cast<int>(bytes[0]), 0xF6);  // -10 en complemento a dos
  CHECK_EQ(static_cast<int>(bytes[1]), 0x00);
}

TEST(quantize_recorta_al_rango_del_tipo) {
  // Un recorte minimo (menos del 1% de los valores) se acepta: el tensor no puede
  // representar esos valores, pero la entrada no esta saturada.
  TensorInfo info = tensor(DataFormat::Uint8, QuantFormat::None, {200});
  std::vector<float> valores(200, 10.0f);
  valores[0] = 300.0f;
  valores[1] = -10.0f;
  const std::vector<std::uint8_t> bytes = quantize(info, valores);
  CHECK_EQ(static_cast<int>(bytes[0]), 255);
  CHECK_EQ(static_cast<int>(bytes[1]), 0);
  CHECK_EQ(static_cast<int>(bytes[2]), 10);

  const TensorInfo con_signo = tensor(DataFormat::Int8, QuantFormat::None, {200});
  std::vector<float> valores_con_signo(200, 0.0f);
  valores_con_signo[0] = -200.0f;
  valores_con_signo[1] = 200.0f;
  const std::vector<std::uint8_t> bytes_con_signo = quantize(con_signo, valores_con_signo);
  CHECK_EQ(static_cast<int>(bytes_con_signo[0]), 0x80);  // -128
  CHECK_EQ(static_cast<int>(bytes_con_signo[1]), 0x7F);  // 127
}

// Saturar la entrada no es un dato de entrada, es un error de configuracion (el modo de
// escala no corresponde al dominio del tensor): hay que fallar, no recortar en silencio.
TEST(quantize_rechaza_una_entrada_que_satura) {
  const TensorInfo info = tensor(DataFormat::Uint8, QuantFormat::None, {3});
  CHECK_THROWS(quantize(info, std::vector<float>{-10.0f, 300.0f, 200.4f}), std::invalid_argument);

  const TensorInfo con_signo = tensor(DataFormat::Int8, QuantFormat::None, {2});
  CHECK_THROWS(quantize(con_signo, std::vector<float>{-200.0f, 200.0f}), std::invalid_argument);
}

TEST(quantize_rechaza_un_valor_no_finito) {
  const TensorInfo info = tensor(DataFormat::Uint8, QuantFormat::None, {2});
  CHECK_THROWS(quantize(info, std::vector<float>{std::nanf(""), 1.0f}), std::invalid_argument);
  CHECK_THROWS(quantize(info, std::vector<float>{std::numeric_limits<float>::infinity(), 1.0f}),
               std::invalid_argument);
}

TEST(quantize_fp32_escribe_los_bytes_del_float) {
  const TensorInfo info = tensor(DataFormat::Fp32, QuantFormat::None, {2});
  const std::vector<float> valores = {1.5f, -2.0f};
  const std::vector<std::uint8_t> bytes = quantize(info, valores);
  CHECK_EQ(bytes.size(), std::size_t{8});
  float leida = 0.0f;
  std::memcpy(&leida, bytes.data(), sizeof(float));
  CHECK_NEAR(leida, 1.5, 1e-9);
  std::memcpy(&leida, bytes.data() + 4, sizeof(float));
  CHECK_NEAR(leida, -2.0, 1e-9);
}

TEST(quantize_fp16_convierte_a_media_precision) {
  const TensorInfo info = tensor(DataFormat::Fp16, QuantFormat::None, {3});
  const std::vector<std::uint8_t> bytes = quantize(info, std::vector<float>{1.0f, -2.0f, 0.5f});
  CHECK_EQ(bytes.size(), std::size_t{6});
  std::uint16_t media = 0;
  std::memcpy(&media, bytes.data(), sizeof(std::uint16_t));
  CHECK_EQ(static_cast<int>(media), 0x3C00);  // 1.0
  std::memcpy(&media, bytes.data() + 2, sizeof(std::uint16_t));
  CHECK_EQ(static_cast<int>(media), 0xC000);  // -2.0
  std::memcpy(&media, bytes.data() + 4, sizeof(std::uint16_t));
  CHECK_EQ(static_cast<int>(media), 0x3800);  // 0.5
}

TEST(quantize_rechaza_formatos_no_soportados_y_tamanos_que_no_cuadran) {
  const std::vector<float> valores = {1.0f, 2.0f};
  CHECK_THROWS(quantize(tensor(DataFormat::Uint16, QuantFormat::None, {2}), valores),
               std::invalid_argument);
  CHECK_THROWS(quantize(tensor(DataFormat::Int16, QuantFormat::None, {2}), valores),
               std::invalid_argument);
  CHECK_THROWS(quantize(tensor(DataFormat::Other, QuantFormat::None, {2}), valores),
               std::invalid_argument);
  CHECK_THROWS(quantize(tensor(DataFormat::Uint8, QuantFormat::None, {3}), valores),
               std::invalid_argument);
}

TEST(el_preproceso_escribe_canal_a_canal_en_el_orden_del_tensor) {
  const TensorInfo info = tensor(DataFormat::Uint8, QuantFormat::None, {2, 2, 3, 1});
  PreprocessOptions options;
  options.color_order = ColorOrder::Rgb;

  const std::vector<float> valores = preprocess_to_floats(imagen_de_prueba(), info, options);
  CHECK_EQ(valores.size(), std::size_t{12});
  // Canal 0 (rojo) de los cuatro pixeles, luego el verde y luego el azul.
  const std::vector<float> esperado = {3, 6, 9, 12, 2, 5, 8, 11, 1, 4, 7, 10};
  for (std::size_t i = 0; i < valores.size(); ++i) {
    CHECK_NEAR(valores[i], esperado[i], 1e-6);
  }
}

TEST(el_preproceso_con_bgr_invierte_los_canales) {
  const TensorInfo info = tensor(DataFormat::Uint8, QuantFormat::None, {2, 2, 3, 1});
  PreprocessOptions options;
  options.color_order = ColorOrder::Bgr;
  const std::vector<float> valores = preprocess_to_floats(imagen_de_prueba(), info, options);
  const std::vector<float> esperado = {1, 4, 7, 10, 2, 5, 8, 11, 3, 6, 9, 12};
  for (std::size_t i = 0; i < valores.size(); ++i) {
    CHECK_NEAR(valores[i], esperado[i], 1e-6);
  }
}

TEST(el_preproceso_arcface_normaliza_a_menos_uno_y_uno) {
  const TensorInfo info = tensor(DataFormat::Fp32, QuantFormat::None, {2, 2, 3, 1});
  PreprocessOptions options;
  options.scale_mode = ScaleMode::ArcFace;
  const std::vector<float> valores = preprocess_to_floats(imagen_de_prueba(), info, options);
  CHECK_NEAR(valores[0], (3.0f - 127.5f) / 127.5f, 1e-6);
  CHECK_NEAR(valores[1], (6.0f - 127.5f) / 127.5f, 1e-6);
}

TEST(el_preproceso_rehace_el_tamano_de_la_imagen_al_del_tensor) {
  const TensorInfo info = tensor(DataFormat::Uint8, QuantFormat::None, {2, 2, 3, 1});
  cv::Mat imagen(4, 4, CV_8UC3, cv::Scalar(7, 9, 11));
  const std::vector<float> valores = preprocess_to_floats(imagen, info, PreprocessOptions{});
  CHECK_EQ(valores.size(), std::size_t{12});
  for (std::size_t i = 0; i < valores.size(); ++i) {
    CHECK_NEAR(valores[i], i < 4 ? 11.0 : (i < 8 ? 9.0 : 7.0), 1e-6);
  }
}

TEST(el_preproceso_rechaza_una_imagen_que_no_cuadra_con_el_tensor) {
  const TensorInfo info = tensor(DataFormat::Uint8, QuantFormat::None, {2, 2, 3, 1});
  CHECK_THROWS(preprocess_to_floats(cv::Mat(), info, PreprocessOptions{}), std::invalid_argument);
  cv::Mat gris(2, 2, CV_8UC1, cv::Scalar(3));
  CHECK_THROWS(preprocess_to_floats(gris, info, PreprocessOptions{}), std::invalid_argument);
  // Un tensor de un solo canal no tiene sentido con una imagen BGR.
  const TensorInfo un_canal = tensor(DataFormat::Uint8, QuantFormat::None, {2, 2, 1, 1});
  CHECK_THROWS(preprocess_to_floats(imagen_de_prueba(), un_canal, PreprocessOptions{}),
               std::invalid_argument);
  // Un tensor sin las tres dimensiones minimas no se puede interpretar.
  const TensorInfo incompleto = tensor(DataFormat::Uint8, QuantFormat::None, {2, 2});
  CHECK_THROWS(preprocess_to_floats(imagen_de_prueba(), incompleto, PreprocessOptions{}),
               std::invalid_argument);
}

TEST(el_preproceso_rechaza_la_escala_arcface_sobre_un_tensor_de_ocho_bits) {
  const TensorInfo info = tensor(DataFormat::Uint8, QuantFormat::None, {2, 2, 3, 1});
  PreprocessOptions options;
  options.scale_mode = ScaleMode::ArcFace;
  CHECK_THROWS(preprocess_to_floats(imagen_de_prueba(), info, options), std::invalid_argument);
}

TEST(el_preproceso_unitario_codifica_el_dominio_real_con_la_cuantizacion_del_tensor) {
  // Un tensor de entrada cuantificado con escala 1/255 (como el de la red de ejemplo del
  // fabricante) espera un dominio real de [0,1]: el byte se recupera al codificar.
  TensorInfo info = tensor(DataFormat::Uint8, QuantFormat::Affine, {2, 2, 3, 1});
  info.scale = 1.0f / 255.0f;
  info.zero_point = 0;
  PreprocessOptions options;
  options.scale_mode = ScaleMode::Unit;
  const std::vector<std::uint8_t> bytes =
      preprocess_to_bytes(imagen_de_prueba(), info, options);
  CHECK_EQ(bytes.size(), info.size_bytes);
  CHECK_EQ(static_cast<int>(bytes[0]), 3);    // rojo del pixel (0,0)
  CHECK_EQ(static_cast<int>(bytes[11]), 10);  // azul del pixel (1,1)

  // El valor real que se cuantiza es el byte dividido entre 255.
  const std::vector<float> valores = preprocess_to_floats(imagen_de_prueba(), info, options);
  CHECK_NEAR(valores[0], 3.0f / 255.0f, 1e-9);
}

// El modo de escala se deriva del tensor: dejarlo a mano era la fuente del fallo
// abierto (el byte sobre un tensor con escala 1/255 saturaba la entrada a 255).
TEST(el_modo_de_escala_se_deriva_del_tensor) {
  TensorInfo unidad = tensor(DataFormat::Uint8, QuantFormat::Affine, {2, 2, 3, 1});
  unidad.scale = 1.0f / 255.0f;
  unidad.zero_point = 0;
  CHECK_EQ(static_cast<int>(modo_escala_efectivo(ScaleMode::Auto, unidad)),
           static_cast<int>(ScaleMode::Unit));

  const TensorInfo sin_cuantizar = tensor(DataFormat::Uint8, QuantFormat::None, {2, 2, 3, 1});
  CHECK_EQ(static_cast<int>(modo_escala_efectivo(ScaleMode::Auto, sin_cuantizar)),
           static_cast<int>(ScaleMode::None));
}

TEST(el_modo_de_escala_falla_cerrado_cuando_no_se_puede_deducir) {
  // 8 bits con cuantizacion afin que no es 1/255 con zero_point 0.
  TensorInfo otra = tensor(DataFormat::Uint8, QuantFormat::Affine, {2, 2, 3, 1});
  otra.scale = 0.5f;
  otra.zero_point = 128;
  CHECK_THROWS(modo_escala_efectivo(ScaleMode::Auto, otra), std::invalid_argument);

  // Tensor de punto flotante: el dominio real no se puede deducir.
  const TensorInfo flotante = tensor(DataFormat::Fp32, QuantFormat::None, {2, 2, 3, 1});
  CHECK_THROWS(modo_escala_efectivo(ScaleMode::Auto, flotante), std::invalid_argument);

  // Un modo explicito que no es compatible con la cuantizacion del tensor.
  TensorInfo unidad = tensor(DataFormat::Uint8, QuantFormat::Affine, {2, 2, 3, 1});
  unidad.scale = 1.0f / 255.0f;
  unidad.zero_point = 0;
  CHECK_THROWS(modo_escala_efectivo(ScaleMode::None, unidad), std::invalid_argument);
  CHECK_THROWS(modo_escala_efectivo(ScaleMode::ArcFace, unidad), std::invalid_argument);
  const TensorInfo sin_cuantizar = tensor(DataFormat::Uint8, QuantFormat::None, {2, 2, 3, 1});
  CHECK_THROWS(modo_escala_efectivo(ScaleMode::ArcFace, sin_cuantizar), std::invalid_argument);
  CHECK_THROWS(modo_escala_efectivo(ScaleMode::Unit, sin_cuantizar), std::invalid_argument);

  // Con un tensor de punto flotante, el modo explicito se respeta (el llamador lo
  // eligio a mano).
  CHECK_EQ(static_cast<int>(modo_escala_efectivo(ScaleMode::ArcFace, flotante)),
           static_cast<int>(ScaleMode::ArcFace));
}

// El preproceso por omision (modo derivado) no satura la entrada de un tensor
// cuantificado con escala 1/255: el byte de la imagen vuelve a ser el byte.
TEST(el_preproceso_por_omision_no_satura_un_tensor_con_escala_1_255) {
  TensorInfo info = tensor(DataFormat::Uint8, QuantFormat::Affine, {2, 2, 3, 1});
  info.scale = 1.0f / 255.0f;
  info.zero_point = 0;
  const std::vector<std::uint8_t> bytes =
      preprocess_to_bytes(imagen_de_prueba(), info, PreprocessOptions{});
  CHECK_EQ(static_cast<int>(bytes[0]), 3);    // rojo del pixel (0,0)
  CHECK_EQ(static_cast<int>(bytes[11]), 10);  // azul del pixel (1,1)
  for (const std::uint8_t byte : bytes) CHECK(byte != 255);
}

// Las dimensiones del tensor vienen del .nb: sin un techo, un .nb corrupto pediria
// decenas de gigabytes y mataria el proceso.
TEST(el_preproceso_rechaza_un_tensor_de_entrada_enorme) {
  const TensorInfo info = tensor(DataFormat::Uint8, QuantFormat::None, {5000, 5000, 3, 1});
  CHECK_THROWS(preprocess_to_floats(imagen_de_prueba(), info, PreprocessOptions{}),
               std::invalid_argument);
}

TEST(el_preproceso_rechaza_un_lote_distinto_de_uno) {
  const TensorInfo info = tensor(DataFormat::Uint8, QuantFormat::None, {2, 2, 3, 2});
  CHECK_THROWS(preprocess_to_floats(imagen_de_prueba(), info, PreprocessOptions{}),
               std::invalid_argument);
}

// El tamano del tensor se comprueba contra el buffer del driver antes de reservar
// memoria.
TEST(el_preproceso_rechaza_un_tensor_que_no_cabe_en_el_buffer_del_driver) {
  TensorInfo info = tensor(DataFormat::Uint8, QuantFormat::None, {2, 2, 3, 1});
  info.size_bytes = 4;  // el driver reservo menos de lo que necesita el tensor
  CHECK_THROWS(preprocess_to_floats(imagen_de_prueba(), info, PreprocessOptions{}),
               std::invalid_argument);
}

TEST(los_bytes_del_preproceso_cubren_exactamente_el_tamano_del_tensor) {
  TensorInfo info = tensor(DataFormat::Uint8, QuantFormat::None, {2, 2, 3, 1});
  const std::vector<std::uint8_t> bytes =
      preprocess_to_bytes(imagen_de_prueba(), info, PreprocessOptions{});
  CHECK_EQ(bytes.size(), info.size_bytes);
  // El pixel (0,0) es BGR (1,2,3): con RGB el primer byte es el 3.
  CHECK_EQ(static_cast<int>(bytes[0]), 3);
  CHECK_EQ(static_cast<int>(bytes[1]), 6);
  CHECK_EQ(static_cast<int>(bytes[2]), 9);
  CHECK_EQ(static_cast<int>(bytes[3]), 12);

  // Si el driver reservo mas bytes, el relleno va en ceros.
  info.size_bytes = 16;
  const std::vector<std::uint8_t> rellenados =
      preprocess_to_bytes(imagen_de_prueba(), info, PreprocessOptions{});
  CHECK_EQ(rellenados.size(), std::size_t{16});
  CHECK_EQ(static_cast<int>(rellenados[12]), 0);
}

// --- Tercera ronda: la validacion del tensor de ENTRADA al arrancar --------------
//
// El tensor de entrada se validaba en el PRIMER FOTOGRAMA: con un .nb de entrada fp32
// o uint16, el proceso arrancaba bien, publicaba en_linea=true y cada pase de lista
// devolvia error_internal. La validacion tiene que salir al construir el adaptador.

// Un tensor de entrada que no vale tiene que hacer fallar el proceso AL ARRANCAR.
TEST(el_tensor_de_entrada_se_valida_al_arrancar) {
  // Un tensor de 16 bits no se sabe escribir: el proceso tiene que salir.
  const TensorInfo uint16 = tensor(DataFormat::Uint16, QuantFormat::None, {2, 2, 3, 1});
  CHECK_THROWS(validar_tensor_entrada(uint16, PreprocessOptions{}, "NpuFaceDetector"),
               std::invalid_argument);

  // Un tensor de punto flotante: el dominio real no se puede deducir (falla cerrado).
  const TensorInfo fp32 = tensor(DataFormat::Fp32, QuantFormat::None, {2, 2, 3, 1});
  CHECK_THROWS(validar_tensor_entrada(fp32, PreprocessOptions{}, "NpuFaceDetector"),
               std::invalid_argument);
  // Con el modo de escala elegido a mano, un tensor de punto flotante vale.
  PreprocessOptions arcface;
  arcface.scale_mode = ScaleMode::ArcFace;
  validar_tensor_entrada(fp32, arcface, "NpuFaceEmbedder");

  // Un tensor de un solo canal no tiene sentido con una imagen BGR.
  const TensorInfo un_canal = tensor(DataFormat::Uint8, QuantFormat::None, {2, 2, 1, 1});
  CHECK_THROWS(validar_tensor_entrada(un_canal, PreprocessOptions{}, "NpuFaceDetector"),
               std::invalid_argument);
  // Un lote distinto de 1, tampoco.
  const TensorInfo lote = tensor(DataFormat::Uint8, QuantFormat::None, {2, 2, 3, 2});
  CHECK_THROWS(validar_tensor_entrada(lote, PreprocessOptions{}, "NpuFaceDetector"),
               std::invalid_argument);
  // Un tensor sin las tres dimensiones minimas no se puede interpretar.
  const TensorInfo incompleto = tensor(DataFormat::Uint8, QuantFormat::None, {2, 2});
  CHECK_THROWS(validar_tensor_entrada(incompleto, PreprocessOptions{}, "NpuFaceDetector"),
               std::invalid_argument);
}

// El mensaje de un tensor que no se puede deducir nombra el formato REAL: no dice
// "punto flotante" si el formato no lo es (el uint16 no lo es).
TEST(el_mensaje_del_formato_no_dice_punto_flotante_cuando_no_lo_es) {
  const TensorInfo uint16 = tensor(DataFormat::Uint16, QuantFormat::None, {2, 2, 3, 1});
  const std::string mensaje = mensaje_de_error(
      [&] { modo_escala_efectivo(ScaleMode::Auto, uint16); });
  CHECK(!mensaje.empty());
  CHECK(mensaje.find("punto flotante") == std::string::npos);
  CHECK(mensaje.find("uint16") != std::string::npos);

  // Con un tensor de punto flotante, el mensaje si lo dice.
  const TensorInfo fp32 = tensor(DataFormat::Fp32, QuantFormat::None, {2, 2, 3, 1});
  const std::string mensaje_fp32 =
      mensaje_de_error([&] { modo_escala_efectivo(ScaleMode::Auto, fp32); });
  CHECK(mensaje_fp32.find("punto flotante") != std::string::npos);
  CHECK(mensaje_fp32.find("fp32") != std::string::npos);
}

// El rango que el tensor puede REPRESENTAR tiene que contener el dominio que implica
// el modo de escala. Con un int8 de escala 1/255 y zero_point 0 el rango real es
// [-0.502, 0.498], que NO cubre el [0,1] del modo Unit deducido: el modelo se rechaza
// al arrancar (no en cada fotograma, y no en silencio).
TEST(un_tensor_int8_no_cubre_el_dominio_del_modo_de_escala) {
  TensorInfo info = tensor(DataFormat::Int8, QuantFormat::Affine, {2, 2, 3, 1});
  info.scale = 1.0f / 255.0f;
  info.zero_point = 0;
  // El modo deducido es Unit (dominio [0,1]).
  CHECK_EQ(static_cast<int>(modo_escala_efectivo(ScaleMode::Auto, info)),
           static_cast<int>(ScaleMode::Unit));

  const std::string mensaje = mensaje_de_error([&] {
    validar_tensor_entrada(info, PreprocessOptions{}, "NpuFaceDetector");
  });
  CHECK(!mensaje.empty());
  CHECK(mensaje.find("NpuFaceDetector") != std::string::npos);
  // El mensaje dice el rango del tensor y el rango que hace falta.
  CHECK(mensaje.find("-0.501961") != std::string::npos);
  CHECK(mensaje.find("0.498039") != std::string::npos);
  CHECK(mensaje.find("[0, 1]") != std::string::npos);
}

// El rango del tensor y el dominio de cada modo de escala.
TEST(el_rango_del_tensor_y_el_dominio_de_cada_modo) {
  // Un uint8 afin con escala 1/255 y zero_point 0 representa [0,1].
  TensorInfo unidad = tensor(DataFormat::Uint8, QuantFormat::Affine, {2, 2, 3, 1});
  unidad.scale = 1.0f / 255.0f;
  unidad.zero_point = 0;
  CHECK_NEAR(rango_tensor(unidad).minimo, 0.0, 1e-6);
  CHECK_NEAR(rango_tensor(unidad).maximo, 1.0, 1e-6);

  // Un int8 con la misma escala representa [-0.502, 0.498].
  TensorInfo con_signo = tensor(DataFormat::Int8, QuantFormat::Affine, {2, 2, 3, 1});
  con_signo.scale = 1.0f / 255.0f;
  con_signo.zero_point = 0;
  CHECK_NEAR(rango_tensor(con_signo).minimo, -0.501961, 1e-5);
  CHECK_NEAR(rango_tensor(con_signo).maximo, 0.498039, 1e-5);

  // Un uint8 sin cuantizacion representa el byte: [0, 255].
  const TensorInfo crudo = tensor(DataFormat::Uint8, QuantFormat::None, {2, 2, 3, 1});
  CHECK_NEAR(rango_tensor(crudo).minimo, 0.0, 1e-6);
  CHECK_NEAR(rango_tensor(crudo).maximo, 255.0, 1e-6);

  CHECK_NEAR(dominio_escala(ScaleMode::None).maximo, 255.0, 1e-6);
  CHECK_NEAR(dominio_escala(ScaleMode::Unit).minimo, 0.0, 1e-6);
  CHECK_NEAR(dominio_escala(ScaleMode::Unit).maximo, 1.0, 1e-6);
  CHECK_NEAR(dominio_escala(ScaleMode::ArcFace).minimo, -1.0, 1e-6);
  CHECK_NEAR(dominio_escala(ScaleMode::ArcFace).maximo, 1.0, 1e-6);
  // El modo Auto no es un modo resuelto.
  CHECK_THROWS(dominio_escala(ScaleMode::Auto), std::invalid_argument);
}

// El techo de memoria es por lado Y total: 2048x2048x3 pasa del techo por lado (4096)
// pero son 12,6 M de elementos (201 MB de float): tiene que rechazarse.
TEST(el_preproceso_rechaza_un_tensor_con_demasiados_elementos) {
  const TensorInfo info = tensor(DataFormat::Uint8, QuantFormat::None, {2048, 2048, 3, 1});
  CHECK_THROWS(validar_tensor_entrada(info, PreprocessOptions{}, "NpuFaceDetector"),
               std::invalid_argument);
  CHECK_THROWS(preprocess_to_floats(imagen_de_prueba(), info, PreprocessOptions{}),
               std::invalid_argument);
}

// Y hay un techo de bytes reservados (float + imagen redimensionada + bytes del
// tensor), comprobado ANTES de reservar y antes del resize.
TEST(el_preproceso_rechaza_un_tensor_que_reserva_demasiados_bytes) {
  // 1158x1158x3 son 4,0 M de elementos (por debajo del techo de elementos) pero, en
  // fp32, el preproceso reservaria mas de 32 MB.
  const TensorInfo info = tensor(DataFormat::Fp32, QuantFormat::None, {1158, 1158, 3, 1});
  CHECK(info.element_count <= kMaxElementosEntrada);
  const std::string mensaje =
      mensaje_de_error([&] { validar_tensor_entrada(info, PreprocessOptions{}, "NpuFaceDetector"); });
  CHECK(!mensaje.empty());
  CHECK(mensaje.find("bytes") != std::string::npos);
}

// Un tensor de entrada que vale (el del modelo de ejemplo del fabricante) pasa la
// validacion y deduce el modo Unit.
TEST(un_tensor_valido_pasa_la_validacion_de_entrada) {
  TensorInfo info = tensor(DataFormat::Uint8, QuantFormat::Affine, {224, 224, 3, 1});
  info.scale = 1.0f / 255.0f;
  info.zero_point = 0;
  validar_tensor_entrada(info, PreprocessOptions{}, "NpuFaceEmbedder");
  CHECK_EQ(static_cast<int>(modo_escala_efectivo(ScaleMode::Auto, info)),
           static_cast<int>(ScaleMode::Unit));
}

// Las anclas del detector se generan con las dimensiones de la entrada: sin acotarlas
// antes, un .nb que declarara 60000x60000 pediria 112 millones de anclas (~2,2 GB)
// antes de que nada lo rechazara.
TEST(las_dimensiones_de_las_anclas_se_acotan_antes_de_generarlas) {
  comprobar_tamano_entrada(640, 640, "NpuFaceDetector");
  comprobar_tamano_entrada(112, 112, "NpuFaceEmbedder");
  // 4096x4096x3 son 50 M de elementos: pasan del techo por lado, pero no del total.
  CHECK_THROWS(comprobar_tamano_entrada(4096, 4096, "NpuFaceDetector"), std::invalid_argument);
  CHECK_THROWS(comprobar_tamano_entrada(60000, 60000, "NpuFaceDetector"),
               std::invalid_argument);
  CHECK_THROWS(comprobar_tamano_entrada(0, 640, "NpuFaceDetector"), std::invalid_argument);
  CHECK_THROWS(comprobar_tamano_entrada(640, -1, "NpuFaceDetector"), std::invalid_argument);
}
