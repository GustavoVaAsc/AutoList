// Pruebas de la configuracion del proceso: el analisis del JSON, los valores por
// omision, el rechazo de los documentos invalidos y el mapeo a ServiceConfig.

#include <cstdio>
#include <fstream>
#include <string>

#include "minitest.hpp"
#include "vision/process_config.hpp"

namespace {

using namespace vision;

// Escribe un documento en un archivo temporal y devuelve su ruta.
std::string archivo_temporal(const std::string& contenido, const std::string& sufijo) {
  const std::string ruta = "/tmp/vision-conf-" + sufijo + ".json";
  std::ofstream file(ruta);
  file << contenido;
  file.close();
  return ruta;
}

const char* kConfigCompleta = R"({
  "mqtt": {"host": "10.0.0.5", "port": 1884, "usuario": "vision",
           "clave_env": "MQTT_PASSWORD", "client_id": "vision-prod"},
  "captura": {"url": "http://192.168.1.50/capture", "rafaga": 5, "espera_luz_ms": 400,
              "http_timeout_ms": 1500, "max_body_bytes": 1048576},
  "galeria": {"url": "https://127.0.0.1:1880/api/galeria", "token_env": "GALERIA_TOKEN",
              "timeout_ms": 4000},
  "umbrales": {"min_sharpness": 12.5, "min_recognized_score": 0.7, "min_dudoso_score": 0.5},
  "servicio": {"poll_ms": 50, "max_inbox_events": 32, "modelo": "w600k_mbf-uint8-v1",
               "acepta_png": true},
  "http": {"allow_invalid_certificates": true}
})";

}  // namespace

TEST(config_documento_vacio_usa_omisiones) {
  ProcessConfig config;
  std::string error;
  CHECK(parse_process_config("{}", config, error));
  CHECK_EQ(config.mqtt.host, std::string("127.0.0.1"));
  CHECK_EQ(config.mqtt.port, 1883);
  CHECK_EQ(config.mqtt.user, std::string("vision"));
  CHECK_EQ(config.mqtt.password_env, std::string("MQTT_PASSWORD"));
  CHECK_EQ(config.capture.url, std::string("http://192.168.1.50/capture"));
  CHECK_EQ(config.capture.burst_size, 4);
  CHECK_EQ(config.capture.settle_ms, 300);
  CHECK_EQ(config.capture.http_timeout_ms, 2000);
  CHECK_EQ(config.capture.max_body_bytes, std::size_t{2097152});
  CHECK_EQ(config.gallery.url, std::string("https://127.0.0.1:1880/api/galeria"));
  CHECK_EQ(config.gallery.token_env, std::string("GALERIA_TOKEN"));
  CHECK_EQ(config.gallery.timeout_ms, 5000);
  CHECK_NEAR(config.thresholds.min_sharpness, 0.0, 1e-9);
  CHECK_NEAR(config.thresholds.min_recognized_score, 0.6, 1e-6);
  CHECK_NEAR(config.thresholds.min_dudoso_score, 0.45, 1e-6);
  CHECK_EQ(config.service.poll_ms, 20);
  CHECK_EQ(config.service.max_inbox_events, std::size_t{16});
  CHECK_EQ(config.service.model_name, std::string("w600k_mbf-uint8-v1"));
  CHECK(!config.service.accept_png);
  CHECK(!config.http.allow_invalid_certificates);
}

TEST(config_completa) {
  ProcessConfig config;
  std::string error;
  CHECK(parse_process_config(kConfigCompleta, config, error));
  CHECK_EQ(config.mqtt.host, std::string("10.0.0.5"));
  CHECK_EQ(config.mqtt.port, 1884);
  CHECK_EQ(config.mqtt.user, std::string("vision"));
  CHECK_EQ(config.mqtt.password_env, std::string("MQTT_PASSWORD"));
  CHECK_EQ(config.mqtt.client_id, std::string("vision-prod"));
  CHECK_EQ(config.capture.url, std::string("http://192.168.1.50/capture"));
  CHECK_EQ(config.capture.burst_size, 5);
  CHECK_EQ(config.capture.settle_ms, 400);
  CHECK_EQ(config.capture.http_timeout_ms, 1500);
  CHECK_EQ(config.capture.max_body_bytes, std::size_t{1048576});
  CHECK_EQ(config.gallery.url, std::string("https://127.0.0.1:1880/api/galeria"));
  CHECK_EQ(config.gallery.token_env, std::string("GALERIA_TOKEN"));
  CHECK_EQ(config.gallery.timeout_ms, 4000);
  CHECK_NEAR(config.thresholds.min_sharpness, 12.5, 1e-9);
  CHECK_NEAR(config.thresholds.min_recognized_score, 0.7, 1e-6);
  CHECK_NEAR(config.thresholds.min_dudoso_score, 0.5, 1e-6);
  CHECK_EQ(config.service.poll_ms, 50);
  CHECK_EQ(config.service.max_inbox_events, std::size_t{32});
  CHECK_EQ(config.service.model_name, std::string("w600k_mbf-uint8-v1"));
  CHECK(config.service.accept_png);
  CHECK(config.http.allow_invalid_certificates);
}

TEST(config_parcial_deja_el_resto_por_omision) {
  ProcessConfig config;
  std::string error;
  CHECK(parse_process_config(R"({"captura": {"rafaga": 3}})", config, error));
  CHECK_EQ(config.capture.burst_size, 3);
  CHECK_EQ(config.capture.http_timeout_ms, 2000);
  CHECK_EQ(config.mqtt.host, std::string("127.0.0.1"));
  CHECK_EQ(config.mqtt.port, 1883);
  CHECK_EQ(config.service.poll_ms, 20);
  CHECK(!config.http.allow_invalid_certificates);
}

TEST(config_rechaza_tipo_incorrecto) {
  ProcessConfig config;
  std::string error;
  CHECK(!parse_process_config(R"({"captura": {"rafaga": "cinco"}})", config, error));
  CHECK(error.find("rafaga") != std::string::npos);
}

TEST(config_rechaza_clave_desconocida) {
  ProcessConfig config;
  std::string error;
  CHECK(!parse_process_config(R"({"captura": {"rafagaa": 3}})", config, error));
  CHECK(error.find("rafagaa") != std::string::npos);
}

TEST(config_rechaza_valor_fuera_de_rango) {
  ProcessConfig config;
  std::string error;
  CHECK(!parse_process_config(R"({"mqtt": {"port": 70000}})", config, error));
  CHECK(!parse_process_config(R"({"umbrales": {"min_dudoso_score": 0.9,
                                               "min_recognized_score": 0.5}})",
                               config, error));
  CHECK(!parse_process_config(R"({"servicio": {"max_inbox_events": 1}})", config, error));
}

TEST(config_rechaza_json_invalido) {
  ProcessConfig config;
  std::string error;
  CHECK(!parse_process_config("{no es json", config, error));
}

TEST(config_mapeo_a_service_config) {
  ProcessConfig config;
  std::string error;
  CHECK(parse_process_config(kConfigCompleta, config, error));
  const ServiceConfig servicio = config.to_service_config();
  CHECK_EQ(servicio.capture_url, config.capture.url);
  CHECK_EQ(servicio.burst_size, config.capture.burst_size);
  CHECK_EQ(servicio.settle_time.count(), 400);
  CHECK_EQ(servicio.min_sharpness, 12.5);
  CHECK_NEAR(servicio.min_recognized_score, 0.7, 1e-6);
  CHECK_NEAR(servicio.min_dudoso_score, 0.5, 1e-6);
  CHECK_EQ(servicio.http_timeout.count(), 1500);
  CHECK_EQ(servicio.max_body_bytes, std::size_t{1048576});
  CHECK_EQ(servicio.max_inbox_events, std::size_t{32});
  CHECK(servicio.accept_png);
  CHECK_EQ(servicio.model_name, config.service.model_name);
}

TEST(config_archivo_inexistente_deja_omisiones) {
  ProcessConfig config;
  bool found = true;
  std::string error;
  CHECK(load_process_config("/tmp/vision-conf-no-existe.json", config, found, error));
  CHECK(!found);
  CHECK_EQ(config.mqtt.port, 1883);
  CHECK_EQ(config.service.poll_ms, 20);
}

TEST(config_archivo_valido_se_carga) {
  const std::string ruta = archivo_temporal(kConfigCompleta, "completa");
  ProcessConfig config;
  bool found = false;
  std::string error;
  CHECK(load_process_config(ruta, config, found, error));
  CHECK(found);
  CHECK_EQ(config.capture.burst_size, 5);
  std::remove(ruta.c_str());
}

TEST(config_archivo_invalido_falla) {
  const std::string ruta = archivo_temporal(R"({"mqtt": {"port": "mil"}})", "invalida");
  ProcessConfig config;
  bool found = false;
  std::string error;
  CHECK(!load_process_config(ruta, config, found, error));
  CHECK(found);
  CHECK(!error.empty());
  std::remove(ruta.c_str());
}
