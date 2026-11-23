// Pruebas de la miniatura del rostro: el recorte, el JPEG en base64 y el tope de
// tamano del contrato MQTT. Con una foto sintetica pequena (nada de caras reales:
// la miniatura es efimera y aqui no se guarda ninguna imagen).

#include <cstddef>
#include <string>
#include <vector>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "minitest.hpp"
#include "vision/thumbnail.hpp"

namespace {

using namespace vision;

// Una foto sintetica: un degradado con un rectangulo claro (el "rostro") en el centro.
cv::Mat foto_sintetica(int filas, int columnas) {
  cv::Mat foto(filas, columnas, CV_8UC3);
  for (int y = 0; y < filas; ++y) {
    for (int x = 0; x < columnas; ++x) {
      foto.at<cv::Vec3b>(y, x) = cv::Vec3b(static_cast<unsigned char>(x % 256),
                                           static_cast<unsigned char>(y % 256), 128);
    }
  }
  const cv::Rect rostro(columnas / 4, filas / 4, columnas / 2, filas / 2);
  foto(rostro).setTo(cv::Scalar(220, 220, 220));
  return foto;
}

// Decodifica base64 (solo para las pruebas): el resultado son los bytes del JPEG.
std::vector<unsigned char> base64_decode(const std::string& texto) {
  auto valor = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  std::vector<unsigned char> bytes;
  int acumulado = 0;
  int bits = 0;
  for (const char c : texto) {
    if (c == '=') break;
    const int v = valor(c);
    if (v < 0) continue;
    acumulado = (acumulado << 6) | v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      bytes.push_back(static_cast<unsigned char>((acumulado >> bits) & 0xFF));
    }
  }
  return bytes;
}

// Solo el alphabet de base64 estandar (con el relleno al final).
bool es_base64(const std::string& texto) {
  if (texto.empty() || texto.size() % 4 != 0) return false;
  std::size_t relleno = 0;
  for (std::size_t i = 0; i < texto.size(); ++i) {
    const char c = texto[i];
    const bool letra = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                       c == '+' || c == '/';
    if (!letra) {
      if (c != '=') return false;
      ++relleno;
      if (i < texto.size() - 2) return false;  // el relleno solo va al final
    } else if (relleno > 0) {
      return false;  // el relleno no puede ir en medio
    }
  }
  return relleno <= 2;
}

}  // namespace

// El base64 es el de RFC 4648 (los vectores de ejemplo de la RFC).
TEST(el_base64_es_el_de_la_rfc_4648) {
  CHECK_EQ(base64_encode({}), std::string(""));
  CHECK_EQ(base64_encode({'f'}), std::string("Zg=="));
  CHECK_EQ(base64_encode({'f', 'o'}), std::string("Zm8="));
  CHECK_EQ(base64_encode({'f', 'o', 'o'}), std::string("Zm9v"));
  CHECK_EQ(base64_encode({'f', 'o', 'o', 'b'}), std::string("Zm9vYg=="));
  CHECK_EQ(base64_encode({'f', 'o', 'o', 'b', 'a'}), std::string("Zm9vYmE="));
  CHECK_EQ(base64_encode({'f', 'o', 'o', 'b', 'a', 'r'}), std::string("Zm9vYmFy"));
}

// El decodificador de la prueba (que el codigo de produccion no usa) tambien es el de
// la RFC: si no, la comprobacion del JPEG no valdria.
TEST(el_decodificador_de_la_prueba_decodifica_lo_mismo) {
  CHECK(base64_decode("Zm9vYmFy") == (std::vector<unsigned char>{'f', 'o', 'o', 'b', 'a', 'r'}));
  CHECK(base64_decode("Zg==") == (std::vector<unsigned char>{'f'}));
}

// Con foto y rostro, la miniatura es un JPEG en base64 que cabe en 160x120.
TEST(la_miniatura_es_un_jpeg_base64_de_160x120) {
  const cv::Mat foto = foto_sintetica(240, 320);
  const std::optional<std::string> miniatura =
      encode_thumbnail(foto, cv::Rect2f(120.0f, 60.0f, 80.0f, 80.0f));
  CHECK(miniatura.has_value());
  CHECK(es_base64(*miniatura));

  const std::vector<unsigned char> jpeg = base64_decode(*miniatura);
  CHECK(jpeg.size() > 2);
  // La firma del JPEG (FF D8 FF): la miniatura es una foto, no un texto cualquiera.
  CHECK_EQ(jpeg[0], static_cast<unsigned char>(0xFF));
  CHECK_EQ(jpeg[1], static_cast<unsigned char>(0xD8));
  CHECK_EQ(jpeg[2], static_cast<unsigned char>(0xFF));
  CHECK(jpeg.size() <= kThumbnailMaxBytes);

  const cv::Mat imagen = cv::imdecode(jpeg, cv::IMREAD_COLOR);
  CHECK(!imagen.empty());
  CHECK(imagen.cols <= kThumbnailWidth);
  CHECK(imagen.rows <= kThumbnailHeight);
  CHECK(imagen.cols > 0);
  CHECK(imagen.rows > 0);
}

// Node-RED y el frontend exigen a la miniatura (ademas de base64 y del tope) que sea un JPEG
// (empieza con "/9j/", los tres bytes FF D8 FF en base64) y que su largo sea multiplo de 4
// (base64 con relleno). encode_thumbnail lo cumple siempre: con fotos lisas, con degradados,
// con ruido (que baja la calidad) y con rostros chicos, grandes y a medias.
TEST(la_miniatura_siempre_es_un_jpeg_base64_con_relleno_que_node_red_acepta) {
  cv::Mat ruido(240, 320, CV_8UC3);
  cv::RNG rng(11);
  rng.fill(ruido, cv::RNG::UNIFORM, 0, 256);
  const std::vector<cv::Mat> fotos = {foto_sintetica(240, 320), foto_sintetica(600, 800),
                                      foto_sintetica(64, 64), ruido,
                                      cv::Mat(240, 320, CV_8UC3, cv::Scalar(170, 170, 170))};
  const std::vector<cv::Rect2f> rostros = {
      cv::Rect2f(120.0f, 60.0f, 80.0f, 80.0f), cv::Rect2f(10.0f, 10.0f, 30.0f, 50.0f),
      cv::Rect2f(0.0f, 0.0f, 320.0f, 240.0f), cv::Rect2f(300.0f, 220.0f, 80.0f, 80.0f),
      cv::Rect2f(100.5f, 50.25f, 77.7f, 91.3f)};
  int con_miniatura = 0;
  for (const cv::Mat& foto : fotos) {
    for (const cv::Rect2f& rostro : rostros) {
      const std::optional<std::string> miniatura = encode_thumbnail(foto, rostro);
      if (!miniatura.has_value()) continue;  // sin miniatura es valido; una mala, no
      ++con_miniatura;
      CHECK(es_base64(*miniatura));
      CHECK_EQ(miniatura->size() % 4, std::size_t{0});
      CHECK_EQ(miniatura->substr(0, 4), std::string("/9j/"));
      CHECK(miniatura->size() <= 12288);  // el tope de Node-RED y del frontend
    }
  }
  CHECK(con_miniatura > 10);
}

// El recorte se lleva un margen alrededor del rostro (la frente y la barbilla): la
// miniatura lleva el rostro y su alrededor, no un recuadro pelado.
TEST(el_recorte_lleva_margen_alrededor_del_rostro) {
  cv::Mat foto(240, 320, CV_8UC3, cv::Scalar(0, 0, 255));  // fondo azul
  foto(cv::Rect(120, 60, 80, 80)).setTo(cv::Scalar(0, 255, 0));  // el rostro, verde
  const std::optional<std::string> miniatura =
      encode_thumbnail(foto, cv::Rect2f(120.0f, 60.0f, 80.0f, 80.0f));
  CHECK(miniatura.has_value());
  const cv::Mat imagen = cv::imdecode(base64_decode(*miniatura), cv::IMREAD_COLOR);
  int verdes = 0;
  int azules = 0;
  for (int y = 0; y < imagen.rows; ++y) {
    for (int x = 0; x < imagen.cols; ++x) {
      const cv::Vec3b pixel = imagen.at<cv::Vec3b>(y, x);
      if (pixel[1] > 180 && pixel[2] < 100) ++verdes;
      if (pixel[2] > 180 && pixel[1] < 100) ++azules;
    }
  }
  CHECK(verdes > 0);  // el rostro
  CHECK(azules > 0);  // el alrededor (el margen)
}

// Sin foto o sin rostro no hay miniatura: el campo se omite, no se manda vacio.
TEST(sin_foto_o_sin_rostro_no_hay_miniatura) {
  const cv::Mat foto = foto_sintetica(240, 320);
  CHECK(!encode_thumbnail(cv::Mat(), cv::Rect2f(120.0f, 60.0f, 80.0f, 80.0f)).has_value());
  CHECK(!encode_thumbnail(foto, cv::Rect2f()).has_value());
  CHECK(!encode_thumbnail(foto, cv::Rect2f(10.0f, 10.0f, 0.0f, 40.0f)).has_value());
  CHECK(!encode_thumbnail(foto, cv::Rect2f(10.0f, 10.0f, 40.0f, -1.0f)).has_value());
}

// Un rostro fuera de la foto no da miniatura; uno a medias se recorta a lo que hay.
TEST(un_rostro_fuera_de_la_foto_no_da_miniatura) {
  const cv::Mat foto = foto_sintetica(240, 320);
  CHECK(!encode_thumbnail(foto, cv::Rect2f(1000.0f, 1000.0f, 80.0f, 80.0f)).has_value());
  const std::optional<std::string> a_medias =
      encode_thumbnail(foto, cv::Rect2f(300.0f, 220.0f, 80.0f, 80.0f));
  CHECK(a_medias.has_value());
}

// Aunque la foto sea ruido (que no comprime), la miniatura no pasa del tope del
// contrato: se baja la calidad hasta que cabe.
TEST(la_miniatura_nunca_pasa_del_tope_del_contrato) {
  cv::Mat ruido(240, 320, CV_8UC3);
  cv::RNG rng(7);
  rng.fill(ruido, cv::RNG::UNIFORM, 0, 256);
  const std::optional<std::string> miniatura =
      encode_thumbnail(ruido, cv::Rect2f(120.0f, 60.0f, 80.0f, 80.0f));
  if (miniatura.has_value()) {
    CHECK(base64_decode(*miniatura).size() <= kThumbnailMaxBytes);
    const cv::Mat imagen = cv::imdecode(base64_decode(*miniatura), cv::IMREAD_COLOR);
    CHECK(imagen.cols <= kThumbnailWidth);
    CHECK(imagen.rows <= kThumbnailHeight);
  }
}
