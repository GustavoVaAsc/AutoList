// Firmware de la XIAO ESP32S3 Sense: nodo de campo del pase de lista.
//
// Qué hace esta versión:
//   - La tira LED de 12 V por MQTT (campo "luz" de aula/actuador/cmd).
//   - La cámara: endpoint HTTP /capture, con la firma HMAC-SHA256 que el
//     servicio de visión manda en las cabeceras X-Captura-TS, X-Captura-Nonce
//     y X-Captura-HMAC. La cámara está apagada salvo mientras se usa, con
//     resolución acotada, límite de ritmo y corte por temperatura (captura.h).
//   - El PIR: publica aula/camara/pir cuando detecta movimiento.
//   - Los actuadores: servo, LED RGB y buzzer (campos "servo", "led" y
//     "buzzer" de aula/actuador/cmd).
//   - La energía: lee el INA219 por I2C y publica aula/camara/energia cada 1 s
//     (voltaje, corriente y potencia de la entrada de 12 V). Sin el módulo, el
//     resto del firmware sigue igual.
//   - La salud: aula/camara/estado (retenido) al conectar y cada 30 s, con
//     rssi, heap y temperatura del chip.
//
// La lógica de cada parte está en módulos de C++ puro con pruebas de host
// (logica_luz, logica_camara, logica_pir, logica_actuadores, logica_proteccion,
// logica_energia);
// aquí solo se hace la E/S: leer el pin, escribir el pin, hablar con el broker.
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
#include "energia.h"
#include "logica_actuadores.h"
#include "logica_camara.h"
#include "logica_energia.h"
#include "logica_luz.h"
#include "logica_pir.h"
#include "logica_proteccion.h"
#include "secrets.h"

// RELAY_IP es obligatoria: es la única IP desde la que /capture acepta conexiones
// (captura.h). Sin ella, cualquiera en la LAN podría abrir conexiones al puerto
// 80 de la cámara y retener sockets o probar firmas. Defínela en secrets.h
// (secrets.h.example, última sección) con la IP del puente (relay) que llega a
// la placa.
#ifndef RELAY_IP
#error "Falta RELAY_IP en secrets.h: la IP del puente (relay) o del servicio de visión que llega a /capture. Ver secrets.h.example. Sin ella la cámara aceptaría conexiones de toda la LAN."
#endif

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
// El INA219 empieza ausente: la primera pasada de loop() intenta detectarlo.
static logica_energia::ControlEnergia controlEnergia;

static bool enVentanaInicial = false;
static unsigned long ultimoIntentoMqtt = 0;
static unsigned long ultimoWifiOk = 0;
static unsigned long ultimoMuestreoPir = 0;
// El último intento de publicar aula/actuador/estado falló (o algo cambió sin
// enlace): loop() lo reintenta mientras haya enlace.
static bool estadoPendiente = false;
static bool reintentoInmediato = false;  // el próximo loop() con enlace publica sin esperar
static unsigned long ultimoReintentoEstado = 0;
static uint32_t ultimoLatido = 0;  // último intento de publicar aula/camara/estado

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

// El INA219: detecta el módulo, lee y publica aula/camara/energia (QoS 0, sin
// retención). Sin módulo, o si falla, solo avisa por el serie (con límite de
// ritmo) y no publica nada: ni lecturas inventadas ni valores que Node-RED
// descartaría. Cada paso son unos pocos milisegundos de I2C con plazo propio
// (energia.h): no bloquea loop() de forma apreciable. Se lee aunque no haya
// enlace, pero entonces la lectura se descarta: no hay nada que acumular.
static void atenderEnergia(uint32_t ahora) {
  using logica_energia::Tarea;
  switch (controlEnergia.siguiente(ahora)) {
    case Tarea::Nada:
      return;
    case Tarea::Iniciar: {
      // Tras un fallo o una pérdida, el bus puede estar colgado: se reinicia
      // antes de reintentar (como mucho una vez por reintento, cada 30 s).
      if (controlEnergia.reiniciarBusAntes() && !energia::reiniciarBus()) {
        Serial.println("INA219: el bus I2C no volvió a arrancar");
      }
      const bool detectado = energia::configurar();
      if (controlEnergia.registrarInicio(detectado)) {
        Serial.println("INA219: detectado, publicando aula/camara/energia");
      } else if (!detectado) {
        Serial.print("INA219: no responde (error I2C ");
        Serial.print(energia::ultimoError());
        Serial.println("); se reintenta más tarde");
      }
      return;
    }
    case Tarea::Leer:
      break;
  }

  int16_t shunt = 0;
  uint16_t bus = 0;
  if (!energia::leerCrudo(&shunt, &bus)) {
    Serial.print("INA219: fallo de lectura (error I2C ");
    Serial.print(energia::ultimoError());
    Serial.println(")");
    if (controlEnergia.registrarLectura(false)) {
      Serial.println("INA219: perdido tras varios fallos seguidos; se reintenta más tarde");
    }
    return;
  }
  controlEnergia.registrarLectura(true);

  logica_energia::Lectura lectura = {};
  const logica_energia::Resultado resultado = logica_energia::interpretar(shunt, bus, &lectura);
  if (resultado != logica_energia::Resultado::Ok) {
    if (controlEnergia.avisarLecturaInvalida(ahora)) {
      Serial.print("INA219: lectura descartada: ");
      Serial.println(logica_energia::describir(resultado));
    }
    return;
  }

  if (!mqtt.connected()) return;  // sin enlace la lectura se pierde (QoS 0, periódica)
  char carga[logica_energia::kEnergiaBytesMax + 1];
  if (logica_energia::construirEnergia(carga, sizeof carga, &lectura) == 0) return;
  mqtt.publish(logica_energia::kTopicoEnergia, carga, false);
}

// Salud de la XIAO (contrato, sección 3): aula/camara/estado retenido, con
// rssi, heap, la temperatura del chip, si la cámara está encendida o en corte
// (por temperatura o por enfriamiento) y si el NVS funciona. El LWT {"en_linea":false} lo pone el
// broker si la placa se cae. Si falla la publicación, el próximo intento es el
// del siguiente latido.
static void publicarLatido() {
  char carga[logica_proteccion::kLatidoBytesMax];
  ultimoLatido = millis();
  const size_t n = logica_proteccion::construirLatido(carga, sizeof carga, WiFi.RSSI(),
                                                      ESP.getFreeHeap(), captura::temperatura(),
                                                      captura::camaraEncendida(), captura::enCorte(),
                                                      captura::nvsBien());
  if (n == 0 || !mqtt.publish(TOPICO_ESTADO_CAMARA, carga, true)) {
    Serial.println("No se pudo publicar aula/camara/estado");
  }
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

  publicarLatido();  // {"en_linea":true,...} al conectar y luego cada 30 s (loop())
  publicarEstado();
  Serial.println("MQTT: conectado");
  return true;
}

// IP fija opcional (secrets.h: IP_ESTATICA, IP_PUERTA_ENLACE, IP_MASCARA y,
// opcionalmente, IP_DNS). Sin esas definiciones, o con alguna vacía o que no sea
// una IPv4, se queda en DHCP. Va con macros y no con constexpr para que un
// secrets.h anterior, que no las tiene, siga compilando.
// Devuelve true si dejó puesta una IP fija. El modo (DHCP o IP fija) se anuncia
// una sola vez, en comenzarWifi().
static bool configurarIpEstatica(IPAddress* fijada) {
#if defined(IP_ESTATICA) && defined(IP_PUERTA_ENLACE) && defined(IP_MASCARA)
  IPAddress ip, puerta, mascara, dns;
  if (IP_ESTATICA[0] == '\0' || IP_PUERTA_ENLACE[0] == '\0' || IP_MASCARA[0] == '\0') {
    return false;  // sin rellenar: DHCP
  }
  if (!ip.fromString(IP_ESTATICA) || !puerta.fromString(IP_PUERTA_ENLACE) ||
      !mascara.fromString(IP_MASCARA)) {
    Serial.println("Wi-Fi: IP fija mal escrita en secrets.h, se usa DHCP");
    return false;
  }
#if defined(IP_DNS)
  if (IP_DNS[0] == '\0' || !dns.fromString(IP_DNS)) {
    dns = puerta;  // sin DNS propio, el de la puerta de enlace
  }
#else
  dns = puerta;
#endif
  if (!WiFi.config(ip, puerta, mascara, dns)) {
    Serial.println("Wi-Fi: no se pudo fijar la IP, se usa DHCP");
    return false;
  }
  *fijada = ip;
  return true;
#else
  (void)fijada;
  return false;
#endif
}

// WiFi.config() tiene que ir antes de cada WiFi.begin(). Anuncia por el serie,
// la primera vez, si usa DHCP o una IP fija.
static void comenzarWifi() {
  static bool anunciado = false;
  IPAddress fija;
  const bool conIpFija = configurarIpEstatica(&fija);
  if (!anunciado) {
    anunciado = true;
    if (conIpFija) {
      Serial.print("Wi-Fi: IP fija ");
      Serial.println(fija);
    } else {
      Serial.println("Wi-Fi: DHCP");
    }
  }
  WiFi.begin(WIFI_SSID, WIFI_CLAVE);
}

static void iniciarWifi() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  // Sin ahorro de energía del módem: con él, el retenido de la suscripción podría
  // llegar más tarde que la ventana de descarte (VENTANA_RETENIDOS_MS).
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  comenzarWifi();
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
      comenzarWifi();
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

// Borrar el piso de ts de la flash (la salida si un ts absurdo lo dejó fuera de
// alcance): con CAPTURA_BORRAR_PISO_TS definida en secrets.h, o manteniendo el
// botón BOOT (GPIO0) pulsado 1 s en los 3 s siguientes al arranque.
//
// OJO: BOOT NO se puede mantener pulsado AL DAR CORRIENTE ni al pulsar RESET:
// GPIO0 en bajo en ese momento hace que el chip entre en el modo de descarga de
// la ROM y no ejecute este firmware. Hay que arrancar normalmente y pulsar BOOT
// después, con el mensaje de abajo en el serie. GPIO0 solo se mira como pin de
// strapping en el reinicio; después es un pin normal (la cámara y el resto del
// firmware usan otros), y se lee con el pull-up interno.
static const uint8_t PIN_BOOT = 0;
static const unsigned long BOOT_VENTANA_MS = 3000;
static const unsigned long BOOT_MANTENER_MS = 1000;

static bool pedidoBorrarPisoTs() {
#if defined(CAPTURA_BORRAR_PISO_TS)
  Serial.println("CAPTURA_BORRAR_PISO_TS definida en secrets.h: se borra el piso de ts (quítala tras este arranque)");
  return true;
#else
  pinMode(PIN_BOOT, INPUT_PULLUP);
  Serial.println("Para borrar el piso de ts: mantén BOOT pulsado 1 s en los próximos 3 s");
  const unsigned long inicio = millis();
  unsigned long desde = 0;
  bool abajo = false;
  while (millis() - inicio < BOOT_VENTANA_MS) {
    if (digitalRead(PIN_BOOT) == LOW) {
      if (!abajo) {
        abajo = true;
        desde = millis();
      } else if (millis() - desde >= BOOT_MANTENER_MS) {
        Serial.println("BOOT mantenido: se borra el piso de ts");
        return true;
      }
    } else {
      abajo = false;
    }
    delay(10);
  }
  return false;
#endif
}

void setup() {
  // Lo primero: tira apagada. D2 es un pin de strapping; el pull-down externo la
  // mantiene apagada hasta aquí.
  pinMode(PIN_LUZ, OUTPUT);
  digitalWrite(PIN_LUZ, LOW);
  // Con pull-down interno: sin PIR conectado la entrada no flota y no publica
  // {"mov":1} de la nada (cada uno dispara una ráfaga de reconocimiento).
  pinMode(PIN_PIR, INPUT_PULLDOWN);

  Serial.begin(115200);  // con "USB CDC On Boot" activado, es el USB Serial/JTAG
  Serial.println("XIAO: nodo de campo (tira, cámara, PIR, actuadores y energía)");

  mqtt.setServer(MQTT_HOST, MQTT_PUERTO);
  mqtt.setCallback(alRecibir);
  red.setConnectionTimeout(TIMEOUT_SOCKET_MS);  // milisegundos; acota la conexión TCP y las lecturas
  mqtt.setSocketTimeout(TIMEOUT_SOCKET_S);   // segundos; acota la espera del CONNACK

  // Servo, LED RGB y buzzer: los canales LEDC y los pines. Si la cámara no
  // arranca, el resto del firmware sigue funcionando.
  actuadores::iniciar();

  // INA219 en el I2C0 (Wire) por D4/D5. La cámara va en el I2C1 por otros pines
  // (captura.cpp), así que no se estorban. Si el bus no arranca, el firmware
  // sigue sin lecturas de energía.
  if (!energia::iniciarBus()) {
    Serial.println("INA219: el bus I2C no arrancó; sin lecturas de energía");
  }

  // El Wi-Fi va ANTES que la cámara: el core de Arduino inicializa la pila
  // TCP/IP (esp_netif_init, que es lo que crea el candado de lwIP) en
  // WiFi.mode()/WiFi.begin(), y el servidor HTTP de /capture abre un socket al
  // arrancar (httpd_start). Si ese socket se abre antes de que lwIP esté
  // inicializado, lwIP toma un candado nulo: la placa revienta con un assert de
  // FreeRTOS y se reinicia en bucle.
  iniciarWifi();
  ultimoWifiOk = millis();
  ultimoMuestreoPir = millis();
  ultimoIntentoMqtt = millis() - MQTT_REINTENTO_MS;  // primer intento en cuanto haya Wi-Fi

  // Endpoint /capture. El servidor HTTP va en su propia tarea: la captura no
  // bloquea el lazo del MQTT (captura.h). La cámara NO se enciende aquí: la
  // enciende la primera petición firmada y se apaga sola en reposo.
  // RELAY_IP (obligatoria, secrets.h): /capture solo acepta esa IP de origen.
  if (captura::iniciar(CAPTURA_HMAC_SECRETO, RELAY_IP, pedidoBorrarPisoTs())) {
    Serial.println("cámara: /capture listo (cámara apagada hasta la primera petición)");
  } else {
    Serial.println("cámara: no se pudo arrancar; /capture no responde");
  }
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

  atenderEnergia(ahora);

  const bool enLinea = WiFi.status() == WL_CONNECTED && mqtt.connected();
  if (enLinea) {
    mqtt.loop();
    if (logica_proteccion::intervaloCumplido(ahora, ultimoLatido,
                                             logica_proteccion::kIntervaloLatidoMs)) {
      publicarLatido();
    }
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
