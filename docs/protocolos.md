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
| **Dirección** | Publican Node-RED, el servicio de visión y el frontend web. Se suscribe la XIAO ESP32S3 Sense. |
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

**Retained no:** un comando retenido se reentregaría al reconectar la XIAO y
volvería a abrir el servo sin motivo.

### 1.5 `aula/actuador/estado`

| | |
|---|---|
| **Dirección** | Publica la XIAO ESP32S3 Sense. Se suscriben Node-RED, la pantalla y el frontend web. |
| **Payload** | `{"servo":"cerrado","led":"azul","luz":0}` |
| **Campos** | `servo` (`string`, requerido): `abierto` o `cerrado`. `led` (`string`, requerido): `verde`, `rojo`, `azul` o `ambar`. `luz` (`int`, requerido): 0 o 1. |
| **Ejemplo** | `{"servo":"cerrado","led":"azul","luz":0}` |
| **QoS** | 1 |
| **Retained** | Sí |

Semántica: el estado real de los actuadores. Va retenido para que un suscriptor
tardío sepa cómo quedó el servo. Node-RED reacciona a este estado publicando
un comando en `aula/actuador/cmd`.

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
| **Ejemplo** | `{"cuenta":"320068234","embedding":[0.0123,-0.0456,0.0789,-0.0321]}` |
| **QoS** | 1 |
| **Retained** | No |

Semántica: el embedding ya promediado y normalizado de las 5 poses. Su
longitud es `EMBEDDING_DIM` (512 por defecto, y el `vector(512)` del esquema de
Postgres). Node-RED lo guarda con `UPDATE alumnos SET embedding`.

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
| **Campos** | `activa` (`bool`, requerido). `id` (`int`, opcional): identificador de la sesión en Postgres; ausente o `null` si no hay sesión activa. |
| **Ejemplo** | `{"activa":true,"id":12}` |
| **QoS** | 1 |
| **Retained** | Sí |

Semántica: el estado de la sesión de clase. Al iniciarla, Node-RED crea el
registro en Postgres y publica aquí; la XIAO enciende el LED azul. Va retenido
para que quien se suscriba después sepa si hay sesión activa.

### 1.12 `aula/sistema/metricas`

| | |
|---|---|
| **Dirección** | Publica la Orange Pi Zero 3W. Se suscribe Node-RED, que lo lleva a InfluxDB. |
| **Payload** | `{"temp":52.1,"cpu":34,"ram":61}` |
| **Campos** | `temp` (`float`, requerido): °C del SoC. `cpu` (`int`, requerido): % de uso. `ram` (`int`, requerido): % de uso. |
| **Ejemplo** | `{"temp":52.1,"cpu":34,"ram":61}` |
| **QoS** | 0 |
| **Retained** | No |

Semántica: métricas del borde para los paneles de Grafana.

---

## 2. Reglas transversales

### 2.1 Prefijo `aula/`

Todos los tópicos empiezan por `aula/`. El prefijo sale de `MQTT_BASE_TOPIC`
en el `.env`, de modo que el broker de pruebas puede usar otro prefijo sin
tocar el código.

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
`aula/vision/enrolamiento` y `aula/sesion/estado`, y a ningún otro tópico del
sistema. `aula/vision/embedding` lo lee solo Node-RED, que es el único que
escribe en Postgres.

La regla se aplica en dos capas independientes:

1. **ACL del broker** (sección 3): el usuario del frontend no tiene permiso de
   lectura sobre `aula/vision/embedding`.
2. **API HTTP** (sección 5): `httpNodeAuth` protege todos los endpoints, y
   `GET /api/galeria` —el único que devuelve embeddings— no lo usa el frontend.

Las fotos nunca se guardan: se descartan tras calcular el embedding y solo
circulan miniaturas en base64 durante el registro.

---

## 3. ACL de Mosquitto

El broker Mosquitto de la Orange Pi va con usuario, password y ACL. El usuario
del frontend es un usuario aparte del broker, no un cliente anónimo.

| Permiso | Usuario del frontend | Resto de módulos |
|---|---|---|
| Publicar en los tópicos de telemetría | Sí | Sí |
| Suscribirse a `aula/vision/enrolamiento` | Sí | Sí |
| Suscribirse a `aula/sesion/estado` | Sí | Sí |
| **Leer `aula/vision/embedding`** | **No** | Solo Node-RED |

Reglas de la ACL, tal y como van en el fichero de configuración del broker:

```
# Usuario del frontend (solo publica y lee lo que necesita)
user frontend
topic write aula/#
topic read aula/vision/enrolamiento
topic read aula/sesion/estado

# El servicio de vision publica el embedding; solo Node-RED lo lee
user vision
topic write aula/vision/#
topic read aula/sesion/estado

user nodered
topic write aula/#
topic read aula/#
```

La diferencia entre `topic write aula/#` para el frontend y `topic read
aula/vision/embedding` ausente es justamente lo que impide que un embedding
llegue al navegador: el permiso de lectura es explícito, no se concede por
omisión.

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

| Método | Ruta | Query | Cuerpo | Respuesta |
|---|---|---|---|---|
| `GET` | `/api/alumnos` | `grupo` (obligatorio): el grupo a listar | — | Lista de alumnos del grupo con su estado de rostro (pendiente o registrado), su consentimiento y su número de cuenta. |
| `POST` | `/api/alumnos/importar` | — | CSV de la lista maestra con las columnas `no_cuenta,nombre,grupo` (cuerpo `text/csv` o `multipart/form-data`) | Resultado de la importación: cuántos alumnos se insertaron y cuántos quedan pendientes de rostro. Todos se insertan con `embedding = NULL`. |
| `POST` | `/api/enrolamiento` | — | `{"cuenta":"320068234","consentimiento":true}` | Confirmación de que la orden salió; el progreso llega por MQTT a `aula/vision/enrolamiento`. |
| `POST` | `/api/enrolamiento/cancelar` | — | `{"cuenta":"320068234"}` | Confirmación de la cancelación del registro en curso. |
| `POST` | `/api/enrolamiento/probar` | — | `{"cuenta":"320068234"}` | Confirmación del arranque de la prueba; el resultado y la confianza llegan por MQTT. |
| `DELETE` | `/api/alumnos/:cuenta/rostro` | — | — | Confirmación del borrado: el embedding pasa a `NULL` y el consentimiento queda retirado. |
| `GET` | `/api/galeria` | — | — | Los embeddings de los alumnos con rostro, para que el servicio de visión sincronice su galería local. **No lo usa el frontend.** |

Notas:

- `DELETE /api/alumnos/:cuenta/rostro` es la forma de **retirar el
  consentimiento**: borrar el embedding deja al alumno como pendiente de rostro
  y deja de reconocerse.
- `GET /api/galeria` es la única respuesta que contiene embeddings. Es el
  caminho controlado entre el servicio de visión y Node-RED; por eso está
  detrás de `httpNodeAuth` y no se expone al frontend.
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
