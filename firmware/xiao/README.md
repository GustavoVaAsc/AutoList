# Firmware de la XIAO ESP32S3 Sense

Primera versión: controla la **tira LED de 12 V** por MQTT (campo `luz` de `aula/actuador/cmd`). Todo lo demás del firmware de la XIAO (cámara y `/capture`, PIR, servo, LEDs RGB indicadores y buzzer) sigue sin escribirse y es del carril de Gustavo.

> **Estado: compila, sin flashear.** El 4 nov el firmware completo (tira, cámara, PIR, servo, LED RGB y buzzer) compiló con el core ESP32 3.3.12, PubSubClient 2.8.0 y ArduinoJson 7.4.3, con `arduino-cli` y sin un solo aviso ([Compilar y flashear](#compilar-y-flashear-gustavo)). El sketch con la lectura del INA219 (`energia.cpp`, `logica_energia.cpp`) también compila con esas mismas versiones y sin avisos. **En la placa no se ha flasheado nada todavía**: lo que necesita la placa está en [Sin verificar](#sin-verificar).

## Contenido

- [Carriles](#carriles)
- [Archivos](#archivos)
- [Qué hace](#qué-hace)
- [La cámara y el endpoint /capture](#la-cámara-y-el-endpoint-capture)
- [Cableado de los sensores y los actuadores](#cableado-de-los-sensores-y-los-actuadores)
- [Cableado de la etapa de potencia](#cableado-de-la-etapa-de-potencia)
- [Bibliotecas](#bibliotecas)
- [Configurar secrets.h](#configurar-secretsh)
- [Compilar y flashear (Gustavo)](#compilar-y-flashear-gustavo)
- [Probar desde Node-RED](#probar-desde-node-red)
- [Integrar el resto del firmware](#integrar-el-resto-del-firmware)
- [Pruebas de host](#pruebas-de-host)
- [Sin verificar](#sin-verificar)
- [Resuelto el 4 nov](#resuelto-el-4-nov)

## Carriles

- **Yordi:** firmware de la tira LED (este código) y su etapa de potencia (el cableado de abajo), con el acuerdo de Gustavo.
- **Gustavo:** el resto del firmware de la XIAO, y quien flashea la placa y tiene la tira, el MOSFET y el adaptador. La XIAO es un equipo físico único (plan, sección 12.4).

## Archivos

| Archivo | Para qué |
|---|---|
| `xiao.ino` | Sketch de Arduino: Wi-Fi, MQTT, pin de la tira y publicación del estado. La carpeta se llama `xiao` para que el IDE lo acepte. |
| `logica_luz.h`, `logica_luz.cpp` | Decisiones en C++ puro, sin Arduino: descarte de retenidos y vacíos, interpretación de `luz`, estado de la luz y payload de `aula/actuador/estado`. |
| `pruebas/test_logica_luz.cpp` | Pruebas de host de `logica_luz` (`g++`, sin dependencias). El IDE de Arduino no compila esta carpeta. |
| `logica_pir.h`, `logica_pir.cpp` | Lógica del PIR: flanco de subida, calentamiento, antirrebote y el payload `{"mov":1}`. |
| `logica_actuadores.h`, `logica_actuadores.cpp` | Lógica del servo, el LED RGB y el buzzer: interpretación de las órdenes, temporizado del servo, prioridad del LED azul y el payload completo del estado. |
| `actuadores.h`, `actuadores.cpp` | La parte con Arduino de los actuadores: pines, canales LEDC, pulso del servo, tono y color. |
| `hmac_sha256.h`, `hmac_sha256.cpp` | SHA-256 y HMAC-SHA256 en C++ puro, sin dependencias. La copia del servicio de visión es la misma. |
| `logica_camara.h`, `logica_camara.cpp` | Lógica de la petición de captura: cabeceras, cadena canónica, firma, frescura y anti-replay. |
| `captura.h`, `captura.cpp` | La parte con Arduino: la cámara y el endpoint HTTP `/capture` con su servidor en su propia tarea. |
| `logica_energia.h`, `logica_energia.cpp` | Lógica de la energía: conversión de los registros del INA219 a V, A y W, validación contra los rangos de Node-RED, el payload `{"v":...,"i":...,"p":...}` y el reparto del tiempo (lectura periódica, reintento de detección, módulo perdido). Las constantes del módulo (shunt, rango, dirección, periodo) están en la cabecera. |
| `energia.h`, `energia.cpp` | La parte con Arduino de la energía: el bus I2C0 (`Wire`) en D4/D5 y la lectura de los registros del INA219, sin biblioteca. |
| `pruebas/test_logica_pir.cpp`, `pruebas/test_logica_actuadores.cpp`, `pruebas/test_hmac_sha256.cpp`, `pruebas/test_logica_camara.cpp`, `pruebas/test_logica_energia.cpp` | Pruebas de host de cada módulo (`g++`, sin dependencias). |
| `herramientas/*.py` | Los scripts de Python que hacen de puente con el JSON de `arduino-cli` (versiones instaladas, sha256 del core, puertos de la placa, URLs del índice). Cada uno en su archivo, con su uso en la cabecera. |
| `compilar.sh` | Compila el sketch con `arduino-cli` y las versiones fijadas (core, bibliotecas y placa). |
| `flashear.sh` | Compila y flashea por USB (busca la placa por su VID:PID). |
| `instalar-arduino-cli.sh` | Instala `arduino-cli` en el usuario, sin `sudo`, en la versión fijada y con el sha256 comprobado. |
| `secrets.h.example` | Plantilla de `secrets.h` (ignorado por git). |

## Qué hace

- **Se conecta** al Wi-Fi y al broker con el usuario `xiao` (client id `xiao`; el broker usa el nombre de usuario como client id, así que **no hay que conectar otro cliente con el usuario `xiao`** mientras la placa esté encendida: se expulsarían entre sí).
- **Se suscribe** a `aula/actuador/cmd` (QoS 1, sesión limpia) y a `aula/sesion/estado` (retenido). Del `cmd` aplica `luz` (el entero `0` apaga y el `1` enciende la tira en D2/GPIO3), `servo` (solo `"abrir"`), `led` (azul, verde, rojo, ambar, apagado) y `buzzer` (0, 1, 2). Cualquier valor fuera de esos se ignora. Del estado de la sesión toma `activa`: con la sesión activa el LED azul manda sobre el color de retroalimentación.
- **Descarta** los mensajes con payload vacío y los que pueden ser retenidos (contrato, sección 1). Ver [Desviaciones del contrato](#desviaciones-del-contrato).
- **Publica** `aula/actuador/estado` retenido y **completo**: `{"servo":"cerrado","led":"azul","luz":0}`. Lo publica cada vez que cambia cualquier actuador, al (re)conectar y en respuesta a **toda** orden válida, aunque no cambie nada. Si una publicación falla, queda pendiente y `loop()` la reintenta cada segundo mientras haya enlace.
- **Cámara:** sirve `GET /capture?res=UXGA` con una JPEG de 2048×1536, **solo si la petición viene firmada** (ver [La cámara y el endpoint /capture](#la-cámara-y-el-endpoint-capture)). El servidor HTTP va en su propia tarea de FreeRTOS: la captura no bloquea el lazo del MQTT.
- **PIR:** lee D0 (GPIO1) cada 50 ms y publica `{"mov":1}` en `aula/camara/pir` (QoS 0, sin retención) en el flanco de subida, con un antirrebote de 2 s y sin tomar en cuenta el primer minuto (el calentamiento del HC-SR501).
- **INA219:** lee el módulo por I2C (D4/D5, `Wire` en el I2C0) y publica `{"v":12.10,"i":0.850,"p":10.29}` en `aula/camara/energia` (QoS 0, sin retención) **cada 1 s** (`kPeriodoMs` en `logica_energia.h`). Calcula la corriente con la tensión del shunt (`kShuntOhm` = 0.1 Ω, rango de ±320 mV, que son 3.2 A) y la tensión del bus (rango de 32 V), sin usar los registros de calibración, corriente y potencia del chip; la potencia es `v × i`. **No publica lo que Node-RED descartaría ni lo que no es fiable:** shunt saturado (3.2 A o más), corriente negativa de más de 10 mA (hasta -10 mA se toma como 0 A, por el desvío del shunt) o valores fuera de v 0-30, i 0-10, p 0-300; en esos casos avisa por el serie, como mucho cada 30 s. **Sin el módulo, o si falla, el resto del firmware sigue igual:** si no responde al arrancar lo dice por el serie y reintenta la detección cada 30 s; si estando activo falla la lectura 3 veces seguidas, lo da por perdido y vuelve a reintentar. Cada lectura son 4 transacciones de I2C a 100 kHz (del orden de 1 ms), cada una con un plazo de 20 ms (un chip ausente responde NACK al instante; solo un bus colgado agota el plazo), y se hace desde `loop()`: cada transacción espera como mucho 20 ms, así que un bus lento pero vivo puede retener `loop()` hasta unos 80 ms por lectura en el peor caso; un bus muerto corta en el primer fallo (20 ms) y con el módulo ausente un NACK vuelve al instante. **Un bus colgado se recupera solo:** antes de cada reintento de detección (como mucho uno cada 30 s, tras un fallo o una pérdida) `energia::reiniciarBus()` cierra `Wire`, suelta SDA con hasta 9 pulsos de SCL y un STOP (solo en GPIO5/6) y vuelve a abrir `Wire`. Solo recupera un SDA retenido; si un esclavo retiene SCL en bajo, el bus sigue colgado hasta el siguiente intento. Sin enlace con el broker la lectura se descarta. **Con el módulo ausente, el core de Arduino-ESP32 puede imprimir sus propias líneas de error de I2C** (según el nivel de depuración del core) junto a los avisos del firmware: es lo esperado y no indica otro problema. No toca la cámara: la SCCB va en el I2C1.
- **Will y salud mínima:** registra el LWT `{"en_linea":false}` (retenido, QoS 1) en `aula/camara/estado` y publica `{"en_linea":true}` al conectar, como propone el contrato (sección 3). El latido de cada 30 s con `rssi`, `heap` y `temp` queda para el firmware de Gustavo.
- **Arranque seguro:** `setup()` pone D2 en LOW antes que cualquier otra cosa, y el pull-down de la compuerta mantiene la tira apagada mientras la placa arranca.
- **Apagado automático (decisión de Yordi):** la tira se apaga sola a los **360 s (6 min) del último `luz:1`** (`kApagadoAutomaticoMs` en `logica_luz.h`), sin bloquear (`millis()` con resta sin signo, correcta con el desbordamiento). Cada `luz:1`, aunque la luz ya esté encendida, reinicia el plazo, y al apagarse publica el estado. **Por qué 360 s:** el servicio de visión publica `luz:1` una sola vez al iniciar el registro de 5 poses y `luz:0` al terminar, y un registro puede durar hasta unos 5 min (el vencimiento de Node-RED); 360 s lo cubren con margen. **Quien necesite la luz más tiempo debe reenviar `luz:1` antes de 360 s.** Si Gustavo agrega a la visión un reenvío periódico de `luz:1`, el plazo se puede bajar (`kApagadoAutomaticoMs`). Hay que avisarle por la orquestación luz–captura.
- **Apagado de seguridad (decisión):** si se pierde el Wi-Fi o la conexión con el broker, la luz se **apaga**. Sin enlace nadie puede ordenar apagarla. Al reconectar publica el estado real (`luz: 0`). El estado retenido del broker puede decir `luz: 1` mientras la placa está desconectada; para saber si es confiable, mirar `aula/camara/estado`. Por eso quien publique `luz:1` no debe asumir que se aplicó.
- **Reconexión sin bloqueo indefinido:** cada vuelta de `loop()` hace como mucho un intento. Wi-Fi: reintento automático del controlador y, tras 30 s sin enlace, reinicio de la conexión (con `WiFi.setSleep(false)`). MQTT: un intento cada 5 s. **Un intento bloquea `loop()`**: la conexión TCP (hasta `TIMEOUT_SOCKET_MS` = 5 s, con `setConnectionTimeout`), la espera del CONNACK (otros `TIMEOUT_SOCKET_S` = 5 s, con `setSocketTimeout`) y, si `MQTT_HOST` es un nombre y no una IP, la resolución DNS, que puede tardar más. Con una IP son unos 10 s en el peor caso, más 1.5 s de la ventana de retenidos si conecta. **Pon una IP en `MQTT_HOST`.** Con la luz apagada por falta de enlace esto no importa; sí importará cuando entren el servo, el PIR y el INA219 (ver [Integrar](#integrar-el-resto-del-firmware)).

### Desviaciones del contrato

Hay tres, todas por límites de PubSubClient 2.8, y ninguna cambia los tópicos ni la ACL (se anotan en el contrato v0.8):

1. **Retain.** El callback de PubSubClient no expone la bandera retain. Un retenido solo se distingue en la entrega inicial que hace el broker justo después de suscribirse (en MQTT 3.1.1 uno publicado en vivo llega sin la bandera: contrato, sección 1). El firmware **descarta todo lo que llegue en los 1500 ms siguientes a la suscripción** (`VENTANA_RETENIDOS_MS`), con el ahorro de energía del Wi-Fi desactivado (`WiFi.setSleep(false)`) para que el retenido no se retrase más allá de la ventana.
   - **Es una heurística, no una garantía:** un retenido que tarde más que la ventana (red lenta, broker cargado) se aplicaría como una orden en vivo.
   - **Costo:** una orden en vivo que llegue en esos 1.5 s se pierde. Ocurre solo justo después de (re)conectar, y con la luz apagada por el apagado de seguridad; el emisor no debe asumir que `luz:1` se aplicó (contrato v0.8).
   - **Cuándo pasar a esp-mqtt** (el cliente MQTT de Espressif, que sí entrega la bandera retain y publica con QoS 1): si Gustavo mide que la ventana no alcanza, o cuando haga falta QoS 1 al publicar o TLS. Hoy se queda PubSubClient, por decisión de Yordi y por el plan (sección 6.1).
2. **QoS del estado.** El contrato pide QoS 1 para `aula/actuador/estado` y `aula/camara/estado`; PubSubClient solo publica con QoS 0. Ambos son retenidos, así que el último valor queda en el broker, pero un paquete perdido no se reintenta a nivel MQTT (el firmware sí reintenta si la escritura al socket falla). El LWT sí se registra con QoS 1.
3. **Estado parcial.** *Cerrada el 4 nov:* el firmware ya controla el servo y los LEDs, así que `aula/actuador/estado` publica los tres campos, `{"servo":"cerrado","led":"azul","luz":0}`, como da el contrato (v0.9). El estado va retenido y cada publicación reemplaza a la anterior.

## La cámara y el endpoint /capture

La cámara del Sense (OV3660, o el OV5640 de las placas más nuevas) la maneja el driver que **viene con el core de Espressif**: el componente `espressif__esp32-camera` está en las librerías precompiladas de `esp32s3-libs`, así que `#include "esp_camera.h"` compila sin instalar nada. Los pines del modelo `CAMERA_MODEL_XIAO_ESP32S3` están en el propio core (`libraries/ESP32/examples/Camera/CameraWebServer/camera_pins.h`) y `captura.h` los copia a constantes.

**La petición va firmada.** El servicio de visión no debe pedir la foto a pelo: la firma con HMAC-SHA256 y un secreto compartido. Cabeceras:

| Cabecera | Qué es |
|---|---|
| `X-Captura-TS` | Epoch en segundos del reloj del **borde** (la XIAO no tiene reloj confiable; la Orange Pi sincroniza su hora por red mediante NTP con systemd-timesyncd) |
| `X-Captura-Nonce` | Entero aleatorio de 64 bits, en decimal |
| `X-Captura-HMAC` | 64 caracteres hex en minúscula |

La cadena firmada es `GET\n/capture\n<ts>\n<nonce>` (UTF-8, sin salto final). **El camino va sin la query**: la query solo pide la resolución y no cambia el contenido de la foto, así que no va firmada. Es una limitación del protocolo, documentada.

Respuestas: **200** con la JPEG, **401** si falta una cabecera o la firma no cuadra, **403** si el nonce ya se usó o el `ts` es más antiguo que el último aceptado (ventana de 300 s), **503** si la cámara no está lista.

**La XIAO no compara el `ts` con su reloj** (no tiene uno confiable): lo compara con el último `ts` que aceptó, y recuerda los **64** últimos nonces.

**Lo que esto protege y lo que no.** Una petición firmada que se reenvíe con su `ts` viejo (más de 300 s) se rechaza, y un nonce repetido también. **Pero el estado vive solo en RAM**: si la placa se reinicia, la memoria se borra y una petición capturada vuelve a ser aceptable. Y como la ráfaga de una captura son 4 fotos, hacen falta 64 peticiones (unas 16 capturas) para que el nonce más antiguo se salga de la ventana. **La respuesta, además, no va autenticada**: la firma protege la petición, no la foto que vuelve. Todo eso está anotado en el README del servicio de visión.

La implementación del HMAC es **la misma en los dos lados**: aquí `firmware/xiao/hmac_sha256.cpp`, y en el borde `orangepi/vision/src/hmac_sha256.cpp`, con vectores de prueba compartidos (los mismos cinco). **La copia del borde todavía no está en esta rama**: vive en la rama de la NPU, que sigue aparte. Mientras tanto, **la placa exige la firma y el servicio no la manda**, así que `/capture` responde 401 y no hay fotos hasta que se junten las dos partes. El secreto se llama `CAPTURA_HMAC_SECRETO` y es **el mismo valor** en `firmware/xiao/secrets.h` (la placa) y en `/etc/pase-lista/vision.env` (el borde). **El HMAC autentica al emisor pero no cifra**: las fotos siguen viajando en claro por la LAN.

## Cableado de los sensores y los actuadores

- **PIR → D0 (GPIO1).** El HC-SR501 es de 5 V: su salida **no puede ir directa** a un GPIO de 3.3 V. En la protoboard va un divisor de **10 kΩ en serie** con la salida y **20 kΩ a GND** (3.33 V en el pin). El sensor se alimenta de 5 V y comparte tierra con la XIAO. Tarda alrededor de un minuto en calentarse: el firmware ignora los movimientos de ese primer minuto.
- **Servo → D1 (GPIO2).** Se alimenta del **buck de 5 V**, nunca del pin de 3.3 V, con un **capacitor de 470 µF** junto a él (un brownout del servo reinicia la cámara) y tierra común. El pulso es de 50 Hz: 500 µs con la puerta cerrada (0°) y 1500 µs con la puerta abierta (90°). Si en la placa abre hacia el otro lado, se ajustan `PULSO_SERVO_CERRADO_US` y `PULSO_SERVO_ABIERTO_US` en `actuadores.h`.
- **LED RGB → D8, D9 y D10 (GPIO 7, 8 y 9).** Son los pines de la microSD de la placa Sense, que el proyecto no usa. Si los LEDs rescatados son **WS2812** (un solo pin de datos), hay que cambiar las constantes de `actuadores.h` y escribir el color con una biblioteca WS2812.
- **Buzzer → D3 (GPIO4).** Tono de 2 kHz al 50 % de duty, que es lo que reproducen tanto los buzzers pasivos como los activos.
- **Tira LED de 12 V → D2 (GPIO3)**, con su MOSFET y el pull-down (sección anterior).
- **INA219 → D4 y D5 (GPIO 5 y 6), en I2C0.** La SCCB de la cámara usa el **I2C1** (el `esp32-camera` del core 3.3.12 está hecho con `CONFIG_SCCB_HARDWARE_I2C_PORT1=y`, con los pines GPIO39/40 de la placa), así que el INA219 va en `Wire.begin(5, 6)`, que en el core es el I2C0. Implementado, sin verificar en la placa (ver [Sin verificar](#sin-verificar)). El módulo se conecta con VCC a 3.3 V (y no a 5 V: si el módulo lleva pull-ups a VCC, SDA y SCL subirían a 5 V y los GPIO de la XIAO no lo toleran), GND común, SDA a D4 y SCL a D5, con la dirección de fábrica 0x40 (A0 y A1 a GND). Los bornes `VIN+` y `VIN-` van **en serie** con el +12 V: `VIN+` del lado del adaptador y `VIN-` del lado de la carga (ver [Cableado de la etapa de potencia](#cableado-de-la-etapa-de-potencia)). Con ellos al revés la corriente sale negativa y no se publica.

## Cableado de la etapa de potencia

Un solo MOSFET de canal N en el lado bajo, con los tres canales (R, G y B) de la tira unidos: la tira se enciende en blanco o se apaga. No hay control de color ni de brillo.

```
Adaptador 12 V (+) ────────────────────────► +12V de la tira
                                              Tira: R, G y B unidos
                                                       │
                                                       ▼
                                                 Drenador (D)
                                                       │
XIAO D2 (GPIO3) ──[ 220 Ω ]──┬──────────────────► Compuerta (G)   MOSFET de canal N
                             │                         │
                         [ 10 kΩ ] pull-down      Fuente (S)
                             │                         │
Adaptador 12 V (−) ──────────┴─────────────────────────┴──── GND de la XIAO (pin GND)
```

Conexiones:

1. **+12V de la tira ← positivo del adaptador** (con un fusible de acción rápida en esa línea, dimensionado por debajo del amperaje del adaptador). Con el INA219 ya instalado, tomar este +12 V de la línea de 12 V **después** del INA219 para que la tira cuente en la medición de energía (plan, sección 8). **Antes de instalarlo, verificar la corriente máxima:** el módulo típico (shunt de 0.1 Ω) satura en unos 3.2 A (320 mV en el shunt). Si la tira, sumada al resto del sistema, puede superar eso, no pasar la corriente de la tira por el INA219 (tomar su +12 V **antes** del INA219, y anotar que la medición no incluye la tira), usar un tramo más corto o un módulo con un shunt menor.
2. **R, G y B de la tira, unidos → drenador (D)** del MOSFET.
3. **Fuente (S) del MOSFET → GND común.**
4. **Compuerta (G) ← D2 de la XIAO** a través de una resistencia en serie de **220 Ω**, con una de **10 kΩ de la compuerta a GND** (pull-down: apaga la tira mientras D2 flota al arrancar).
5. **Tierra común:** el negativo del adaptador de 12 V y el pin GND de la XIAO van unidos, junto con la fuente del MOSFET. Sin tierra común el MOSFET no conmuta de forma fiable.
6. **Nunca llevar 12 V a la XIAO.** Los pines de la placa toleran 3.3 V; el pin 5V solo recibe 5 V. Solo la tira y el MOSFET ven los 12 V.

Antes de conectar la tira: con el adaptador desconectado, revisar con el multímetro que la compuerta queda en 0 V cuando la XIAO está apagada o arrancando, y que D (drenador) no está en corto con la compuerta.

### Dos opciones de MOSFET (plan, sección 8)

| | IRLZ44N | AO3400 |
|---|---|---|
| Encapsulado | TO-220, con orificios | SOT-23, soldable; normalmente viene en una placa pequeña |
| Patillas | Mirando el lado con texto, de izquierda a derecha: **G, D, S** | En el SOT-23 sin placa: **G = 1, S = 2, D = 3**. Con placa, seguir su serigrafía |
| Nivel lógico | Su resistencia de conducción está especificada a Vgs de 4 V o más. A 3.3 V conduce, pero sin garantía del fabricante: es la opción más ajustada | Especificada desde 2.5 V: va sobrada con 3.3 V |
| Corriente | Mucha más que el AO3400; con más de unos 2 A conviene un disipador (la lengüeta es el drenador). Pero a 3.3 V de compuerta su resistencia de conducción no está garantizada | **Solo hasta unos 2 A en la práctica, y con cobre o placa que disipe el calor.** El SOT-23 no tiene disipador: la cifra de 5 A de la hoja de datos supone una placa con mucho cobre |

El orden de las patillas **cambia entre las dos piezas** (G, D, S contra G, S, D): confirmar con la hoja de datos de la pieza comprada. Con el IRLZ44N, comprobar con la tira encendida que la tensión entre drenador y GND baja de unos 0.3 V y que el MOSFET no se calienta; si no, usar el AO3400 (si la tira consume menos de unos 2 A) o un módulo de MOSFET para 3.3 V. La carcasa metálica del IRLZ44N está unida al drenador: no la dejes tocar GND.

### Adaptador y consumo de la tira

La fuente es el "AC/DC adapter" negro del kit, que según Yordi dice 12 V. **No se sabe su amperaje.** El controlador IR del kit (la cajita con el receptor) **no se usa**: la XIAO sustituye su función.

Estimación orientativa para una tira 5050 RGB de 12 V **con los tres canales encendidos** (blanco): unos 60 mA por cada 3 LEDs, es decir

| LEDs por metro | Potencia por metro | Corriente por metro | Ejemplo de 5 m |
|---|---|---|---|
| 30 | unos 7.2 W | unos 0.6 A | unos 3 A |
| 60 | unos 14.4 W | unos 1.2 A | unos 6 A |

Con 30 LEDs/m, más de unos 3 m ya superan los 2 A del AO3400; con 60 LEDs/m, más de unos 1.6 m. Por encima de eso: IRLZ44N con disipador o un módulo de MOSFET. Los datos de la tira siguen pendientes de Gustavo (abajo), así que la elección de MOSFET también.

El dato real está en la etiqueta de la tira (W/m) o en el carrete. **Comparar antes de enchufar:** el amperaje del adaptador debe ser al menos un 25 % mayor que el consumo de la tira que se conecte (más el del resto del sistema si comparten fuente; el plan prevé una fuente de ≥ 3 A para todo, sección 8). Si el adaptador es menor, usar un tramo más corto de tira o una fuente mayor: un adaptador sobrecargado se calienta, cae su tensión y puede reiniciar la XIAO si comparten la fuente.

### Datos pendientes de confirmar por Gustavo (tiene la tira)

Hasta tenerlos, este cableado es un diseño sin validar contra la pieza real:

- [ ] **MOSFET comprado:** IRLZ44N o AO3400 (y, si es el AO3400, si es la pieza suelta o en placa).
- [ ] **Largo de la tira** o número de LEDs, y la densidad (LEDs por metro), para calcular el consumo.
- [ ] **Amperaje del adaptador** (etiqueta del adaptador, junto a los 12 V).
- [ ] **Qué dice impreso junto a los 4 pines de la tira.** Debe ser `+12V`, `R`, `G`, `B` (ánodo común). Si el pin común dice `-`, `GND` o `0V` (cátodo común), este cableado **no sirve**: la etapa tendría que ser de lado alto. No conectar y avisar a Yordi.

### D2 (GPIO3) es un pin de strapping

El ESP32-S3 lo usa en el arranque para elegir el origen de las señales JTAG, y por defecto está flotante. El pull-down de la compuerta lo fija a 0 y el firmware lo pone en LOW de inmediato. **Al conectar el MOSFET por primera vez hay que comprobar que el USB Serial/JTAG sigue funcionando** (que la placa se programa y el monitor serie responde); es la vía de consola y de programación (plan, sección 13). Si falla, desconectar la compuerta de D2 y avisar.

## Bibliotecas

Instalar desde el Gestor de bibliotecas del IDE de Arduino, o con `arduino-cli` (sección [Compilar y flashear](#compilar-y-flashear-gustavo)). Las versiones están fijadas y son las que se compilaron el 4 nov:

| Componente | Versión | Nota |
|---|---|---|
| Paquete de placas **esp32** de Espressif (core Arduino de ESP32) | 3.3.12 | Placa **XIAO_ESP32S3**. `USB CDC On Boot` ya viene activo en esta placa. |
| **PubSubClient** (Nick O'Leary) | 2.8.0 | `setSocketTimeout` y `connect` con LWT y sesión limpia. Búfer de 256 bytes por defecto: basta para estos mensajes. |
| **ArduinoJson** (Benoît Blanchon) | 7.4.3 | Para leer `luz` del JSON; el sketch usa `JsonDocument`, que es de la serie 7. Se eligió sobre un extractor manual porque un texto con `"luz":1` dentro de otro campo no debe encender la tira. |
| **Driver de cámara** | el del core | **No hay que instalar nada.** El componente `espressif__esp32-camera` va en las librerías precompiladas de `esp32s3-libs`, así que `#include "esp_camera.h"` compila tal cual. Los pines del modelo `CAMERA_MODEL_XIAO_ESP32S3` están en el core (`libraries/ESP32/examples/Camera/CameraWebServer/camera_pins.h`) y `captura.h` los copia a constantes. |
| **Wire** (I2C) | el del core | **No hay que instalar nada.** El INA219 se lee con `Wire` y los registros a mano (`energia.cpp`), sin la biblioteca de Adafruit: son tres registros, y así el firmware controla los plazos y los errores del bus y no hay una versión más que fijar. |
| **HTTP de la IDF** | el del core | El servidor de `/capture` es el `httpd` del IDF (`esp_http_server.h`), que ya viene en el core y va en su propia tarea. |

`arduino-cli` se usa en la versión **1.5.1**. La placa es `XIAO_ESP32S3` con `PSRAM=opi` (8 MB de flash y 8 MB de PSRAM OPI; sin PSRAM no hay JPEG de la cámara a UXGA), `USBMode=hwcdc` (consola y programación por el USB Serial/JTAG) y `PartitionScheme=default_8MB`.

`WiFi.h` viene con el core de ESP32.

**Limitaciones conocidas del toolchain.** El índice de placas de Espressif se descarga por HTTPS y `arduino-cli` verifica el sha256 de cada paquete contra ese índice, pero **la firma GPG del índice no se comprueba**: ese índice es la única raíz de confianza y **no está anclada**. Si alguien sustituyera el índice (un servicio de Espressif comprometido o un intermediario TLS en la red), el `core install` descargaría un paquete distinto, cuyo script de instalación se ejecuta en el equipo que compila: **ejecución de código con acceso a `secrets.h` y al `.bin` con la clave MQTT**, y un firmware falseado en la placa. Como mitigación parcial, el repositorio **ancla el sha256 del paquete `esp32:esp32@3.3.12`** (`SHA256_CORE` en `compilar.sh`): si el índice no sirve ese paquete exacto, el script no instala nada. Verificar la firma GPG del índice con `gpg` sería la mitigación completa y queda pendiente.

El toolchain (el compilador de Xtensa y las bibliotecas del chip) llega como herramienta normal del índice y `core install` la instala; el core 3.3.12 no trae script post-install, así que no hace falta `--run-post-install`.

## Configurar secrets.h

1. Crear `secrets.h` en esta misma carpeta (junto a `xiao.ino`) a partir de la plantilla, **con permisos solo del propietario**: el archivo lleva el SSID, la clave del Wi-Fi y la clave MQTT del usuario `xiao`.

   ```bash
   install -m 600 secrets.h.example secrets.h
   ```

   `cp` a secas deja el archivo en 644 (legible por cualquier cuenta local del equipo).
2. Rellenar el SSID y la clave del Wi-Fi de 2.4 GHz, el host o la IP de la Orange Pi y la clave del usuario `xiao`. Esa clave es la `MQTT_XIAO_PASSWORD` de `orangepi/.env`: pídesela a Yordi; no la copies ni la pegues en ningún documento.
3. `secrets.h` lo ignora git (`.gitignore`). Comprobar con `git status` que no aparece antes de hacer un commit. No lo leas, no lo imprimas y no lo cites: es una regla del repo, igual que con `orangepi/.env`.
4. Las claves quedan en la memoria flash de la XIAO y se pueden extraer con acceso físico. **Si una XIAO flasheada se extravía, hay que rotar la clave del usuario `xiao` (en `orangepi/.env`, lo hace Yordi) y la del Wi-Fi.**
5. **`CAPTURA_HMAC_SECRETO`** es el secreto compartido con el servicio de visión: **el mismo valor** tiene que estar en `/etc/pase-lista/vision.env` (en la Orange Pi). Con 32 caracteres o más, y no puede ser el valor de ejemplo de la plantilla: la placa **no arranca la cámara** con el placeholder (el valor de ejemplo está versionado en el repo, así que con él cualquiera firmaría; el borde comprueba el placeholder exacto y la placa cualquier valor que empiece por `cambiar_`). Los dos lados lo llaman así a propósito, pero son dos archivos distintos en dos equipos distintos: si no coinciden, el servicio pide fotos y la placa las rechaza con 401, y el servicio avisa por log de que la placa no devolvió la foto.
6. Lo mismo vale para el **binario compilado**: el `.bin` y el `.elf` llevan el SSID y las claves como cadenas en claro. Los scripts compilan con `--build-path` en un directorio temporal que se borra al terminar, y `.gitignore` excluye `firmware/**/build/`, `firmware/**/*.bin` y `firmware/**/*.elf`. No los copies a ningún lado ni los subas a un Drive.
7. **La caché de `arduino-cli` también guarda las claves.** Los scripts compilan con `--build-path`, que no deja copia; pero **una compilación a mano o desde el IDE deja una copia de `secrets.h` y del `.bin` en `~/.cache/arduino/sketches/<hash>/`**. Después de compilar con el IDE, o antes de irte de ese equipo, bórrala:

   ```bash
   rm -rf ~/.cache/arduino/sketches
   ```

## Compilar y flashear (Gustavo)

### Con arduino-cli (reproducible)

El toolchain va en el equipo que compila, nunca en la Orange Pi: en la placa no se instala `arduino-cli` ni PlatformIO (AGENTS.md, [Hardware compartido](../../AGENTS.md#hardware-compartido)).

1. Instalar `arduino-cli` en el usuario, sin `sudo`. El script baja la versión fijada (1.5.1) y **comprueba su sha256** contra el que publica la release:

   ```bash
   cd firmware/xiao
   ./instalar-arduino-cli.sh
   ```

   Los hashes salen de `1.5.1-checksums.txt` de la release; el script los lleva escritos y el README dice cómo volver a derivarlos. El instalador comprueba además que la carpeta de destino no sea escribible por el grupo ni por otros (en tu `$HOME` la corrige él mismo) y que el binario instalado responda como esa versión.

2. Crear `secrets.h` como se explica más arriba y rellenarlo.
3. Compilar:

   ```bash
   ./compilar.sh
   ```

   Los scripts de Python que usa están en `herramientas/` (uno por cosa: leer el JSON de la CLI, el sha256 del core, los puertos de la placa). No hay Python escrito dentro de los scripts de bash.

   El script comprueba que el core y las bibliotecas sean **exactamente** las versiones fijadas (y los instala o reemplaza si no lo son), avisa si `secrets.h` conserva los valores de ejemplo y compila con `PSRAM=opi`, `USBMode=hwcdc` y `PartitionScheme=default_8MB`. Se puede pasar la ruta a `arduino-cli` con la variable de entorno `ARDUINO_CLI`.

   Con `BUILD_DIR=<carpeta>` el build va a esa carpeta en vez de a un temporal, y **no se borra al terminar**: sirve para inspeccionar el `.bin`, que lleva las claves en claro, así que hay que borrarlo a mano.

4. Flashear con la placa conectada por USB-C:

   ```bash
   ./flashear.sh              # busca la XIAO entre los puertos
   ./flashear.sh /dev/ttyACM0 # o el puerto explícito
   arduino-cli board list     # para ver los puertos
   ```

   El script identifica la placa por su VID:PID y **no flashea a un puerto que no sea una ESP32 con USB nativo**: el VID:PID `303a:1001` es el USB Serial/JTAG del ROM de Espressif, que comparten todas las ESP32-S3 de la familia, así que **una DevKitC también passaría el filtro**. Para estar seguros, flashea con el puerto explícito y comprueba que `board list` muestra esa placa. Si hay más de una, pide el puerto a mano. Si la placa está conectada y la CLI no la ve, se puede forzar con `PERMITIR_CUALQUIER_PUERTO=1` (solo para depurar; es un escape del control de identidad, no de los permisos de `secrets.h`, que tienen su propio `PERMITIR_SECRETOS_LEGIBLES=1`).

5. Monitor serie a 115200. **El puerto es el del USB Serial/JTAG**, que con `USBMode=hwcdc` es el USB del ROM (VID:PID `303a:1001`); en modo TinyUSB sería el de la variante (`2886:0056`). Se elige con `arduino-cli board list` (columna `Port`) o con `lsusb | grep -i 303a`:

   ```bash
   arduino-cli monitor --port <puerto> --config baudrate=115200
   ```

### Con el IDE de Arduino

Sigue siendo válido: usa el mismo core y las mismas bibliotecas, pero hay que **elegir las mismas opciones que el FQBN de los scripts**, o el binario y la consola no son los mismos.

1. Instalar el paquete de placas y las bibliotecas de la sección anterior.
2. Abrir `firmware/xiao/xiao.ino` (la carpeta tiene el mismo nombre que el sketch), crear `secrets.h` y elegir la placa **XIAO_ESP32S3** con **USBMode en "Hardware CDC and JTAG"** (es el equivalente en el IDE del `USBMode=hwcdc` de los scripts), **PSRAM en OPI** y el esquema de particiones **Default with spiffs**. Con ese modo, el "USB CDC On Boot" del menú no aplica: es el que usa el modo TinyUSB.
3. Compilar y cargar con la tira **aún sin conectar** a los 12 V. Abrir el monitor serie a 115200: debe verse `XIAO: tira LED por MQTT` y luego `MQTT: conectado`.
4. Conectar la etapa de potencia como en el cableado y seguir con las pruebas de abajo. **No conectar el adaptador a la tira hasta resolver los cuatro datos pendientes** (MOSFET, largo y densidad de la tira, amperaje del adaptador y rotulado de los pines): si el amperaje del adaptador no cubre 1.25 veces el consumo de la tira, usar un tramo más corto.

Si la compilación falla por una API distinta a la esperada, avisar a Yordi con el mensaje del compilador. Con las versiones fijadas arriba, el 4 nov compiló sin avisos. **No pegues la salida de la compilación en un chat, en un documento ni en un log compartido si has tocado `secrets.h`**: el compilador puede citar la línea que falló y esa línea lleva una clave. Los scripts ya ocultan esas líneas al imprimirlas, pero la Terminal y el IDE no.

## Probar desde Node-RED

Las pruebas se hacen desde el editor de Node-RED, que ya está autenticado como `nodered` y no obliga a repartir claves de otros usuarios MQTT ni a escribirlas en una línea de comandos. En una **pestaña temporal** del editor:

1. Un nodo `inject` con el payload JSON de la prueba (tipo JSON) y el tópico `aula/actuador/cmd`, conectado a un `mqtt out` que use el servidor MQTT que ya existe en los flujos (`Mosquitto`, id `a1f0c0de00000002` en `nodered/flows/flows.json`; **no crees otro nodo `mqtt-broker` ni escribas credenciales**), con QoS 1 y retain en `false`.
2. Un segundo `mqtt out` igual pero con retain en `true`, para la prueba del retenido.
3. Dos `mqtt in` (`aula/actuador/estado` y `aula/camara/estado`, QoS 1) conectados a nodos `debug`.

**Al terminar, borra la pestaña temporal y vuelve a desplegar:** el editor escribe `nodered/flows/flows.json`, que se versiona, y esa prueba no debe llegar a un commit (comprobar que `git diff nodered/flows/flows.json` queda vacío **en el checkout desde el que corre el stack**, hoy `/home/orangepi/work-yordi`, el que monta `orangepi/docker-compose.yml`, y no en otro worktree).

No uses desde la línea de comandos el usuario `xiao` (es la placa) ni `nodered` (Node-RED lo usa siempre): el broker usa el nombre de usuario como client id y un segundo cliente con el mismo usuario expulsa al primero. Si alguna vez usas `mosquitto_pub` con otro usuario, no pongas la clave con `-P` en la línea de comandos (queda en el historial y en la lista de procesos) y ten en cuenta que expulsa al servicio que use ese usuario.

| Prueba | Payload del `inject` | Resultado esperado |
|---|---|---|
| Encender | `{"luz":1}` | La tira enciende; llega `aula/actuador/estado {"luz":1}` |
| Repetir | `{"luz":1}` otra vez | Sin cambio en la tira; llega `{"luz":1}` de nuevo (toda orden válida se responde con el estado) y el plazo de 360 s se reinicia |
| Apagar | `{"luz":0}` | La tira apaga; llega `{"luz":0}` |
| Con otros campos | `{"servo":"abrir","led":"verde","buzzer":1,"luz":1}` | Enciende; `servo`, `led` y `buzzer` no hacen nada |
| Sin `luz` | `{"servo":"abrir"}` o `{}` | Sin cambio ni mensaje de estado |
| Inválidos | `{"luz":2}`, `{"luz":"1"}`, `{"luz":true}`, `{"luz":1.5}`, `no es json`, `[1]` | Sin cambio ni mensaje de estado; el monitor serie explica por qué |
| Vacío | Payload cadena vacía | Sin cambio (el monitor dice que descartó el mensaje) |
| Retenido | `{"luz":1}` por el `mqtt out` con retain `true`, y reiniciar la XIAO (botón RESET) | La tira **no** enciende al reconectar. Limpiar después enviando un payload vacío por el mismo `mqtt out` |
| Estado al reconectar | Con la luz encendida, reiniciar la XIAO | Tras reconectar llega `{"luz":0}` retenido |
| Apagado automático | `{"luz":1}` y esperar | A los 360 s la tira apaga sola y llega `{"luz":0}` |
| Renovación | `{"luz":1}` y otro `{"luz":1}` a los ~300 s | La tira sigue encendida hasta 360 s después del segundo |
| Apagar antes del plazo | `{"luz":1}` y `{"luz":0}` a los ~30 s | A los 360 s no pasa nada |
| Apagado de seguridad | Con la luz encendida, detener Mosquitto (lo hace Yordi en la placa) | La tira apaga en cuanto se cierra la conexión (hasta unos 30 s si la red se cae sin cerrar el socket) y el monitor lo dice; al volver el broker reconecta en unos 5 s |
| Arranque apagado | Quitar y volver a dar alimentación a la XIAO con la luz encendida | La tira no destella al arrancar; el USB sigue funcionando |
| Will | Quitar la alimentación de la XIAO | Tras unos 20 a 30 s llega `{"en_linea":false}` retenido a `aula/camara/estado` |
| Actuadores | `{"servo":"abrir"}` | El servo abre 90° y se cierra solo a los 3 s; llega `aula/actuador/estado {"servo":"cerrado","led":"apagado","luz":0}` (el LED está apagado si no hay sesión activa) |
| LED y buzzer | `{"led":"verde","buzzer":1}` | El LED se pone verde y suena un pitido; el estado trae los tres campos |
| Sesión activa | (la publica Node-RED) `aula/sesion/estado {"activa":true}` | El LED se pone azul mientras la sesión está activa; al terminarla vuelve al último color |
| PIR | (no hay orden: es un evento) | Al pasar por delante del PIR llega `aula/camara/pir {"mov":1}`; en la terminal de Node-RED, `aula/camara/pir` con `mqtt in` |
| Energía | (no hay orden: es periódico) | Con el INA219 conectado llega `aula/camara/energia {"v":12.10,"i":0.850,"p":10.29}` cada 1 s; el monitor serie dice `INA219: detectado`. Sin el módulo, no llega nada y el monitor dice `INA219: no responde` (reintenta cada 30 s) |
| Cámara | (la pide la visión) | `curl http://<ip-xiao>/capture?res=UXGA` **sin firma** devuelve 401; con la firma bien, devuelve la JPEG. La firma la calcula el servicio de visión: `orangepi/vision` la manda sola |

Mosquitto descarta en silencio lo que su ACL deniega: si una orden no hace nada, mirar el log del broker antes de culpar al firmware.

## Lo que falta (el endurecimiento)

Lo que sigue sin implementar, todo del carril de Gustavo:

- **Latido de salud:** `aula/camara/estado` pasa de publicarse solo al conectar a publicar **cada 30 s** con `rssi`, `heap` y `temp`, conservando el LWT `{"en_linea":false}` (contrato, sección 3).
- **Bloqueo de `loop()`:** los reintentos de conexión MQTT bloquean `loop()` hasta unos 10 s con IP (más con un nombre, por el DNS), más 1.5 s de la ventana de retenidos. Con el servo, el PIR y la cámara ya es un problema: la conexión tiene que pasar a otra tarea de FreeRTOS o a un `connect` no bloqueante. **La cámara ya va en su propia tarea** (`captura.cpp`), pero el MQTT sigue en el lazo.
- **Búfer:** PubSubClient tiene 256 bytes por defecto; si algo que publique o reciba lo supera, `mqtt.setBufferSize()`.
- **Apagado automático:** `loop()` revisa el plazo de 360 s de la luz; cualquier trabajo bloqueante nuevo lo retrasa. La visión y la orquestación luz–captura deben reenviar `luz:1` antes de 360 s (contrato v0.8).
- **QoS 1 al publicar:** PubSubClient solo publica con QoS 0. Para el LWT y para el estado retenido basta; si hace falta QoS 1 de verdad, hay que migrar a esp-mqtt.
- **Pruebas de la cámara en la placa:** ver [Sin verificar](#sin-verificar).

## Pruebas de host

Prueban los módulos puros sin Arduino ni dependencias, solo con `g++`. Cada módulo va en su propio archivo de prueba y se compila aparte. Desde `firmware/xiao/`:

```bash
d=$(mktemp -d)
g++ -std=c++17 -Wall -Wextra -Werror -I. logica_luz.cpp pruebas/test_logica_luz.cpp -o "$d/t_luz"
"$d/t_luz"   # termina con "N verificaciones, 0 fallos" y código 0
```

Los seis módulos y sus pruebas:

| Módulo | Qué cubre |
|---|---|
| `logica_luz` | Descarte de retenidos y de payloads vacíos, `luz` ausente, no entera y fuera de 0 y 1, cambio de estado solo cuando cambia, el apagado automático (vence en el límite, se renueva con otro `luz:1`, no vence si se apaga antes, y con el desbordamiento de `millis()`). |
| `logica_pir` | Flanco de subida (el de bajada no publica), antirrebote, calentamiento del sensor, el desbordamiento de `millis()` y el payload exacto `{"mov":1}`. |
| `logica_actuadores` | Los valores del contrato de `servo`, `led` y `buzzer` (y lo que se ignora), el temporizado de 3 s del servo, la prioridad del LED azul con la sesión activa, y el payload `{"servo":...,"led":...,"luz":N}` incluido cuando no cabe en el búfer. |
| `hmac_sha256` | Los vectores del protocolo (los mismos cinco que usa el servicio de visión) y la comparación en tiempo constante. |
| `logica_camara` | Las cabeceras ausentes, la firma inválida, el nonce repetido, el `ts` antiguo, el camino con query y el payload que no cabe. |
| `logica_energia` | La conversión de los registros a V, A y W (con los límites exactos: 30 V, saturación a 32000 cuentas, -10 mA), lo que no se publica (saturada, negativa, fuera de rango, no finita), el payload exacto y el que no cabe, y el reparto del tiempo: lectura cada 1 s, reintento de detección cada 30 s, módulo perdido tras 3 fallos seguidos, sin acumular lecturas tras un `loop()` bloqueado, el límite de ritmo de los avisos y el desbordamiento de `millis()`. |

**No** cubren `xiao.ino`, `captura.cpp`, `actuadores.cpp` ni `energia.cpp`: el parseo del JSON, el Wi-Fi, el MQTT, la cámara y los pines necesitan la placa.

## Sin verificar

Hasta que Gustavo flashee la placa, **nada de lo siguiente se ha ejecutado**:

- Que el callback de PubSubClient 2.8 no expone la bandera retain (desviación 1), y que la ventana de 1500 ms (con `WiFi.setSleep(false)`) basta para la entrega inicial del broker.
- Que `ArduinoJson` rechaza como se espera `true`, `"1"` y `1.5` en `is<int>()`.
- La conexión, el LWT, la sesión limpia y la suscripción con QoS 1 contra el Mosquitto real y su ACL.
- La reconexión del Wi-Fi y del broker, el tiempo real que bloquea un intento y el apagado de seguridad.
- El apagado automático de 360 s con la placa (la lógica está probada en host; el cableado a `loop()` no).
- El reintento del estado pendiente tras una publicación fallida.
- El camino de flasheo completo (`compile --build-path` y `upload --input-dir`): nunca se ha ejecutado contra hardware. En el primer flasheo hay que comprobar que el build tiene los tres `.bin` (bootloader, particiones y app) y que el `.bin` que se graba es el recién compilado.
- Que D2 (GPIO3) arranca bajo y que el USB Serial/JTAG sigue funcionando con el pull-down puesto.
- El cableado completo con el MOSFET real, el adaptador y la tira; en particular que el IRLZ44N conmuta bien con 3.3 V.
- Los datos de la tira, el adaptador y el MOSFET de la lista de pendientes.
- **La cámara en la placa:** que arranque, que el sensor se detecte (OV3660 u OV5640), que la foto quepa en los 2 MB que acepta la visión, y que el tiempo de la captura deje margen al plazo de 2 s del cliente.
- **El endpoint `/capture` de punta a punta:** que la visión (con la firma) saque la foto y que la placa la rechace con 401 si la firma está mal o el nonce se repite. El lado de la visión **no está compilado** (en este equipo no hay OpenCV 4.6): el `cmake` y el `ctest` se correrán en la Orange Pi.
- **El divisor del PIR** (10 kΩ/20 kΩ) y el nivel real del HC-SR501 en D0.
- **El sentido del servo:** si la puerta abre hacia el lado contrario, se ajustan los dos pulsos en `actuadores.h`.
- **Los LEDs RGB** en D8/D9/D10: que el ámbar (rojo + verde) se vea y que el azul de la sesión mande sobre el color de retroalimentación.
- **El buzzer:** que suene y que el doble pitido del error se oiga.
- **El INA219 en la placa** (el firmware compila, pero nunca se ha ejecutado con el módulo):
  - Que `Wire.begin(5, 6)` arranque el I2C0 y que **la cámara siga arrancando y capturando** con el bus del INA219 activo (la SCCB va en el I2C1; es lo que dice `captura.cpp`, no está comprobado).
  - Que el módulo conteste en 0x40 y que la configuración que se escribe (0x3E67) se lea de vuelta igual; si el módulo comprado trae otra dirección, o el chip de la placa devuelve bits distintos, la detección falla (el serie dice `error I2C 12`).
  - Que el shunt del módulo sea de 0.1 Ω (`kShuntOhm`): con otro valor las corrientes salen mal por el mismo factor.
  - Las lecturas contra un multímetro (tensión del bus y corriente), con la tira apagada y encendida, y que `VIN-` quede del lado de la carga: el bus que mide el chip es el de `VIN-`.
  - Que la tira, sumada al resto del sistema, no pase de 3.2 A (con más, el shunt satura y las lecturas no se publican).
  - El comportamiento sin el módulo (desconectado: avisa por el serie y reintenta cada 30 s, sin afectar al MQTT, el PIR ni el apagado de la luz) y con un cable suelto durante la marcha (pasa a perdido a los 3 fallos).
  - Que Node-RED guarde las lecturas en `metricas_energia` (la prueba de la ingesta con datos reales).

### Resuelto el 4 nov

- **Compila.** El sketch compiló con el core ESP32 3.3.12, PubSubClient 2.8.0 y ArduinoJson 7.4.3, con `arduino-cli` 1.5.1 y sin un solo aviso (`--warnings all`).
- **Los timeouts del socket.** Las unidades de `Stream::setTimeout` son milisegundos, y además esa llamada **no hacía nada aquí**: `NetworkClient` declara su propio `int _timeout` y lo oculta por nombre, así que la llamada escribía un miembro sin usar. El timeout de la conexión TCP y el del socket (`SO_RCVTIMEO`) salen del `_timeout` de `NetworkClient`, que arranca en 3000 ms (`WIFI_CLIENT_DEF_CONN_TIMEOUT_MS`) y solo cambia con `setConnectionTimeout(ms)`. El sketch usaba `red.setTimeout(5)`: era inerte, no rompía la conexión, y el valor real era el de 3 s del core. Ahora usa `red.setConnectionTimeout(TIMEOUT_SOCKET_MS)` (5000 ms), que sí acota la conexión TCP, y `mqtt.setSocketTimeout(TIMEOUT_SOCKET_S)` (5 s, en segundos) para el CONNACK. Con eso, el peor caso del intento de conexión son 5 s + 5 s = 10 s, como decía el README.
