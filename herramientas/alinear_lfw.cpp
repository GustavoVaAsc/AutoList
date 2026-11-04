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
// boca derecha. Se aceptan tres formatos:
//   (a) "ruta x1 y1 x2 y2 ... x5 y5": todo separado por espacios.
//   (b) "ruta<TAB>x1 y1 x2 y2 ... x5 y5": TAB entre la ruta y las coordenadas.
//   (c) "ruta<TAB>x1<TAB>y1<TAB>...<TAB>y5": todo separado por TAB.
// En (b) y (c) la ruta puede contener espacios. Las lineas vacias y las que
// empiezan por # se ignoran.
//
// Salida: un PNG de 112x112 por imagen, en el directorio de salida, con el
// nombre del archivo original sin extension.
//
// AVISO: la salida son recortes de rostros, es decir datos biometricos
// irreversibles. Por eso la herramienta se niega a escribir dentro del
// repositorio: un "git add ." meteria la biometria en el historial de forma
// permanente. El directorio de salida tiene que estar fuera del repositorio.
//
// Uso:
//   alinear_lfw --manifest RUTA --out DIR [--repo-root DIR]

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
#include <system_error>
#include <vector>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "vision/alignment.hpp"

namespace {

struct Entrada {
  std::string ruta;
  std::array<cv::Point2f, 5> landmarks;
  // Numero de linea del manifiesto. Los avisos dicen la linea y no la ruta: la
  // ruta suele llevar el nombre de la persona (LFW: "Apolo_W_Newton_0001.jpg")
  // y eso es informacion biometrica que no tiene que salir en un log.
  int linea = 0;
};

// Lado maximo admitido por imagen, en pixeles. La placa tiene 4 GB: una imagen
// enorme haria que la herramienta reserve mucha memoria al decodificar.
constexpr int kLadoMaximoPx = 4000;

constexpr const char* kUso =
    "Uso: alinear_lfw --manifest RUTA --out DIR [--repo-root DIR]\n"
    "\n"
    "Alinea las imagenes del manifiesto a 112x112 con vision::align_face y escribe\n"
    "un PNG por imagen en el directorio de salida.\n"
    "\n"
    "Formatos de manifiesto aceptados (una imagen por linea):\n"
    "  (a) ruta x1 y1 x2 y2 x3 y3 x4 y4 x5 y5      (todo separado por espacios)\n"
    "  (b) ruta<TAB>x1 y1 x2 y2 x3 y3 x4 y4 x5 y5   (TAB entre ruta y coordenadas)\n"
    "  (c) ruta<TAB>x1<TAB>y1<TAB>...<TAB>y5       (todo separado por TAB)\n"
    "En (b) y (c) la ruta puede contener espacios.\n"
    "\n"
    "AVISO: la salida contiene recortes de rostros (datos biometricos\n"
    "irreversibles) y no debe versionarse. Por eso la herramienta rechaza\n"
    "escribir dentro del repositorio: el directorio de salida tiene que estar\n"
    "fuera de el.\n";

// Divide el texto en campos separados por cualquier espacio en blanco
// (espacios o TAB). Los campos vacios se descartan.
std::vector<std::string> dividir_por_espacios(const std::string& texto) {
  std::vector<std::string> campos;
  std::stringstream ss(texto);
  std::string campo;
  while (ss >> campo) campos.push_back(campo);
  return campos;
}

// Quita los espacios y TAB de los extremos.
std::string recortar(const std::string& texto) {
  const std::size_t ini = texto.find_first_not_of(" \t");
  if (ini == std::string::npos) return std::string();
  const std::size_t fin = texto.find_last_not_of(" \t");
  return texto.substr(ini, fin - ini + 1);
}

// Resultado de convertir un texto en float.
enum class EstadoNumero {
  kOk,
  kNoNumerico,    // no es un numero completo
  kFueraDeRango,  // el numero no cabe en un float
};

// Motivo de un fallo de conversion, sin el valor de la coordenada.
const char* motivo_numero(EstadoNumero estado) {
  switch (estado) {
    case EstadoNumero::kFueraDeRango:
      return "fuera de rango";
    case EstadoNumero::kNoNumerico:
    default:
      return "no numerica";
  }
}

// Convierte un texto en float. Solo admite el numero completo: "12" vale,
// "12x" no. "nan" e "inf" se parsean, asi que el llamante comprueba despues
// que el valor sea finito.
EstadoNumero a_float(const std::string& texto, float& valor) {
  errno = 0;
  char* fin = nullptr;
  const float resultado = std::strtof(texto.c_str(), &fin);
  if (fin == texto.c_str() || *fin != '\0') return EstadoNumero::kNoNumerico;
  if (errno == ERANGE) return EstadoNumero::kFueraDeRango;
  valor = resultado;
  return EstadoNumero::kOk;
}

// Parsea una linea del manifiesto. Devuelve false con un motivo en `error` si la
// linea esta mal formada. El motivo nunca incluye el valor de la coordenada:
// son datos biometricos derivados y no van a la consola ni al historial.
bool parsear_linea(const std::string& linea, Entrada& entrada, std::string& error) {
  std::string ruta;
  std::vector<std::string> coordenadas;
  const std::size_t tab = linea.find('\t');
  if (tab == std::string::npos) {
    // Formato (a): todo separado por espacios.
    const std::vector<std::string> campos = dividir_por_espacios(linea);
    if (campos.size() != 11) {
      error = "se esperaban 11 campos (ruta y 10 coordenadas) y hay " +
              std::to_string(campos.size());
      return false;
    }
    ruta = campos.front();
    coordenadas.assign(campos.begin() + 1, campos.end());
  } else {
    // Formatos (b) y (c): la ruta va hasta el primer TAB y puede contener
    // espacios; las coordenadas van separadas por espacios o por TAB.
    ruta = recortar(linea.substr(0, tab));
    coordenadas = dividir_por_espacios(linea.substr(tab + 1));
    if (coordenadas.size() != 10) {
      error = "se esperaban 10 coordenadas (x1 y1 ... x5 y5) y hay " +
              std::to_string(coordenadas.size());
      return false;
    }
  }
  if (ruta.empty()) {
    error = "la ruta de la imagen esta vacia";
    return false;
  }
  entrada.ruta = ruta;
  for (std::size_t i = 0; i < entrada.landmarks.size(); ++i) {
    float x = 0.0f;
    float y = 0.0f;
    const std::string etiqueta = "landmark " + std::to_string(i + 1);
    const EstadoNumero estado_x = a_float(coordenadas[2 * i], x);
    if (estado_x != EstadoNumero::kOk) {
      error = std::string("coordenada ") + motivo_numero(estado_x) + " en el " + etiqueta + " (x)";
      return false;
    }
    const EstadoNumero estado_y = a_float(coordenadas[2 * i + 1], y);
    if (estado_y != EstadoNumero::kOk) {
      error = std::string("coordenada ") + motivo_numero(estado_y) + " en el " + etiqueta + " (y)";
      return false;
    }
    if (!std::isfinite(x) || !std::isfinite(y)) {
      error = "coordenada no finita en el " + etiqueta + " (x o y)";
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

// Localiza la raiz del repositorio subiendo desde `inicio` hasta encontrar un
// directorio que tenga .git y herramientas/alinear_lfw.sh.
bool buscar_raiz_repo(const std::filesystem::path& inicio, std::filesystem::path& raiz) {
  std::error_code ec;
  std::filesystem::path dir = std::filesystem::weakly_canonical(inicio, ec);
  if (ec) return false;
  while (!dir.empty()) {
    if (std::filesystem::exists(dir / ".git") &&
        std::filesystem::exists(dir / "herramientas" / "alinear_lfw.sh")) {
      raiz = dir;
      return true;
    }
    const std::filesystem::path padre = dir.parent_path();
    if (padre == dir) break;
    dir = padre;
  }
  return false;
}

// Devuelve true si `hijo` esta dentro de `padre` (o es el mismo directorio).
bool esta_dentro(const std::filesystem::path& hijo, const std::filesystem::path& padre) {
  auto it_padre = padre.begin();
  auto it_hijo = hijo.begin();
  for (; it_padre != padre.end(); ++it_padre, ++it_hijo) {
    if (it_hijo == hijo.end()) return false;
    if (*it_hijo != *it_padre) return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  std::string manifiesto;
  std::string salida;
  std::filesystem::path raiz_repo;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--manifest" && i + 1 < argc) {
      manifiesto = argv[++i];
    } else if (arg == "--out" && i + 1 < argc) {
      salida = argv[++i];
    } else if (arg == "--repo-root" && i + 1 < argc) {
      raiz_repo = argv[++i];
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

  // Barrera: la salida son recortes de rostros (datos biometricos
  // irreversibles). Si cae dentro del repositorio, un "git add ." la meteria en
  // el historial de forma permanente, asi que se rechaza antes de escribir nada.
  if (raiz_repo.empty()) {
    const char* entorno = std::getenv("PASELISTA_RAIZ_REPO");
    if (entorno != nullptr && *entorno != '\0') {
      raiz_repo = entorno;
    } else if (!buscar_raiz_repo(std::filesystem::current_path(), raiz_repo)) {
      std::cerr << "Error: no se pudo determinar la raiz del repositorio. Pasa "
                   "--repo-root DIR o define PASELISTA_RAIZ_REPO.\n";
      return 1;
    }
  }
  std::error_code ec;
  const std::filesystem::path raiz_abs = std::filesystem::weakly_canonical(raiz_repo, ec);
  if (ec) {
    std::cerr << "Error: no se pudo resolver la raiz del repositorio " << raiz_repo
              << ": " << ec.message() << "\n";
    return 1;
  }
  const std::filesystem::path salida_abs = std::filesystem::weakly_canonical(salida, ec);
  if (ec) {
    std::cerr << "Error: no se pudo resolver el directorio de salida " << salida << ": "
              << ec.message() << "\n";
    return 1;
  }
  if (esta_dentro(salida_abs, raiz_abs)) {
    std::cerr << "Error: el directorio de salida " << salida_abs.string()
              << " esta dentro del repositorio (" << raiz_abs.string() << ").\n"
              << "Los recortes de rostros son datos biometricos irreversibles: escribirlos "
                 "dentro del repo haria que un 'git add .' los metiera en el historial de "
                 "forma permanente.\n"
              << "Usa un directorio de salida fuera del repositorio (por ejemplo /tmp/salida "
                 "o un volumen externo) y no los versiona.\n";
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
    fila.linea = numero_linea;
    std::string error;
    if (!parsear_linea(recortada, fila, error)) {
      std::cerr << "Error: " << manifiesto << ":" << numero_linea << ": " << error
                << "\n";
      return 1;
    }
    entradas.push_back(fila);
  }

  // Fase 2: alinear y escribir. Los fallos por imagen se omiten con un aviso.
  std::filesystem::create_directories(salida_abs, ec);
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
      std::cerr << "Aviso: se omite la linea " << fila.linea << ": no se pudo leer la imagen\n";
      ++omitidas;
      continue;
    }
    // La imagen ya esta decodificada: se comprueba el tamano y se omite si es
    // enorme, para no reserving mas memoria en la placa.
    if (imagen.cols > kLadoMaximoPx || imagen.rows > kLadoMaximoPx) {
      std::cerr << "Aviso: se omite la linea " << fila.linea << ": la imagen mide "
                << imagen.cols << "x" << imagen.rows << " y excede el limite de "
                << kLadoMaximoPx << " px por lado\n";
      ++omitidas;
      continue;
    }

    cv::Mat alineada;
    try {
      alineada = vision::align_face(imagen, fila.landmarks);
    } catch (const std::exception& e) {
      std::cerr << "Aviso: se omite la linea " << fila.linea << ": " << e.what() << "\n";
      ++omitidas;
      continue;
    }

    const std::filesystem::path destino = salida_abs / (nombre_salida(fila.ruta) + ".png");
    // Dos imagenes distintas con el mismo nombre sin extension apuntan al
    // mismo PNG: la segunda pisa a la primera. Se avisa, pero se sigue.
    if (!nombres_usados.insert(nombre_salida(fila.ruta)).second) {
      std::cerr << "Aviso: la linea " << fila.linea
                << " comparte nombre de salida con otra imagen: se sobrescribe\n";
    }
    if (!cv::imwrite(destino.string(), alineada)) {
      std::cerr << "Aviso: se omite la linea " << fila.linea
                << ": no se pudo escribir el PNG\n";
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
