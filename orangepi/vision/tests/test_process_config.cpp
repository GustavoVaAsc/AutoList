// Pruebas de la configuracion del proceso: el analisis del JSON, los valores por
// omision, el rechazo de los documentos invalidos y el mapeo a ServiceConfig.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
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
// entrada) estan en el bloque "npu". Los dos colores son obligatorios fuera de
// --dry-run (comprobar_colores_npu) y el embedder no hereda nada del detector.
TEST(config_carga_el_bloque_npu) {
  ProcessConfig config;
  std::string error;
  CHECK(parse_process_config(
      R"({"npu": {"color": "bgr", "escala": "arcface", "ancho": 320, "alto": 320}})",
      config, error));
  CHECK(config.npu.color_order.has_value());
  CHECK_EQ(static_cast<int>(config.npu.color_order.value()), static_cast<int>(ColorOrder::Bgr));
  CHECK_EQ(static_cast<int>(config.npu.scale_mode), static_cast<int>(ScaleMode::ArcFace));
  CHECK_EQ(config.npu.detector_width, 320);
  CHECK_EQ(config.npu.detector_height, 320);
}

// Sin bloque "npu" el analisis no falla (el archivo es valido), pero los colores quedan
// sin fijar: es comprobar_colores_npu quien decide.
TEST(config_sin_bloque_npu_deja_los_colores_sin_fijar) {
  ProcessConfig config;
  std::string error;
  CHECK(parse_process_config(R"({"servicio": {"poll_ms": 50}})", config, error));
  CHECK(!config.npu.color_order.has_value());
  CHECK(!config.npu.embed_color_order.has_value());
  CHECK_EQ(static_cast<int>(config.npu.scale_mode), static_cast<int>(ScaleMode::Auto));
  CHECK_EQ(static_cast<int>(config.npu.embed_scale_mode), static_cast<int>(ScaleMode::Auto));
  // 0 = "el tamano que tenga el .nb".
  CHECK_EQ(config.npu.detector_width, 0);
  CHECK_EQ(config.npu.detector_height, 0);
  CHECK_EQ(config.npu.embed_width, 0);
  CHECK_EQ(config.npu.embed_height, 0);
}

// Fuera de --dry-run los dos colores son obligatorios: sin el bloque, sin "color" o sin
// "embed_color" es un error de configuracion, con la clave que falta y el valor de los
// modelos actuales en el mensaje.
TEST(config_los_dos_colores_de_la_npu_son_obligatorios) {
  ProcessConfig config;
  std::string error;

  CHECK(parse_process_config("{}", config, error));
  CHECK(!comprobar_colores_npu(config.npu, error));
  CHECK(error.find("npu.color") != std::string::npos);
  CHECK(error.find("npu.embed_color") != std::string::npos);

  CHECK(parse_process_config(R"({"npu": {"embed_color": "rgb"}})", config, error));
  CHECK(!comprobar_colores_npu(config.npu, error));
  CHECK(error.find("npu.color") != std::string::npos);
  CHECK(error.find("\"bgr\"") != std::string::npos);
  CHECK(error.find("embed_color") == std::string::npos);

  CHECK(parse_process_config(R"({"npu": {"color": "bgr"}})", config, error));
  CHECK(!comprobar_colores_npu(config.npu, error));
  CHECK(error.find("npu.embed_color") != std::string::npos);
  CHECK(error.find("\"rgb\"") != std::string::npos);

  CHECK(parse_process_config(R"({"npu": {"color": "bgr", "embed_color": "rgb"}})", config,
                             error));
  error.clear();
  CHECK(comprobar_colores_npu(config.npu, error));
  CHECK(error.empty());
}

// Con los dos colores, cada uno es el suyo: el del embedder no copia el del detector en
// ninguna direccion.
TEST(config_el_color_del_embedder_no_hereda_el_del_detector) {
  ProcessConfig config;
  std::string error;
  CHECK(parse_process_config(R"({"npu": {"color": "bgr", "embed_color": "rgb"}})", config,
                             error));
  CHECK_EQ(static_cast<int>(config.npu.color_order.value()), static_cast<int>(ColorOrder::Bgr));
  CHECK_EQ(static_cast<int>(config.npu.embed_color_order.value()),
           static_cast<int>(ColorOrder::Rgb));
  CHECK(parse_process_config(R"({"npu": {"color": "rgb", "embed_color": "bgr"}})", config,
                             error));
  CHECK_EQ(static_cast<int>(config.npu.color_order.value()), static_cast<int>(ColorOrder::Rgb));
  CHECK_EQ(static_cast<int>(config.npu.embed_color_order.value()),
           static_cast<int>(ColorOrder::Bgr));
}

// La linea de arranque con el color y la escala resueltos de los dos modelos.
TEST(config_el_resumen_de_la_npu_dice_color_y_escala_de_los_dos_modelos) {
  ProcessConfig config;
  std::string error;
  CHECK(parse_process_config(
      R"({"npu": {"color": "bgr", "embed_color": "rgb", "embed_escala": "arcface"}})", config,
      error));
  CHECK_EQ(resumen_npu(config.npu),
           std::string("detector: color bgr, escala auto; embedder: color rgb, escala arcface"));
  CHECK(parse_process_config("{}", config, error));
  CHECK_EQ(resumen_npu(config.npu), std::string("detector: color sin fijar, escala auto; "
                                                "embedder: color sin fijar, escala auto"));
}

// El ejemplo versionado trae los valores de los modelos actuales y pasa la comprobacion.
TEST(config_el_ejemplo_versionado_trae_los_colores_de_los_modelos) {
  const std::filesystem::path ejemplo =
      std::filesystem::path(VISION_SOURCE_DIR) / "vision.conf.example";
  ProcessConfig config;
  bool encontrado = false;
  std::string error;
  CHECK(load_process_config(ejemplo.string(), config, encontrado, error));
  CHECK(encontrado);
  CHECK(comprobar_colores_npu(config.npu, error));
  CHECK_EQ(resumen_npu(config.npu),
           std::string("detector: color bgr, escala auto; embedder: color rgb, escala arcface"));
}

// El embedder es otro modelo: su escala no se hereda de la del detector (el detector
// del fabricante usa el byte crudo y el embedder EB necesita arcface). Sin
// "embed_escala" queda en auto, que falla cerrado al arrancar si no se puede deducir.
TEST(config_el_embedder_no_hereda_la_escala_del_detector) {
  ProcessConfig config;
  std::string error;
  CHECK(parse_process_config(
      R"({"npu": {"color": "bgr", "escala": "ninguna", "embed_color": "rgb"}})", config,
      error));
  CHECK_EQ(static_cast<int>(config.npu.scale_mode), static_cast<int>(ScaleMode::None));
  CHECK_EQ(static_cast<int>(config.npu.embed_scale_mode), static_cast<int>(ScaleMode::Auto));
}

TEST(config_el_bloque_npu_admite_los_parametros_del_embedder) {
  ProcessConfig config;
  std::string error;
  CHECK(parse_process_config(
      R"({"npu": {"color": "bgr", "escala": "arcface", "embed_color": "rgb",
                 "embed_escala": "unidad", "embed_ancho": 224, "embed_alto": 224}})",
      config, error));
  CHECK_EQ(static_cast<int>(config.npu.color_order.value()), static_cast<int>(ColorOrder::Bgr));
  CHECK_EQ(static_cast<int>(config.npu.scale_mode), static_cast<int>(ScaleMode::ArcFace));
  CHECK_EQ(static_cast<int>(config.npu.embed_color_order.value()),
           static_cast<int>(ColorOrder::Rgb));
  CHECK_EQ(static_cast<int>(config.npu.embed_scale_mode), static_cast<int>(ScaleMode::Unit));
  CHECK_EQ(config.npu.embed_width, 224);
  CHECK_EQ(config.npu.embed_height, 224);
  // Una clave desconocida del bloque sigue rechazando el archivo.
  CHECK(!parse_process_config(R"({"npu": {"embed_tamanio": 224}})", config, error));
  CHECK(error.find("npu") != std::string::npos);
}

TEST(config_rechaza_un_color_o_una_escala_desconocidos) {
  ProcessConfig config;
  std::string error;
  CHECK(!parse_process_config(R"({"npu": {"color": "rgbx"}})", config, error));
  CHECK(error.find("npu.color") != std::string::npos);
  // El mensaje nombra la clave que esta mal, no siempre "npu.color".
  CHECK(!parse_process_config(R"({"npu": {"embed_color": "rgbx"}})", config, error));
  CHECK(error.find("npu.embed_color") != std::string::npos);
  CHECK(!parse_process_config(R"({"npu": {"embed_escala": "centrada"}})", config, error));
  CHECK(error.find("npu.embed_escala") != std::string::npos);
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

// --- El secreto de la firma de la captura ------------------------------------------

namespace {

// Un secreto valido: 32 caracteres con 16 distintos, sin "cambiar_".
const std::string kSecreto32 = "0123456789abcdef0123456789abcdef";

CaptureSettings captura_con(bool requerido, const std::string& env = "CAPTURA_HMAC_SECRETO") {
  CaptureSettings captura;
  captura.hmac_secret_env = env;
  captura.hmac_required = requerido;
  return captura;
}

}  // namespace

// Sin "hmac_requerido" en el archivo no hay valor fijado: manda la URL (ver los tests de
// resolver_secreto_captura con la URL de loopback).
TEST(config_sin_hmac_requerido_deja_el_valor_sin_fijar) {
  ProcessConfig config;
  std::string error;
  CHECK(parse_process_config("{}", config, error));
  CHECK_EQ(config.capture.hmac_secret_env, std::string("CAPTURA_HMAC_SECRETO"));
  CHECK(!config.capture.hmac_required.has_value());
}

TEST(config_carga_el_bloque_de_la_firma_de_la_captura) {
  ProcessConfig config;
  std::string error;
  CHECK(parse_process_config(
      R"({"captura": {"url": "http://10.0.0.7:8080/capture?res=SVGA",
                      "hmac_secreto_env": "OTRO_SECRETO", "hmac_requerido": true}})",
      config, error));
  CHECK_EQ(config.capture.url, std::string("http://10.0.0.7:8080/capture?res=SVGA"));
  CHECK_EQ(config.capture.hmac_secret_env, std::string("OTRO_SECRETO"));
  CHECK(config.capture.hmac_required.value_or(false));
}

TEST(config_rechaza_la_firma_de_la_captura_con_tipos_equivocados) {
  ProcessConfig config;
  std::string error;
  CHECK(!parse_process_config(R"({"captura": {"hmac_requerido": "si"}})", config, error));
  CHECK(error.find("hmac_requerido") != std::string::npos);
  CHECK(!parse_process_config(R"({"captura": {"hmac_secreto_env": 7}})", config, error));
  CHECK(error.find("hmac_secreto_env") != std::string::npos);
}

TEST(el_secreto_valido_se_devuelve_sin_aviso) {
  for (const bool requerido : {true, false}) {
    const SecretoCaptura secreto =
        resolver_secreto_captura(captura_con(requerido), kSecreto32, false);
    CHECK_EQ(secreto.valor, kSecreto32);
    CHECK(!secreto.fatal);
    CHECK(secreto.mensaje.empty());
  }
}

TEST(un_secreto_de_mas_de_32_caracteres_sirve) {
  const SecretoCaptura secreto =
      resolver_secreto_captura(captura_con(true), kSecreto32 + "xyz-0123456789", false);
  CHECK_EQ(secreto.valor, kSecreto32 + "xyz-0123456789");
  CHECK(!secreto.fatal);
}

// Ausente, vacio, el placeholder y corto (31): con hmac_requerido el proceso no arranca.
TEST(un_secreto_que_no_sirve_es_fatal_con_hmac_requerido) {
  const std::optional<std::string> invalidos[] = {
      std::nullopt, std::string(""), std::string(kSecretoPlaceholder), kSecreto32.substr(1),
      std::string(40, 'k')};
  for (const auto& entorno : invalidos) {
    const SecretoCaptura secreto = resolver_secreto_captura(captura_con(true), entorno, false);
    CHECK(secreto.fatal);
    CHECK(secreto.valor.empty());
    CHECK(secreto.mensaje.find("CAPTURA_HMAC_SECRETO") != std::string::npos);
    CHECK(secreto.mensaje.find("tiene que ir firmada") != std::string::npos);
  }
}

// Sin hmac_requerido (por omision) el mismo secreto solo avisa: la peticion va sin firma y
// el proceso arranca.
TEST(un_secreto_que_no_sirve_solo_avisa_sin_hmac_requerido) {
  const std::optional<std::string> invalidos[] = {
      std::nullopt, std::string(""), std::string(kSecretoPlaceholder), kSecreto32.substr(1),
      std::string(40, 'k')};
  for (const auto& entorno : invalidos) {
    const SecretoCaptura secreto = resolver_secreto_captura(captura_con(false), entorno, false);
    CHECK(!secreto.fatal);
    CHECK(secreto.valor.empty());
    CHECK(secreto.mensaje.find("AVISO") != std::string::npos);
    CHECK(secreto.mensaje.find("sin firma") != std::string::npos);
  }
}

// --dry-run arranca sin credenciales aunque hmac_requerido este en true.
TEST(el_dry_run_arranca_sin_secreto_aunque_sea_requerido) {
  const SecretoCaptura sin = resolver_secreto_captura(captura_con(true), std::nullopt, true);
  CHECK(!sin.fatal);
  CHECK(sin.valor.empty());
  CHECK(sin.mensaje.find("--dry-run") != std::string::npos);
  // Y si hay un secreto bueno, el dry-run tambien firma.
  const SecretoCaptura con = resolver_secreto_captura(captura_con(true), kSecreto32, true);
  CHECK_EQ(con.valor, kSecreto32);
  CHECK(con.mensaje.empty());
}

// Los motivos son distintos y no incluyen el valor: ni el del placeholder corto ni el de un
// secreto corto de verdad.
TEST(los_avisos_del_secreto_dicen_el_motivo_y_nunca_el_valor) {
  const std::string corto = "secreto-corto-123";
  const SecretoCaptura s_corto = resolver_secreto_captura(captura_con(true), corto, false);
  CHECK(s_corto.mensaje.find("menos de 32") != std::string::npos);
  CHECK(s_corto.mensaje.find(corto) == std::string::npos);

  const SecretoCaptura s_placeholder =
      resolver_secreto_captura(captura_con(true), std::string(kSecretoPlaceholder), false);
  CHECK(s_placeholder.mensaje.find("placeholder") != std::string::npos);

  const SecretoCaptura s_ausente = resolver_secreto_captura(captura_con(true), std::nullopt, false);
  CHECK(s_ausente.mensaje.find("no esta la variable") != std::string::npos);
}

// Con el nombre de la variable vacio no hay de donde leer el secreto.
TEST(sin_nombre_de_variable_el_secreto_no_existe) {
  const SecretoCaptura secreto = resolver_secreto_captura(captura_con(true, ""), kSecreto32, false);
  CHECK(secreto.fatal);
  CHECK(secreto.valor.empty());
}

// El ejemplo versionado es el de la placa real: firma obligatoria y la URL del puente
// (un placeholder, con la query de la resolucion).
TEST(config_el_ejemplo_versionado_exige_la_firma_y_apunta_al_puente) {
  const std::filesystem::path ejemplo =
      std::filesystem::path(VISION_SOURCE_DIR) / "vision.conf.example";
  ProcessConfig config;
  bool encontrado = false;
  std::string error;
  CHECK(load_process_config(ejemplo.string(), config, encontrado, error));
  CHECK(encontrado);
  CHECK(config.capture.hmac_required.value_or(false));
  CHECK_EQ(config.capture.hmac_secret_env, std::string("CAPTURA_HMAC_SECRETO"));
  // La primera captura tras un rato de inactividad incluye el arranque en frio de la camara
  // (sin medir): plazo holgado en el ejemplo. Y el intervalo entre rafagas de la placa.
  CHECK_EQ(config.capture.http_timeout_ms, 5000);
  CHECK_EQ(config.capture.burst_interval_ms, 3000);
  CHECK_EQ(config.capture.url,
           std::string("http://<ip-zerotier-del-puente>:<puerto>/capture?res=SVGA"));
}

// El archivo de credenciales de ejemplo trae la variable con el placeholder: copiarlo
// tal cual no arranca la firma hasta que alguien ponga el secreto de verdad.
TEST(el_env_de_ejemplo_trae_el_secreto_con_el_placeholder) {
  const std::filesystem::path ejemplo =
      std::filesystem::path(VISION_SOURCE_DIR) / "vision.env.example";
  std::ifstream archivo(ejemplo);
  CHECK(archivo.good());
  std::string linea;
  bool encontrada = false;
  while (std::getline(archivo, linea)) {
    if (linea == std::string("CAPTURA_HMAC_SECRETO=") + kSecretoPlaceholder) encontrada = true;
  }
  CHECK(encontrada);
}

// Igual de estricto que la placa y que Node-RED: cualquier valor que CONTENGA "cambiar_".
TEST(un_secreto_que_contiene_cambiar_se_rechaza_aunque_sea_largo) {
  const std::string casos[] = {
      "cambiar_esta_clave_larga_0123456789abcdef",       // el de Portainer
      "cambiar_otra_cosa_distinta_0123456789abcdef",     // empieza por el prefijo
      "0123456789abcdefcambiar_esta_clave0123456789"};   // lo contiene en medio
  for (const std::string& caso : casos) {
    CHECK(caso.size() >= kMinLongitudSecreto);
    const SecretoCaptura secreto = resolver_secreto_captura(captura_con(true), caso, false);
    CHECK(secreto.fatal);
    CHECK(secreto.valor.empty());
    CHECK(secreto.mensaje.find("placeholder") != std::string::npos);
    CHECK(secreto.mensaje.find(caso) == std::string::npos);
  }
}

// Como GALERIA_TOKEN en Node-RED: al menos 8 caracteres distintos entre si.
TEST(un_secreto_necesita_al_menos_8_caracteres_distintos) {
  const std::string siete = "abcdefgabcdefgabcdefgabcdefgabcdefg";  // 35 caracteres, 7 distintos
  CHECK(siete.size() >= kMinLongitudSecreto);
  const SecretoCaptura malo = resolver_secreto_captura(captura_con(true), siete, false);
  CHECK(malo.fatal);
  CHECK(malo.mensaje.find("distintos") != std::string::npos);
  CHECK(malo.mensaje.find(siete) == std::string::npos);

  const std::string ocho = "abcdefghabcdefghabcdefghabcdefgh";  // 32 caracteres, 8 distintos
  const SecretoCaptura bueno = resolver_secreto_captura(captura_con(true), ocho, false);
  CHECK(!bueno.fatal);
  CHECK_EQ(bueno.valor, ocho);
}

TEST(config_el_intervalo_entre_rafagas_por_omision_es_3000_ms) {
  ProcessConfig config;
  std::string error;
  CHECK(parse_process_config("{}", config, error));
  CHECK_EQ(config.capture.burst_interval_ms, 3000);
  CHECK_EQ(config.to_service_config().burst_interval.count(), 3000);
}

TEST(config_carga_y_valida_el_intervalo_entre_rafagas) {
  ProcessConfig config;
  std::string error;
  CHECK(parse_process_config(R"({"captura": {"intervalo_rafagas_ms": 4500}})", config, error));
  CHECK_EQ(config.capture.burst_interval_ms, 4500);
  CHECK_EQ(config.to_service_config().burst_interval.count(), 4500);
  CHECK(parse_process_config(R"({"captura": {"intervalo_rafagas_ms": 0}})", config, error));
  CHECK(!parse_process_config(R"({"captura": {"intervalo_rafagas_ms": -1}})", config, error));
  CHECK(error.find("intervalo_rafagas_ms") != std::string::npos);
  CHECK(!parse_process_config(R"({"captura": {"intervalo_rafagas_ms": "3000"}})", config,
                              error));
}

// El archivo de ejemplo explica algunos valores con comentarios.
TEST(config_admite_comentarios_en_el_archivo) {
  ProcessConfig config;
  std::string error;
  CHECK(parse_process_config(
      "{\n  // un comentario\n  \"captura\": {\"rafaga\": 5 /* otro */}\n}", config, error));
  CHECK_EQ(config.capture.burst_size, 5);
}

// --- hmac_requerido por omision: true, salvo dry-run o una URL de loopback ----------------

namespace {

CaptureSettings captura_sin_fijar(const std::string& url) {
  CaptureSettings captura;
  captura.url = url;
  return captura;  // hmac_required sin fijar
}

}  // namespace

// Sin fijar y con la URL de la placa (o de su puente): es obligatorio, falla cerrado.
TEST(por_omision_el_secreto_es_obligatorio_con_una_url_que_no_es_de_loopback) {
  for (const char* url : {"http://192.168.1.50/capture", "http://10.147.17.9:8080/capture?res=SVGA",
                          "http://<ip-zerotier-del-puente>:<puerto>/capture?res=SVGA",
                          "http://puente.local/capture", "http://128.0.0.1/capture",
                          "http://127.0.0.1.example.com/capture", "no-es-una-url"}) {
    const SecretoCaptura secreto =
        resolver_secreto_captura(captura_sin_fijar(url), std::nullopt, false);
    CHECK(secreto.fatal);
    CHECK(secreto.valor.empty());
  }
}

// Sin fijar y con loopback (donde corre camara_falsa): arranca, avisa y va sin firma.
TEST(por_omision_el_secreto_no_es_obligatorio_con_una_url_de_loopback) {
  for (const char* url : {"http://127.0.0.1:18080/capture", "http://127.255.0.3/capture",
                          "http://localhost:18080/capture?res=SVGA", "http://LOCALHOST/capture",
                          "http://[::1]:18080/capture", "http://[::1]/capture",
                          "http://usuario@127.0.0.1/capture"}) {
    CHECK(url_es_loopback(url));
    const SecretoCaptura secreto =
        resolver_secreto_captura(captura_sin_fijar(url), std::nullopt, false);
    CHECK(!secreto.fatal);
    CHECK(secreto.valor.empty());
    CHECK(secreto.mensaje.find("AVISO") != std::string::npos);
  }
}

// Con --dry-run nunca es fatal, con la URL que sea.
TEST(por_omision_el_dry_run_arranca_sin_secreto_con_cualquier_url) {
  for (const char* url : {"http://192.168.1.50/capture", "http://127.0.0.1/capture"}) {
    const SecretoCaptura secreto =
        resolver_secreto_captura(captura_sin_fijar(url), std::nullopt, true);
    CHECK(!secreto.fatal);
  }
}

// Lo que el archivo dice explicitamente manda sobre la URL.
TEST(un_hmac_requerido_explicito_manda_sobre_la_url) {
  CaptureSettings loopback_obligatorio = captura_sin_fijar("http://127.0.0.1:18080/capture");
  loopback_obligatorio.hmac_required = true;
  CHECK(resolver_secreto_captura(loopback_obligatorio, std::nullopt, false).fatal);

  CaptureSettings placa_opcional = captura_sin_fijar("http://192.168.1.50/capture");
  placa_opcional.hmac_required = false;
  CHECK(!resolver_secreto_captura(placa_opcional, std::nullopt, false).fatal);
}

TEST(url_es_loopback_solo_reconoce_loopback) {
  CHECK(url_es_loopback("http://127.0.0.1/capture"));
  CHECK(url_es_loopback("https://127.1.2.3:8443/capture"));
  CHECK(url_es_loopback("http://[::1]:80/capture"));
  CHECK(!url_es_loopback("http://127.0.0.256/capture"));
  CHECK(!url_es_loopback("http://127.0.0/capture"));
  CHECK(!url_es_loopback("http://126.0.0.1/capture"));
  CHECK(!url_es_loopback("http://[::2]/capture"));
  CHECK(!url_es_loopback("http://[::1/capture"));
  CHECK(!url_es_loopback("http://localhost.evil.com/capture"));
  CHECK(!url_es_loopback("http:///capture"));
  CHECK(!url_es_loopback(""));
}

// El archivo de estado del reloj (ancla de la primera firma sin RTC).
TEST(config_el_archivo_de_estado_del_reloj_tiene_omision_y_se_puede_cambiar) {
  ProcessConfig config;
  std::string error;
  CHECK(parse_process_config("{}", config, error));
  CHECK_EQ(config.capture.clock_state_file, std::string("/var/lib/autolist/reloj-captura.estado"));
  CHECK(parse_process_config(R"({"captura": {"estado_reloj": "/tmp/otro.estado"}})", config, error));
  CHECK_EQ(config.capture.clock_state_file, std::string("/tmp/otro.estado"));
  CHECK(parse_process_config(R"({"captura": {"estado_reloj": ""}})", config, error));
  CHECK(config.capture.clock_state_file.empty());
  CHECK(!parse_process_config(R"({"captura": {"estado_reloj": 5}})", config, error));
}
