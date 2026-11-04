// Pruebas de la configuracion del proceso: el analisis del JSON, los valores por
// omision, el rechazo de los documentos invalidos y el mapeo a ServiceConfig.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
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
  "umbrales": {"min_sharpness": 12.5, "min_recognized_score": 0.7, "min_dudoso_score": 0.5,
               "min_similitud_pose": 0.55},
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
  CHECK_NEAR(config.thresholds.min_similitud_pose, 0.50, 1e-6);
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
  CHECK_NEAR(config.thresholds.min_similitud_pose, 0.55, 1e-6);
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

// El umbral de similitud de las poses del registro (descarte de outliers, plan 5.4).
TEST(config_carga_el_umbral_de_similitud_de_las_poses) {
  ProcessConfig config;
  std::string error;
  CHECK(parse_process_config(R"({"umbrales": {"min_similitud_pose": 0.75}})", config, error));
  CHECK_NEAR(config.thresholds.min_similitud_pose, 0.75, 1e-6);
  CHECK_NEAR(config.to_service_config().min_pose_similarity, 0.75, 1e-6);
}

TEST(config_rechaza_el_umbral_de_similitud_de_las_poses_fuera_de_rango) {
  ProcessConfig config;
  std::string error;
  CHECK(!parse_process_config(R"({"umbrales": {"min_similitud_pose": 1.5}})", config, error));
  CHECK(error.find("min_similitud_pose") != std::string::npos);
  CHECK(!parse_process_config(R"({"umbrales": {"min_similitud_pose": -0.1}})", config, error));
  CHECK(error.find("min_similitud_pose") != std::string::npos);
}

TEST(config_rechaza_una_clave_desconocida_en_los_umbrales) {
  ProcessConfig config;
  std::string error;
  CHECK(!parse_process_config(R"({"umbrales": {"min_similitud_pose_": 0.6}})", config, error));
  CHECK(error.find("min_similitud_pose_") != std::string::npos);
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
  CHECK_NEAR(servicio.min_pose_similarity, 0.55, 1e-6);
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

// El CA con el que se valida el certificado del servidor (el autofirmado de Node-RED).
TEST(config_acepta_el_ca_del_certificado) {
  ProcessConfig config;
  std::string error;
  CHECK(parse_process_config(
      R"({"http": {"allow_invalid_certificates": false, "ca_file": "/etc/pase-lista/nodered.crt"}})",
      config, error));
  CHECK_EQ(config.http.ca_file, std::string("/etc/pase-lista/nodered.crt"));
}

// Con token, la galeria tiene que ir por https (o por http contra el localhost): el
// token no puede viajar en claro.
TEST(config_rechaza_una_galeria_en_claro_con_token) {
  ProcessConfig config;
  std::string error;
  CHECK(!parse_process_config(
      R"({"galeria": {"url": "http://192.168.1.50:1880/api/galeria", "token_env": "GALERIA_TOKEN"}})",
      config, error));
  CHECK(error.find("claro") != std::string::npos);
}

TEST(config_acepta_una_galeria_https_o_en_localhost) {
  ProcessConfig config;
  std::string error;
  CHECK(parse_process_config(
      R"({"galeria": {"url": "https://nodered.local:1880/api/galeria", "token_env": "GALERIA_TOKEN"}})",
      config, error));
  CHECK(parse_process_config(
      R"({"galeria": {"url": "http://127.0.0.1:1880/api/galeria", "token_env": "GALERIA_TOKEN"}})",
      config, error));
  // Sin token configurado, la galeria puede ir en claro (no viaja ninguna credencial).
  CHECK(parse_process_config(
      R"({"galeria": {"url": "http://192.168.1.50:1880/api/galeria", "token_env": ""}})",
      config, error));
}

// El nombre del modelo se convierte en una ruta: un "../" en el nombre ejecutaria un
// .nb de donde quiera en el driver del kernel.
TEST(config_rechaza_un_nombre_de_modelo_con_una_ruta) {
  ProcessConfig config;
  std::string error;
  CHECK(!parse_process_config(R"({"servicio": {"modelo": "../otro"}})", config, error));
  CHECK(error.find("modelo") != std::string::npos);
  CHECK(!parse_process_config(R"({"servicio": {"modelo": "a/b"}})", config, error));
  CHECK(!parse_process_config(R"({"servicio": {"modelo": "modelo con espacios.nb"}})", config,
                               error));
  CHECK(!parse_process_config(R"({"servicio": {"modelo": ".."}})", config, error));
  CHECK(parse_process_config(R"({"servicio": {"modelo": "w600k_mbf-uint8-v1"}})", config,
                             error));
  CHECK_EQ(config.service.model_name, std::string("w600k_mbf-uint8-v1"));
}

TEST(el_nombre_del_modelo_solo_admite_un_patron_seguro) {
  CHECK(vision::nombre_modelo_valido("w600k_mbf-uint8-v1"));
  CHECK(vision::nombre_modelo_valido("modelo.2"));
  CHECK(!vision::nombre_modelo_valido(""));
  CHECK(!vision::nombre_modelo_valido("."));
  CHECK(!vision::nombre_modelo_valido(".."));
  CHECK(!vision::nombre_modelo_valido("../otro"));
  CHECK(!vision::nombre_modelo_valido("con espacio"));
  CHECK(!vision::nombre_modelo_valido(std::string(65, 'a')));
}

TEST(la_ruta_del_modelo_no_sale_del_directorio_de_modelos) {
  const std::filesystem::path raiz = "/opt/autolist/models";
  CHECK_EQ(vision::ruta_modelo("detector", raiz).string(), std::string("/opt/autolist/models/detector.nb"));
  CHECK_EQ(vision::ruta_modelo("w600k_mbf-uint8-v1", raiz).string(),
           std::string("/opt/autolist/models/w600k_mbf-uint8-v1.nb"));
  CHECK_THROWS(vision::ruta_modelo("../otro", raiz), std::invalid_argument);
  CHECK_THROWS(vision::ruta_modelo("", raiz), std::invalid_argument);
}

TEST(url_admite_token_solo_con_https_o_localhost) {
  CHECK(url_admite_token("https://nodered.local:1880/api/galeria"));
  CHECK(url_admite_token("http://127.0.0.1:1880/api/galeria"));
  CHECK(url_admite_token("http://localhost:1880/api/galeria"));
  CHECK(!url_admite_token("http://192.168.1.50:1880/api/galeria"));
  CHECK(!url_admite_token("ftp://192.168.1.50/api/galeria"));
  CHECK(!url_admite_token("esto-no-es-una-url"));
}

// --- Tercera ronda: el bloque "npu" ----------------------------------------------
//
// Los parametros de los adaptadores sobre la NPU (color, modo de escala y tamano de
// entrada del detector) estaban codificados en el codigo. Ahora estan en el bloque
// "npu" de la configuracion, que es opcional: si no esta, el comportamiento es el de
// siempre.
TEST(config_carga_el_bloque_npu) {
  ProcessConfig config;
  std::string error;
  CHECK(parse_process_config(
      R"({"npu": {"color": "bgr", "escala": "arcface", "ancho": 320, "alto": 320}})",
      config, error));
  CHECK_EQ(static_cast<int>(config.npu.color_order), static_cast<int>(ColorOrder::Bgr));
  CHECK_EQ(static_cast<int>(config.npu.scale_mode), static_cast<int>(ScaleMode::ArcFace));
  CHECK_EQ(config.npu.detector_width, 320);
  CHECK_EQ(config.npu.detector_height, 320);
}

TEST(config_sin_bloque_npu_deja_los_valores_por_omision) {
  ProcessConfig config;
  std::string error;
  CHECK(parse_process_config(R"({"servicio": {"poll_ms": 50}})", config, error));
  CHECK_EQ(static_cast<int>(config.npu.color_order), static_cast<int>(ColorOrder::Rgb));
  CHECK_EQ(static_cast<int>(config.npu.scale_mode), static_cast<int>(ScaleMode::Auto));
  CHECK_EQ(config.npu.detector_width, 640);
  CHECK_EQ(config.npu.detector_height, 640);
}

TEST(config_rechaza_un_color_o_una_escala_desconocidos) {
  ProcessConfig config;
  std::string error;
  CHECK(!parse_process_config(R"({"npu": {"color": "rgbx"}})", config, error));
  CHECK(error.find("npu.color") != std::string::npos);
  CHECK(!parse_process_config(R"({"npu": {"escala": "centrada"}})", config, error));
  CHECK(error.find("npu.escala") != std::string::npos);
  // Una clave desconocida dentro del bloque, tambien.
  CHECK(!parse_process_config(R"({"npu": {"color": "rgb", "colores": 1}})", config, error));
  CHECK(error.find("colores") != std::string::npos);
  // Un tamano negativo no tiene sentido.
  CHECK(!parse_process_config(R"({"npu": {"ancho": -1}})", config, error));
  CHECK(error.find("npu.ancho") != std::string::npos);
}

// Los cuatro valores validos de la escala.
TEST(config_acepta_las_cuatro_escalas) {
  ProcessConfig config;
  std::string error;
  CHECK(parse_process_config(R"({"npu": {"escala": "auto"}})", config, error));
  CHECK_EQ(static_cast<int>(config.npu.scale_mode), static_cast<int>(ScaleMode::Auto));
  CHECK(parse_process_config(R"({"npu": {"escala": "ninguna"}})", config, error));
  CHECK_EQ(static_cast<int>(config.npu.scale_mode), static_cast<int>(ScaleMode::None));
  CHECK(parse_process_config(R"({"npu": {"escala": "unidad"}})", config, error));
  CHECK_EQ(static_cast<int>(config.npu.scale_mode), static_cast<int>(ScaleMode::Unit));
  CHECK(parse_process_config(R"({"npu": {"escala": "arcface"}})", config, error));
  CHECK_EQ(static_cast<int>(config.npu.scale_mode), static_cast<int>(ScaleMode::ArcFace));
}

// Un tamano de 0 significa "el que tenga el .nb".
TEST(config_acepta_un_tamano_de_entrada_en_cero) {
  ProcessConfig config;
  std::string error;
  CHECK(parse_process_config(R"({"npu": {"ancho": 0, "alto": 0}})", config, error));
  CHECK_EQ(config.npu.detector_width, 0);
  CHECK_EQ(config.npu.detector_height, 0);
}

// ruta_modelo y nombre_modelo_valido no se contradicen: un nombre que empieza por ".."
// pero no es un ".." (como "..modelo") es valido para los dos.
TEST(la_ruta_del_modelo_acepta_un_nombre_que_empieza_por_dos_puntos) {
  const std::filesystem::path raiz = "/opt/autolist/models";
  CHECK(vision::nombre_modelo_valido("..modelo"));
  CHECK_EQ(vision::ruta_modelo("..modelo", raiz).string(),
           std::string("/opt/autolist/models/..modelo.nb"));
  // Un ".." de verdad (un directorio) sigue sin valer.
  CHECK(!vision::nombre_modelo_valido(".."));
  CHECK_THROWS(vision::ruta_modelo("..", raiz), std::invalid_argument);
  CHECK_THROWS(vision::ruta_modelo("../otro", raiz), std::invalid_argument);
}
