// Herramienta de diagnostico del detector: carga un .nb de detector con NpuFaceDetector
// (el mismo adaptador que usa el servicio), le pasa una imagen e imprime las detecciones
// en pixeles de la imagen original: score, caja [x0, y0, x1, y1] y los 5 landmarks.
//
// Sirve para comparar el pre y el postproceso del servicio con la demo del fabricante
// (examples/retinaface del awnpu_model_zoo) sobre la misma imagen.
//
// Uso: detector_prueba <detector.nb> <imagen> [bgr|rgb] [iteraciones=1]
// Codigos de salida: 0 bien, 1 error (modelo o imagen), 2 uso.
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include "vision/npu.hpp"
#include "vision/npu_face_detector.hpp"

namespace {

using namespace vision;

void uso(const char* programa) {
  std::cerr << "uso: " << programa << " <detector.nb> <imagen> [bgr|rgb] [iteraciones=1]\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3 || argc > 5) {
    uso(argv[0]);
    return 2;
  }
  NpuDetectorOptions opciones;
  // El RetinaFace del fabricante espera BGR (REVERSE_CHANNEL = True en su config_yml.py).
  opciones.color_order = ColorOrder::Bgr;
  if (argc >= 4) {
    const std::string color = argv[3];
    if (color == "rgb") {
      opciones.color_order = ColorOrder::Rgb;
    } else if (color != "bgr") {
      uso(argv[0]);
      return 2;
    }
  }
  long iteraciones = 1;
  if (argc == 5) {
    char* fin = nullptr;
    errno = 0;
    iteraciones = std::strtol(argv[4], &fin, 10);
    if (errno != 0 || fin == argv[4] || *fin != '\0' || iteraciones < 1 || iteraciones > 10000) {
      uso(argv[0]);
      return 2;
    }
  }

  // Como la demo: cv::imread en color (BGR).
  const cv::Mat imagen = cv::imread(argv[2], cv::IMREAD_COLOR);
  if (imagen.empty()) {
    std::cerr << "no se pudo leer la imagen " << argv[2] << '\n';
    return 1;
  }

  try {
    npu::Driver driver;
    NpuFaceDetector detector(driver, argv[1], opciones);

    std::printf("imagen %dx%d, color %s\n", imagen.cols, imagen.rows,
                opciones.color_order == ColorOrder::Bgr ? "bgr" : "rgb");

    std::vector<FaceDetection> detecciones;
    double total_ms = 0.0;
    for (long i = 0; i < iteraciones; ++i) {
      const auto inicio = std::chrono::steady_clock::now();
      detecciones = detector.detect(imagen);
      const auto fin = std::chrono::steady_clock::now();
      total_ms += std::chrono::duration<double, std::milli>(fin - inicio).count();
    }
    std::printf("detect (letterbox + preproceso + NPU + decode): %.2f ms de media en %ld "
                "iteraciones\n",
                total_ms / static_cast<double>(iteraciones), iteraciones);

    std::printf("detecciones: %zu\n", detecciones.size());
    for (const FaceDetection& d : detecciones) {
      const float x1 = d.bbox.x + d.bbox.width;
      const float y1 = d.bbox.y + d.bbox.height;
      // La misma linea que imprime la demo ("%3.0f%%, [%4.0f, %4.0f, %4.0f, %4.0f]") y,
      // debajo, los valores con dos decimales.
      std::printf("%3.0f%%, [%4.0f, %4.0f, %4.0f, %4.0f], face\n", d.score * 100.0f, d.bbox.x,
                  d.bbox.y, x1, y1);
      std::printf("  score %.6f caja [%.2f, %.2f, %.2f, %.2f]\n", d.score, d.bbox.x, d.bbox.y,
                  x1, y1);
      for (const cv::Point2f& punto : d.landmarks) {
        std::printf("  %.2f %.2f\n", punto.x, punto.y);
      }
    }
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
