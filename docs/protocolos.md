# Contrato de telemetría

Este documento es el contrato que comparten todos los módulos del sistema: el
firmware de la XIAO, el servicio de visión, los flujos de Node-RED, la pantalla
y el frontend web. La idea es que el mismo error de protocolo no rompa a la vez
el pase de lista, la ingesta de energía y los flujos de Node-RED.

La fuente de verdad de los payloads es la [sección 7.1 del plan](HIGH-LEVEL-PLAN.md).
La versión legible por máquina de la tabla de tópicos está en
[`mqtt-topics.json`](mqtt-topics.json) y es la que consumen el validador
([`herramientas/validar_protocolo.py`](../herramientas/validar_protocolo.py)) y,
a partir de ahora, el firmware, los flujos y el servicio de visión.

---

## 1. Tópicos MQTT

Todos los tópicos viven bajo el prefijo `aula/` (`MQTT_BASE_TOPIC` en el `.env`).

### 1.1 `aula/camara/pir`

| | |
|---|---|
| **Dirección** | Publica la XIAO ESP32S3 Sense. Se suscriben el servicio de visión y Node-RED. |
| **Payload** | `{"mov":1}` |
| **Campos** | `mov` (`int`, requerido): 1 indica movimiento detectado por el PIR. |
| **Ejemplo** | `{"mov":1}` |
| **QoS** | 1 |
| **Retained** | No |

Semántica: es el disparo de la captura. El servicio de visión lo escucha, enciende
la tira LED y pide una ráfaga de fotos. Mientras hay un registro de rostro en
curso, el PIR se ignora.

### 1.2 `aula/camara/energia`

| | |
|---|---|
| **Dirección** | Publica la XIAO ESP32S3 Sense. Se suscribe Node-RED, que lo lleva a InfluxDB. |
| **Payload** | `{"v":12.1,"i":0.85,"p":10.3}` |
| **Campos** | `v` (`float`, requerido): voltios. `i` (`float`, requerido): amperios. `p` (`float`, requerido): vatios. |
| **Ejemplo** | `{"v":12.1,"i":0.85,"p":10.3}` |
| **QoS** | 0 |
| **Retained** | No |

Semántica: el INA219 está en la entrada de 12 V, así que estas cifras son el
consumo total del sistema sin la PC, que es lo que necesita la estimación de
batería del experimento E5.

### 1.3 `aula/camara/estado`

| | |
|---|---|
| **Dirección** | Publica la XIAO ESP32S3 Sense. Se suscribe Node-RED. |
| **Payload** | `{"rssi":-58,"heap":120000,"temp":48}` |
| **Campos** | `rssi` (`int`, requerido): dBm, negativo. `heap` (`int`, requerido): bytes de heap libre. `temp` (`int`, requerido): °C del SoC. |
| **Ejemplo** | `{"rssi":-58,"heap":120000,"temp":48}` |
| **QoS** | 0 |
| **Retained** | No |

Semántica: diagnóstico del nodo de campo. Sirve para ver si el RSSI o la
temperatura se disparan cuando la XIAO se cuelga o se calienta.

### 1.4 `aula/actuador/cmd`

| | |
|---|---|
| **Dirección** | Publican Node-RED y el servicio de visión. Se suscribe la XIAO ESP32S3 Sense. |
| **Payload** | `{"servo":"abrir","led":"verde","buzzer":1,"luz":0}` |
| **Campos** | `servo` (`string`, opcional): `abrir` o `cerrar`. `led` (`string`, opcional): `verde`, `rojo`, `azul` o `ambar`. `buzzer` (`int`, opcional): 0 o 1. `luz` (`int`, opcional): 0 o 1. |
| **Ejemplo** | `{"servo":"abrir","led":"verde","buzzer":1,"luz":0}` |
| **QoS** | 1 |
| **Retained** | No |

Semántica: es el bus de comandos hacia los actuadores. Cada campo es opcional
por separado y el payload lleva **al menos uno**: el servicio de visión encarece
solo la luz (`{"luz":1}`) y Node-RED abre el servo y pone el LED verde en una
sola orden. El servo gira 90° (abierto), espera 3 s y regresa; los patrones de
LED son los del proyecto previo (azul: sesión activa, verde: registrado, rojo:
error, ámbar: zona gris).

El frontend no publica aquí: no tiene permiso de escritura (sección 3). Su
control manual del servo, los LEDs y el buzzer llega a la XIAO a través de
Node-RED, que es el único rol con permiso de escritura amplio: el navegador
llama a `POST /api/actuadores` (sección 5) y Node-RED publica aquí.

**Retained no:** un comando retenido se reentregaría al reconectar la XIAO y
volvería a abrir el servo sin motivo.

### 1.5 `aula/actuador/estado`

| | |
|---|---|
| **Dirección** | Publica la XIAO ESP32S3 Sense. Se suscriben Node-RED y el frontend web. |
| **Payload** | `{"servo":"cerrado","led":"azul","luz":0}` |
| **Campos** | `servo` (`string`, requerido): `abierto` o `cerrado`. `led` (`string`, requerido): `verde`, `rojo`, `azul` o `ambar`. `luz` (`int`, requerido): 0 o 1. |
| **Ejemplo** | `{"servo":"cerrado","led":"azul","luz":0}` |
| **QoS** | 1 |
| **Retained** | Sí |

Semántica: el estado real de los actuadores. Va retenido para que un suscriptor
tardío sepa cómo quedó el servo.

**Decisión de diseño de este contrato:** Node-RED reacciona a este estado
publicando un comando en `aula/actuador/cmd`. El plan no lo fija; es una
decisión de esta capa, y por eso se marca aquí como tal: es lo que permite a
Node-RED rectificar el estado del servo y de los LEDs cuando le hace falta.

### 1.6 `aula/vision/resultado`

| | |
|---|---|
| **Dirección** | Publica el servicio de visión (NPU). Se suscribe Node-RED. |
| **Payload** | `{"cuenta":"320068234","conf":0.78,"lat_ms":{"cap":180,"det":12,"emb":6,"total":240}}` |
| **Campos** | `cuenta` (`string`, requerido): número de cuenta; vacío si no se reconoce a nadie. `conf` (`float`, requerido): similitud coseno, entre 0 y 1. `lat_ms` (`object`, requerido): ver la tabla de latencias de la sección 4. `motivo` (`string`, opcional): `imagen_insuficiente`, `desconocido` o `duplicado`. |
| **Ejemplo** | `{"cuenta":"320068234","conf":0.78,"lat_ms":{"cap":180,"det":12,"emb":6,"total":240}}` |
| **QoS** | 1 |
| **Retained** | No |

Semántica: una identidad reconocida (o no) con sus latencias. Node-RED aplica
encima las reglas de sesión: que el alumno esté en la lista del grupo, que no
esté ya registrado y si es presente o retardo según la tolerancia.

`motivo` aparece cuando el intento no produce un registro válido, por ejemplo
cuando el filtro de calidad descarta todas las fotos.

### 1.7 `aula/vision/cmd`

| | |
|---|---|
| **Dirección** | Publica Node-RED. Se suscribe el servicio de visión (NPU). |
| **Payload** | `{"accion":"enrolar","cuenta":"320068234"}` |
| **Campos** | `accion` (`string`, requerido): `enrolar`, `cancelar` o `probar`. `cuenta` (`string`, opcional): obligatoria en `enrolar` y `probar`, opcional en `cancelar`. |
| **Ejemplo** | `{"accion":"enrolar","cuenta":"320068234"}` |
| **QoS** | 1 |
| **Retained** | No |

Semántica: órdenes al servicio de visión. El frontend no publica aquí: llama a
la API HTTP de Node-RED y es Node-RED quien publica la orden.

### 1.8 `aula/vision/enrolamiento`

| | |
|---|---|
| **Dirección** | Publica el servicio de visión (NPU). Se suscriben el frontend web y la pantalla ILI9341. |
| **Payload** | `{"cuenta":"320068234","paso":3,"total":5,"pose":"izquierda","estado":"rechazada","motivo":"borrosa","miniatura":"<base64>"}` |
| **Campos** | `cuenta` (`string`, requerido). `paso` (`int`, requerido): pose en curso, de 1 a 5. `total` (`int`, requerido): 5. `pose` (`string`, requerido): `frente`, `izquierda`, `derecha` o `arriba`. `estado` (`string`, requerido). `motivo` (`string`, opcional): por qué se rechaza la foto. `miniatura` (`string`, opcional): JPEG en base64. |
| **Ejemplo** | `{"cuenta":"320068234","paso":3,"total":5,"pose":"izquierda","estado":"rechazada","motivo":"borrosa","miniatura":"<base64>"}` |
| **QoS** | 1 |
| **Retained** | No |

Semántica: el progreso del registro de rostro. Son 5 poses: frente, un poco a la
izquierda, un poco a la derecha, un poco hacia arriba y de frente otra vez. La
pantalla usa `paso` y `pose` para guiar al alumno, que no ve la laptop del
profesor; el frontend recibe el payload completo, incluida la miniatura.

Cada foto se valida: exactamente un rostro, cara de al menos ~100 px de ancho,
nitidez mínima y giro de cabeza moderado. Si se rechaza, el frontend muestra el
motivo y se repite esa pose.

**Retained no:** el progreso es una secuencia, no un estado; retenerlo
entregaría poses viejas a un suscriptor nuevo.

### 1.9 `aula/vision/embedding`

| | |
|---|---|
| **Dirección** | Publica el servicio de visión (NPU). Se suscribe **solo** Node-RED. |
| **Payload** | `{"cuenta":"320068234","embedding":[...]}` |
| **Campos** | `cuenta` (`string`, requerido). `embedding` (`array` de `float`, requerido): vector de identidad facial. |
| **Ejemplo** | `{"cuenta":"320068234","embedding":[0.0123,-0.0456,0.0789,-0.0321]}` (ejemplo **abreviado** a 4 valores) |
| **QoS** | 1 |
| **Retained** | No |

Semántica: el embedding ya promediado y normalizado de las 5 poses. Su
longitud es `EMBEDDING_DIM` (512 por defecto, y el `vector(512)` del esquema de
Postgres). Node-RED lo guarda con `UPDATE alumnos SET embedding`.

**El ejemplo de arriba está abreviado a 4 valores**: el vector real tiene
`EMBEDDING_DIM` (512) elementos. En [`mqtt-topics.json`](mqtt-topics.json) ese
campo va marcado con `ejemplo_abreviado`, y el validador solo acepta el ejemplo
abreviado porque el propio JSON lo declara.

**Este tópico nunca lo lee el frontend.** Ver la sección 3.

### 1.10 `aula/sesion/cmd`

| | |
|---|---|
| **Dirección** | Publica la pantalla ILI9341. Se suscribe Node-RED. |
| **Payload** | `{"accion":"iniciar","materia":"FSE","grupo":"1","tol_min":10}` |
| **Campos** | `accion` (`string`, requerido): `iniciar` o `terminar`. `materia` (`string`, opcional). `grupo` (`string`, opcional). `tol_min` (`int`, opcional): tolerancia de retardo en minutos. |
| **Ejemplo** | `{"accion":"iniciar","materia":"FSE","grupo":"1","tol_min":10}` |
| **QoS** | 1 |
| **Retained** | No |

Semántica: inicio y fin de la sesión de clase desde la pantalla. En `iniciar`
son necesarios `materia`, `grupo` y `tol_min`; en `terminar` solo `accion`. Si
falta `tol_min`, se usa `TOLERANCIA_MIN` del `.env`.

### 1.11 `aula/sesion/estado`

| | |
|---|---|
| **Dirección** | Publica Node-RED. Se suscriben la XIAO ESP32S3 Sense, la pantalla ILI9341 y el frontend web. |
| **Payload** | `{"activa":true,"id":12}` |
| **Campos** | `activa` (`bool`, requerido). `id` (`int`, opcional): identificador de la sesión en Postgres, que asigna Node-RED; va ausente si no hay sesión activa. |
| **Ejemplo** | `{"activa":true,"id":12}` |
| **QoS** | 1 |
| **Retained** | Sí |

Semántica: el estado de la sesión de clase. Al iniciarla, Node-RED crea el
registro en Postgres y publica aquí; la XIAO enciende el LED azul. Va retenido
para que quien se suscriba después sepa si hay sesión activa.

**El `id` lo asigna Node-RED** y siempre es un entero: no vale `null`, y si
no hay sesión activa el campo va ausente.

**Retained y sesiones:** como el tópico va retenido, un suscriptor que se
reconecte puede recibir el estado de una sesión anterior. Quien se suscriba
tiene que **ignorar el valor retenido si su `id` de sesión no coincide con la
sesión actual**: si no, encendería el LED azul de una sesión ya terminada.

### 1.12 `aula/sistema/metricas`

| | |
|---|---|
| **Dirección** | Publica el agente de métricas del sistema (rol `sistema`): un script que vive en el **host** de la Orange Pi y que se construirá en la Capa 3. Se suscribe Node-RED, que lo lleva a InfluxDB. |
| **Payload** | `{"temp":52.1,"cpu":34,"ram":61}` |
| **Campos** | `temp` (`float`, requerido): °C del SoC. `cpu` (`int`, requerido): % de uso. `ram` (`int`, requerido): % de uso. |
| **Ejemplo** | `{"temp":52.1,"cpu":34,"ram":61}` |
| **QoS** | 0 |
| **Retained** | No |

Semántica: métricas del borde para los paneles de Grafana.

---

## 2. Reglas transversales

### 2.1 Prefijo `aula/`

Todos los tópicos empiezan por `aula/`. En la v1 el prefijo **no es
configurable**: está fijado en la ACL del broker (sección 3) y en
[`mqtt-topics.json`](mqtt-topics.json). `MQTT_BASE_TOPIC` aparece en el `.env`
como documentación del valor, no como una opción: cambiarlo obligaría a
regenerar la ACL y el contrato JSON a la vez.

### 2.2 Reconexión y backoff

Todos los clientes MQTT reconectan solos con backoff exponencial: 1 s, 2 s, 4 s,
8 s… hasta un tope de 60 s, con un jitter aleatorio para que la XIAO y la
Orange Pi no se reintenten en el mismo instante. Tras reconectar, cada cliente
vuelve a suscribirse a sus tópicos y, si lo necesita, pide el estado actual
(`aula/sesion/estado` va retenido, así que llega solo).

El firmware de la XIAO además tiene watchdog: si se cuelga, se reinicia. Como
la XIAO concentra sensores y actuadores, es el punto de fallo crítico del
nodo de campo.

### 2.3 Idempotencia de los comandos

El broker entrega con duplicados: QoS 1 garantiza la entrega, no que se entregue
una sola vez, y una reconexión puede reentregar el último mensaje. **Todo
suscriptor de un tópico de comandos tiene que ser idempotente.**

En concreto, `aula/actuador/cmd` se puede repetir en cualquier momento:

- `servo: "abrir"` repetido abre el servo una sola vez: el temporizado con
  `millis()` no se reinicia por el segundo comando, así que el servo no espera
  3 s desde cada repetición.
- `led` y `luz` repetidos dejan el LED y la tira en el mismo estado.
- `buzzer` repetido suena una vez, no encadenado.

Node-RED es el consumidor de `aula/actuador/estado` y `aula/sesion/estado`, así
que también tiene que tolerar repeticiones de esos mensajes sin registrar dos
veces la misma asistencia: la restricción `UNIQUE (no_cuenta, sesion_id)` de
Postgres es la última red de seguridad.

### 2.4 Reloj y desfase de timestamps

Los payloads de telemetría **no llevan marca de tiempo**: el reloj de cada
módulo es distinto y desincronizado, y meterlo en el payload lo haría
inconsistente. El tiempo lo pone quien escribe en la base de datos:

- Postgres usa `now()` (`TIMESTAMPTZ`), con la hora del servidor de la PC, que
  es la referencia.
- La Orange Pi tiene el RTC DS3231 para tener hora confiable si no hay NTP.
- La XIAO no tiene reloj: sus únicas medidas de tiempo son las latencias
  relativas (`lat_ms`), que no dependen de la hora de pared.

Cuando se necesite saber el orden entre dos eventos de módulos distintos, se usa
la hora de inserción en Postgres, no la hora del emisor. Los desfases entre el
RTC de la Orange Pi y el reloj de la PC se corrigen con NTP en cuanto hay red;
sin internet, el desfase queda documentado como limitación.

### 2.5 `aula/vision/embedding` nunca es legible para el frontend

**El embedding no llega nunca al navegador.** El frontend web se suscribe a
`aula/vision/enrolamiento`, `aula/sesion/estado` y `aula/actuador/estado`, y a
ningún otro tópico del sistema. `aula/vision/embedding` lo lee solo Node-RED,
que es el único que escribe en Postgres.

La regla se apoya en dos controles, y conviene decir con exactitud qué es cada
uno, porque los dos son de **acceso** y ninguno es una convención:

1. **ACL del broker** (sección 3): el usuario del frontend no tiene permiso de
   lectura sobre `aula/vision/embedding`. Solo `nodered` lo lee, porque es el
   único que hace el `UPDATE alumnos SET embedding`.
2. **Realm de la API HTTP** (sección 5): `GET /api/galeria` —el único endpoint
   que devuelve embeddings— **no está en el realm del navegador**. Vive en un
   realm de `httpNodeAuth` distinto, con credenciales de servicio propias del
   servicio de visión, que el navegador no tiene. `httpNodeAuth` protege todos
   los endpoints, pero protegerlos no basta: los que el navegador puede
   llamar tienen que estar en un realm que el navegador no comparta.

El frontend tampoco escribe: no tiene ningún permiso de escritura en el broker
(sección 3), así que no puede publicar un embedding suplantando a otro alumno.
Todo lo que el frontend escribe pasa por la API HTTP de Node-RED.

Las fotos de la captura no se guardan: se descartan en cuanto se calcula el
embedding. Lo que sí circula es la **miniatura en base64** de cada pose,
durante el registro: viaja en claro por MQTT en `aula/vision/enrolamiento` y la
reciben el frontend y la pantalla.

---

## 3. ACL de Mosquitto

El broker Mosquitto de la Orange Pi va con usuario, password y ACL. Hay **un
usuario por rol del sistema** y ninguno es anónimo: el usuario del frontend es
un usuario del broker, no un cliente anónimo, y no comparte credenciales con
nadie.

### 3.1 Configuración de referencia del broker

Para que la ACL signifique algo, el broker tiene que aplicarla. Esta es la
configuración de referencia (la Capa 3 la monta en el `docker-compose.yml` de la
Orange Pi):

```
# mosquitto.conf

# Sin clientes anonimos: sin esta linea el broker acepta escritura anonima y
# el sistema es forjable sin credenciales.
allow_anonymous false

# Credenciales y ACL. En el host viven en mosquitto/ y se montan en
# /mosquitto/config dentro del contenedor.
password_file /mosquitto/config/passwd
acl_file /mosquitto/config/acl

# Sin persistencia: los embeddings y las miniaturas no deben quedar guardados
# en mosquitto.db.
persistence false

# Cota de tamano de mensaje (Mosquitto la aplica a los clientes MQTT v5).
# 1 MiB es el techo de una miniatura JPEG en base64 (unos 750 KiB de JPEG).
max_message_size 1048576
```

Los usuarios del broker se crean con:

```
mosquitto passwd -c mosquitto/passwd <usuario>
```

Con `-c` se crea el fichero; sin `-c` se añade un usuario al que ya existe.
**El fichero `mosquitto/passwd` no se versiona**: son las contraseñas del broker
en claro, y está en el `.gitignore`. El fichero de ACL sí puede versionarse,
porque no lleva contraseñas.

### 3.2 Permisos por rol

Mosquitto deniega por defecto: lo que no aparece en la ACL, no se puede hacer.
Un `topic read aula/#` de más reabre la lectura del embedding, así que la
wildcard solo se concede al rol de Node-RED y por escrito.

| Rol | Publica | Se suscribe a | Lee `aula/vision/embedding` |
|---|---|---|---|
| `frontend` | — | `aula/vision/enrolamiento`, `aula/sesion/estado`, `aula/actuador/estado` | No |
| `xiao` | `aula/camara/pir`, `aula/camara/energia`, `aula/camara/estado`, `aula/actuador/estado` | `aula/actuador/cmd`, `aula/sesion/estado` | No |
| `vision` | `aula/vision/resultado`, `aula/vision/enrolamiento`, `aula/vision/embedding`, `aula/actuador/cmd` | `aula/camara/pir`, `aula/vision/cmd` | No |
| `pantalla` | `aula/sesion/cmd` | `aula/vision/enrolamiento`, `aula/sesion/estado` | No |
| `sistema` | `aula/sistema/metricas` | `aula/sesion/estado` | No |
| `nodered` | `aula/vision/cmd`, `aula/actuador/cmd`, `aula/sesion/estado` | `aula/#` | Sí |

Reglas de la ACL, tal y como van en el fichero de configuración del broker:

```
# ACL de Mosquitto (mosquitto/acl)

# --- Frontend web (navegador del profesor) -------------------------------
# Solo lectura, y de tres topicos. El frontend no publica por MQTT: usa la API
# HTTP de Node-RED para todo lo que escribe y MQTT solo para RECIBIR el
# progreso del registro (mqtt.js sobre WebSocket, puerto 9001). El trigger del
# registro lo publica Node-RED, no el navegador.
#
# El permiso de escritura es lo que hace fiable un "UPDATE alumnos SET
# embedding"; por eso el usuario del frontend no lo tiene, aunque su credencial
# viva en el navegador.
user frontend
topic read aula/vision/enrolamiento
topic read aula/sesion/estado
topic read aula/actuador/estado

# --- XIAO ESP32S3 Sense (nodo de campo) ---------------------------------
user xiao
topic write aula/camara/pir
topic write aula/camara/energia
topic write aula/camara/estado
topic write aula/actuador/estado
topic read aula/actuador/cmd
topic read aula/sesion/estado

# --- Servicio de vision (NPU) --------------------------------------------
# Publica los tres topicos de vision y lee el disparo del PIR y las ordenes de
# enrolar. Tambien escribe en aula/actuador/cmd, con topico exacto y sin
# wildcard: el plan (seccion 5.1) enciende la tira LED desde aqui ({"luz":1})
# para que la cara quede iluminada antes de la captura.
user vision
topic write aula/vision/resultado
topic write aula/vision/enrolamiento
topic write aula/vision/embedding
topic write aula/actuador/cmd
topic read aula/camara/pir
topic read aula/vision/cmd

# --- Pantalla ILI9341 ----------------------------------------------------
# Inicia y termina la sesion y sigue el progreso del registro. No toca los
# actuadores: el control manual del servo, los LEDs y el buzzer pasa por
# Node-RED (seccion 6.5 del plan).
user pantalla
topic write aula/sesion/cmd
topic read aula/vision/enrolamiento
topic read aula/sesion/estado

# --- Agente de metricas del sistema (rol sistema) ------------------------
# Script del HOST de la Orange Pi que publica las metricas del borde (SoC).
# Solo publica esas metricas y lee el estado de sesion, que necesita para
# saber si hay clase en marcha. No lee el embedding ni escribe en ningun otro
# topico.
user sistema
topic write aula/sistema/metricas
topic read aula/sesion/estado

# --- Node-RED (borde) ----------------------------------------------------
# Es el unico que escribe en Postgres y el que consume todo el sistema, asi que
# es el unico con lectura amplia: por eso es el unico que puede leer
# aula/vision/embedding, que es justo el vector que guarda con "UPDATE alumnos
# SET embedding". Ningun otro rol lo necesita.
#
# Escribe en los topicos de orden y de estado, no en los de telemetria de los
# sensores (aula/camara/*) ni en los de vision (aula/vision/enrolamiento,
# aula/vision/embedding), que solo los publica quien los produce.
user nodered
topic write aula/vision/cmd
topic write aula/actuador/cmd
topic write aula/sesion/estado
topic read aula/#
```

Cuatro reglas que se comprueban a ojo sobre esta ACL:

- **El frontend no escribe.** No tiene ni una línea de `topic write`, así que
  no puede publicar `aula/vision/embedding` (que suplantaría a otro alumno),
  ni `aula/vision/resultado` (que falsearía una asistencia), ni
  `aula/actuador/cmd` (que movería el servo).
- **El embedding solo lo escribe `vision` y solo lo lee `nodered`.** Los demás
  roles no tienen permiso de lectura ni de escritura sobre ese tópico.
- **La wildcard es solo de Node-RED.** Con `topic read aula/#` en el rol
  equivocado, la lectura del embedding vuelve a abrirse.
- **Cada tópico tiene un solo publicador, salvo `aula/actuador/cmd`.** Ese lo
  publican Node-RED y el servicio de visión, que lo usa para encender la tira
  LED antes de capturar. El resto son de un solo publicador:
  `aula/sistema/metricas` lo publica el rol `sistema` (el agente de métricas
  del host de la Orange Pi) y nadie más: por eso Node-RED lo lee, pero no lo
  escribe.

---

## 4. Tabla de tiempos y latencias

El campo `lat_ms` de `aula/vision/resultado` es un objeto con cuatro subcampos
enteros, todos en milisegundos:

| Subcampo | Mide | Incluye |
|---|---|---|
| `cap` | **Captura** | La ráfaga de 3 a 5 fotos: la espera de ~300 ms a que la exposición se estabilice tras encender la tira LED, el descarte de los primeros cuadros al despertar la cámara y la descarga HTTP de la imagen por `/capture?res=UXGA`. |
| `det` | **Detección** | La detección del rostro y sus 5 puntos clave en la NPU (SCRFD o RetinaFace). |
| `emb` | **Embedding** | La alineación de la cara a 112×112 y el cálculo del vector de identidad en la NPU (MobileFaceNet o ArcFace). |
| `total` | **Total** | Todo el ciclo del servicio de visión: desde el disparo hasta la publicación del resultado, incluyendo el filtro de calidad de imagen y la comparación por similitud coseno contra la galería local. |

Notas:

- `total` es mayor o igual que `cap + det + emb`; la diferencia es el filtro de
  calidad, la comparación con la galería y la propia publicación.
- Las cuatro cifras son medidas relativas, con el reloj monotónico del propio
  servicio: no dependen de la hora de pared y no se ven afectadas por el
  desfase entre el reloj de la XIAO y el de la Orange Pi.
- La latencia de extremo a extremo del sistema (del PIR al servo, desglosada por
  etapa) es la del experimento E4 y no cabe en `lat_ms`: `lat_ms` solo mide la
  parte de visión.
- El campo `conf` no es un tiempo, pero viaja en el mismo mensaje para que Node-RED
  tenga el resultado y su confianza de un solo golpe.

---

## 5. API HTTP de Node-RED

Todos los endpoints de la API de Node-RED están protegidos con `httpNodeAuth`
(autenticación básica de Node-RED): sin credenciales válidas responden 401 y no
ejecutan nada. El frontend los llama desde el navegador del profesor, en la misma
red.

La API tiene **dos realms**, y no es un detalle: son dos conjuntos de
credenciales distintos. Los siete endpoints que usa el frontend web están en el
realm `frontend`; `GET /api/galeria`, que es el único que devuelve embeddings,
está en el realm `vision` (servicio), que el navegador no tiene.

| Método | Ruta | Query | Cuerpo | Respuesta |
|---|---|---|---|---|
| `GET` | `/api/alumnos` | `grupo` (obligatorio): el grupo a listar | — | Lista de alumnos del grupo con su estado de rostro (pendiente o registrado), su consentimiento y su número de cuenta. |
| `POST` | `/api/alumnos/importar` | — | CSV de la lista maestra con las columnas `no_cuenta,nombre,grupo` (cuerpo `text/csv` o `multipart/form-data`) | Resultado de la importación: cuántos alumnos se insertaron y cuántos quedan pendientes de rostro. Todos se insertan con `embedding = NULL`. |
| `POST` | `/api/enrolamiento` | — | `{"cuenta":"320068234","consentimiento":true}` | Confirmación de que la orden salió; el progreso llega por MQTT a `aula/vision/enrolamiento`. |
| `POST` | `/api/enrolamiento/cancelar` | — | `{"cuenta":"320068234"}` | Confirmación de la cancelación del registro en curso. |
| `POST` | `/api/enrolamiento/probar` | — | `{"cuenta":"320068234"}` | Confirmación del arranque de la prueba; el resultado y la confianza llegan por MQTT. |
| `DELETE` | `/api/alumnos/:cuenta/rostro` | — | — | Confirmación del borrado: el embedding pasa a `NULL` y el consentimiento queda retirado. |
| `POST` | `/api/actuadores` | — | `{"servo":"abrir","led":"verde","buzzer":1}` | Confirmación de que la orden salió hacia `aula/actuador/cmd`. |
| `GET` | `/api/galeria` | — | — | Los embeddings de los alumnos con rostro, para que el servicio de visión sincronice su galería local. **Endpoint de servicio: realm `vision`, no el del navegador.** |

Notas:

- `POST /api/actuadores` es el **control manual del servo, los LEDs y el
  buzzer** desde el frontend (plan, secciones 6.5 y 6.7). Los tres campos del
  cuerpo son opcionales por separado, pero el cuerpo tiene que llevar al
  menos uno. Node-RED lo publica en `aula/actuador/cmd`; el navegador nunca
  publica en MQTT. Está en el realm del navegador.
- El endpoint `/stream` (MJPEG en QVGA) **existe en el firmware de la XIAO,
  pero la v1 no lo usa** (plan, secciones 6.1 y 6.7), así que no está en esta
  tabla: esta tabla es solo la API HTTP de Node-RED.
- `DELETE /api/alumnos/:cuenta/rostro` es la forma de **retirar el
  consentimiento**: borrar el embedding deja al alumno como pendiente de rostro
  y deja de reconocerse.
- `GET /api/galeria` es la única respuesta que contiene embeddings. Es el
  caminho controlado entre el servicio de visión y Node-RED. Vive en un realm
  de `httpNodeAuth` distinto del que usan los otros siete endpoints: el
  navegador tiene credenciales del realm del frontend, y esas credenciales no
  dan acceso a este endpoint. El servicio de visión se autentica con las
  credenciales de servicio del realm `vision`, que no están en el repo: en
  Node-RED van cifradas en los bloques `creds` de `nodered/flows.json` y la
  copia que usa el servicio de visión está en el `.env` de la Orange Pi
  (`MQTT_USER_VISION` y `MQTT_PASSWORD_VISION` para el broker).
- En Node-RED esto se documenta con un grupo de nodos `httpIn` por realm: cada
  grupo lleva su propio nodo `httpNodeAuth`, con su `realm` y sus
  credenciales. La credencial del realm `frontend` viaja en el bundle JS que
  sirve `httpStatic` desde `/data/public`, de modo que cualquiera que abra la
  página puede extraerla: por eso ese realm no puede incluir la galería.
- El `credentialSecret` de Node-RED sale de la variable de entorno
  `NODERED_CREDENTIAL_SECRET`, que solo existe en el `.env` de la Orange Pi.
  `nodered/flows.json` sí se versiona, pero sus bloques `creds` van cifrados
  con ese secreto: sin el secreto en el repo no son descifrables.
- La importación del CSV de la lista maestra se hace con `curl` contra
  `POST /api/alumnos/importar`, sin vista propia en el frontend: el profesor la
  ejecuta una vez por grupo.
- El frontend estático de `web/` lo sirve Node-RED con `httpStatic` desde
  `/data/public`, también protegido.

---

## 6. Validación

El contrato se puede comprobar antes de mandar nada por el broker:

```bash
python3 herramientas/validar_protocolo.py herramientas/ejemplos/payloads_validos.json
python3 herramientas/validar_protocolo.py herramientas/ejemplos/payloads_invalidos.json
python3 herramientas/validar_protocolo.py --lista
```

El script usa solo la biblioteca estándar de Python 3. Los errores salen por
stderr y el proceso sale con código 1 si hay alguno.

Además del tipo y los campos requeridos, el validador comprueba lo que el
contrato declara de forma explícita:

- los valores `enum` de cada campo (la acción de `aula/vision/cmd` y de
  `aula/sesion/cmd`, el LED, el servo, la pose, el motivo, el buzzer y la luz);
- los límites numéricos: `conf` entre 0 y 1, `paso` entre 1 y 5 y `total` igual
  a 5;
- la longitud del vector de `embedding`, que tiene que ser `EMBEDDING_DIM` (la
  variable de entorno, 512 por defecto);
- las reglas declaradas dentro de `items` (`enum`, `min`, `max` y `length`),
  que se aplican a cada elemento del array;
- que `aula/actuador/cmd` lleve al menos un campo;
- las reglas condicionales: `aula/sesion/cmd` con `accion: iniciar` exige
  `materia`, `grupo` y `tol_min`.

Si el contrato no declara una regla para un campo, el validador no la inventa:
comprueba solo lo que dice [`mqtt-topics.json`](mqtt-topics.json).

**El contrato se valida a sí mismo**, al cargarlo. Si el JSON está mal
editado, el script aborta con **código 2** y un mensaje que dice qué entrada
del JSON es la que está mal, en vez de reventar con una traza:

- un `type` que no sea uno de los soportados (`int`, `float`, `string`, `bool`,
  `object` y `array`): con un `type` desconocido, la comprobación de tipos se
  desactivaría en silencio y dejaría pasar cualquier valor;
- un campo declarado sin `name` o sin `type`;
- un tópico que no empiece por el prefijo `base_topic` que declara el
  contrato;
- los ejemplos del propio contrato, que tienen que cumplir las reglas que el
  contrato declara. El ejemplo del `embedding` va abreviado a 4 valores y solo
  se acepta porque el JSON lo marca con `ejemplo_abreviado`.

El código 2 es también el de una entrada ilegible o vacía: no hay mensajes que
validar es un error de uso, no un payload válido.

---

## 7. Limitaciones conocidas de esta capa

Esta capa no resuelve los riesgos de la sección 13 del plan, los deja
escritos. Aunque el sistema está en la LAN del aula, sin cifrar:

- **Mosquitto sin TLS** (1883 y 9001): las credenciales del broker, los
  embeddings y las miniaturas viajan **en claro** por la LAN. Cualquiera que
  escuche la red los lee.
- **Postgres y Grafana sin cifrar** en la LAN: el tráfico entre la Orange Pi y
  la PC, con los embeddings y los registros de asistencia, va en claro.
- **API HTTP de Node-RED con Basic auth sobre HTTP plano**: el usuario y la
  contraseña de la API viajan en claro en cada petición.
- **La credencial del frontend vive en el navegador**: viaja en el bundle JS
  que sirve `httpStatic` desde `/data/public`, así que quien tenga acceso a la
  página puede extraerla. La ACL la limita a solo lectura, pero expone los
  nombres y los estados de la clase a quien tenga acceso a esa página.
- **La ACL no filtra campos dentro de un tópico**: el rol `vision` tiene
  permiso de escritura sobre `aula/actuador/cmd` **completo** —servo, LED y
  buzzer—, no solo sobre el campo `luz` que le da el plan (§5.1), porque
  Mosquitto no puede filtrar por campo. Es una limitación asumida de esta
  capa, con la mitigación de que Node-RED valida y limita el alcance de lo que
  reenvía a la XIAO.
- **El frontend está en la ruta crítica del registro**: si el navegador no
  arranca o se cae a mitad de un registro, el alta de rostro se queda parada.
  Es el mismo riesgo que ya tiene el plan (§13) y aquí queda anotado: la
  mitigación es probar los endpoints con `curl` desde temprano y tener la
  herramienta de enrolamiento por lote como respaldo.

Cada una de estas líneas es un trabajo pendiente de una capa posterior; aquí
solo se dejan escritas para que no se pierdan.
