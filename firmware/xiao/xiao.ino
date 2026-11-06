// Firmware de la XIAO ESP32S3 Sense: nodo de campo del pase de lista.
//
// Qué hace esta versión:
//   - La tira LED de 12 V por MQTT (campo "luz" de aula/actuador/cmd).
//   - La cámara: endpoint HTTP /capture, con la firma HMAC-SHA256 que el
//     servicio de visión manda en las cabeceras X-Captura-TS, X-Captura-Nonce
//     y X-Captura-HMAC.
//   - El PIR: publica aula/camara/pir cuando detecta movimiento.
//   - Los actuadores: servo, LED RGB y buzzer (campos "servo", "led" y
//     "buzzer" de aula/actuador/cmd).
//
// La lógica de cada parte está en módulos de C++ puro con pruebas de host
// (logica_luz, logica_camara, logica_pir, logica_actuadores); aquí solo se
// hace la E/S: leer el pin, escribir el pin, hablar con el broker.
//
// La tira se apaga sola a los kApagadoAutomaticoMs (360 s) del último luz:1: quien
// necesite la luz más tiempo debe reenviar luz:1 antes (logica_luz.h).
//
// Contrato: docs/contrato-mqtt.md (secciones 1, 3 y 6). Cableado: README.md.

#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <string.h>

#include "actuadores.h"
#include "captura.h"
#include "logica_actuadores.h"
#include "logica_camara.h"
#include "logica_luz.h"
#include "logica_pir.h"
#include "secrets.h"

using logica_actuadores::PedidoBuzzer;
using logica_actuadores::PedidoLed;
using logica_actuadores::PedidoServo;
using logica_luz::PedidoLuz;

// D2 es GPIO3, un pin de strapping del ESP32-S3. La compuerta del MOSFET lleva un
// pull-down externo (README.md) para que la tira quede apagada mientras la XIAO
// arranca; setup() fija además la salida en LOW antes de cualquier otra cosa.
static const uint8_t PIN_LUZ = D2;
// PIR HC-SR501 en D0 (GPIO1). Su salida es de 5 V: entre el sensor y este pin va
// un divisor de tensión (README.md, cableado).
static const uint8_t PIN_PIR = D0;

static const char* const TOPICO_CMD = "aula/actuador/cmd";
static const char* const TOPICO_ESTADO_ACTUADOR = "aula/actuador/estado";
static const char* const TOPICO_ESTADO_CAMARA = "aula/camara/estado";
static const char* const TOPICO_PIR = "aula/camara/pir";
// Retenido: la XIAO lo lee para encender el LED azul de la sesión activa. A
// diferencia de los retenidos de los tópicos de órdenes, este hay que aplicarlo.
static const char* const TOPICO_SESION_ESTADO = "aula/sesion/estado";

// El broker usa el nombre de usuario como client id (use_username_as_clientid):
// lo que se ponga aquí lo ignora, pero que coincida evita confusiones.
static const char* const CLIENT_ID = "xiao";

static const unsigned long MQTT_REINTENTO_MS = 5000;
static const unsigned long WIFI_REINICIO_MS = 30000;
static const unsigned long ESTADO_REINTENTO_MS = 1000;
// Cada cuánto se lee el PIR. El módulo es de flanco de subida: si el sensor
// sube y baja entre dos muestras, el movimiento se pierde. Con el tiempo de
// retención del HC-SR501 (2 a 5 s) y 50 ms de muestreo no hay problema.
static const unsigned long MUESTREO_PIR_MS = 50;
// Tiempos de espera del socket, con la API y las unidades de cada biblioteca:
// setConnectionTimeout (el de WiFiClient) va en milisegundos y acota la
// conexión TCP y las lecturas del socket; PubSubClient::setSocketTimeout va en
// segundos y acota la espera del CONNACK. Ojo con Stream::setTimeout, que es
// la que hereda WiFiClient: NetworkClient declara su propio _timeout y lo
// oculta, así que esa llamada no toca nada (ver el README, Sin verificar).
static const uint32_t TIMEOUT_SOCKET_MS = 5000;
static const uint16_t TIMEOUT_SOCKET_S = 5;
// Tras suscribirse, el broker entrega de inmediato los retenidos del tópico. Los
// mensajes que llegan en esta ventana se descartan (ver conectarMqtt()). Es una
// heurística: 1.5 s sobra en una LAN, pero no es una garantía.
// OJO: el descarte vale solo para los tópicos de órdenes, que nunca se retienen.
// El estado de la sesión SÍ es retenido y hay que aplicarlo.
static const unsigned long VENTANA_RETENIDOS_MS = 1500;

static WiFiClient red;
static PubSubClient mqtt(red);
static logica_luz::ControlLuz controlLuz;
static logica_actuadores::ControlActuadores controlActuadores;
// El calentamiento del PIR corre desde el arranque de la placa: millis() empieza
// en 0, así que el validador con arranque en 0 cuenta los primeros 60 s.
static logica_pir::ControlPir controlPir;

static bool enVentanaInicial = false;
static unsigned long ultimoIntentoMqtt = 0;
static unsigned long ultimoWifiOk = 0;
static unsigned long ultimoMuestreoPir = 0;
// El último intento de publicar aula/actuador/estado falló (o algo cambió sin
// enlace): loop() lo reintenta mientras haya enlace.
static bool estadoPendiente = false;
static bool reintentoInmediato = false;  // el próximo loop() con enlace publica sin esperar
static unsigned long ultimoReintentoEstado = 0;

static void escribirPin() {
  digitalWrite(PIN_LUZ, controlLuz.encendida() ? HIGH : LOW);
}

// Publica el estado de los actuadores: servo, LED y luz (contrato, sección 3).
// Si no se pudo, queda pendiente y loop() lo reintenta. Usa el búfer de
// PubSubClient, que es el mismo en el que llegó el cmd en curso.
static void publicarEstado() {
  char carga[64];
  bool publicado = false;
  reintentoInmediato = false;
  if (logica_actuadores::construirEstado(carga, sizeof carga, controlActuadores,
                                         controlLuz.encendida()) > 0) {
    // PubSubClient solo publica con QoS 0; el contrato pide QoS 1 (README.md,
    // sección Desviaciones). Es retenido, así que el último estado queda en el broker.
    publicado = mqtt.publish(TOPICO_ESTADO_ACTUADOR, carga, true);
  }
  estadoPendiente = !publicado;
  ultimoReintentoEstado = millis();
  if (!publicado) Serial.println("No se pudo publicar aula/actuador/estado: se reintenta");
}

// Algo cambió: el estado hay que publicarlo en cuanto haya enlace, sin esperar
// el intervalo de reintento.
static void marcarEstadoPendiente() {
  estadoPendiente = true;
  reintentoInmediato = true;
}

// El estado de la sesión (aula/sesion/estado, retenido): con la sesión activa el
// LED muestra azul. Es un tópico retenido, así que NO pasa por la ventana de
// descarte: el valor retenido es justo lo que hay que aplicar.
static void aplicarEstadoSesion(byte* carga, unsigned int longitud) {
  if (longitud == 0) return;  // payload vacío: no es una orden
  JsonDocument doc;
  if (deserializeJson(doc, reinterpret_cast<const char*>(carga), longitud) ||
      !doc.is<JsonObject>()) {
    return;
  }
  const JsonVariantConst activa = doc.as<JsonObjectConst>()["activa"];
  if (activa.isNull() || !activa.is<bool>()) return;
  if (!controlActuadores.aplicarSesionActiva(activa.as<bool>())) return;
  actuadores::aplicar(controlActuadores);
  marcarEstadoPendiente();
  Serial.println(activa.as<bool>() ? "sesión activa: LED azul" : "sesión terminada");
}

// El PIR detectó movimiento: se publica el evento (QoS 0, sin retención).
static void publicarMovimiento() {
  char carga[16];
  if (logica_pir::construirMovimiento(carga, sizeof carga) == 0) return;
  if (!mqtt.connected()) {
    Serial.println("PIR: movimiento sin enlace: el evento se pierde");
    return;
  }
  mqtt.publish(TOPICO_PIR, carga, false);
  Serial.println("PIR: movimiento");
}

static void alRecibir(char* topico, byte* carga, unsigned int longitud) {
  if (strcmp(topico, TOPICO_SESION_ESTADO) == 0) {
    aplicarEstadoSesion(carga, longitud);
    return;
  }
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

  const JsonObjectConst objeto = doc.as<JsonObjectConst>();
  const unsigned long ahora = millis();
  bool cambio = false;

  // --- Tira LED --------------------------------------------------------------
  const JsonVariantConst valorLuz = objeto["luz"];
  const PedidoLuz pedidoLuz =
      logica_luz::interpretarLuz(!valorLuz.isNull(), valorLuz.is<int>(), valorLuz.as<int>());
  if (pedidoLuz == PedidoLuz::Invalido) {
    Serial.println("cmd: \"luz\" inválida, se ignora (solo 0 o 1)");
  }
  // Ausente e Invalido no cambian nada. Un Encender, aunque la luz ya esté
  // encendida, reinicia el plazo del apagado automático.
  if (controlLuz.aplicar(pedidoLuz, ahora)) {
    escribirPin();
    cambio = true;
    Serial.println(controlLuz.encendida() ? "luz encendida" : "luz apagada");
  }

  // --- Servo -----------------------------------------------------------------
  const JsonVariantConst valorServo = objeto["servo"];
  const PedidoServo pedidoServo = logica_actuadores::interpretarServo(
      !valorServo.isNull(), valorServo.is<const char*>(), valorServo.as<const char*>());
  if (pedidoServo == PedidoServo::Invalido) {
    Serial.println("cmd: \"servo\" inválido, se ignora (solo \"abrir\")");
  }
  if (controlActuadores.aplicarServo(pedidoServo, ahora)) {
    cambio = true;
    Serial.println(controlActuadores.servoAbierto() ? "servo abierto" : "servo cerrado");
  }

  // --- LED RGB ---------------------------------------------------------------
  const JsonVariantConst valorLed = objeto["led"];
  const PedidoLed pedidoLed = logica_actuadores::interpretarLed(
      !valorLed.isNull(), valorLed.is<const char*>(), valorLed.as<const char*>());
  if (pedidoLed == PedidoLed::Invalido) {
    Serial.println("cmd: \"led\" inválido, se ignora");
  }
  if (controlActuadores.aplicarLed(pedidoLed)) {
    cambio = true;
    Serial.println("LED cambiado");
  }

  // --- Buzzer ----------------------------------------------------------------
  // El buzzer no va en el payload del estado, así que un pitido no marca el
  // estado como pendiente: solo hay que escribir el pin.
  const JsonVariantConst valorBuzzer = objeto["buzzer"];
  const PedidoBuzzer pedidoBuzzer = logica_actuadores::interpretarBuzzer(
      !valorBuzzer.isNull(), valorBuzzer.is<int>(), valorBuzzer.as<int>());
  if (pedidoBuzzer == PedidoBuzzer::Invalido) {
    Serial.println("cmd: \"buzzer\" inválido, se ignora (solo 0, 1 o 2)");
  }
  bool cambioBuzzer = false;
  if (controlActuadores.aplicarBuzzer(pedidoBuzzer, ahora)) {
    cambioBuzzer = true;
  }

  if (cambio) {
    actuadores::aplicar(controlActuadores);
    marcarEstadoPendiente();
  } else if (cambioBuzzer) {
    actuadores::aplicar(controlActuadores);
  }

  // Toda orden válida se responde con el estado, aunque no haya cambiado: si el
  // retenido del broker se quedó atrás (publicación fallida), se corrige.
  if (logica_luz::esOrden(pedidoLuz) || logica_actuadores::esOrden(pedidoServo) ||
      logica_actuadores::esOrden(pedidoLed) || logica_actuadores::esOrden(pedidoBuzzer)) {
    publicarEstado();
  }
}

// Un intento de conexión al broker. Bloquea loop() mientras dura: la conexión TCP
// (hasta TIMEOUT_SOCKET_MS), la espera del CONNACK (otros TIMEOUT_SOCKET_S) y, si
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
  bool suscrito = mqtt.subscribe(TOPICO_CMD, 1);
  // El estado de la sesión también es retenido, pero hay que aplicarlo: se
  // suscribe fuera de la ventana de descarte (que solo afecta a las órdenes).
  suscrito = mqtt.subscribe(TOPICO_SESION_ESTADO, 1) && suscrito;
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
  pinMode(PIN_PIR, INPUT);

  Serial.begin(115200);  // con "USB CDC On Boot" activado, es el USB Serial/JTAG
  Serial.println("XIAO: nodo de campo (tira, cámara, PIR y actuadores)");

  mqtt.setServer(MQTT_HOST, MQTT_PUERTO);
  mqtt.setCallback(alRecibir);
  red.setConnectionTimeout(TIMEOUT_SOCKET_MS);  // milisegundos; acota la conexión TCP y las lecturas
  mqtt.setSocketTimeout(TIMEOUT_SOCKET_S);   // segundos; acota la espera del CONNACK

  // Servo, LED RGB y buzzer: los canales LEDC y los pines. Si la cámara no
  // arranca, el resto del firmware sigue funcionando.
  actuadores::iniciar();

  // Cámara y endpoint /capture. El servidor HTTP va en su propia tarea: la
  // captura no bloquea el lazo del MQTT (captura.h).
  if (captura::iniciar(CAPTURA_HMAC_SECRETO)) {
    Serial.println("cámara: /capture listo");
  } else {
    Serial.println("cámara: no se pudo arrancar; /capture no responde");
  }

  iniciarWifi();
  ultimoWifiOk = millis();
  ultimoMuestreoPir = millis();
  ultimoIntentoMqtt = millis() - MQTT_REINTENTO_MS;  // primer intento en cuanto haya Wi-Fi
}

void loop() {
  const uint32_t ahora = millis();

  // Apagado automático: ver kApagadoAutomaticoMs. Se revisa con o sin enlace y
  // antes de mantenerConexion(), que es lo único que bloquea loop().
  if (controlLuz.revisarVencimiento(ahora)) {
    escribirPin();
    marcarEstadoPendiente();
    Serial.println("Apagado automático: venció el plazo desde el último luz:1");
  }

  // El servo se cierra solo a los 3 s de abrirse, sin bloquear.
  if (controlActuadores.revisarServo(ahora)) {
    actuadores::aplicar(controlActuadores);
    marcarEstadoPendiente();
    Serial.println("servo: se cierra solo");
  }

  // El buzzer avanza su patrón (un pitido o dos, con su pausa).
  if (controlActuadores.revisarBuzzer(ahora)) {
    actuadores::aplicar(controlActuadores);
  }

  // El PIR se muestrea cada MUESTREO_PIR_MS.
  if (ahora - ultimoMuestreoPir >= MUESTREO_PIR_MS) {
    ultimoMuestreoPir = ahora;
    if (controlPir.procesarNivel(logica_pir::esMovimiento(digitalRead(PIN_PIR)), ahora)) {
      publicarMovimiento();
    }
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
  if (controlLuz.aplicar(PedidoLuz::Apagar, ahora)) {
    escribirPin();
    Serial.println("Sin enlace con el broker: luz apagada");
  }
  mantenerConexion();
}
