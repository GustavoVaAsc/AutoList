// Endpoint /capture de la XIAO: cámara, firma HMAC y respuesta con la foto.
//
// Esta es la parte que depende de Arduino (la lógica pura vive en
// logica_camara.h). El sketch (xiao.ino) la llama desde setup().
//
// El servidor HTTP va en su propia tarea de FreeRTOS (la que crea el servidor
// HTTP del IDF, httpd): el manejador de /capture saca la foto y la sirve ahí
// dentro, así que el lazo de Arduino, donde vive el MQTT, no se bloquea. La
// alternativa (el servidor web de Arduino, que atiende en loop()) dejaría el
// MQTT sin responder mientras dura la captura.
//
// Protocolo de la petición (tiene que coincidir byte a byte con el del
// servicio de visión):
//   GET /capture?res=UXGA
//   X-Captura-TS:    epoch en segundos del reloj del BORDE
//   X-Captura-Nonce: entero aleatorio de 64 bits
//   X-Captura-HMAC:  64 caracteres hex
// Respuestas: 200 con la JPEG, 401 si falta una cabecera o la firma no cuadra,
// 403 si el nonce se repitió o el ts es antiguo, 503 si la cámara no está lista.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace captura {

// --- Pines de la cámara en la placa XIAO ESP32S3 Sense -------------------------
// Copiados del ejemplo CameraWebServer del core de Espressif
// (libraries/ESP32/examples/Camera/CameraWebServer/camera_pins.h, bloque
// CAMERA_MODEL_XIAO_ESP32S3), que es donde están verificados para esta placa.
constexpr int kPinPwdn = -1;    // la placa no usa power-down
constexpr int kPinReset = -1;   // ni reset
constexpr int kPinXclk = 10;
constexpr int kPinSccbSda = 40;  // SIOD
constexpr int kPinSccbScl = 39;  // SIOC
constexpr int kPinD7 = 48;       // Y9
constexpr int kPinD6 = 11;       // Y8
constexpr int kPinD5 = 12;       // Y7
constexpr int kPinD4 = 14;       // Y6
constexpr int kPinD3 = 16;       // Y5
constexpr int kPinD2 = 18;       // Y4
constexpr int kPinD1 = 17;       // Y3
constexpr int kPinD0 = 15;       // Y2
constexpr int kPinVsync = 38;
constexpr int kPinHref = 47;
constexpr int kPinPclk = 13;

// --- Resolución y calidad ----------------------------------------------------
// La visión acepta hasta 2048x1536 y corta el cuerpo a 2 MB. El sensor OV3660
// de la placa llega hasta 2048x1536, así que se sirve a esa resolución.
//
// OJO con el nombre: en el driver de cámara, FRAMESIZE_QXGA es 2048x1536 y
// FRAMESIZE_UXGA son 1600x1200. El "UXGA" del plan y de la URL (res=UXGA)
// significa 2048x1536 para este proyecto. La constante del driver se usa en
// captura.cpp.
constexpr int kResolucionAncho = 2048;
constexpr int kResolucionAlto = 1536;

// Calidad del JPEG del sensor: 0 es la máxima, 63 la mínima. Con 8 la foto
// típico ronda 0.2-0.4 MB (el driver reserva 1 MB, ver kBufferJpegBytes), muy
// por debajo del corte de 2 MB de la visión, y con detalle de sobra para el
// recorte a 320x320 que hace la NPU. Bajar más (4, 2, 0) acerca la foto al
// megabyte y puede hacer que un cuadro no quepa en el búfer.
constexpr int kCalidadJpeg = 8;

// Tamaño del búfer de cada cuadro JPEG. El driver, si no se le dice otra
// cosa, reserva ancho*alto/5 (unos 630 KB a 2048x1536); se reserva 1 MB para
// que un cuadro con mucho detalle (una escena ruidosa) no se quede corto y se
// pierda la foto. Con dos búferes son 2 MB de PSRAM.
constexpr size_t kBufferJpegBytes = 1024 * 1024;

// Cuadros que se descartan al despertar el sensor: los primeros salen con la
// exposición y el balance de blanco todavía settling (plan, sección 6.1).
constexpr int kCuadrosInicialesDescartados = 3;

// Puerto del servidor HTTP. La visión apunta a http://<ip-de-la-xiao>/capture.
constexpr uint16_t kPuertoHttp = 80;

// Arranca la cámara y el servidor HTTP de /capture.
//
// `secreto` es el secreto compartido con el servicio de visión (en el sketch
// sale de secrets.h, CAPTURA_HMAC_SECRETO). Debe seguir vivo mientras la
// cámara esté en marcha: se guarda el puntero, no se copia.
//
// Devuelve true si todo arrancó. Si la cámara no se puede inicializar, el
// servidor también se queda sin arrancar y /capture no responde.
bool iniciar(const char* secreto);

// Apaga en orden: primero el servidor HTTP (deja de aceptar peticiones) y
// luego la cámara (libera la PSRAM de los búferes). Se puede volver a
// arrancar después con iniciar().
void detener();

// Si la cámara está inicializada y el servidor accepting peticiones.
bool activa();

}  // namespace captura
