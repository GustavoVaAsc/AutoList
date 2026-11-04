// Genera el dataset de rostros recortados y alineados a 112x112 para la calibracion
// del .nb (Acuity/Pegasus) y para el umbral de decision con LFW.
//
// El recorte usa EXACTAMENTE el mismo warp que el servicio en produccion:
// vision::align_face (orangepi/vision/src/alignment.cpp), que aplica la
// transformacion de similitud (Umeyama) de los 5 landmarks a kArcFaceReference
// y luego warpAffine a 112x112. No se reimplementa la transformacion aqui.
//
// El manifiesto de entrada tiene una linea por imagen:
//   ruta_de_imagen<sep>x1 y1 x2 y2 x3 y3 x4 y4 x5 y5
// con los 5 landmarks en pixeles de la imagen, en el orden de
// FaceDetection::landmarks: ojo izquierdo, ojo derecho, nariz, boca izquierda,
// boca derecha. El separador puede ser TAB (una ruta con espacios) o espacios.
// Las lineas vacias y las que empiezan por # se ignoran.
//
// Salida: un PNG de 112x112 por imagen, en el directorio de salida, con el
// nombre del archivo original sin extension.
//
// Uso:
//   alinear_lfw --manifest RUTA --out DIR

#include <array>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "vision/alignment.hpp"

namespace {

struct Entrada {
  std::string ruta;
  std::array<cv::Point2f, 5> landmarks;
};

constexpr const char* kUso =
    "Uso: alinear_lfw --manifest RUTA --out DIR\n"
    "\n"
    "Alinea las imagens del manifiesto a 112x112 con vision::align_face y escribe\n"
    "un PNG por imagen en el directorio de salida.\n";

// Divide la linea en campos: por TAB si hay tabs, por espacios si no. Devuelve
// false si la linea no tiene la forma esperada.
bool separar_campos(const std::string& linea, std::vector<std::string>& campos) {
  campos.clear();
  if (linea.find('\t') != std::string::npos) {
    std::stringstream ss(linea);
    std::string campo;
    while (std::getline(ss, campo, '\t')) campos.push_back(campo);
    // Un TAB final deja un campo vacio: se descarta.
    if (!campos.empty() && campos.back().empty()) campos.pop_back();
    return true;
  }
  std::stringstream ss(linea);
  std::string campo;
  while (ss >> campo) campos.push_back(campo);
  return true;
}

// Convierte un texto en float. Devuelve false si no es un numero completo.
bool a_float(const std::string& texto, float& valor) {
  errno = 0;
  char* fin = nullptr;
  const float resultado = std::strtof(texto.c_str(), &fin);
  if (fin == texto.c_str() || *fin != '\0') return false;
  if (errno == ERANGE) return false;
  valor = resultado;
  return true;
}

// Parsea una linea del manifiesto. Devuelve false con un motivo en `error` si la
// linea esta mal formada.
bool parsear_linea(const std::string& linea, Entrada& entrada, std::string& error) {
  std::vector<std::string> campos;
  separar_campos(linea, campos);
  if (campos.size() != 11) {
    error = "se esperaban 11 campos (ruta y 10 coordenadas) y hay " +
            std::to_string(campos.size());
    return false;
  }
  entrada.ruta = campos[0];
  if (entrada.ruta.empty()) {
    error = "la ruta de la imagen esta vacia";
    return false;
  }
  for (std::size_t i = 0; i < entrada.landmarks.size(); ++i) {
    float x = 0.0f;
    float y = 0.0f;
    if (!a_float(campos[1 + 2 * i], x) || !a_float(campos[2 + 2 * i], y)) {
      error = "coordenada no numerica en el landmark " + std::to_string(i + 1) +
              " (valor '" + campos[1 + 2 * i] + " " + campos[2 + 2 * i] + "')";
      return false;
    }
    entrada.landmarks[i] = cv::Point2f(x, y);
  }
  return true;
}

// Nombre de salida: el nombre del archivo original sin extension.
std::string nombre_salida(const std::string& ruta) {
  const std::filesystem::path p(ruta);
  std::string nombre = p.stem().string();
  if (nombre.empty()) nombre = p.filename().string();
  return nombre;
}

}  // namespace

int main(int argc, char** argv) {
  std::string manifiesto;
  std::string salida;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--manifest" && i + 1 < argc) {
      manifiesto = argv[++i];
    } else if (arg == "--out" && i + 1 < argc) {
      salida = argv[++i];
    } else if (arg == "-h" || arg == "--help") {
      std::cout << kUso;
      return 0;
    } else {
      std::cerr << "Error: argumento desconocido o incompleto: " << arg << "\n" << kUso;
      return 1;
    }
  }

  if (manifiesto.empty() || salida.empty()) {
    std::cerr << "Error: falta --manifest o --out.\n" << kUso;
    return 1;
  }

  std::ifstream entrada(manifiesto);
  if (!entrada) {
    std::cerr << "Error: no se pudo abrir el manifiesto " << manifiesto << "\n";
    return 1;
  }

  // Fase 1: leer y validar el manifiesto entero. Una linea mal formada es un
  // error del manifiesto: no se procesa nada y se sale con error.
  std::vector<Entrada> entradas;
  std::string linea;
  int numero_linea = 0;
  while (std::getline(entrada, linea)) {
    ++numero_linea;
    if (!linea.empty() && linea.back() == '\r') linea.pop_back();
    const std::string recortada = [&] {
      std::size_t ini = linea.find_first_not_of(" \t");
      if (ini == std::string::npos) return std::string();
      std::size_t fin = linea.find_last_not_of(" \t");
      return linea.substr(ini, fin - ini + 1);
    }();
    if (recortada.empty() || recortada[0] == '#') continue;

    Entrada fila;
    std::string error;
    if (!parsear_linea(recortada, fila, error)) {
      std::cerr << "Error: " << manifiesto << ":" << numero_linea << ": " << error
                << "\n";
      return 1;
    }
    entradas.push_back(fila);
  }

  // Fase 2: alinear y escribir. Los fallos por imagen se omiten con un aviso.
  std::error_code ec;
  std::filesystem::create_directories(salida, ec);
  if (ec) {
    std::cerr << "Error: no se pudo crear el directorio de salida " << salida << ": "
              << ec.message() << "\n";
    return 1;
  }

  int escritas = 0;
  int omitidas = 0;
  std::set<std::string> nombres_usados;
  for (const Entrada& fila : entradas) {
    // IMREAD_COLOR deja siempre BGR de 8 bits, el formato con el que trabaja el
    // servicio, y descarta el canal alfa si lo hubiera.
    const cv::Mat imagen = cv::imread(fila.ruta, cv::IMREAD_COLOR);
    if (imagen.empty()) {
      std::cerr << "Aviso: se omite " << fila.ruta << ": no se pudo leer la imagen\n";
      ++omitidas;
      continue;
    }

    cv::Mat alineada;
    try {
      alineada = vision::align_face(imagen, fila.landmarks);
    } catch (const std::exception& e) {
      std::cerr << "Aviso: se omite " << fila.ruta << ": " << e.what() << "\n";
      ++omitidas;
      continue;
    }

    const std::filesystem::path destino =
        std::filesystem::path(salida) / (nombre_salida(fila.ruta) + ".png");
    // Dos imagenes distintas con el mismo nombre sin extension apuntan al
    // mismo PNG: la segunda pisa a la primera. Se avisa, pero se sigue.
    if (!nombres_usados.insert(nombre_salida(fila.ruta)).second) {
      std::cerr << "Aviso: " << fila.ruta << " comparte nombre de salida con otra imagen ("
                << nombre_salida(fila.ruta) << ".png): se sobrescribe\n";
    }
    if (!cv::imwrite(destino.string(), alineada)) {
      std::cerr << "Aviso: se omite " << fila.ruta << ": no se pudo escribir " << destino
                << "\n";
      ++omitidas;
      continue;
    }
    ++escritas;
  }

  std::cout << "Resumen: " << escritas << " escritas, " << omitidas << " omitidas, "
            << entradas.size() << " lineas de datos" << std::endl;
  if (escritas == 0) {
    std::cerr << "Error: no se escribio ninguna imagen\n";
    return 1;
  }
  return 0;
}
