#include "vision/thumbnail.hpp"

#include <algorithm>
#include <cmath>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace vision {

namespace {

// Margen alrededor del recuadro del rostro (fraccion de su tamano): el recorte lleva
// algo de contexto (frente y barbilla) para que la miniatura se entienda.
constexpr float kMargen = 0.30f;

// El mismo margen, en el alto y en el ancho.
constexpr float kEnsanche = 1.0f + 2.0f * kMargen;

// Calidades con las que se intenta codificar el JPEG, de la mejor a la mas pequena:
// la primera que cabe en el tope gana.
constexpr int kCalidades[] = {80, 70, 60, 50, 40, 30, 20};

}  // namespace

std::string base64_encode(const std::vector<unsigned char>& datos) {
  static constexpr char kAlfabeto[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string salida;
  salida.reserve(((datos.size() + 2) / 3) * 4);
  std::size_t i = 0;
  while (i + 2 < datos.size()) {
    const unsigned int trio =
        (static_cast<unsigned int>(datos[i]) << 16) |
        (static_cast<unsigned int>(datos[i + 1]) << 8) | static_cast<unsigned int>(datos[i + 2]);
    salida.push_back(kAlfabeto[(trio >> 18) & 0x3Fu]);
    salida.push_back(kAlfabeto[(trio >> 12) & 0x3Fu]);
    salida.push_back(kAlfabeto[(trio >> 6) & 0x3Fu]);
    salida.push_back(kAlfabeto[trio & 0x3Fu]);
    i += 3;
  }
  const std::size_t resto = datos.size() - i;
  if (resto == 1) {
    const unsigned int trio = static_cast<unsigned int>(datos[i]) << 16;
    salida.push_back(kAlfabeto[(trio >> 18) & 0x3Fu]);
    salida.push_back(kAlfabeto[(trio >> 12) & 0x3Fu]);
    salida.push_back('=');
    salida.push_back('=');
  } else if (resto == 2) {
    const unsigned int trio =
        (static_cast<unsigned int>(datos[i]) << 16) | (static_cast<unsigned int>(datos[i + 1]) << 8);
    salida.push_back(kAlfabeto[(trio >> 18) & 0x3Fu]);
    salida.push_back(kAlfabeto[(trio >> 12) & 0x3Fu]);
    salida.push_back(kAlfabeto[(trio >> 6) & 0x3Fu]);
    salida.push_back('=');
  }
  return salida;
}

std::optional<std::string> encode_thumbnail(const cv::Mat& frame, const cv::Rect2f& rostro) {
  if (frame.empty()) return std::nullopt;
  if (!(rostro.width > 0.0f) || !(rostro.height > 0.0f)) return std::nullopt;
  if (!std::isfinite(rostro.x) || !std::isfinite(rostro.y) || !std::isfinite(rostro.width) ||
      !std::isfinite(rostro.height)) {
    return std::nullopt;
  }

  // Recuadro del rostro con margen, recortado a la foto.
  cv::Rect recorte(
      static_cast<int>(std::floor(rostro.x - rostro.width * kMargen)),
      static_cast<int>(std::floor(rostro.y - rostro.height * kMargen)),
      static_cast<int>(std::ceil(rostro.width * kEnsanche)),
      static_cast<int>(std::ceil(rostro.height * kEnsanche)));
  recorte &= cv::Rect(0, 0, frame.cols, frame.rows);
  if (recorte.width < 1 || recorte.height < 1) return std::nullopt;

  // Reduce a 160x120 guardando la proporcion (el lado que manda manda).
  const double escala = std::min(static_cast<double>(kThumbnailWidth) / recorte.width,
                                 static_cast<double>(kThumbnailHeight) / recorte.height);
  const int ancho = std::max(1, static_cast<int>(std::lround(recorte.width * escala)));
  const int alto = std::max(1, static_cast<int>(std::lround(recorte.height * escala)));
  cv::Mat pequena;
  cv::resize(frame(recorte), pequena, cv::Size(ancho, alto), 0, 0,
             escala < 1.0 ? cv::INTER_AREA : cv::INTER_LINEAR);

  for (const int calidad : kCalidades) {
    std::vector<unsigned char> jpeg;
    try {
      if (!cv::imencode(".jpg", pequena, jpeg, {cv::IMWRITE_JPEG_QUALITY, calidad})) continue;
    } catch (const cv::Exception&) {
      return std::nullopt;
    }
    if (jpeg.size() <= kThumbnailMaxBytes) return base64_encode(jpeg);
  }
  // Ni con la calidad mas baja cabe: no hay miniatura (preferible a mandar una que el
  // contrato no admite).
  return std::nullopt;
}

}  // namespace vision
