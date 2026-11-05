# Contrato MQTT

> **Borrador v0.8.** Fuente de verdad de los tópicos y payloads del sistema. Deriva de la sección 7.1 de `HIGH-LEVEL-PLAN.md`; cuando este documento y el plan difieran, manda este. Las marcas **[propuesta]** son cambios o huecos detectados respecto al plan que el equipo tiene que aprobar; las marcas **[decidir]** son preguntas abiertas.

## 1. Convenciones generales

- **Broker:** Mosquitto en la Orange Pi. Un solo listener, el `1883` (TCP, servicios del edge y XIAO). No hay listener WebSocket (el `9001` se quitó en v0.5): el frontend no habla MQTT y recibe todo por la API de Node-RED. Sin acceso anónimo; usuario y contraseña por rol (sección 6).
- **Formato:** JSON en UTF-8, un objeto por mensaje. Nombres de campo en español, `snake_case`, sin acentos.
- **Campos desconocidos se ignoran.** Un consumidor no debe fallar si llega un campo que no conoce; así se pueden agregar campos sin romper a nadie. Quitar o renombrar un campo sí es un cambio incompatible y se anota en la sección 7.
- **Números de cuenta** siempre como *string* de 9 dígitos (`"320068234"`), nunca como número: un cero a la izquierda se perdería.
- **Tiempo:** `ts` en ISO 8601 con zona (`"2026-11-04T09:12:33.120-06:00"`) en los mensajes que publica la Orange Pi (visión, Node-RED, pantalla), que tiene RTC. La XIAO no manda `ts`: Node-RED sella la hora al recibir. El servicio de visión estampa `ts` en los cuatro mensajes que publica (`resultado`, `enrolamiento`, `embedding` y `estado`); Node-RED y la pantalla todavía no lo hacen.
- **Unidades** en el nombre o en la tabla: `lat_ms` en milisegundos, `v` en volts, `i` en amperes, `p` en watts, `temp` en °C.
- **Tamaño en la XIAO:** PubSubClient trae un búfer de 256 bytes por defecto. Todo lo que la XIAO publica o recibe debe caber ahí, o el firmware debe llamar a `setBufferSize()`. Ningún mensaje hacia la XIAO lleva miniaturas ni embeddings.
- **Retained:** solo para estados (lo último que se sabe de algo). Nunca para eventos ni órdenes: un `cmd` retenido se volvería a ejecutar al reconectar.
- **Los consumidores ignoran los mensajes con la bandera retain y los de payload vacío, en todo tópico cuya columna Retain sea "no" en la tabla de la sección 2 (obligación nueva de v0.5).** La ACL de Mosquitto no puede impedir el retain por tópico, así que quien tenga credenciales de un publicador (o su Will) podría dejar retenida una orden o una lectura falsa, que se repetiría en cada reconexión del consumidor. Por eso todo consumidor de esos tópicos ignora cualquier mensaje con retain y cualquiera con payload vacío (no es una orden ni una lectura válida), y no solo en los `*/cmd`: la XIAO en `aula/actuador/cmd`, el servicio de visión en `aula/vision/cmd` y en `aula/camara/pir`, y Node-RED en `aula/sesion/cmd`, `aula/camara/energia` y los demás. **Afecta al firmware de la XIAO y al servicio de visión (en `aula/camara/pir` y en `aula/vision/cmd`, además de las órdenes de la XIAO), que son de Gustavo.** Además, Node-RED, en cada conexión al broker (al arrancar y tras reconectar, por ejemplo al reiniciar Mosquitto), borra los retenidos que haya en `aula/actuador/cmd`, `aula/sesion/cmd`, `aula/vision/cmd`, `aula/camara/pir` y `aula/camara/energia` (flujo "Limpieza de retenidos" de `flows.json`). **Esa limpieza provoca, en cada conexión al broker, un mensaje vacío en vivo (sin bandera retain) en cada tópico de su lista**, que los suscriptores deben ignorar por vacío. El flujo de ingesta descarta `aula/camara/energia` con retain (con aviso) y en silencio con payload vacío. **Ya descartan retain y lo vacío** los manejadores de `aula/vision/enrolamiento` y `aula/vision/embedding` de Node-RED (pestaña `Registro`, v0.7). **Limitación mientras el broker de Node-RED siga en MQTT 3.1.1** (`protocolVersion 4`): un retenido publicado en vivo llega a los suscriptores conectados con `retain = false` (la opción `rap` no existe en 3.1.1), y la bandera solo se ve en la entrega inicial al (re)conectar. El descarte por retain de esos dos manejadores solo aplica entonces; no se pasa a MQTT 5 antes de la demo. **Pendiente para los flujos del 11 nov:** los demás tópicos con Retain = no que la limpieza no cubre (`aula/vision/resultado`, `aula/vision/galeria/alta`, `aula/vision/galeria/baja`, `aula/pantalla/reinicio` y `aula/sistema/metricas`) deben descartar retain en el flujo que los consuma. La alternativa es ampliar la lista de la limpieza; se prefiere descartar en el consumidor, porque la limpieza solo borra al conectar y no protege entre conexiones.

## 2. Resumen de tópicos

| Tópico | Publica | Consume | QoS | Retain |
|---|---|---|---|---|
| `aula/camara/pir` | XIAO | visión | 0 | no |
| `aula/camara/energia` | XIAO (y `simulador`, solo pruebas) | Node-RED | 0 | no |
| `aula/camara/estado` | XIAO (+ LWT) | Node-RED | 1 | sí |
| `aula/actuador/cmd` | Node-RED (incluye las órdenes del frontend), pantalla, visión | XIAO | 1 | no |
| `aula/actuador/estado` | XIAO | Node-RED, pantalla | 1 | sí |
| `aula/vision/resultado` | visión | Node-RED (el frontend recibe por la API los de `modo: prueba`) | 1 | no |
| `aula/vision/cmd` | Node-RED | visión | 1 | no |
| `aula/vision/enrolamiento` | visión | pantalla, Node-RED (el frontend lo recibe por la API) | 0 | no |
| `aula/vision/embedding` | visión | Node-RED | 1 | no |
| `aula/vision/estado` | visión (+ LWT) | Node-RED (el frontend lo recibe por la API) | 1 | sí |
| `aula/vision/galeria/alta` | Node-RED | visión | 1 | no |
| `aula/vision/galeria/baja` | Node-RED | visión | 1 | no |
| `aula/sesion/cmd` | pantalla, Node-RED (a nombre del frontend) | Node-RED | 1 | no |
| `aula/sesion/estado` | Node-RED | XIAO, pantalla | 1 | sí |
| `aula/pantalla/reinicio` | Node-RED | pantalla | 1 | no |
| `aula/sistema/metricas` | Node-RED | Node-RED | 0 | no |

**Nota (v0.8): la XIAO publica con QoS 0.** PubSubClient 2.8, la biblioteca del firmware (plan, sección 6.1), solo publica con QoS 0: `aula/actuador/estado` y `aula/camara/estado` en vivo salen con QoS 0 y retenidos, aunque la tabla diga 1. El LWT de `aula/camara/estado` sí se registra con QoS 1. Los consumidores no deben depender de la entrega QoS 1 de esos dos tópicos; el retenido deja el último valor en el broker. Si hace falta QoS 1 al publicar (o TLS), el firmware tendría que pasar a esp-mqtt.

## 3. Nodo de campo (XIAO ESP32S3)

### `aula/camara/pir`
Evento de movimiento. Se publica en el flanco de subida del PIR, con un antirrebote de al menos 2 s en el firmware.

```json
{"mov": 1}
```

| Campo | Tipo | Obligatorio | Notas |
|---|---|---|---|
| `mov` | int | sí | `1` = movimiento detectado. No se publica `0`. |

### `aula/camara/energia`
Lectura periódica del INA219 (propuesta: cada 1 s en reposo; el periodo es configurable en el firmware).

```json
{"v": 12.1, "i": 0.85, "p": 10.3}
```

| Campo | Tipo | Obligatorio | Notas |
|---|---|---|---|
| `v` | float | sí | V, entrada de 12 V |
| `i` | float | sí | A |
| `p` | float | sí | W |

### `aula/camara/estado` (retenido)
Salud de la XIAO, cada 30 s y al conectar. **[propuesta]** La XIAO registra un LWT en este tópico con `{"en_linea": false}`, retenido, para que Node-RED y el frontend sepan si se cayó.

```json
{"en_linea": true, "rssi": -58, "heap": 120000, "temp": 48}
```

| Campo | Tipo | Obligatorio | Notas |
|---|---|---|---|
| `en_linea` | bool | sí | **[propuesta]** `false` solo lo publica el LWT |
| `rssi` | int | no | dBm |
| `heap` | int | no | bytes libres |
| `temp` | float | no | °C del chip |

### `aula/actuador/cmd`
Orden a los actuadores. Todos los campos son opcionales: solo se aplica lo que viene.

```json
{"servo": "abrir", "led": "verde", "buzzer": 1, "luz": 0}
```

| Campo | Tipo | Valores | Notas |
|---|---|---|---|
| `servo` | string | `abrir` | Abre 90°, espera 3 s y cierra solo. No hay `cerrar`. |
| `led` | string | `azul`, `verde`, `rojo`, `ambar`, `apagado` | Patrones de la sección 6.1 del plan |
| `buzzer` | int | `0`, `1`, `2` | **[decidir]** `1` = pitido corto de éxito, `2` = doble pitido de error |
| `luz` | int | `0`, `1` | Tira LED de 12 V vía MOSFET |

Publican: Node-RED (retroalimentación del pase de lista y las órdenes manuales que llegan del frontend por la API), la pantalla (botones "Abrir puerta" y "Probar luz LED") y el servicio de visión (solo `luz`, para la orquestación luz–captura).

**Comportamiento de `luz` en la XIAO (v0.8; firmware de la tira en `firmware/xiao`).** Afecta al carril de Gustavo (visión): quien publique `luz:1` **no debe asumir que se aplicó**, y debe **reenviar `luz:1` antes de 360 s** si necesita la luz más tiempo.
- **Apagado automático:** la tira se apaga sola a los 360 s del último `luz:1` y publica `aula/actuador/estado`. Cada `luz:1`, aunque ya esté encendida, reinicia el plazo. Los 360 s cubren con margen el vencimiento de 5 min del registro de 5 poses: la visión publica `luz:1` una sola vez al iniciar el registro y `luz:0` al terminar. Si la visión agrega un reenvío periódico de `luz:1`, el plazo se puede bajar.
- **Apagado de seguridad:** si la XIAO pierde el Wi-Fi o el broker, apaga la tira; al reconectar publica el estado real. Una orden `luz:1` enviada mientras está desconectada (sesión limpia, QoS 1) se pierde.
- **Descarte por ventana:** PubSubClient 2.8 no expone la bandera retain, así que la XIAO descarta todo mensaje de `aula/actuador/cmd` que llegue en los 1500 ms siguientes a suscribirse (donde llegaría un retenido). Es una heurística que aproxima la obligación de la sección 1, sin garantizarla: un retenido que llegue después de la ventana se aplicaría como una orden en vivo. A cambio se pierde una orden en vivo que coincida con la ventana, es decir, justo después de reconectar. Pendiente de medir en la placa.
- Mientras solo existe el firmware de la tira, `servo`, `led` y `buzzer` se ignoran.
- **Salud de la XIAO:** hasta que entre el firmware de Gustavo, `aula/camara/estado` se publica solo al conectar (`{"en_linea":true}`, más el LWT `{"en_linea":false}`): sin latido de 30 s y sin `rssi`, `heap` ni `temp`.

### `aula/actuador/estado` (retenido)
La XIAO lo publica cada vez que cambia un actuador.

```json
{"servo": "cerrado", "led": "azul", "luz": 0}
```

**Estado parcial (v0.8).** Mientras el firmware solo controla la tira, la XIAO publica `{"luz": 0}` o `{"luz": 1}`, sin `servo` ni `led`: no inventa el estado de actuadores que no controla. Cuando entren el servo y los LEDs, el estado se publica completo, porque cada publicación retenida reemplaza a la anterior. La XIAO lo publica al cambiar la luz, al (re)conectar y como respuesta a toda orden válida de `luz`, y lo hace con QoS 0 (ver la nota de la sección 2).

## 4. Servicio de visión

### `aula/vision/resultado`
Un mensaje por intento de reconocimiento, sea del pase de lista o de una prueba de registro.

```json
{
  "ts": "2026-11-04T09:12:33.120-06:00",
  "modo": "pase",
  "decision": "reconocido",
  "cuenta": "320068234",
  "conf": 0.78,
  "margen": 0.21,
  "lat_ms": {"cap": 180, "det": 12, "emb": 6, "total": 240}
}
```

| Campo | Tipo | Obligatorio | Notas |
|---|---|---|---|
| `ts` | string | sí | ISO 8601 con zona. |
| `modo` | string | sí | `pase` o `prueba`. Node-RED **solo registra asistencia con `pase`**; con `prueba` solo reenvía al frontend. Sin este campo, la prueba inmediata del registro marcaría asistencia. |
| `decision` | string | sí | `reconocido`, `dudoso` (zona gris → LED ámbar), `desconocido`, `sin_rostro`, `imagen_insuficiente`, `error` (fallo interno del servicio; extensión de este documento) |
| `cuenta` | string \| null | sí | Mejor candidato. `null` si `decision` es `sin_rostro`, `imagen_insuficiente` o `error`, y si la galería está vacía. Con `desconocido` y con `dudoso` se manda el mejor candidato para auditoría (tabla `intentos`). |
| `conf` | float \| null | sí | Similitud coseno del mejor candidato, 0 a 1. `null` si no hubo candidato. |
| `margen` | float \| null | no | Diferencia con el segundo candidato. `null` (o ausente) si la galería tiene menos de dos alumnos: no hay con quién comparar. Un margen chico indica que se puede confundir con otra persona. |
| `lat_ms` | object | sí | `cap`, `det`, `emb`, `total`, en ms. Etapas que no corrieron van en `null` |
| `motivo` | string | no | Detalle fino de la `decision`; extensión de este documento (el esquema SQL `intentos.motivo` lo necesita y el frontend lo muestra). `captura_fallida`, `imagen_insuficiente`, `sin_rostro`, `desconocido`, `galeria_vacia`, `error_interno`, `ocupado`, `cuenta_invalida`. Con `decision: "reconocido"` o `"dudoso"` no se manda. |

**El umbral lo aplica el servicio de visión**, porque es el dueño del modelo y del umbral calibrado con LFW (los umbrales `reconocido` y `dudoso` viven en su configuración; Node-RED decide sobre `decision` y no compara números). Lo que Node-RED sí decide son las reglas de sesión: lista del grupo, duplicado y retardo. Los umbrales actuales son provisionales: la calibración con LFW es el hito del 18 de noviembre.

### `aula/vision/cmd`
Órdenes de Node-RED al servicio de visión.

```json
{"accion": "enrolar", "cuenta": "320068234", "nombre": "Ana López"}
```

| `accion` | Campos | Efecto |
|---|---|---|
| `enrolar` | `cuenta`, `nombre` | Inicia el registro de 5 poses. Mientras dura, se ignora el PIR. Si ya hay un registro en curso, se rechaza con `estado: "error"` y `motivo: "ocupado"` en `aula/vision/enrolamiento`. |
| `cancelar` | `cuenta` | Aborta el registro en curso y descarta lo capturado. |
| `probar` | `cuenta` (opcional) | Una captura y reconocimiento; publica `aula/vision/resultado` con `modo: "prueba"`. |
`nombre` va en `enrolar` para que la pantalla lo muestre en la guía de registro sin consultar la base. El servicio de visión lo copia al progreso del registro.

### `aula/vision/enrolamiento`
Progreso del registro, un mensaje por cambio de estado. QoS 0: si se pierde uno, el siguiente lo corrige. Node-RED lo consume (v0.7): solo actualiza la inscripción abierta si la `cuenta` coincide y el `ts` no es anterior al inicio del registro (con 2 s de margen); descarta lo retenido y lo vacío. `completado` no cierra el registro en Node-RED, porque el embedding llega después y por otro tópico: lo cierra el embedding guardado, `cancelado` o `error`.

```json
{
  "ts": "2026-11-04T09:12:33.120-06:00",
  "cuenta": "320068234",
  "nombre": "Ana López",
  "paso": 3,
  "total": 5,
  "pose": "izquierda",
  "estado": "rechazada",
  "motivo": "borrosa",
  "miniatura": "<base64 JPEG>"
}
```

| Campo | Tipo | Obligatorio | Notas |
|---|---|---|---|
| `ts` | string | sí | ISO 8601 con zona. |
| `cuenta` | string | sí | Vacía si la cuenta del comando no era un `no_cuenta` válido: no se repite un dato inválido. |
| `nombre` | string | no | Para la ILI9341 |
| `paso`, `total` | int | sí | `paso` va de 1 a 5. En un rechazo de una orden (`estado: "error"`) el `paso` va en 0. |
| `pose` | string | sí | `frente`, `izquierda`, `derecha`, `arriba`, `frente_2`. La quinta pose es `frente_2`: otra toma de frente, no una repetición de la primera. Vacía en un rechazo de una orden. |
| `estado` | string | sí | `esperando_pose`, `capturando`, `aceptada`, `rechazada`, `advertencia`, `completado`, `cancelado`, `error` |
| `motivo` | string | no | Con `rechazada`: `sin_rostro`, `varios_rostros`, `rostro_chico`, `borrosa`, `giro_excesivo`, `fallo_captura`. Con `error`: `ocupado`, `inconsistente` (las fotos no se parecen entre sí), `npu`, `cuenta_invalida` (la cuenta del comando no es válida; extensión de este documento). Un registro que se agota (demasiadas poses rechazadas) se cierra con `estado: "error"` y `motivo: "npu"`. |
| `parecido_a` | string | no | **[propuesta]** Con `advertencia`: cuenta del alumno al que se parece. Nunca incluye embeddings. La advertencia es informativa (plan, sección 5.4, paso 6): la visión no espera confirmación y sigue con `completado` y la publicación del embedding. Si el profesor no está de acuerdo, aplica la baja del rostro. El servicio de visión todavía no emite `advertencia`. |
| `miniatura` | string | no | JPEG en base64, como máximo 160×120 y unos 8 KB. Solo con `aceptada` o `rechazada`. La pantalla la ignora. El servicio de visión todavía no emite miniaturas. |

### `aula/vision/embedding`
Embedding final de un registro. **Solo el usuario `nodered` puede leer este tópico (ACL, sección 6).** El frontend no es cliente MQTT y nunca lo recibe.

```json
{"ts": "2026-11-04T09:12:33.120-06:00", "cuenta": "320068234", "dim": 512, "modelo": "w600k_mbf-uint8-v1", "embedding": [0.0123, -0.0456, "…"]}
```

| Campo | Tipo | Obligatorio | Notas |
|---|---|---|---|
| `ts` | string | sí | ISO 8601 con zona. |
| `cuenta` | string | sí | |
| `dim` | int | sí | Node-RED rechaza el mensaje si no coincide con la columna `vector(N)`. Es el tamaño del arreglo `embedding`. |
| `modelo` | string | sí | Nombre y versión del `.nb`. Si se cambia de modelo, los embeddings viejos dejan de ser comparables y hay que volver a registrar a todos. El nombre viene de la configuración del servicio de visión. |
| `embedding` | float[] | sí | Normalizado L2, 4 decimales (unos 4 KB en JSON con 512 floats) |

### `aula/vision/estado` (retenido)
Salud del servicio de visión, con LWT `{"en_linea": false}`. Le dice a Node-RED, y por la API al frontend, si se puede registrar o pasar lista.

```json
{"ts": "2026-11-04T09:12:33.120-06:00", "en_linea": true, "modo": "inactivo", "galeria": 38, "modelo": "w600k_mbf-uint8-v1"}
```

| Campo | Tipo | Obligatorio | Notas |
|---|---|---|---|
| `ts` | string | no | ISO 8601 con zona. Lo lleva el estado que publica el servicio; el Will del broker no lo lleva (ver abajo). |
| `en_linea` | bool | sí | `false` lo publica el LWT del broker y el estado final de la salida ordenada. |
| `modo` | string | no | `inactivo`, `pase`, `enrolando` |
| `galeria` | int | no | Número de embeddings en RAM |
| `modelo` | string | no | Nombre y versión del `.nb` |

**Las dos formas del estado.** El Will que registra el broker lleva la forma **mínima**, `{"en_linea": false}`: no puede llevar `ts` ni el resto, porque el Will se registra al conectar, antes de que el servicio exista. El estado que publica el servicio, y el estado final de la salida ordenada, llevan el **esquema completo** (`ts`, `en_linea`, `modo`, `galeria` y `modelo`).

La forma mínima es válida para los consumidores: `en_linea` es el único campo obligatorio del estado y el resto es opcional cuando `en_linea` es `false`. Un consumidor que use la capa JSON del contrato (`orangepi/vision/include/vision/json_payload.hpp`) acepta las dos formas y no descarta el Will por faltar campos.

**Cuándo se publica.** El servicio publica `aula/vision/estado` en cada cambio de modo (`inactivo`, `pase`, `enrolando`), con el número de embeddings en RAM y el nombre del modelo. En la salida ordenada publica el estado final con `en_linea: false` **antes** de cerrar la conexión: la publicación es asíncrona y, como la desconexión es limpia, el Will no salta. Si el estado final no llegara al broker, el valor retenido se quedaría en `en_linea: true` para siempre (un "en línea" fantasma). Para eso el proceso vacía la cola de salida del bus antes de pararlo.

### `aula/vision/galeria/alta` y `aula/vision/galeria/baja`
La galería en RAM solo cambia por orden de Node-RED, que publica estos tópicos **después** de escribir en Postgres (plan, sección 5.4). La visión nunca agrega una entrada por su cuenta, ni siquiera tras publicar `aula/vision/embedding`. Así la base es la única fuente de verdad. Los dos van con QoS 1 y sin retención.

```json
{"cuenta": "320068234"}
```

- **`alta`:** Node-RED guardó el embedding de esa cuenta. La visión recarga la galería completa con `GET /api/galeria` (unas 40 entradas). El mensaje no lleva el embedding: así no viaja otra vez por MQTT y la galería siempre sale de la base.
- **`baja`:** se retiró el consentimiento de esa cuenta. La visión borra su entrada de la galería en RAM.
- **Recarga completa** con `GET /api/galeria` también al arrancar y en cada reconexión al broker; cubre una `baja` perdida mientras la visión estaba desconectada.
- `GET /api/galeria` exige el encabezado `X-Galeria-Token` igual a `GALERIA_TOKEN`, que solo tienen Node-RED y la visión, y **no** acepta las credenciales HTTP Basic del profesor (el resto de `/api/*` sí las exige; Node-RED lo resuelve con un `httpNodeMiddleware` propio y no con `httpNodeAuth`, que bloquearía a la visión). Sin token o con uno erróneo, 401. Es de solo lectura (`GET`): la cabecera `X-Pase-Lista: 1`, que el resto de la API exige en todo método que cambia datos (defensa contra CSRF, v0.6), no afecta a la visión. Implementado en el repo desde v0.6; falta desplegarlo y verificarlo en la placa (plan, secciones 6.2 y 6.7).
- **Respuesta (200, `application/json`):** `{"embeddings":[{"cuenta":"320068234","embedding":[...]}]}`, un elemento por alumno con consentimiento y embedding no nulo, ordenado por cuenta; sin ninguno, `{"embeddings":[]}`. `cuenta` es el string de 9 dígitos y `embedding` es un arreglo de 512 números JSON. Es la primera de las formas que acepta `orangepi/vision/src/gallery_store.cpp`, así que la visión no necesita cambios.

## 5. Edge (Node-RED, pantalla, Orange Pi)

### `aula/sesion/cmd`
```json
{"accion": "iniciar", "materia": "FSE", "grupo": "1", "tol_min": 10}
{"accion": "terminar"}
{"accion": "marcar", "cuenta": "320068234"}
```
`marcar` **[propuesta]** es el registro manual del profesor desde la pantalla para la zona gris (paso 7 de la sección 5.1 del plan); se guarda con `metodo: "manual"`.

### `aula/sesion/estado` (retenido)
```json
{"activa": true, "id": 12, "materia": "FSE", "grupo": "1", "inicio": "…", "tol_min": 10}
{"activa": false, "id": 12}
```
Va retenido para que la XIAO y la pantalla recuperen el estado al reconectar (LED azul). Al publicarse `activa: false` se genera el reporte.

### `aula/pantalla/reinicio`
```json
{"reiniciar": 1}
```

### `aula/sistema/metricas`
Cada 10 s. Lo publica Node-RED con el usuario `nodered`, que ya tiene escritura en `aula/#`, con la temperatura, la CPU y la RAM de la placa. El flujo que lo publica aún no existe. Condición futura: si lo publica otro proceso del host, ese proceso necesita un usuario MQTT propio, y eso es un cambio de este contrato (sección 6) y de la ACL.
```json
{"ts": "…", "temp": 52.1, "cpu": 34, "ram": 61}
```
`temp` en °C del SoC; `cpu` y `ram` en porcentaje.

## 6. Control de acceso (ACL de Mosquitto)

La aplica `orangepi/mosquitto/acl` (versionado, sin secretos) con `acl_file`, y los usuarios salen del archivo de contraseñas que genera el entrypoint del broker. Los nombres de usuario son fijos; cada uno tiene su propia contraseña en `orangepi/.env` (`MQTT_<USUARIO>_PASSWORD`).

| Usuario | Lee | Escribe |
|---|---|---|
| `xiao` | `aula/actuador/cmd`, `aula/sesion/estado` | `aula/camara/pir`, `aula/camara/energia`, `aula/camara/estado`, `aula/actuador/estado` |
| `vision` | `aula/camara/pir`, `aula/vision/cmd`, `aula/vision/galeria/#` | `aula/vision/resultado`, `aula/vision/enrolamiento`, `aula/vision/embedding`, `aula/vision/estado`, `aula/actuador/cmd` |
| `nodered` | `aula/#` | `aula/#` |
| `pantalla` | `aula/sesion/estado`, `aula/vision/enrolamiento`, `aula/actuador/estado`, `aula/pantalla/reinicio` | `aula/sesion/cmd`, `aula/actuador/cmd` |
| `simulador` | nada | `aula/camara/energia` |

Notas:

- **No hay usuario `frontend`.** El navegador no habla MQTT (decisión de Yordi, v0.5): todo pasa por la API HTTP de Node-RED, que publica en `aula/actuador/cmd` y `aula/sesion/cmd` con el usuario `nodered`. Eso deja un solo punto de control y de registro de las órdenes manuales, y resuelve el **[decidir]** de v0.4. El progreso en vivo del registro llega al frontend por sondeo: Node-RED guarda el último estado de `aula/vision/enrolamiento` y el frontend consulta `GET /api/enrolamiento/estado` (decidido en v0.7; los endpoints están en el plan, sección 6.7, no en este contrato).
- `xiao` escribe los tres tópicos de cámara uno por uno y no `aula/camara/#`: es la clave que viaja en claro por la LAN, y así no sirve para publicar en tópicos que se añadan después bajo `aula/camara/`.
- `simulador` lo usa solo `herramientas/simular_energia.sh`, para publicar lecturas falsas de energía. No lee nada.
- Solo `nodered` publica en `aula/vision/galeria/#` y en `aula/vision/cmd`: por eso `vision` escribe sus tópicos uno por uno y no `aula/vision/#`. Solo `nodered` lee `aula/vision/embedding`.
- **`use_username_as_clientid true`.** El broker usa el nombre de usuario como client id e ignora el que traiga el cliente (el `client_id` de `vision.conf`, el `clientid` del nodo MQTT de Node-RED). Consecuencia: dos clientes con el mismo usuario se expulsan entre sí (el segundo desconecta al primero, y si ambos reconectan solos se desconectan en bucle). Cada rol tiene una sola instancia; para depurar no se usa el usuario de un servicio en marcha.
- Mosquitto descarta **en silencio** la publicación que la ACL deniega: el publicador no recibe error. Para comprobar un permiso, verificar con un suscriptor autorizado, no con el código de salida del publicador. Una suscripción denegada tampoco recibe mensajes.
- El Will (LWT) también pasa por la ACL: el de `xiao` va en `aula/camara/estado` y el de `vision` en `aula/vision/estado`.
- La ACL no impide el retain por tópico: los consumidores descartan lo retenido en los tópicos con Retain = no (sección 1).

## 7. Historial de cambios

| Versión | Cambio |
|---|---|
| v0.1 | Borrador inicial a partir de la sección 7.1 del plan, con las propuestas marcadas |
| v0.2 | `aula/galeria/cambio` se reemplaza por `aula/vision/galeria/alta` y `aula/vision/galeria/baja` (solo `cuenta`; la visión recarga con `GET /api/galeria`), acordado entre Gustavo y Yordi. En la ACL, `vision` escribe sus tópicos uno por uno y lee `aula/vision/galeria/#`; solo `nodered` publica en la galería. Se retira la acción `confirmar` de `aula/vision/cmd`: la advertencia de parecido es informativa |
| v0.3 | El servicio de visión implementa el contrato: `decision` sustituye a `motivo` en `aula/vision/resultado` (el servicio es quien aplica los dos umbrales, `reconocido` y `dudoso`) y se añade `motivo` como detalle fino de la `decision`; `decision: "error"` con `motivo: "error_interno"` para el fallo interno; `ts` en los cuatro mensajes que publica el servicio (`resultado`, `enrolamiento`, `embedding` y `estado`); `aula/vision/estado` con `en_linea`, `modo`, `galeria` y `modelo`; `dim` y `modelo` en `aula/vision/embedding`; el nombre del comando `enrolar` se copia al progreso del registro; la quinta pose es `frente_2`; `cuenta_invalida` como motivo de `error` (extensión). El vocabulario de `motivo` del enrolamiento es el de este documento: una captura fallida es `fallo_captura` y un fallo interno del pipeline es `error` con `npu` |
| v0.4 | Se fija la forma del estado de `aula/vision/estado`: el Will del broker lleva la forma **mínima** `{"en_linea": false}` (no puede llevar `ts` ni el resto, porque el Will se registra al conectar), mientras que el estado que publica el servicio y el estado final de la salida ordenada llevan el **esquema completo** (`ts`, `en_linea`, `modo`, `galeria`, `modelo`). `en_linea` es el único campo obligatorio del estado: el resto es opcional cuando `en_linea` es `false`, así que la forma mínima es válida para los consumidores. Se documenta que el servicio publica el estado en cada cambio de modo y que, en la salida ordenada, publica el estado final con `en_linea: false` **antes** de cerrar la conexión (la publicación es asíncrona y la desconexión es limpia, así que el Will no salta: sin eso quedaría retenido un "en línea" fantasma) |
| v0.5 | Se activa la ACL de Mosquitto (sección 6) con un usuario por rol: `xiao`, `vision`, `nodered`, `pantalla` y `simulador` (nuevo, solo escribe `aula/camara/energia`). **Sin usuario `frontend`:** el navegador no habla MQTT y todo pasa por la API de Node-RED, que publica `aula/actuador/cmd` y `aula/sesion/cmd`; se resuelve así el **[decidir]** de v0.4. Se quita el listener WebSocket `9001` (sección 1). Los tópicos que listaban al frontend como consumidor directo (`aula/camara/estado`, `aula/actuador/estado`, `aula/vision/resultado`, `aula/vision/enrolamiento`, `aula/vision/estado`) y como publicador (`aula/sesion/cmd`) pasan a la API; el mecanismo del progreso en vivo se decide el 11 nov. Se fija `use_username_as_clientid true` (el client id es el usuario; dos clientes con el mismo usuario se expulsan). Los consumidores de todo tópico con Retain = no ignoran los mensajes con retain y los de payload vacío (sección 1; afecta al firmware de la XIAO y al servicio de visión, incluidos `aula/camara/pir` y `aula/vision/cmd`). Node-RED borra los retenidos de los `*/cmd`, `aula/camara/pir` y `aula/camara/energia` en cada conexión al broker, lo que provoca un mensaje vacío en vivo en cada uno de esos tópicos. `xiao` escribe `aula/camara/pir`, `aula/camara/energia` y `aula/camara/estado` en vez de `aula/camara/#`. `aula/camara/energia` la publica también `simulador` (solo pruebas). El publicador de `aula/sistema/metricas` es Node-RED (usuario `nodered`, con la temperatura, la CPU y la RAM de la placa); el flujo que lo publica aún no existe, y no lleva usuario MQTT propio. Sin cambios en los payloads |
| v0.6 | Se fija la forma de `GET /api/galeria` (sección 4): `{"embeddings":[{"cuenta","embedding"}]}`, que ya aceptaba el parser de la visión, y su autenticación: solo el encabezado `X-Galeria-Token`, sin HTTP Basic, mientras el resto de `/api/*` exige Basic del profesor (`httpNodeMiddleware` propio de Node-RED, no `httpNodeAuth`). Todo método que no sea `GET` ni `HEAD` exige además la cabecera `X-Pase-Lista: 1` (defensa contra CSRF; la visión solo hace `GET`). Sin cambios en los tópicos, los payloads MQTT ni la ACL. **Afecta a la visión (Gustavo)** solo en su documentación: `orangepi/vision/README.md` sigue llamando provisional al formato y menciona `httpNodeAuth` |
| v0.7 | Node-RED consume `aula/vision/enrolamiento` y `aula/vision/embedding` (pestaña `Registro`). **El progreso en vivo llega al frontend por sondeo** (`GET /api/enrolamiento/estado`, cada ~1 s con un registro abierto): se cierra el "se decide el 11 nov" de v0.5. Los dos tópicos se descartan si traen retain (solo al (re)conectar, mientras el broker de Node-RED siga en MQTT 3.1.1: en vivo el retenido llega sin la bandera) o payload vacío. Se descartan también los mensajes cuyo `ts` falte o sea anterior al inicio del registro (con 2 s de margen) o posterior a la hora actual más 5 minutos; con una excepción: un embedding de la cuenta abierta cuyo `ts` sea ilegible, vacío o de antes de 2000 (la visión usa 1970 como hora de reserva) no se descarta en silencio, sino que cierra el registro como `embedding_invalido` y revierte el consentimiento, para que un `cancelado`, un `error` o un embedding de una sesión anterior de la misma cuenta no afecte al registro nuevo (**afecta a la visión (Gustavo)**: `ts` es obligatorio en ambos, y debe venir de un reloj correcto). **`completado` no cierra el registro en Node-RED**: la visión lo publica antes que el embedding y por otro tópico y otra QoS, sin garantía de orden, así que el registro lo cierra el embedding guardado, `cancelado`, `error`, la cancelación del profesor o el vencimiento. Al vencer el registro (5 minutos sin progreso) Node-RED publica `{"accion":"cancelar","cuenta"}` en `aula/vision/cmd`. Node-RED también valida el embedding: `dim` 512, 512 números, `modelo` y norma L2 igual a 1 con tolerancia de 0.01 (el contrato ya lo define normalizado), y publica `galeria/alta` solo si lo guardó con el registro aún abierto; si el registro ya estaba cerrado, retira el rostro y publica `galeria/baja`. Los códigos `vencido`, `no_guardado`, `embedding_invalido` y `desconocido` son solo de la API HTTP (el `motivo` de `GET /api/enrolamiento/estado`) y no viajan por MQTT. Sin cambios en los tópicos, los payloads ni la ACL |
| v0.8 | La XIAO ya tiene firmware de la tira LED (`firmware/xiao`; Yordi, con el acuerdo de Gustavo). **Sin cambios en los tópicos, la ACL ni la forma de los payloads de la tabla.** Se documentan sus límites: (1) la XIAO publica `aula/actuador/estado` y `aula/camara/estado` en vivo con QoS 0 por la limitación de PubSubClient 2.8 (el LWT va con QoS 1); (2) el estado es parcial, `{"luz": N}`, hasta que entren el servo y los LEDs; (3) descarta los mensajes de `aula/actuador/cmd` que llegan en los 1500 ms siguientes a suscribirse, porque la biblioteca no expone la bandera retain; (4) la tira se apaga sola a los 360 s del último `luz:1` (cubre con margen el vencimiento de 5 min del registro; la visión publica `luz:1` una sola vez) y también al perder el enlace con el broker; (5) hasta que entre el firmware de Gustavo, `aula/camara/estado` se publica solo al conectar, sin latido de 30 s ni `rssi`, `heap` ni `temp`. **Afecta al carril de Gustavo (visión y orquestación luz–captura):** no asumir que `luz:1` se aplicó y reenviarlo antes de 360 s si hace falta más tiempo |
