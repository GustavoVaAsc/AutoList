// Camara falsa: un servidor HTTP que devuelve una JPEG sintetica (o una imagen dada) en GET /capture.
// Simula a la XIAO para probar el servicio de vision sin la placa de campo.
//
// Uso: camara_falsa [--puerto N] [--host H] [--ancho W] [--alto H] [--imagen RUTA]
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <httplib.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace {

// Imagen de prueba: ruido sobre un fondo con un par de bordes, para que la varianza
// del Laplaciano (el filtro de nitidez) no sea cero.
cv::Mat imagen_sintetica(int ancho, int alto) {
  cv::Mat imagen(alto, ancho, CV_8UC3);
  cv::randu(imagen, cv::Scalar::all(0), cv::Scalar::all(255));
  cv::rectangle(imagen, cv::Rect(ancho / 4, alto / 4, ancho / 2, alto / 2), cv::Scalar::all(255),
                cv::FILLED);
  cv::rectangle(imagen, cv::Rect(ancho / 4 + 10, alto / 4 + 10, ancho / 2 - 20, alto / 2 - 20),
                cv::Scalar::all(0), cv::FILLED);
  return imagen;
}

}  // namespace

int main(int argc, char** argv) {
  int puerto = 18080;
  std::string host = "127.0.0.1";
  int ancho = 640;
  int alto = 480;
  std::string ruta_imagen;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      std::cout << "Uso: camara_falsa [--puerto N] [--host H] [--ancho W] [--alto H] [--imagen RUTA]\n"
                   "\n"
                   "Sirve una JPEG sintetica (o la imagen indicada) en GET /capture.\n";
      return 0;
    }
    if (arg == "--puerto" && i + 1 < argc) {
      puerto = std::atoi(argv[++i]);
      continue;
    }
    if (arg == "--host" && i + 1 < argc) {
      host = argv[++i];
      continue;
    }
    if (arg == "--ancho" && i + 1 < argc) {
      ancho = std::atoi(argv[++i]);
      continue;
    }
    if (arg == "--alto" && i + 1 < argc) {
      alto = std::atoi(argv[++i]);
      continue;
    }
    if (arg == "--imagen" && i + 1 < argc) {
      ruta_imagen = argv[++i];
      continue;
    }
    std::cerr << "opcion desconocida: " << arg << '\n';
    return 2;
  }
  if (puerto <= 0 || puerto > 65535 || ancho <= 0 || alto <= 0) {
    std::cerr << "el puerto, el ancho y el alto tienen que ser positivos\n";
    return 2;
  }

  std::vector<std::uint8_t> jpeg;
  if (!ruta_imagen.empty()) {
    const cv::Mat img = cv::imread(ruta_imagen, cv::IMREAD_COLOR);
    if (img.empty()) {
      std::cerr << "no se pudo leer la imagen " << ruta_imagen << '\n';
      return 1;
    }
    if (!cv::imencode(".jpg", img, jpeg)) {
      std::cerr << "no se pudo codificar la imagen " << ruta_imagen << '\n';
      return 1;
    }
  } else {
    if (!cv::imencode(".jpg", imagen_sintetica(ancho, alto), jpeg)) {
      std::cerr << "no se pudo codificar la imagen de prueba\n";
      return 1;
    }
  }

  httplib::Server server;
  server.Get("/capture", [&jpeg](const httplib::Request&, httplib::Response& respuesta) {
    respuesta.set_content(reinterpret_cast<const char*>(jpeg.data()), jpeg.size(),
                           "image/jpeg");
  });
  server.Get("/estado", [](const httplib::Request&, httplib::Response& respuesta) {
    respuesta.set_content("{\"ok\":true}\n", "application/json");
  });

  std::cout << "camara falsa escuchando en http://" << host << ':' << puerto << "/capture"
            << std::endl;
  if (!server.listen(host, puerto)) {
    std::cerr << "no se pudo escuchar en " << host << ':' << puerto << '\n';
    return 1;
  }
  return 0;
}
