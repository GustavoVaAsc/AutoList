// Endpoint /capture de la XIAO: cámara, firma HMAC y respuesta con la foto.
//
// Esta es la parte que depende de Arduino (la lógica pura vive en
// logica_camara.h y logica_proteccion.h). El sketch (xiao.ino) la llama desde
// setup().
//
// La cámara es segura por construcción (la placa anterior se quemó con captura
// continua a alta resolución):
//   - Apagada en reposo: setup() NO la inicializa. Se enciende con la primera
//     petición que pasó la firma y se apaga (esp_camera_deinit) tras
//     kInactividadCamaraMs (15 s) sin capturas. Con la cámara encendida el
//     sensor transmite sin parar aunque nadie pida fotos, por eso no se deja así.
//   - Presupuesto de encendido: como mucho 300 s encendida en cualquier ventana
//     de 10 min; pasado eso, 503 "enfriamiento" y 2 min sin capturas.
//   - Un solo búfer y captura bajo demanda (fb_count = 1, GRAB_WHEN_EMPTY).
//   - Resolución acotada a HVGA, VGA, SVGA o XGA (res=, SVGA por defecto).
//   - Límite de 4 capturas en 2 s con pausa tras la ráfaga: si se pasa, 429.
//   - Una sola captura con la cámara a la vez (503 "ocupada" si el candado
//     está tomado). El envío de la foto va fuera del candado y con plazo.
//   - La respuesta va CIFRADA con AES-256-GCM (X-Captura-IV); el piso de ts se
//     guarda en la flash y se rechazan los reenvíos tras un reinicio, y un ts
//     absurdo no puede bloquear la cámara para siempre.
//   - El enfriamiento y el corte térmico sobreviven a un reinicio por software.
//   - Guarda térmica con el sensor interno: aviso a 65 °C, corte a 75 °C.
//   - No hay /stream ni otro endpoint de vídeo.
//
// El servidor HTTP va en su propia tarea de FreeRTOS (la que crea el servidor
// HTTP del IDF, httpd): el manejador de /capture saca la foto y la sirve ahí
// dentro, así que el lazo de Arduino, donde vive el MQTT, no se bloquea. La
// alternativa (el servidor web de Arduino, que atiende en loop()) dejaría el
// MQTT sin responder mientras dura la captura.
//
// Protocolo de la petición (tiene que coincidir byte a byte con el del
// servicio de visión):
//   GET /capture?res=SVGA
//   X-Captura-TS:    epoch en segundos del reloj del BORDE
//   X-Captura-Nonce: entero aleatorio de 64 bits
//   X-Captura-HMAC:  64 caracteres hex
// La query (res=) NO va firmada: limitación documentada del protocolo. Quien
// pueda reenviar una petición firmada podría cambiar la resolución, pero solo
// dentro de la lista blanca.
// Respuestas: 200 con la JPEG cifrada (application/octet-stream, X-Captura-IV),
// 401 si falta una cabecera o la firma no cuadra, 403 si el nonce se repitió o
// el ts es antiguo o futuro, 429 si
// se pasó el límite de capturas, 503 si la cámara no está lista, está ocupada
// ("ocupada"), hace demasiado calor ("temperatura") o está en enfriamiento
// ("enfriamiento").
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
// La resolución y la calidad de cada foto salen de logica_proteccion.h
// (perfil()): HVGA, VGA, SVGA y XGA, y nada más.
//
// El driver se inicializa SIEMPRE con la mayor resolución permitida (XGA,
// 1024x768): con ella dimensiona sus búferes y DMA, y después baja a la pedida
// con sensor->set_framesize. Inicializar a una resolución menor y subir luego
// desbordaría el búfer (logica_proteccion::kBufferJpegBytes, 512 KB, un solo
// búfer). En el driver FRAMESIZE_XGA es 1024x768.

// Puerto del servidor HTTP. La visión apunta a http://<ip-de-la-xiao>/capture.
constexpr uint16_t kPuertoHttp = 80;

// Arranca el servidor HTTP de /capture y la tarea de mantenimiento. NO enciende
// la cámara: se enciende con la primera petición firmada y se apaga sola tras
// kInactividadCamaraMs sin capturas, al agotar el presupuesto de encendido o
// por calor (todo en la tarea de mantenimiento, que corre cada segundo aparte
// de loop(): una reconexión del MQTT no la retrasa).
//
// `secreto` es el secreto compartido con el servicio de visión (en el sketch
// sale de secrets.h, CAPTURA_HMAC_SECRETO). Debe seguir vivo mientras el
// servidor esté en marcha: se guarda el puntero, no se copia. Tiene que medir
// 32 caracteres o más, con 8 distintos o más, y no ser el valor de ejemplo.
//
// `ipPermitida` (RELAY_IP de secrets.h) es OBLIGATORIA: httpd solo acepta
// conexiones de esa IPv4. Si falta o está mal escrita, la cámara no arranca (no
// se abre el puerto a todos por un descuido). No autentica: el ARP spoofing la
// salta; sube el listón, lo que autentica es la firma.
//
// `borrarPisoTs`: borra de la flash el ts guardado (el piso contra reenvíos)
// antes de leerlo. Es la salida si un ts absurdo lo dejó fuera de alcance (la
// pulsación de BOOT al arrancar, o CAPTURA_BORRAR_PISO_TS en secrets.h).
//
// `imagenGirada` (CAMARA_GIRADA_180 de secrets.h): gira la imagen 180 grados en
// el sensor, para la placa montada al revés; el servicio de visión recibe la
// foto derecha. Se aplica en cada arranque en frío de la cámara.
//
// Al arrancar también valida el cifrado de la respuesta (autodiagnóstico de
// AES-256-GCM contra vectores conocidos); si falla, la cámara no arranca.
//
// Devuelve true si el servidor arrancó. Si falla (secreto flojo, IP ausente o
// mal escrita, cifrado, servidor), /capture no responde.
bool iniciar(const char* secreto, const char* ipPermitida, bool borrarPisoTs = false,
             bool imagenGirada = false);

// Última temperatura leída del chip (°C), o NaN si aún no hay lectura. La lee
// quien la necesite (el latido de salud) sin tocar el sensor: solo captura.cpp
// lee temperatureRead(), así que dos tareas no lo usan a la vez.
float temperatura();

// Si la cámara está encendida ahora, y si las capturas están suspendidas (por
// temperatura o por el enfriamiento del presupuesto de encendido). Para el
// latido de salud; se pueden leer desde cualquier tarea.
bool camaraEncendida();
bool enCorte();
// Si el NVS (donde se guarda el piso de ts) funciona: abierto y sin fallo de
// escritura. Con false no hay defensa contra el reenvío tras un reinicio.
bool nvsBien();

// Apaga en orden: primero el servidor HTTP (deja de aceptar peticiones), luego
// la tarea de mantenimiento y por último la cámara si estaba encendida (libera
// la PSRAM del búfer). Se puede volver a arrancar después con iniciar().
void detener();

// Si el servidor de /capture está en marcha (la cámara puede estar apagada).
bool activa();

}  // namespace captura
