# Contrato MQTT

> **Borrador v0.3.** Fuente de verdad de los tópicos y payloads del sistema. Deriva de la sección 7.1 de `HIGH-LEVEL-PLAN.md`; cuando este documento y el plan difieran, manda este. Las marcas **[propuesta]** son cambios o huecos detectados respecto al plan que el equipo tiene que aprobar; las marcas **[decidir]** son preguntas abiertas.

## 1. Convenciones generales

- **Broker:** Mosquitto en la Orange Pi. Listener `1883` (TCP, servicios del borde y XIAO) y `9001` (WebSocket, frontend). Usuario y contraseña por cliente (sección 6).
- **Formato:** JSON en UTF-8, un objeto por mensaje. Nombres de campo en español, `snake_case`, sin acentos.
- **Campos desconocidos se ignoran.** Un consumidor no debe fallar si llega un campo que no conoce; así se pueden agregar campos sin romper a nadie. Quitar o renombrar un campo sí es un cambio incompatible y se anota en la sección 7.
- **Números de cuenta** siempre como *string* de 9 dígitos (`"320068234"`), nunca como número: un cero a la izquierda se perdería.
- **Tiempo:** `ts` en ISO 8601 con zona (`"2026-11-04T09:12:33.120-06:00"`) en los mensajes que publica la Orange Pi (visión, Node-RED, pantalla), que tiene RTC. La XIAO no manda `ts`: Node-RED sella la hora al recibir. El servicio de visión estampa `ts` en los cuatro mensajes que publica (`resultado`, `enrolamiento`, `embedding` y `estado`); Node-RED y la pantalla todavía no lo hacen.
- **Unidades** en el nombre o en la tabla: `lat_ms` en milisegundos, `v` en volts, `i` en amperes, `p` en watts, `temp` en °C.
- **Tamaño en la XIAO:** PubSubClient trae un búfer de 256 bytes por defecto. Todo lo que la XIAO publica o recibe debe caber ahí, o el firmware debe llamar a `setBufferSize()`. Ningún mensaje hacia la XIAO lleva miniaturas ni embeddings.
- **Retained:** solo para estados (lo último que se sabe de algo). Nunca para eventos ni órdenes: un `cmd` retenido se volvería a ejecutar al reconectar.

## 2. Resumen de tópicos

| Tópico | Publica | Consume | QoS | Retain |
|---|---|---|---|---|
| `aula/camara/pir` | XIAO | visión | 0 | no |
| `aula/camara/energia` | XIAO | Node-RED | 0 | no |
| `aula/camara/estado` | XIAO (+ LWT) | Node-RED, frontend | 1 | sí |
| `aula/actuador/cmd` | Node-RED, pantalla, visión | XIAO | 1 | no |
| `aula/actuador/estado` | XIAO | Node-RED, pantalla, frontend | 1 | sí |
| `aula/vision/resultado` | visión | Node-RED, frontend (solo `modo: prueba`) | 1 | no |
| `aula/vision/cmd` | Node-RED | visión | 1 | no |
| `aula/vision/enrolamiento` | visión | frontend, pantalla | 0 | no |
| `aula/vision/embedding` | visión | Node-RED | 1 | no |
| `aula/vision/estado` | visión (+ LWT) | Node-RED, frontend | 1 | sí |
| `aula/vision/galeria/alta` | Node-RED | visión | 1 | no |
| `aula/vision/galeria/baja` | Node-RED | visión | 1 | no |
| `aula/sesion/cmd` | pantalla, frontend | Node-RED | 1 | no |
| `aula/sesion/estado` | Node-RED | todos | 1 | sí |
| `aula/pantalla/reinicio` | Node-RED | pantalla | 1 | no |
| `aula/sistema/metricas` | Orange Pi | Node-RED | 0 | no |

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

Publican: Node-RED (retroalimentación del pase de lista), la pantalla (botones "Abrir puerta" y "Probar luz LED") y el servicio de visión (solo `luz`, para la orquestación luz–captura).

### `aula/actuador/estado` (retenido)
La XIAO lo publica cada vez que cambia un actuador.

```json
{"servo": "cerrado", "led": "azul", "luz": 0}
```

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
Progreso del registro, un mensaje por cambio de estado. QoS 0: si se pierde uno, el siguiente lo corrige.

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
Embedding final de un registro. **El usuario MQTT del frontend no puede leer este tópico (ACL).**

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
Salud del servicio de visión, con LWT `{"en_linea": false}`. Le dice a Node-RED y al frontend si se puede registrar o pasar lista.

```json
{"ts": "2026-11-04T09:12:33.120-06:00", "en_linea": true, "modo": "inactivo", "galeria": 38, "modelo": "w600k_mbf-uint8-v1"}
```

| Campo | Tipo | Obligatorio | Notas |
|---|---|---|---|
| `ts` | string | sí | ISO 8601 con zona. |
| `en_linea` | bool | sí | `false` solo lo publica el LWT del broker. |
| `modo` | string | sí | `inactivo`, `pase`, `enrolando` |
| `galeria` | int | sí | Número de embeddings en RAM |
| `modelo` | string | sí | Nombre y versión del `.nb` |

### `aula/vision/galeria/alta` y `aula/vision/galeria/baja`
La galería en RAM solo cambia por orden de Node-RED, que publica estos tópicos **después** de escribir en Postgres (plan, sección 5.4). La visión nunca agrega una entrada por su cuenta, ni siquiera tras publicar `aula/vision/embedding`. Así la base es la única fuente de verdad. Los dos van con QoS 1 y sin retención.

```json
{"cuenta": "320068234"}
```

- **`alta`:** Node-RED guardó el embedding de esa cuenta. La visión recarga la galería completa con `GET /api/galeria` (unas 40 entradas). El mensaje no lleva el embedding: así no viaja otra vez por MQTT y la galería siempre sale de la base.
- **`baja`:** se retiró el consentimiento de esa cuenta. La visión borra su entrada de la galería en RAM.
- **Recarga completa** con `GET /api/galeria` también al arrancar y en cada reconexión al broker; cubre una `baja` perdida mientras la visión estaba desconectada.
- Desde el 11 nov, `GET /api/galeria` exige `httpNodeAuth` y el encabezado `X-Galeria-Token` (`GALERIA_TOKEN`), que solo tienen Node-RED y la visión (plan, secciones 6.2 y 6.7).

## 5. Borde (Node-RED, pantalla, Orange Pi)

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
Cada 10 s.
```json
{"ts": "…", "temp": 52.1, "cpu": 34, "ram": 61}
```
`temp` en °C del SoC; `cpu` y `ram` en porcentaje.

## 6. Control de acceso (ACL de Mosquitto)

| Usuario | Lee | Escribe |
|---|---|---|
| `xiao` | `aula/actuador/cmd`, `aula/sesion/estado` | `aula/camara/#`, `aula/actuador/estado` |
| `vision` | `aula/camara/pir`, `aula/vision/cmd`, `aula/vision/galeria/#` | `aula/vision/resultado`, `aula/vision/enrolamiento`, `aula/vision/embedding`, `aula/vision/estado`, `aula/actuador/cmd` |
| `nodered` | `aula/#` | `aula/#` |
| `pantalla` | `aula/sesion/estado`, `aula/vision/enrolamiento`, `aula/actuador/estado`, `aula/pantalla/reinicio` | `aula/sesion/cmd`, `aula/actuador/cmd` |
| `frontend` (WS) | `aula/vision/enrolamiento`, `aula/vision/resultado`, `aula/vision/estado`, `aula/sesion/estado`, `aula/actuador/estado`, `aula/camara/estado` | `aula/actuador/cmd`, `aula/sesion/cmd` |

El `frontend` nunca lee `aula/vision/embedding` ni `aula/vision/galeria/#`. Solo `nodered` publica en `aula/vision/galeria/#`: por eso `vision` escribe sus tópicos uno por uno y no `aula/vision/#`. **[decidir]** ¿El frontend publica directo en `aula/actuador/cmd`, o pasa por la API de Node-RED? Pasar por la API deja un solo punto de control y de registro.

## 7. Historial de cambios

| Versión | Cambio |
|---|---|
| v0.1 | Borrador inicial a partir de la sección 7.1 del plan, con las propuestas marcadas |
| v0.2 | `aula/galeria/cambio` se reemplaza por `aula/vision/galeria/alta` y `aula/vision/galeria/baja` (solo `cuenta`; la visión recarga con `GET /api/galeria`), acordado entre Gustavo y Yordi. En la ACL, `vision` escribe sus tópicos uno por uno y lee `aula/vision/galeria/#`; solo `nodered` publica en la galería. Se retira la acción `confirmar` de `aula/vision/cmd`: la advertencia de parecido es informativa |
| v0.3 | El servicio de visión implementa el contrato: `decision` sustituye a `motivo` en `aula/vision/resultado` (el servicio es quien aplica los dos umbrales, `reconocido` y `dudoso`) y se añade `motivo` como detalle fino de la `decision`; `decision: "error"` con `motivo: "error_interno"` para el fallo interno; `ts` en los cuatro mensajes que publica el servicio (`resultado`, `enrolamiento`, `embedding` y `estado`); `aula/vision/estado` con `en_linea`, `modo`, `galeria` y `modelo`; `dim` y `modelo` en `aula/vision/embedding`; el nombre del comando `enrolar` se copia al progreso del registro; la quinta pose es `frente_2`; `cuenta_invalida` como motivo de `error` (extensión). El vocabulario de `motivo` del enrolamiento es el de este documento: una captura fallida es `fallo_captura` y un fallo interno del pipeline es `error` con `npu` |
