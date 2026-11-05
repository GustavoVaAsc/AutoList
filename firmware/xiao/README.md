# Firmware de la XIAO ESP32S3 Sense

Primera versión: controla la **tira LED de 12 V** por MQTT (campo `luz` de `aula/actuador/cmd`). Todo lo demás del firmware de la XIAO (cámara y `/capture`, PIR, INA219, servo, LEDs RGB indicadores y buzzer) sigue sin escribirse y es del carril de Gustavo.

> **Estado: sin compilar y sin flashear.** Esta versión se escribió en la Orange Pi, que no tiene `arduino-cli` ni PlatformIO. Solo se probó en el host la lógica de decisión (`logica_luz`, sección [Pruebas de host](#pruebas-de-host)). Lo demás está en [Sin verificar](#sin-verificar).

## Contenido

- [Carriles](#carriles)
- [Archivos](#archivos)
- [Qué hace](#qué-hace)
- [Cableado de la etapa de potencia](#cableado-de-la-etapa-de-potencia)
- [Bibliotecas](#bibliotecas)
- [Configurar secrets.h](#configurar-secretsh)
- [Compilar y flashear (Gustavo)](#compilar-y-flashear-gustavo)
- [Probar desde Node-RED](#probar-desde-node-red)
- [Integrar el resto del firmware](#integrar-el-resto-del-firmware)
- [Pruebas de host](#pruebas-de-host)
- [Sin verificar](#sin-verificar)

## Carriles

- **Yordi:** firmware de la tira LED (este código) y su etapa de potencia (el cableado de abajo), con el acuerdo de Gustavo.
- **Gustavo:** el resto del firmware de la XIAO, y quien flashea la placa y tiene la tira, el MOSFET y el adaptador. La XIAO es un equipo físico único (plan, sección 12.4).

## Archivos

| Archivo | Para qué |
|---|---|
| `xiao.ino` | Sketch de Arduino: Wi-Fi, MQTT, pin de la tira y publicación del estado. La carpeta se llama `xiao` para que el IDE lo acepte. |
| `logica_luz.h`, `logica_luz.cpp` | Decisiones en C++ puro, sin Arduino: descarte de retenidos y vacíos, interpretación de `luz`, estado de la luz y payload de `aula/actuador/estado`. |
| `pruebas/test_logica_luz.cpp` | Pruebas de host de `logica_luz` (`g++`, sin dependencias). El IDE de Arduino no compila esta carpeta. |
| `secrets.h.example` | Plantilla de `secrets.h` (ignorado por git). |

## Qué hace

- **Se conecta** al Wi-Fi y al broker con el usuario `xiao` (client id `xiao`; el broker usa el nombre de usuario como client id, así que **no hay que conectar otro cliente con el usuario `xiao`** mientras la placa esté encendida: se expulsarían entre sí).
- **Se suscribe** a `aula/actuador/cmd` (QoS 1, sesión limpia). Aplica solo `luz`: el entero `0` apaga y el `1` enciende la tira en D2 (GPIO3). Cualquier otro valor (`2`, `-1`, `true`, `"1"`, `1.5`), un JSON que no sea un objeto o un mensaje sin `luz` no cambia nada. `servo`, `led` y `buzzer` se ignoran por ahora.
- **Descarta** los mensajes con payload vacío y los que pueden ser retenidos (contrato, sección 1). Ver [Desviaciones del contrato](#desviaciones-del-contrato).
- **Publica** `aula/actuador/estado` retenido, `{"luz":0}` o `{"luz":1}`, cada vez que la luz cambia, al (re)conectar y en respuesta a **toda** orden válida de `luz`, aunque no cambie nada. Si una publicación falla, queda pendiente y `loop()` la reintenta cada segundo mientras haya enlace.
- **Will y salud mínima:** registra el LWT `{"en_linea":false}` (retenido, QoS 1) en `aula/camara/estado` y publica `{"en_linea":true}` al conectar, como propone el contrato (sección 3). El latido de cada 30 s con `rssi`, `heap` y `temp` queda para el firmware de Gustavo.
- **Arranque seguro:** `setup()` pone D2 en LOW antes que cualquier otra cosa, y el pull-down de la compuerta mantiene la tira apagada mientras la placa arranca.
- **Apagado automático (decisión de Yordi):** la tira se apaga sola a los **360 s (6 min) del último `luz:1`** (`kApagadoAutomaticoMs` en `logica_luz.h`), sin bloquear (`millis()` con resta sin signo, correcta con el desbordamiento). Cada `luz:1`, aunque la luz ya esté encendida, reinicia el plazo, y al apagarse publica el estado. **Por qué 360 s:** el servicio de visión publica `luz:1` una sola vez al iniciar el registro de 5 poses y `luz:0` al terminar, y un registro puede durar hasta unos 5 min (el vencimiento de Node-RED); 360 s lo cubren con margen. **Quien necesite la luz más tiempo debe reenviar `luz:1` antes de 360 s.** Si Gustavo agrega a la visión un reenvío periódico de `luz:1`, el plazo se puede bajar (`kApagadoAutomaticoMs`). Hay que avisarle por la orquestación luz–captura.
- **Apagado de seguridad (decisión):** si se pierde el Wi-Fi o la conexión con el broker, la luz se **apaga**. Sin enlace nadie puede ordenar apagarla. Al reconectar publica el estado real (`luz: 0`). El estado retenido del broker puede decir `luz: 1` mientras la placa está desconectada; para saber si es confiable, mirar `aula/camara/estado`. Por eso quien publique `luz:1` no debe asumir que se aplicó.
- **Reconexión sin bloqueo indefinido:** cada vuelta de `loop()` hace como mucho un intento. Wi-Fi: reintento automático del controlador y, tras 30 s sin enlace, reinicio de la conexión (con `WiFi.setSleep(false)`). MQTT: un intento cada 5 s. **Un intento bloquea `loop()`**: la conexión TCP (hasta 5 s), la espera del CONNACK (otros 5 s) y, si `MQTT_HOST` es un nombre y no una IP, la resolución DNS, que puede tardar más. Con una IP son unos 10 s en el peor caso, más 1.5 s de la ventana de retenidos si conecta. **Pon una IP en `MQTT_HOST`.** Con la luz apagada por falta de enlace esto no importa; sí importará cuando entren el servo, el PIR y el INA219 (ver [Integrar](#integrar-el-resto-del-firmware)).

### Desviaciones del contrato

Hay tres, todas por límites de PubSubClient 2.8, y ninguna cambia los tópicos ni la ACL (se anotan en el contrato v0.8):

1. **Retain.** El callback de PubSubClient no expone la bandera retain. Un retenido solo se distingue en la entrega inicial que hace el broker justo después de suscribirse (en MQTT 3.1.1 uno publicado en vivo llega sin la bandera: contrato, sección 1). El firmware **descarta todo lo que llegue en los 1500 ms siguientes a la suscripción** (`VENTANA_RETENIDOS_MS`), con el ahorro de energía del Wi-Fi desactivado (`WiFi.setSleep(false)`) para que el retenido no se retrase más allá de la ventana.
   - **Es una heurística, no una garantía:** un retenido que tarde más que la ventana (red lenta, broker cargado) se aplicaría como una orden en vivo.
   - **Costo:** una orden en vivo que llegue en esos 1.5 s se pierde. Ocurre solo justo después de (re)conectar, y con la luz apagada por el apagado de seguridad; el emisor no debe asumir que `luz:1` se aplicó (contrato v0.8).
   - **Cuándo pasar a esp-mqtt** (el cliente MQTT de Espressif, que sí entrega la bandera retain y publica con QoS 1): si Gustavo mide que la ventana no alcanza, o cuando haga falta QoS 1 al publicar o TLS. Hoy se queda PubSubClient, por decisión de Yordi y por el plan (sección 6.1).
2. **QoS del estado.** El contrato pide QoS 1 para `aula/actuador/estado` y `aula/camara/estado`; PubSubClient solo publica con QoS 0. Ambos son retenidos, así que el último valor queda en el broker, pero un paquete perdido no se reintenta a nivel MQTT (el firmware sí reintenta si la escritura al socket falla). El LWT sí se registra con QoS 1.
3. **Estado parcial.** El contrato da como ejemplo `{"servo": "cerrado", "led": "azul", "luz": 0}`. Este firmware no controla el servo ni los LEDs, así que no inventa su estado: publica solo `{"luz": N}`. Hoy nadie consume ese tópico en el repo, y el contrato manda ignorar los campos desconocidos y no declara obligatorios los de ese estado. Cuando entren el servo y los LEDs, `construirEstado()` debe publicar los tres campos juntos: el estado va retenido y cada publicación reemplaza a la anterior.

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

Instalar desde el Gestor de bibliotecas del IDE de Arduino. Las versiones son las que el código asume; no se compiló con ninguna:

| Componente | Versión | Nota |
|---|---|---|
| Paquete de placas **esp32** de Espressif (core Arduino de ESP32) | 3.x | Placa **XIAO_ESP32S3**. Activar **USB CDC On Boot** para que `Serial` salga por el USB. |
| **PubSubClient** (Nick O'Leary) | 2.8 | Versión que el código asume (`setSocketTimeout` y `connect` con LWT y sesión limpia). Búfer de 256 bytes por defecto: basta para estos mensajes. |
| **ArduinoJson** (Benoît Blanchon) | 7.x | Para leer `luz` del JSON; el sketch usa `JsonDocument`, que es de la serie 7. Se eligió sobre un extractor manual porque un texto con `"luz":1` dentro de otro campo no debe encender la tira. |

`WiFi.h` viene con el core de ESP32.

## Configurar secrets.h

1. Copiar `secrets.h.example` a `secrets.h` en esta misma carpeta (junto a `xiao.ino`).
2. Rellenar el SSID y la clave del Wi-Fi de 2.4 GHz, el host o la IP de la Orange Pi y la clave del usuario `xiao`. Esa clave es la `MQTT_XIAO_PASSWORD` de `orangepi/.env`: pídesela a Yordi; no la copies ni la pegues en ningún documento.
3. `secrets.h` lo ignora git (`.gitignore`). Comprobar con `git status` que no aparece antes de hacer un commit. No lo leas, no lo imprimas y no lo cites: es una regla del repo, igual que con `orangepi/.env`.
4. Las claves quedan en la memoria flash de la XIAO y se pueden extraer con acceso físico. **Si una XIAO flasheada se extravía, hay que rotar la clave del usuario `xiao` (en `orangepi/.env`, lo hace Yordi) y la del Wi-Fi.**

## Compilar y flashear (Gustavo)

Desde su laptop, con el IDE de Arduino:

1. Instalar el paquete de placas y las bibliotecas de la sección anterior.
2. Abrir `firmware/xiao/xiao.ino` (la carpeta tiene el mismo nombre que el sketch), crear `secrets.h` y elegir la placa **XIAO_ESP32S3** con **USB CDC On Boot** activado.
3. Compilar y cargar con la tira **aún sin conectar** a los 12 V. Abrir el monitor serie a 115200: debe verse `XIAO: tira LED por MQTT` y luego `MQTT: conectado`.
4. Conectar la etapa de potencia como en el cableado y seguir con las pruebas de abajo. **No conectar el adaptador a la tira hasta resolver los cuatro datos pendientes** (MOSFET, largo y densidad de la tira, amperaje del adaptador y rotulado de los pines): si el amperaje del adaptador no cubre 1.25 veces el consumo de la tira, usar un tramo más corto.

Si la compilación falla por una API distinta a la esperada (lo más probable: `JsonDocument` si se instaló ArduinoJson 6, o `setSocketTimeout` en otra versión de PubSubClient, o `setTimeout` de `WiFiClient`), avisar a Yordi con el mensaje del compilador.

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

Mosquitto descarta en silencio lo que su ACL deniega: si una orden no hace nada, mirar el log del broker antes de culpar al firmware.

## Integrar el resto del firmware

Gustavo agrega cámara, PIR, INA219, servo, LEDs y buzzer **en este mismo sketch** (`xiao.ino` y archivos nuevos en la carpeta), porque la placa corre un solo programa y comparte el Wi-Fi y la conexión MQTT. Puntos de unión:

- **Más campos de `cmd`:** en `alRecibir()`, junto al bloque de `luz`. El procesamiento no se corta con un `return` para que los bloques de cada actuador convivan.
- **Estado:** `construirEstado()` (en `logica_luz`) hoy solo escribe `luz`. Hay que ampliarlo para que publique `servo`, `led` y `luz` juntos, y llamarlo cada vez que cambie cualquiera de los tres (ver [Desviaciones](#desviaciones-del-contrato), punto 3).
- **Salud:** el `{"en_linea":true}` de `conectarMqtt()` se reemplaza por el latido de cada 30 s de `aula/camara/estado` (contrato, sección 3), con el mismo LWT.
- **Otros tópicos:** hay que suscribirse en `conectarMqtt()`; `aula/sesion/estado` también es retenido y su ventana de descarte (1500 ms) es la misma. Las publicaciones nuevas necesitan el permiso en la ACL, que ya lo tiene para `aula/camara/pir`, `aula/camara/energia` y `aula/camara/estado`.
- **Bloqueo de `loop()`:** los reintentos de conexión MQTT (`conectarMqtt()`) bloquean `loop()` hasta unos 10 s con IP (más con un nombre, por el DNS), más 1.5 s de ventana de retenidos. Es aceptable mientras solo hay una tira, pero no con servo, PIR e INA219 en el mismo `loop()`: cuando entren, mover la conexión a otra tarea de FreeRTOS o usar un connect no bloqueante.
- **Apagado automático:** `loop()` revisa el plazo de 360 s de la luz; cualquier trabajo bloqueante nuevo lo retrasa. La visión y la orquestación luz–captura deben reenviar `luz:1` antes de 360 s (contrato v0.8).
- **Búfer:** PubSubClient tiene 256 bytes por defecto; si algo que publique o reciba lo supera, `mqtt.setBufferSize()`.
- **LEDC:** el sketch no usa LEDC: la tira es todo o nada con `digitalWrite`. No consume uno de los 4 timers que se reparten el servo, el buzzer y los LEDs RGB (plan, sección 13).
- **Pruebas unitarias:** las de cámara, INA219 y servo que prevé el plan (sección 11) son de Gustavo; las de `logica_luz` viven en `pruebas/` de esta carpeta.

## Pruebas de host

Prueban `logica_luz` sin Arduino ni dependencias, solo con `g++`. Desde `firmware/xiao/`:

```bash
d=$(mktemp -d)
g++ -std=c++17 -Wall -Wextra -Werror -I. logica_luz.cpp pruebas/test_logica_luz.cpp -o "$d/test_logica_luz"
"$d/test_logica_luz"   # termina con "N verificaciones, 0 fallos" y código 0
```

Cubren: descarte de retenidos y de payloads vacíos, `luz` ausente, no entera y fuera de 0 y 1, cambio de estado solo cuando cambia (también para el apagado de seguridad), el apagado automático (vence justo en el límite, se renueva con otro `luz:1`, no vence si se apaga antes y funciona con el desbordamiento de `millis()`) y el payload de `aula/actuador/estado`, incluido cuando no cabe en el búfer. **No** cubren `xiao.ino`: el parseo del JSON, Wi-Fi, MQTT y el pin necesitan la placa.

## Sin verificar

Hasta que Gustavo flashee la placa, **nada de lo siguiente se ha ejecutado**:

- Que `xiao.ino` compila con el core ESP32 3.x, PubSubClient 2.8 y ArduinoJson 7. Solo se comprobó la sintaxis contra declaraciones propias, que reflejan lo que se recuerda de esas APIs y no son las bibliotecas reales.
- Que el callback de PubSubClient 2.8 no expone la bandera retain (desviación 1), y que la ventana de 1500 ms (con `WiFi.setSleep(false)`) basta para la entrega inicial del broker.
- Que `ArduinoJson` rechaza como se espera `true`, `"1"` y `1.5` en `is<int>()`.
- La conexión, el LWT, la sesión limpia y la suscripción con QoS 1 contra el Mosquitto real y su ACL.
- La reconexión del Wi-Fi y del broker, el tiempo real que bloquea un intento y el apagado de seguridad.
- El apagado automático de 360 s con la placa (la lógica está probada en host; el cableado a `loop()` no).
- El reintento del estado pendiente tras una publicación fallida.
- Que `WiFiClient::setTimeout` recibe segundos en el core 3.x.
- Que D2 (GPIO3) arranca bajo y que el USB Serial/JTAG sigue funcionando con el pull-down puesto.
- El cableado completo con el MOSFET real, el adaptador y la tira; en particular que el IRLZ44N conmuta bien con 3.3 V.
- Los datos de la tira, el adaptador y el MOSFET de la lista de pendientes.
