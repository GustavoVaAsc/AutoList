// Endpoint /capture de la XIAO: cámara, firma HMAC y respuesta con la foto.
//
// El servidor HTTP va en su propia tarea de FreeRTOS (la que crea el servidor
// HTTP del IDF, httpd): el manejador de /capture saca la foto y la sirve ahí
// dentro, así que el lazo de Arduino, donde vive el MQTT, no se bloquea. La
// alternativa (el servidor web de Arduino, que atiende en loop()) dejaría el
// MQTT sin responder mientras dura la captura.
//
// La SCCB de la cámara va por GPIO39/40. Aquí no se inicializa ningún bus de
// I2C ni se llama a Wire.begin(): el driver de cámara instala el suyo.
// OJO: el esp32-camera que viene precompilado en el core 3.3.12 está hecho con
// CONFIG_SCCB_HARDWARE_I2C_PORT1=y, así que su bus de SCCB cae en el
// peripheral I2C1 (no en el I2C0). En el core, Wire es el I2C0 y sus pines por
// defecto en esta placa son GPIO5/6 (D4/D5), justo donde irá el INA219; por
// eso el INA219 debería ir con Wire.begin(5,6) y no con Wire1. Ver el informe
// de esta fase.
//
// Protocolo de la petición (tiene que coincidir byte a byte con el del
// servicio de visión):
//   GET /capture?res=UXGA
//   X-Captura-TS:    epoch en segundos del reloj del BORDE
//   X-Captura-Nonce: entero aleatorio de 64 bits
//   X-Captura-HMAC:  64 caracteres hex
// Respuestas: 200 con la JPEG, 401 si falta una cabecera o la firma no cuadra,
// 403 si el nonce se repitió o el ts es antiguo, 503 si la cámara no está lista.
#include <Arduino.h>
#include <esp_camera.h>
#include <esp_http_server.h>
#include <string.h>

#include "captura.h"
#include "logica_camara.h"

namespace captura {
namespace {

// Estado del módulo. El servidor HTTP se guarda para poder pararlo.
httpd_handle_t g_servidor = nullptr;
bool g_camaraLista = false;

// El validador de la firma: recuerda los nonces ya usados y el último ts
// aceptado, así que tiene que sobrevivir entre peticiones.
logica_camara::Validador* g_validador = nullptr;

// El secreto viene de secrets.h (CAPTURA_HMAC_SECRETO), que es estático: el
// puntero vale toda la vida del firmware.
const char* g_secreto = nullptr;

// Escribe una respuesta corta con su código y su motivo, y la registra.
void responderError(httpd_req_t* req, int codigo, const char* motivo) {
  char texto[96];
  snprintf(texto, sizeof texto, "%d %s", codigo, motivo);
  httpd_resp_set_status(req, texto);
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_sendstr(req, texto);
  Serial.printf("[captura] %s\n", texto);
}

// Lee el valor de una cabecera. Devuelve false si la cabecera no está o si su
// valor no cabe en el búfer.
bool leerCabecera(httpd_req_t* req, const char* nombre, char* destino, size_t capacidad) {
  const size_t largo = httpd_req_get_hdr_value_len(req, nombre);
  if (largo == 0 || largo + 1 > capacidad) {
    return false;
  }
  return httpd_req_get_hdr_value_str(req, nombre, destino, capacidad) == ESP_OK;
}

// El manejador de GET /capture.
//
// httpd da el uri sin la query y la query aparte: la firma se calcula sobre
// el camino sin la query (ver logica_camara). La query no se lee: la
// resolución la fija el firmware (captura.h), no quien pregunta.
esp_err_t manejadorCaptura(httpd_req_t* req) {
  if (!g_camaraLista || g_validador == nullptr) {
    responderError(req, 503, "camara no lista");
    return ESP_OK;
  }

  char ts[64];
  char nonce[64];
  char firma[128];
  const bool hayTs = leerCabecera(req, logica_camara::kCabeceraTs, ts, sizeof ts);
  const bool hayNonce = leerCabecera(req, logica_camara::kCabeceraNonce, nonce, sizeof nonce);
  const bool hayFirma = leerCabecera(req, logica_camara::kCabeceraHmac, firma, sizeof firma);

  logica_camara::Peticion peticion;
  peticion.ts = hayTs ? ts : nullptr;
  peticion.nonce = hayNonce ? nonce : nullptr;
  peticion.hmac = hayFirma ? firma : nullptr;

  const logica_camara::Resultado resultado = g_validador->validar(peticion);
  if (resultado != logica_camara::Resultado::Aceptada) {
    const int codigo = logica_camara::codigoHttp(resultado);
    char motivo[96];
    snprintf(motivo, sizeof motivo, "%s", logica_camara::aTexto(resultado));
    responderError(req, codigo, motivo);
    return ESP_OK;
  }

  // La cámara está en PSRAM y el búfer de la foto también. Se toma el cuadro,
  // se sirve y se devuelve el búfer al driver.
  camera_fb_t* fb = esp_camera_fb_get();
  if (fb == nullptr) {
    responderError(req, 503, "no hay cuadro");
    return ESP_OK;
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=captura.jpg");
  const esp_err_t enviado = httpd_resp_send(req, reinterpret_cast<const char*>(fb->buf), fb->len);
  esp_camera_fb_return(fb);

  if (enviado != ESP_OK) {
    Serial.printf("[captura] no se pudo servir la foto: %d\n", (int)enviado);
  }
  return ESP_OK;
}

}  // namespace

bool iniciar(const char* secreto) {
  if (g_servidor != nullptr) {
    return true;  // ya está en marcha
  }
  if (secreto == nullptr || secreto[0] == '\0') {
    Serial.println("[captura] falta el secreto de la firma");
    return false;
  }
  // Misma politica que el borde (vision/src/process_config.cpp): el secreto no
  // puede ser el placeholder de la plantilla (que esta versionado en el repo y
  // por tanto es publico: con el, cualquiera en la LAN firmaria y sacaria
  // fotos) ni medir menos de 32 caracteres. El valor no se imprime.
  const size_t longitudSecreto = strlen(secreto);
  if (longitudSecreto < 32) {
    Serial.println("[captura] el secreto de la firma es demasiado corto (minimo 32 caracteres)");
    return false;
  }
  if (strstr(secreto, "cambiar_") != nullptr) {
    Serial.println("[captura] el secreto de la firma es el valor de ejemplo de la plantilla: no vale");
    return false;
  }
  g_secreto = secreto;

  // La configuración de la cámara. Los pines vienen de captura.h, que los copió
  // del ejemplo CameraWebServer del core (bloque CAMERA_MODEL_XIAO_ESP32S3).
  camera_config_t config;
  memset(&config, 0, sizeof config);
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = kPinD0;
  config.pin_d1 = kPinD1;
  config.pin_d2 = kPinD2;
  config.pin_d3 = kPinD3;
  config.pin_d4 = kPinD4;
  config.pin_d5 = kPinD5;
  config.pin_d6 = kPinD6;
  config.pin_d7 = kPinD7;
  config.pin_xclk = kPinXclk;
  config.pin_pclk = kPinPclk;
  config.pin_vsync = kPinVsync;
  config.pin_href = kPinHref;
  config.pin_sccb_sda = kPinSccbSda;   // SCCB de la cámara: GPIO40/39
  config.pin_sccb_scl = kPinSccbScl;
  config.pin_pwdn = kPinPwdn;
  config.pin_reset = kPinReset;
  config.xclk_freq_hz = 20000000;  // 20 MHz, el reloj que quiere el OV3660
  config.pixel_format = PIXFORMAT_JPEG;
  // En el driver, 2048x1536 es FRAMESIZE_QXGA (FRAMESIZE_UXGA son 1600x1200).
  config.frame_size = FRAMESIZE_QXGA;
  config.jpeg_quality = kCalidadJpeg;
  config.jpeg_buffer_size = kBufferJpegBytes;
  config.fb_count = 2;                       // dos cuadros: se sirve uno mientras se toma el otro
  config.grab_mode = CAMERA_GRAB_LATEST;     // el más reciente, no uno viejo
  config.fb_location = CAMERA_FB_IN_PSRAM;   // la foto no cabe en la RAM interna

  const esp_err_t error = esp_camera_init(&config);
  if (error != ESP_OK) {
    Serial.printf("[captura] la cámara no arrancó: 0x%x\n", (int)error);
    return false;
  }
  g_camaraLista = true;

  // Los primeros cuadros salen con la exposición y el balance de blanco aún
  // settling: se descartan (plan, sección 6.1).
  for (int i = 0; i < kCuadrosInicialesDescartados; ++i) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb != nullptr) {
      esp_camera_fb_return(fb);
    }
  }

  // El sensor: el OV3660 de la placa y el OV5640 de las más nuevas. El driver
  // los detecta por su identificador, así que el mismo firmware vale para los
  // dos. El autofocus del OV5640 (el VCM de los módulos con lente motorizada)
  // no se activa aquí: el esp32-camera del core viene sin
  // CONFIG_CAMERA_AF_SUPPORT, así que solo haría falta la biblioteca aparte
  // que controla el VCM.
  const sensor_t* sensor = esp_camera_sensor_get();
  if (sensor != nullptr) {
    Serial.printf("[captura] sensor PID=0x%04x\n", (int)sensor->id.PID);
  }

  // El validador de la firma: el secreto vive en secrets.h, que es estático.
  g_validador = new logica_camara::Validador(g_secreto, strlen(g_secreto));

  // El servidor HTTP. httpd_start crea su propia tarea de FreeRTOS: el
  // manejador de /capture se ejecuta ahí, no en el lazo de Arduino, así que el
  // MQTT sigue respondiendo mientras se saca y se sirve la foto.
  httpd_config_t configHttp = HTTPD_DEFAULT_CONFIG();
  configHttp.server_port = kPuertoHttp;
  configHttp.max_uri_handlers = 4;
  configHttp.stack_size = 8192;   // el manejador tiene locals de la petición
  configHttp.lru_purge_enable = true;

  const esp_err_t errorServidor = httpd_start(&g_servidor, &configHttp);
  if (errorServidor != ESP_OK) {
    Serial.printf("[captura] el servidor HTTP no arrancó: 0x%x\n", (int)errorServidor);
    g_servidor = nullptr;
    delete g_validador;
    g_validador = nullptr;
    g_camaraLista = false;
    esp_camera_deinit();
    return false;
  }

  httpd_uri_t uri;
  memset(&uri, 0, sizeof uri);
  uri.uri = "/capture";
  uri.method = HTTP_GET;
  uri.handler = manejadorCaptura;
  uri.user_ctx = nullptr;

  const esp_err_t errorUri = httpd_register_uri_handler(g_servidor, &uri);
  if (errorUri != ESP_OK) {
    Serial.printf("[captura] no se pudo registrar /capture: 0x%x\n", (int)errorUri);
    httpd_stop(g_servidor);
    g_servidor = nullptr;
    delete g_validador;
    g_validador = nullptr;
    g_camaraLista = false;
    esp_camera_deinit();
    return false;
  }

  Serial.println("[captura] listo: GET /capture");
  return true;
}

void detener() {
  // Primero el servidor: deja de aceptar peticiones y el manejador no vuelve a
  // tocar la cámara.
  if (g_servidor != nullptr) {
    httpd_stop(g_servidor);
    g_servidor = nullptr;
  }
  if (g_camaraLista) {
    esp_camera_deinit();
    g_camaraLista = false;
  }
  delete g_validador;
  g_validador = nullptr;
  g_secreto = nullptr;
}

bool activa() {
  return g_camaraLista && g_servidor != nullptr;
}

}  // namespace captura
