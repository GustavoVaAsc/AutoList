// Firmware de la XIAO ESP32S3 Sense: tira LED de 12 V por MQTT.
//
// Alcance de esta versión: solo el campo "luz" de aula/actuador/cmd. Los campos
// "servo", "led" y "buzzer" se reciben dentro del mismo JSON pero se ignoran por
// ahora; la cámara, el PIR, el INA219, el servo, los LEDs RGB y el buzzer son del
// carril de Gustavo y entran en este mismo sketch (ver firmware/xiao/README.md).
//
// La tira se apaga sola a los kApagadoAutomaticoMs (360 s) del último luz:1: quien
// necesite la luz más tiempo debe reenviar luz:1 antes (logica_luz.h).
//
// Contrato: docs/contrato-mqtt.md (secciones 1, 3 y 6). Cableado: README.md.

#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <string.h>

#include "logica_luz.h"
#include "secrets.h"

using logica_luz::PedidoLuz;

// D2 es GPIO3, un pin de strapping del ESP32-S3. La compuerta del MOSFET lleva un
// pull-down externo (README.md) para que la tira quede apagada mientras la XIAO
// arranca; setup() fija además la salida en LOW antes de cualquier otra cosa.
static const uint8_t PIN_LUZ = D2;

static const char* const TOPICO_CMD = "aula/actuador/cmd";
static const char* const TOPICO_ESTADO_ACTUADOR = "aula/actuador/estado";
static const char* const TOPICO_ESTADO_CAMARA = "aula/camara/estado";

// El broker usa el nombre de usuario como client id (use_username_as_clientid):
// lo que se ponga aquí lo ignora, pero que coincida evita confusiones.
static const char* const CLIENT_ID = "xiao";

static const unsigned long MQTT_REINTENTO_MS = 5000;
static const unsigned long WIFI_REINICIO_MS = 30000;
static const unsigned long ESTADO_REINTENTO_MS = 1000;
// Segundos de espera de WiFiClient y de PubSubClient en las conexiones y lecturas.
static const uint16_t TIMEOUT_SOCKET_S = 5;
// Tras suscribirse, el broker entrega de inmediato los retenidos del tópico. Los
// mensajes que llegan en esta ventana se descartan (ver conectarMqtt()). Es una
// heurística: 1.5 s sobra en una LAN, pero no es una garantía.
static const unsigned long VENTANA_RETENIDOS_MS = 1500;

static WiFiClient red;
static PubSubClient mqtt(red);
static logica_luz::ControlLuz control;

static bool enVentanaInicial = false;
static unsigned long ultimoIntentoMqtt = 0;
static unsigned long ultimoWifiOk = 0;
// El último intento de publicar aula/actuador/estado falló (o la luz cambió sin
// enlace): loop() lo reintenta mientras haya enlace.
static bool estadoPendiente = false;
static bool reintentoInmediato = false;  // el próximo loop() con enlace publica sin esperar
static unsigned long ultimoReintentoEstado = 0;

static void escribirPin() {
  digitalWrite(PIN_LUZ, control.encendida() ? HIGH : LOW);
}

// Publica el estado de la luz. Si no se pudo, queda pendiente y loop() lo reintenta.
// Usa el búfer de PubSubClient, que es el mismo en el que llegó el cmd en curso.
static void publicarEstado() {
  char carga[32];
  bool publicado = false;
  reintentoInmediato = false;
  if (logica_luz::construirEstado(carga, sizeof carga, control.encendida()) > 0) {
    // PubSubClient solo publica con QoS 0; el contrato pide QoS 1 (README.md,
    // sección Desviaciones). Es retenido, así que el último estado queda en el broker.
    publicado = mqtt.publish(TOPICO_ESTADO_ACTUADOR, carga, true);
  }
  estadoPendiente = !publicado;
  ultimoReintentoEstado = millis();
  if (!publicado) Serial.println("No se pudo publicar aula/actuador/estado: se reintenta");
}

// La luz cambió: el estado hay que publicarlo en cuanto haya enlace, sin esperar
// el intervalo de reintento.
static void marcarEstadoPendiente() {
  estadoPendiente = true;
  reintentoInmediato = true;
}

static void alRecibir(char* topico, byte* carga, unsigned int longitud) {
  if (strcmp(topico, TOPICO_CMD) != 0) return;
  if (logica_luz::debeDescartar(enVentanaInicial, longitud)) {
    Serial.println("cmd descartado: posible retenido o payload vacío");
    return;
  }

  // PubSubClient ya descarta lo que no cabe en su búfer (kMaxCmdBytes, el tamaño
  // por defecto); se comprueba aquí también para no depender de ese detalle.
  if (!logica_luz::cabeCmd(longitud)) {
    Serial.println("cmd descartado: no cabe en el búfer");
    return;
  }

  // Se pasa const char* (no byte* ni char*) para que ArduinoJson copie lo que
  // necesite y no use el búfer de PubSubClient como memoria del documento
  // (modo sin copia): publicarEstado() lo sobrescribe.
  JsonDocument doc;
  const DeserializationError error =
      deserializeJson(doc, reinterpret_cast<const char*>(carga), longitud);
  if (error || !doc.is<JsonObject>()) {
    Serial.println("cmd descartado: no es un objeto JSON");
    return;
  }

  const JsonVariantConst valor = doc.as<JsonObjectConst>()["luz"];
  const PedidoLuz pedido =
      logica_luz::interpretarLuz(!valor.isNull(), valor.is<int>(), valor.as<int>());
  // "servo", "led" y "buzzer" se ignoran por ahora (carril de Gustavo): sus
  // bloques irían aquí, junto a este, sin cortar el procesamiento con un return.
  if (pedido == PedidoLuz::Invalido) {
    Serial.println("cmd: \"luz\" inválida, se ignora (solo 0 o 1)");
  }
  // Ausente e Invalido no cambian nada. Un Encender, aunque la luz ya esté
  // encendida, reinicia el plazo del apagado automático.
  const bool cambio = control.aplicar(pedido, millis());
  if (cambio) {
    escribirPin();
    Serial.println(control.encendida() ? "luz encendida" : "luz apagada");
  }
  // Toda orden válida se responde con el estado, aunque no haya cambiado: si el
  // retenido del broker se quedó atrás (publicación fallida), se corrige.
  if (logica_luz::esOrden(pedido)) publicarEstado();
}

// Un intento de conexión al broker. Bloquea loop() mientras dura: la conexión TCP
// (hasta TIMEOUT_SOCKET_S), la espera del CONNACK (otros TIMEOUT_SOCKET_S) y, si
// MQTT_HOST es un nombre y no una IP, la resolución DNS, que puede tardar más que
// cualquiera de las dos. Con IP son unos 10 s en el peor caso, más la ventana de
// retenidos de 1.5 s si conecta. Nunca es indefinido. Con la luz apagada por falta de
// enlace no hay nada que atender, pero cuando entren el servo, el PIR y el INA219
// esto debe pasar a otra tarea de FreeRTOS o a un connect no bloqueante.
static bool conectarMqtt() {
  const bool conectado = mqtt.connect(CLIENT_ID, MQTT_USUARIO, MQTT_CLAVE,
                                      TOPICO_ESTADO_CAMARA, 1, true, "{\"en_linea\":false}",
                                      true);  // sesión limpia: un cmd perdido no se repite
  if (!conectado) {
    Serial.print("MQTT: fallo al conectar, estado ");
    Serial.println(mqtt.state());
    return false;
  }

  // PubSubClient 2.8 no pasa la bandera retain al callback. La única forma en que
  // un retenido llega marcado como tal es la entrega inicial tras suscribirse (en
  // MQTT 3.1.1 un retenido publicado en vivo llega sin la bandera; contrato,
  // sección 1). Por eso todo lo que llegue en esta ventana se trata como posible
  // retenido y se descarta.
  enVentanaInicial = true;
  const bool suscrito = mqtt.subscribe(TOPICO_CMD, 1);
  const unsigned long inicio = millis();
  while (suscrito && mqtt.connected() && (millis() - inicio) < VENTANA_RETENIDOS_MS) {
    mqtt.loop();
    delay(10);
  }
  enVentanaInicial = false;

  if (!suscrito || !mqtt.connected()) {
    Serial.println("MQTT: se perdió la conexión al suscribirse");
    mqtt.disconnect();
    return false;
  }

  mqtt.publish(TOPICO_ESTADO_CAMARA, "{\"en_linea\":true}", true);
  publicarEstado();
  Serial.println("MQTT: conectado");
  return true;
}

static void iniciarWifi() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  // Sin ahorro de energía del módem: con él, el retenido de la suscripción podría
  // llegar más tarde que la ventana de descarte (VENTANA_RETENIDOS_MS).
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_CLAVE);
}

// Una pasada de reconexión por cada vuelta de loop(); nunca espera en un bucle.
static void mantenerConexion() {
  const unsigned long ahora = millis();

  if (WiFi.status() != WL_CONNECTED) {
    // El controlador reintenta solo (setAutoReconnect); si tras 30 s sigue sin
    // enlace, se reinicia el intento desde cero.
    if (ahora - ultimoWifiOk >= WIFI_REINICIO_MS) {
      Serial.println("Wi-Fi: sin enlace, se reinicia la conexión");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_CLAVE);
      ultimoWifiOk = ahora;
    }
    return;
  }
  ultimoWifiOk = ahora;

  if (ahora - ultimoIntentoMqtt >= MQTT_REINTENTO_MS) {
    ultimoIntentoMqtt = ahora;
    conectarMqtt();
  }
}

void setup() {
  // Lo primero: tira apagada. D2 es un pin de strapping; el pull-down externo la
  // mantiene apagada hasta aquí.
  pinMode(PIN_LUZ, OUTPUT);
  digitalWrite(PIN_LUZ, LOW);

  Serial.begin(115200);  // con "USB CDC On Boot" activado, es el USB Serial/JTAG
  Serial.println("XIAO: tira LED por MQTT");

  mqtt.setServer(MQTT_HOST, MQTT_PUERTO);
  mqtt.setCallback(alRecibir);
  red.setTimeout(TIMEOUT_SOCKET_S);          // segundos; acota la conexión TCP
  mqtt.setSocketTimeout(TIMEOUT_SOCKET_S);   // segundos; acota la espera del CONNACK

  iniciarWifi();
  ultimoWifiOk = millis();
  ultimoIntentoMqtt = millis() - MQTT_REINTENTO_MS;  // primer intento en cuanto haya Wi-Fi
}

void loop() {
  const uint32_t ahora = millis();

  // Apagado automático: ver kApagadoAutomaticoMs. Se revisa con o sin enlace y
  // antes de mantenerConexion(), que es lo único que bloquea loop().
  if (control.revisarVencimiento(ahora)) {
    escribirPin();
    marcarEstadoPendiente();
    Serial.println("Apagado automático: venció el plazo desde el último luz:1");
  }

  const bool enLinea = WiFi.status() == WL_CONNECTED && mqtt.connected();
  if (enLinea) {
    mqtt.loop();
    if (estadoPendiente &&
        (reintentoInmediato || ahora - ultimoReintentoEstado >= ESTADO_REINTENTO_MS)) {
      reintentoInmediato = false;
      publicarEstado();  // si falla, vuelve a quedar pendiente con el plazo de 1 s
    }
    delay(1);  // cede la CPU a las tareas del Wi-Fi
    return;
  }

  // Apagado de seguridad: sin enlace con el broker nadie puede ordenar apagar la
  // tira, así que se apaga. El estado se publica al reconectar (conectarMqtt()).
  if (control.aplicar(PedidoLuz::Apagar, ahora)) {
    escribirPin();
    Serial.println("Sin enlace con el broker: luz apagada");
  }
  mantenerConexion();
}
