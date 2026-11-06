#pragma once

// Configuracion del proceso de produccion (archivo JSON) y su mapeo a ServiceConfig.
//
// El archivo de configuracion NO es un secreto: la contrasena del broker y el token de
// la galeria se leen de las variables de entorno que indica el propio archivo
// ("mqtt.clave_env" y "galeria.token_env"). Un archivo de configuracion vive en disco
// y en los respaldos: no puede llevar credenciales.
//
// Las claves van en español, como el resto del proyecto. Un archivo invalido (un tipo
// equivocado, un valor fuera de rango o una clave desconocida) hace fallar la carga:
// arrancar con omisiones a medias seria peor que no arrancar.

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

#include "vision/npu_preprocess.hpp"
#include "vision/vision_service.hpp"

namespace vision {

// Bloque "mqtt": como se conecta el bus al broker.
struct MqttSettings {
  std::string host = "127.0.0.1";
  int port = 1883;
  std::string user = "vision";
  // Nombre de la variable de entorno con la contrasena (nunca la contrasena misma).
  std::string password_env = "MQTT_PASSWORD";
  std::string client_id = "vision";
};

// Bloque "captura": de donde se piden las fotos y con que plazo.
struct CaptureSettings {
  std::string url = "http://192.168.1.50/capture";
  int burst_size = 4;
  int settle_ms = 300;
  int http_timeout_ms = 2000;
  std::size_t max_body_bytes = 2u * 1024u * 1024u;
};

// Bloque "galeria": el endpoint de Node-RED y el token.
struct GallerySettings {
  std::string url = "https://127.0.0.1:1880/api/galeria";
  // Nombre de la variable de entorno con el token (nunca el token mismo).
  std::string token_env = "GALERIA_TOKEN";
  int timeout_ms = 5000;
};

// Bloque "umbrales": los del filtro de calidad y los de la decision.
struct ThresholdSettings {
  double min_sharpness = 0.0;
  float min_recognized_score = 0.60f;
  float min_dudoso_score = 0.45f;
  // Similitud (coseno) minima entre una pose y la media de las candidatas para que la pose
  // sobreviva al descarte de outliers del registro (plan 5.4, paso 5; el descarte es
  // iterativo: solo se descarta la peor mientras su coseno con la media quede por debajo
  // del umbral). PROVISIONAL: se recalibra con LFW (hito del 18 de noviembre, plan,
  // secciones 9 y 12.1).
  float min_similitud_pose = 0.50f;
};

// Bloque "servicio": el ritmo del bucle y lo que el servicio publica.
struct ServiceSettings {
  int poll_ms = 20;
  std::size_t max_inbox_events = 16;
  std::string model_name = "w600k_mbf-uint8-v1";
  bool accept_png = false;
};

// Bloque "http": como se comporta el cliente HTTP.
struct HttpSettings {
  // El certificado de Node-RED es autofirmado: con false (por omision) la conexion a
  // https:// se rechaza. Hay que activarlo a mano hasta que haya un certificado valido.
  bool allow_invalid_certificates = false;
  // Ruta a un CA (o al certificado autofirmado de Node-RED) con el que se valida el
  // servidor. Con esta ruta se confía en el certificado concreto en vez de apagar la
  // validacion. Vacia = sin CA adicional.
  std::string ca_file;
};

// Bloque "npu": los parametros de los adaptadores sobre la NPU, que dependen de como se
// convirtio cada .nb.
//
// El orden de color NO tiene valor por omision: no se puede deducir del tensor y
// equivocarlo no falla, degrada en silencio la deteccion o el embedding (fallo
// abierto). Por eso "color" (detector) y "embed_color" (embedder) son OBLIGATORIAS
// fuera de --dry-run: el analisis solo registra si estan (std::nullopt si no) y
// comprobar_colores_npu decide. Los modelos actuales llevan "bgr" en el detector
// (RetinaFace del fabricante) y "rgb" en el embedder (w600k_mbf de InsightFace).
//
// El embedder es otro modelo: NO hereda nada del detector. Su escala por omision es
// "auto", que se deduce de la cuantizacion de su tensor o falla cerrado al arrancar
// (el EB, uint8 afin con zero_point 127, necesita "arcface").
struct NpuSettings {
  // "color": "rgb" o "bgr". Obligatoria fuera de --dry-run.
  std::optional<ColorOrder> color_order;
  // "escala": "auto" (por omision, se deduce del tensor), "ninguna" (el byte crudo),
  // "unidad" (x/255) o "arcface" ((x-127.5)/127.5).
  ScaleMode scale_mode = ScaleMode::Auto;
  // "ancho" y "alto": tamano de entrada del detector. 0 (por omision) significa "el que
  // tenga el .nb": las anclas se generan para ese tamano.
  int detector_width = 0;
  int detector_height = 0;
  // "embed_color": "rgb" o "bgr". Obligatoria fuera de --dry-run.
  std::optional<ColorOrder> embed_color_order;
  // "embed_escala": como "escala", por omision "auto". No hereda la del detector.
  ScaleMode embed_scale_mode = ScaleMode::Auto;
  // "embed_ancho" y "embed_alto": tamano de entrada del embedder; 0 = el del .nb.
  int embed_width = 0;
  int embed_height = 0;
};

// Comprueba que el bloque "npu" trae los dos ordenes de color ("color" y
// "embed_color"). Devuelve false y deja en `error` las claves que faltan y el valor que
// llevan los modelos actuales. El proceso lo exige fuera de --dry-run (salida 78); en
// --dry-run solo avisa, porque los modelos no se cargan.
bool comprobar_colores_npu(const NpuSettings& npu, std::string& error);

// Una linea con el color y la escala del detector y del embedder tal como quedan tras
// el analisis ("sin fijar" si falta un color), para el registro de arranque. No lleva
// rutas.
std::string resumen_npu(const NpuSettings& npu);

// La configuracion completa del proceso.
struct ProcessConfig {
  MqttSettings mqtt;
  CaptureSettings capture;
  GallerySettings gallery;
  ThresholdSettings thresholds;
  ServiceSettings service;
  HttpSettings http;
  NpuSettings npu;

  // El mapeo a la configuracion de la maquina de estados.
  ServiceConfig to_service_config() const;
};

// Analiza el documento JSON de la configuracion. Devuelve false y deja el motivo en
// `error` si el documento no es valido.
bool parse_process_config(const std::string& document, ProcessConfig& out, std::string& error);

// true si la URL admite el token de la galeria: https, o http contra el propio
// localhost. El token no puede viajar en claro por la red.
bool url_admite_token(const std::string& url);

// Carga el archivo de configuracion. `found` dice si el archivo existia: si no, `out`
// queda con los valores por omision y el proceso avisa por stderr (para poder probar
// el servicio sin instalar nada). Devuelve false y deja el motivo en `error` si el
// archivo existe pero no se puede leer o no es valido.
bool load_process_config(const std::string& path, ProcessConfig& out, bool& found,
                         std::string& error);

// El nombre del modelo ("servicio.modelo") solo puede llevar letras, digitos, punto,
// guion y guion bajo, hasta 64 caracteres. Asi el nombre no puede construir una ruta
// fuera del directorio de modelos (un "../" en el nombre ejecutaria un .nb de donde
// quiera en el driver del kernel).
bool nombre_modelo_valido(const std::string& nombre);

// Ruta del modelo .nb dentro del directorio de modelos. Falla cerrado (lanza
// std::invalid_argument) si el nombre no es valido o si la ruta sale del directorio.
std::filesystem::path ruta_modelo(const std::string& nombre,
                                  const std::filesystem::path& directorio);

}  // namespace vision
