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
#include <string>

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
  // Similitud (coseno) minima entre una pose y la media de las demas para que la pose
  // sobreviva al descarte de outliers del registro (plan 5.4, paso 5). PROVISIONAL: se
  // recalibra con LFW (hito del 18 de noviembre, plan, secciones 9 y 12.1).
  float min_similitud_pose = 0.60f;
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

// La configuracion completa del proceso.
struct ProcessConfig {
  MqttSettings mqtt;
  CaptureSettings capture;
  GallerySettings gallery;
  ThresholdSettings thresholds;
  ServiceSettings service;
  HttpSettings http;

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

}  // namespace vision
