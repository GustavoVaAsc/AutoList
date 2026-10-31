# Pase de lista con reconocimiento facial en Edge AI — documento interno del equipo

**Materia:** Fundamentos de Sistemas Embebidos 2027-1
**Profesor:** Eduardo Fragoso Navarro
**Equipo:** Gustavo · Yordi · Arnau
**Plataforma Edge AI:** Orange Pi Zero 3W (Allwinner A733, NPU de 3 TOPS, 4 GB de RAM)

> Documento interno del equipo. La arquitectura, los flujos, los datos y el plan están en [`HIGH-LEVEL-PLAN.md`](HIGH-LEVEL-PLAN.md), que es el entregable. Aquí quedan las decisiones, los pendientes y el detalle de la semana.

## 1. Estado del proyecto

El entregable es `docs/HIGH-LEVEL-PLAN.md`. Este documento no repite la arquitectura: si algo de aquí contradice al entregable, manda el entregable.

| Módulo | Responsable | Estado | Primer hito |
|---|---|---|---|
| Servicio de visión en la NPU | Gustavo | Sin empezar | 4 nov: Armbian y `/dev/vipcore` |
| Firmware de la XIAO | Arnau | Sin empezar | 4 nov: `/capture` y sensores por MQTT |
| Pantalla ILI9341 y unidad systemd | Arnau | Sin empezar | 4 nov: overlays de SPI |
| Docker, Node-RED y Postgres | Yordi | Sin empezar | 4 nov: compose y esquema |
| Frontend de registro | Yordi | Sin empezar | 11 nov: API y esqueleto |
| Paneles de Grafana | Arnau, salvo el primero de energía, que va con la ingesta | Sin empezar | 4 nov el de energía, 18 nov el resto |

Del hito del 28 de octubre está entregado el spec y la exposición (Gustavo), la lista de compras (Arnau) y el repo con `docs/` (Yordi); falta crear el resto de la estructura.

## 2. Registro de decisiones

| Fecha | Decisión | Motivo |
|---|---|---|
| 25 oct | Pase de lista con reconocimiento facial, fusionado con el proyecto Bluetooth previo | Cierra el hueco de suplantación del proyecto anterior y aprovecha la NPU |
| 25 oct | Postgres con pgvector en lugar de MariaDB | Embeddings nativos y consultas de similitud; esquema portable como plan B |
| 25 oct | Servo como puerta simulada; MOSFET con tira LED como etapa de potencia | Sin cerradura real; la iluminación mejora el reconocimiento |
| 25 oct | Servicio de visión en C++ | Los ejemplos de la NPU del A733 están en C++ |
| 25 oct | Sensores en la XIAO ESP32S3; actuadores en la Pico 2 W (reemplazada el 28 oct) | Cumple "la ESP32 lee el sensor" con el hardware disponible |
| 25 oct | Se descarta la app móvil; el registro de alumnos se hace desde un frontend web (Vite + React) servido por Node-RED | Menos alcance; el registro necesita tablas, subir un CSV y retroalimentación visual que en la ILI9341 serían incómodas; cubre la opción "HTML" de la Fig. 1 |
| 25 oct | Captura del registro siempre con la XIAO y embedding en la NPU | Mismo pipeline que el pase de lista; registrar con otra cámara baja la precisión |
| 25 oct | Comparación en vivo local en la Orange Pi; Postgres como fuente de verdad | Latencia y funcionamiento aunque la PC esté apagada |
| 28 oct | Un solo microcontrolador: la XIAO ESP32S3 Sense lee los sensores y maneja los actuadores; se retira la Pico 2 W | Un solo firmware y un nodo menos en la red; los 9 pines necesarios caben en la XIAO |
| 28 oct | Etapa de potencia: tira LED de 12 V conmutada por un MOSFET de nivel lógico | Se alimenta directo de la fuente de 12 V del sistema, sin otro regulador |
| 30 oct | `docs/HIGH-LEVEL-PLAN.md` es el entregable y `docs/SPEC.md` queda como documento interno | Una sola copia de la arquitectura: los dos documentos repetían casi todo el contenido y ya se habían desincronizado |
| 30 oct | Las tareas se planifican por dependencias para poder paralelizar los tres carriles | El profesor pidió en la presentación del 28 oct paralelizar las tareas y mostrar avances cada miércoles |

## 3. Pendientes inmediatos

- [ ] Confirmar con el profesor que se usa Postgres en lugar de MariaDB (se le preguntó el 28 oct y sigue sin confirmar)
- [ ] Verificar la NPU en la Orange Pi (`ls /dev/vipcore` y un ejemplo del ai-sdk)
- [x] Revisar con el profesor la etapa de potencia propuesta: quedó decidida la tira LED de 12 V con MOSFET
- [ ] Comprar: tira LED de 12 V, MOSFET, servo, INA219, PIR, buck de 5 V, fuente de 12 V
- [ ] Decidir el modelo de reconocimiento y su dimensión (128 con MobileFaceNet, 512 con ArcFace): bloquea el esquema de Postgres
- [ ] Redactar `docs/consentimiento.md`
- [ ] Crear la estructura del repo con la sección 11 del entregable (`/firmware`, `/orangepi`, `/nodered`, `/pc`, `/web`, `/herramientas`); por ahora solo existe `docs/`
- [ ] Acordar el formato del CSV de la lista maestra (`no_cuenta,nombre,grupo`)

## 4. Semana del 4 de noviembre

Hito: primer avance. Tres carriles en paralelo, con el orden interno de cada uno y lo que espera a otro.

### 4.1 Gustavo — visión

| Orden | Tarea | Arranca | Entregable del miércoles |
|---|---|---|---|
| 1 | Toolchain Pegasus en la PC y conversión de un modelo de prueba | Ya | El modelo convertido en `.nb` |
| 2 | Armbian en la Orange Pi y verificación de `/dev/vipcore` | Ya, en el turno de la Orange Pi | El nodo `/dev/vipcore` verificado |
| 3 | Ejemplo de detección en la NPU | Al cerrar 2 | Una captura con un rostro detectado |

La dimensión del embedding (128 con MobileFaceNet, 512 con ArcFace) la decide Gustavo antes del lunes: Yordi la necesita para fijar el esquema de Postgres.

### 4.2 Arnau — campo, pantalla y Grafana

| Orden | Tarea | Arranca | Entregable del miércoles |
|---|---|---|---|
| 1 | Comprar INA219, PIR, servo, LEDs RGB, MOSFET, tira LED de 12 V, buck de 5 V y fuente de 12 V | Ya | Pedido hecho |
| 2 | Esqueleto del firmware, PubSubClient y `/capture` con la XIAO en la mesa | Ya | El endpoint responde una foto |
| 3 | Pruebas unitarias del firmware, con los sensores simulados | Al cerrar 2 | Las pruebas de cámara, PIR, INA219, servo, MOSFET y LEDs en verde |
| 4 | Publicación de `aula/camara/pir`, `aula/camara/energia` y `aula/camara/estado` por MQTT | Al cerrar 2 | Los tres tópicos publishes |
| 5 | Suscripción a `aula/actuador/cmd`, con servo y LEDs por MQTT | Al cerrar 2 y con el servo comprado | El servo abre y cambia el LED |
| 6 | Overlays de SPI y unidad systemd de la pantalla | Ya, en el turno de la Orange Pi | El resultado de `ls /dev/spidev*` y la unidad instalada |

Las tareas 4 y 5 dependen de la compra: si el material no llega, el avance del miércoles se limita a las tareas 2 y 3 y a los overlays de SPI.

### 4.3 Yordi — borde, datos y frontend

| Orden | Tarea | Arranca | Entregable del miércoles |
|---|---|---|---|
| 1 | `docker-compose.yml` de la Orange Pi y de la PC | Ya | Los dos compose levantan |
| 2 | Esquema de Postgres con pgvector | Al fijar Gustavo la dimensión del embedding | `schema.sql` aplicado |
| 3 | Ingesta MQTT → InfluxDB y primer panel de energía | Al cerrar 1 | El panel con datos reales o simulados |
| 4 | Imagen de Node-RED con `openssh-client` y la clave restringida | Ya | La imagen construida con el cliente SSH |
| 5 | Configurar Node-RED con los flujos de ingesta MQTT → InfluxDB | Al cerrar 1 | Node-RED Publicando energía a InfluxDB |

Los flujos de pase de lista y de sesiones, la API de alumnos y el esqueleto del frontend son del hito del 11 de noviembre, según la tabla de hitos del entregable.

### 4.4 Turnos de los equipos físicos

| Equipo | Lunes | Martes | Miércoles (demostraciones) |
|---|---|---|---|
| Orange Pi Zero 3W | Gustavo: Armbian y `/dev/vipcore` | Arnau: overlays de SPI y unidad systemd | Gustavo: detección en la NPU; Arnau: el servicio systemd; Yordi: el compose |
| XIAO ESP32S3 Sense | Arnau | Arnau | Arnau |

El turno se acuerda por hito, no por día de la semana, y el miércoles por la mañana es la franja de demostraciones: la Orange Pi se enciende y cada persona muestra lo que trae. Los turnos se confirman el lunes por la mañana. Arnau necesita la XIAO casi toda la semana; si el material no llega, la prueba de servo se reprograma para la semana siguiente.

## 5. Notas técnicas

Verificaciones hechas contra el código de Espressif, no de memoria. Sirven para no perder tiempo en la semana 1.

### 5.1 La cámara no usa un timer LEDC en el ESP32-S3

En el driver oficial `esp32-camera`, el reloj XCLK de la cámara lo genera el periférico LCD_CAM del ESP32-S3 y no un canal de LEDC: el propio driver lo anula con `#if CONFIG_IDF_TARGET_ESP32S3`. Los campos `ledc_timer` y `ledc_channel` de la configuración de la cámara no tienen efecto en esta placa.

Consecuencia: el conflicto que había que evitar no es entre la cámara y los actuadores, sino entre el servo, el buzzer y los tres LEDs RGB. El ESP32-S3 tiene 8 canales LEDC repartidos en 4 timers, y todos los timers comparten la misma fuente de reloj, así que las frecuencias se eligen con el divisor. Un reparto que cabe: servo en el timer 0 a 50 Hz, buzzer en el timer 1, y los tres LEDs RGB en los timers 2 y 3 a la misma frecuencia, dos canales por timer.

- Ojo con la biblioteca del servo: `ESP32Servo` define `MAX_SERVOS` 16 y su tabla de canales (2 grupos de 4 timers) está escrita para el ESP32 clásico. En el S3 solo hay 8 canales LEDC, así que el canal del servo tiene que caer entre 0 y 7.

### 5.2 Los pines de la cámara

En la placa Sense, el DVP de la cámara ocupa GPIO10 a GPIO18, GPIO38 a GPIO40, GPIO47 y GPIO48; el micrófono PDM usa GPIO41 y GPIO42. El XCLK es GPIO10. Ninguno de los pines propuestos, D0 a D5 y D8 a D10, choca con la cámara.

D8, D9 y D10, que son GPIO7, GPIO8 y GPIO9, son el bus SPI de la ranura microSD de la placa Sense. Como el proyecto no usa la microSD, quedan libres para los LEDs RGB.

### 5.3 La SCCB de la cámara y el INA219

La SCCB de la cámara es un bus I2C sobre GPIO40 (SDA) y GPIO39 (SCL), y el driver la instala por defecto en el puerto I2C0. El INA219 en D4 y D5 usa GPIO5 y GPIO6, que también es I2C0. Antes de leer la energía hay que separar los dos, y lo más simple es poner el INA219 en `Wire1` (I2C1) con los pines explícitos: `Wire1.begin(5, 6)`. La otra vía es que la cámara use un puerto ya inicializado, pero el driver solo la respeta si además se dejan `pin_sccb_sda` y `pin_sccb_scl` a -1 y se pasa el número de puerto en `sccb_i2c_port`. Comprobar en la semana 1 que la cámara sigue detectando el sensor con el INA219 conectado.

### 5.4 D2 es un pin de strapping

GPIO3, que es D2, es pin de strapping del ESP32-S3: en el arranque elige el origen de las señales JTAG y por defecto está flotante. El pull-down de la compuerta del MOSFET lo fija a 0 durante el arranque, así que hay que verificar en la semana 1 que el USB Serial/JTAG sigue funcionando: es la vía de consola y de programación de la placa.
