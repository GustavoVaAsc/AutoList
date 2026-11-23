# Contrato MQTT

> **Borrador v0.17.** Fuente de verdad de los tópicos y payloads del sistema. Deriva de la sección 7.1 de `HIGH-LEVEL-PLAN.md`; cuando este documento y el plan difieran, manda este. Las marcas **[propuesta]** son cambios o huecos detectados respecto al plan que el equipo tiene que aprobar; las marcas **[decidir]** son preguntas abiertas.

## 1. Convenciones generales

- **Broker:** Mosquitto en la Orange Pi. Un solo listener, el `1883` (TCP, servicios del edge y XIAO). No hay listener WebSocket (el `9001` se quitó en v0.5): el frontend no habla MQTT y recibe todo por la API de Node-RED. Sin acceso anónimo; usuario y contraseña por rol (sección 6).
- **Formato:** JSON en UTF-8, un objeto por mensaje. Nombres de campo en español, `snake_case`, sin acentos.
- **Campos desconocidos se ignoran.** Un consumidor no debe fallar si llega un campo que no conoce; así se pueden agregar campos sin romper a nadie. Quitar o renombrar un campo sí es un cambio incompatible y se anota en la sección 7.
- **Números de cuenta** siempre como *string* de 9 dígitos (`"320068234"`), nunca como número: un cero a la izquierda se perdería.
- **Tiempo:** `ts` en ISO 8601 con zona (`"2026-11-04T09:12:33.120-06:00"`) en los mensajes que publica la Orange Pi (visión, Node-RED, pantalla), que tiene hora sincronizada por red (NTP). La XIAO no manda `ts`: Node-RED sella la hora al recibir. El servicio de visión estampa `ts` en los cuatro mensajes que publica (`resultado`, `enrolamiento`, `embedding` y `estado`); Node-RED y la pantalla todavía no lo hacen.
- **Unidades** en el nombre o en la tabla: `lat_ms` en milisegundos, `v` en volts, `i` en amperes, `p` en watts, `temp` en °C.
- **Tamaño en la XIAO:** PubSubClient trae un búfer de 256 bytes por defecto. Todo lo que la XIAO publica o recibe debe caber ahí, o el firmware debe llamar a `setBufferSize()`. Ningún mensaje hacia la XIAO lleva miniaturas ni embeddings.
- **Retained:** solo para estados (lo último que se sabe de algo). Nunca para eventos ni órdenes: un `cmd` retenido se volvería a ejecutar al reconectar.
- **Los consumidores ignoran los mensajes con la bandera retain y los de payload vacío, en todo tópico cuya columna Retain sea "no" en la tabla de la sección 2 (obligación nueva de v0.5).** La ACL de Mosquitto no puede impedir el retain por tópico, así que quien tenga credenciales de un publicador (o su Will) podría dejar retenida una orden o una lectura falsa, que se repetiría en cada reconexión del consumidor. Por eso todo consumidor de esos tópicos ignora cualquier mensaje con retain y cualquiera con payload vacío (no es una orden ni una lectura válida), y no solo en los `*/cmd`: la XIAO en `aula/actuador/cmd`, el servicio de visión en `aula/vision/cmd` y en `aula/camara/pir`, y Node-RED en `aula/sesion/cmd`, `aula/camara/energia` y los demás. **Afecta al firmware de la XIAO y al servicio de visión (en `aula/camara/pir` y en `aula/vision/cmd`, además de las órdenes de la XIAO), que son de Gustavo.** Además, Node-RED, en cada conexión al broker (al arrancar y tras reconectar, por ejemplo al reiniciar Mosquitto), borra los retenidos que haya en `aula/actuador/cmd`, `aula/sesion/cmd`, `aula/vision/cmd`, `aula/camara/pir`, `aula/camara/energia` y `aula/vision/resultado` (este último desde v0.11; flujo "Limpieza de retenidos" de `flows.json`). **Esa limpieza provoca, en cada conexión al broker, un mensaje vacío en vivo (sin bandera retain) en cada tópico de su lista**, que los suscriptores deben ignorar por vacío. El flujo de ingesta descarta `aula/camara/energia` con retain (con aviso) y en silencio con payload vacío. **Ya descartan retain y lo vacío** los manejadores de `aula/vision/enrolamiento` y `aula/vision/embedding` de Node-RED (pestaña `Registro`, v0.7) y el de `aula/sesion/cmd` (pestaña `Sesiones`, v0.10). **Limitación mientras el broker de Node-RED siga en MQTT 3.1.1** (`protocolVersion 4`): un retenido publicado en vivo llega a los suscriptores conectados con `retain = false` (la opción `rap` no existe en 3.1.1), y la bandera solo se ve en la entrega inicial al (re)conectar. El descarte por retain de esos dos manejadores solo aplica entonces; no se pasa a MQTT 5 antes de la demo. **Ya descarta retain y lo vacío** también el manejador de `aula/vision/resultado` (pestaña `Pase de lista`, v0.11), con la misma limitación de MQTT 3.1.1; lo que sí cubre siempre es la ventana de 60 s de `ts` de la sección 4. **Pendiente:** los demás tópicos con Retain = no que la limpieza no cubre (`aula/vision/galeria/alta`, `aula/vision/galeria/baja` y `aula/pantalla/reinicio`) deben descartar retain en el flujo que los consuma. `aula/sistema/metricas` ya no está pendiente (v0.15): Node-RED lo publica y no lo suscribe, así que no hay consumidor que lo necesite. La alternativa es ampliar la lista de la limpieza; se prefiere descartar en el consumidor, porque la limpieza solo borra al conectar y no protege entre conexiones.

## 2. Resumen de tópicos

| Tópico | Publica | Consume | QoS | Retain |
|---|---|---|---|---|
| `aula/camara/pir` | XIAO | visión | 0 | no |
| `aula/camara/energia` | XIAO (y `simulador`, solo pruebas) | Node-RED | 0 | no |
| `aula/camara/estado` | XIAO (+ LWT) | Node-RED | 1 | sí |
| `aula/actuador/cmd` | Node-RED (incluye las órdenes del frontend), pantalla, visión | XIAO | 1 | no |
| `aula/actuador/estado` | XIAO | Node-RED, pantalla | 1 | sí |
| `aula/vision/resultado` | visión | Node-RED (el frontend recibe por la API los de `modo: prueba` y, con la sesión abierta, la `miniatura` de los `reconocido` del pase) | 1 | no |
| `aula/vision/cmd` | Node-RED | visión | 1 | no |
| `aula/vision/enrolamiento` | visión | pantalla, Node-RED (el frontend lo recibe por la API) | 0 | no |
| `aula/vision/embedding` | visión | Node-RED | 1 | no |
| `aula/vision/estado` | visión (+ LWT) | Node-RED (el frontend lo recibe por la API) | 1 | sí |
| `aula/vision/galeria/alta` | Node-RED | visión | 1 | no |
| `aula/vision/galeria/baja` | Node-RED | visión | 1 | no |
| `aula/sesion/cmd` | pantalla, Node-RED (a nombre del frontend) | Node-RED | 1 | no |
| `aula/sesion/estado` | Node-RED | XIAO, pantalla | 1 | sí |
| `aula/sesion/asistencia` | Node-RED | pantalla | 1 | sí |
| `aula/pantalla/reinicio` | Node-RED | pantalla | 1 | no |
| `aula/sistema/metricas` | Node-RED | ninguno (Node-RED guarda el último valor en memoria para `GET /api/sistema/estado`) | 0 | no |

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
| `temp` | float | no | °C del chip. Se omite si la lectura no es válida |
| `camara` | bool | no | **v0.15** `true` si la cámara está encendida (firmware, `logica_proteccion.h`) |
| `corte` | bool | no | **v0.15** `true` si las capturas están suspendidas por temperatura o por el presupuesto de encendido |
| `nvs` | bool | no | **v0.15** `false` si la flash donde se guarda el piso de `ts` no funciona (sin defensa contra el reenvío tras un reinicio) |

### `aula/actuador/cmd`
Orden a los actuadores. Todos los campos son opcionales: solo se aplica lo que viene.

```json
{"servo": "abrir", "led": "verde", "buzzer": 1, "luz": 0}
```

| Campo | Tipo | Valores | Notas |
|---|---|---|---|
| `servo` | string | `abrir` | Abre 90°, espera 3 s y cierra solo. No hay `cerrar`. |
| `led` | string | `azul`, `verde`, `rojo`, `ambar`, `apagado` | Patrones de la sección 6.1 del plan |
| `buzzer` | int | `0`, `1`, `2` | `0` = silencio, `1` = pitido corto de éxito, `2` = doble pitido de error (v0.11) |
| `luz` | int | `0`, `1` | Tira LED de 12 V vía MOSFET |

Publican: Node-RED (retroalimentación del pase de lista, con la tabla de la sección 4 en `aula/vision/resultado`, y las órdenes manuales que llegan del frontend por la API), la pantalla (botones "Abrir puerta" y "Probar luz LED") y el servicio de visión (solo `luz`, para la orquestación luz–captura).

Las órdenes manuales del frontend (v0.15) entran por `POST /api/actuadores` y se publican aquí con el vocabulario de esta tabla, una por segundo (sección 6).

**Comportamiento de `luz` en la XIAO (v0.8; firmware de la tira en `firmware/xiao`).** Afecta al carril de Gustavo (visión): quien publique `luz:1` **no debe asumir que se aplicó**, y debe **reenviar `luz:1` antes de 360 s** si necesita la luz más tiempo.
- **Apagado automático:** la tira se apaga sola a los 360 s del último `luz:1` y publica `aula/actuador/estado`. Cada `luz:1`, aunque ya esté encendida, reinicia el plazo. Los 360 s cubren con margen el vencimiento de 5 min del registro de 5 poses: la visión publica `luz:1` una sola vez al iniciar el registro y `luz:0` al terminar. Si la visión agrega un reenvío periódico de `luz:1`, el plazo se puede bajar.
- **Apagado de seguridad:** si la XIAO pierde el Wi-Fi o el broker, apaga la tira; al reconectar publica el estado real. Una orden `luz:1` enviada mientras está desconectada (sesión limpia, QoS 1) se pierde.
- **Descarte por ventana:** PubSubClient 2.8 no expone la bandera retain, así que la XIAO descarta todo mensaje de `aula/actuador/cmd` que llegue en los 1500 ms siguientes a suscribirse (donde llegaría un retenido). Es una heurística que aproxima la obligación de la sección 1, sin garantizarla: un retenido que llegue después de la ventana se aplicaría como una orden en vivo. A cambio se pierde una orden en vivo que coincida con la ventana, es decir, justo después de reconectar. Pendiente de medir en la placa.
- **El firmware de la XIAO ya no ignora `servo`, `led` ni `buzzer`** (v0.12): los aplica. El estado que publica es completo.
- **El LED azul y la sesión (v0.12; retroalimentación temporal en v0.14).** La XIAO se suscribe a `aula/sesion/estado` (que es **retenido**) y enciende el LED azul mientras la sesión está activa. Ese retenido **sí se aplica**: es el estado de la sesión, no una orden, y su valor retenido es justo lo que hay que recuperar al reconectar. Con la sesión activa, un `led` `verde`, `rojo` o `ambar` se muestra **3 s** y después el LED vuelve al azul; un color nuevo dentro de esa ventana la reinicia con el color nuevo. `azul` y `apagado` no destellan: con la sesión activa el LED sigue azul. Cada cambio del color efectivo (al empezar el destello y al volver al azul) republica `aula/actuador/estado`, así que `led` en ese estado es el color que se ve en ese momento y puede ser transitorio. Al terminar la sesión el LED se **apaga** (`apagado`) y el color recordado se olvida, de modo que un color viejo no reaparece; si la sesión termina con una ventana abierta, ese color se ve hasta que venza y entonces el LED se apaga. Sin sesión, el color persiste hasta la siguiente orden.
- **Salud de la XIAO (v0.15):** el firmware publica `aula/camara/estado` (retenido) al conectar y **cada 30 s** (`kIntervaloLatidoMs` en `firmware/xiao/logica_proteccion.h`, enviado desde `xiao.ino`) con `en_linea`, `rssi`, `heap`, `temp`, `camara`, `corte` y `nvs`, más el LWT `{"en_linea":false}`. Está implementado en el firmware del repo; la versión grabada en la placa no se verificó desde el código.
- **Órdenes manuales (v0.15).** Node-RED publica en este tópico las órdenes del frontend que llegan a `POST /api/actuadores`: solo `servo` `"abrir"` (no hay `cerrar`; el servo se cierra solo a los 3 s), `led` con los cinco colores de la tabla, `buzzer` 0, 1 o 2 y `luz` 0 o 1, todos enteros o textos fijos. Una orden por segundo; la que llega antes responde 429 con `Retry-After`. El mensaje se rearma campo por campo, nunca reenvía el cuerpo recibido.

### `aula/actuador/estado` (retenido)
La XIAO lo publica cada vez que cambia un actuador.

```json
{"servo": "cerrado", "led": "azul", "luz": 0}
```

**Estado completo (v0.12).** Desde el 4 nov el firmware de la XIAO controla el servo, el LED RGB y el buzzer, así que el estado lleva **los tres campos** (`servo`, `led` y `luz`), como en el ejemplo. Antes era parcial (`{"luz": N}`), porque la XIAO solo controlaba la tira: esa desviación queda cerrada. Cada publicación retenida **reemplaza** a la anterior, así que el estado siempre completo. La XIAO lo publica al cambiar cualquier actuador, al (re)conectar y como respuesta a toda orden válida.

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
| `modo` | string | sí | `pase` o `prueba`. Node-RED **solo registra asistencia con `pase`**; con `prueba` no registra nada y entrega el resultado a la prueba de reconocimiento de la pestaña `Registro` (`POST /api/enrolamiento/probar` y `GET /api/enrolamiento/prueba`). Sin este campo, la prueba inmediata del registro marcaría asistencia. |
| `decision` | string | sí | `reconocido`, `dudoso` (zona gris → LED ámbar), `desconocido`, `sin_rostro`, `imagen_insuficiente`, `error` (fallo interno del servicio; extensión de este documento) |
| `cuenta` | string \| null | sí | Mejor candidato. `null` si `decision` es `sin_rostro`, `imagen_insuficiente` o `error`, y si la galería está vacía. Con `desconocido` y con `dudoso` se manda el mejor candidato para auditoría (tabla `intentos`). |
| `conf` | float \| null | sí | Similitud coseno del mejor candidato, 0 a 1. `null` si no hubo candidato. |
| `margen` | float \| null | no | Diferencia con el segundo candidato. `null` (o ausente) si la galería tiene menos de dos alumnos: no hay con quién comparar. Un margen chico indica que se puede confundir con otra persona. |
| `lat_ms` | object | sí | `cap`, `det`, `emb`, `total`, en ms. Etapas que no corrieron van en `null` |
| `motivo` | string | no | Detalle fino de la `decision`; extensión de este documento (el esquema SQL `intentos.motivo` lo necesita y el frontend lo muestra). `captura_fallida`, `imagen_insuficiente`, `sin_rostro`, `desconocido`, `galeria_vacia`, `error_interno`, `ocupado`, `cuenta_invalida`. Con `decision: "reconocido"` o `"dudoso"` no se manda. |
| `miniatura` | string | no | **(v0.17)** JPEG en base64 del rostro con que se reconoció al alumno: el mismo recorte, formato y topes que la `miniatura` de `aula/vision/enrolamiento` (como máximo 160×120 y unos 8 KB, que en base64 son unos 11 KB). **Solo la manda la visión cuando `modo` es `pase` y `decision` es `reconocido`**; en `prueba` y en cualquier otra decisión el campo no va. Es opcional también en ese caso: si la visión no la puede armar (el recorte se sale de la foto, no cabe en el tope o el codificador falla), el resultado sale igual y sin el campo, y la asistencia no depende de ella. **Es efímera, como la del registro:** Node-RED la guarda solo en memoria, asociada a la asistencia que ese resultado insertó, y solo mientras la sesión sigue abierta (se borra al cerrarla, al iniciar otra y al arrancar Node-RED (y con un despliegue completo o que reinicie la pestaña `Pase de lista`; un despliegue que no la toque las conserva, a propósito: quitaría las fotos de la clase en curso); la miniatura 101 de una sesión no se guarda). Sale al navegador por `GET /api/sesiones/:id` (campo `foto` de cada alumno), no se persiste (ni base de datos, ni archivos, ni logs, ni el CSV del reporte) y no viaja a la pantalla ni a la XIAO. La ACL no cambia. |

**El umbral lo aplica el servicio de visión**, porque es el dueño del modelo y del umbral calibrado con LFW (los umbrales `reconocido` y `dudoso` viven en su configuración; Node-RED decide sobre `decision` y no compara números). Lo que Node-RED sí decide son las reglas de sesión: lista del grupo, duplicado y retardo. Los umbrales actuales son provisionales: la calibración con LFW es el hito del 18 de noviembre.

**Consumo en Node-RED (v0.11; pestaña `Pase de lista`).** Se suscribe con QoS 1. Descarta, sin retroalimentación y con un aviso que nunca repite el contenido, el retain, el JSON inválido, los campos que no cumplan la tabla (`modo` distinto de `pase` y `prueba`; `decision` fuera de la lista; `cuenta` que no sea `null` ni 9 dígitos; `conf` que no sea `null` ni un número de -1 a 1, porque una similitud coseno sin parecido puede ser negativa; `lat_ms` que no sea un objeto; un `reconocido` sin `cuenta` o sin `conf`) y el payload vacío (en silencio). **`ts` es obligatorio** y debe estar a menos de 60 s del reloj de Node-RED, hacia atrás o hacia adelante: así cae un retenido viejo, una reentrega tardía y el `1970-01-01T00:00:00.000+00:00` de reserva de la visión (**afecta a la visión (Gustavo)**: sin una hora correcta, sus resultados no se registran). **Solo `modo: "pase"` registra asistencia;** `modo: "prueba"` no registra nada y sale por un enlace a la pestaña `Registro` (v0.15), donde solo cuenta si hay una prueba pedida con `POST /api/enrolamiento/probar` hace menos de 15 s y el resultado es posterior a la solicitud (con 2 s de margen). La sesión abierta y su tolerancia salen de la base; **sin sesión abierta no se registra ni se manda retroalimentación** (solo un aviso). Aparte de la asistencia, `lat_ms` de todo resultado válido (cualquier `modo` y `decision`, con o sin sesión) se guarda en `metricas_energia` con `origen = 'npu'` (v0.15), con su propio límite de 2 por segundo: los que llegan antes se descartan. Un valor fuera de rango solo omite esa fila; la asistencia del mismo resultado no cambia. La retroalimentación se publica en `aula/actuador/cmd` **después** de escribir en la base:

| `decision` | Resultado | `aula/actuador/cmd` | Base |
|---|---|---|---|
| `reconocido`, `cuenta` del grupo de la sesión y aún no registrada | `presente` si `now()` ≤ `inicio` + `tol_min` minutos (reloj de la base), `retardo` si no | `{"servo":"abrir","led":"verde","buzzer":1}` | `asistencias` (`metodo = 'rostro'`, `confianza = conf`) |
| `reconocido`, ya registrada en la sesión (también si llegan dos seguidos) | `duplicado` | `{"led":"rojo","buzzer":2}` | `intentos`, motivo `duplicado` |
| `reconocido`, `cuenta` que no es del grupo de la sesión, o sin consentimiento o sin rostro vigentes en la base (por ejemplo, un rostro retirado cuya `baja` la visión aún no procesó) | `fuera_de_grupo` | `{"led":"rojo","buzzer":2}` | `intentos`, motivo `desconocido` (el `CHECK` de `motivo` no admite otro y la tabla no guarda la cuenta) |
| `desconocido` | | `{"led":"rojo","buzzer":2}` | `intentos`, motivo `desconocido` |
| `imagen_insuficiente` | | `{"led":"ambar"}` (se repite la captura) | `intentos`, motivo `imagen_insuficiente` |
| `dudoso` | | `{"led":"ambar"}` | nada (el marcado manual, `marcar`, sigue como propuesta; el `CHECK` de `intentos.motivo` no tiene un motivo para la zona gris) |
| `sin_rostro`, `error` | | nada (casi siempre fue un disparo del PIR sin nadie enfrente) | nada; solo un aviso |

**Miniatura del pase (v0.17).** Node-RED toma `miniatura` solo de un `reconocido` de `modo: "pase"` (con otra decisión o con `prueba` la ignora sin copiarla) y la valida antes de usarla: JPEG en base64 con relleno (largo múltiplo de 4, empieza con `/9j/`) de 12 288 caracteres como máximo; una miniatura inválida se ignora con un aviso (que solo dice el tamaño, nunca el contenido, y sale a lo más una vez cada 10 s, con la cuenta de los suprimidos) y no descarta el resultado ni impide registrar la asistencia. El tope del payload completo pasa de 2048 a 16 384 caracteres para que quepa: los 12 288 de la miniatura más unos 4 KB de margen para el resto del mensaje, que real pesa unos 250 bytes. La guarda en el contexto global de Node-RED, en memoria (`miniaturas_pase`: `{ sesion_id, fotos: { <cuenta>: <base64> } }`, nunca en el almacén persistente), **solo cuando la sentencia devuelve `presente` o `retardo`**, es decir, cuando ese resultado acaba de insertar la asistencia: con `duplicado`, `fuera_de_grupo`, un intento (`desconocido`, `imagen_insuficiente`) o sin sesión abierta no se guarda. Tiene un tope de 100 miniaturas por sesión: **la miniatura 101 de una sesión no se guarda** (con aviso; la asistencia se registra igual y su fila queda con `foto: null`). Se borra al cerrar la sesión (`terminar` o el `iniciar` que cierra una olvidada), al iniciar otra y al arrancar Node-RED (y con un despliegue completo o que reinicie la pestaña `Pase de lista`; un despliegue que no la toque las conserva, a propósito: quitaría las fotos de la clase en curso), y también cuando la reconciliación con la base dice que no hay sesión abierta y devuelve la última cerrada (un cierre por otro camino), salvo que las fotos sean de una sesión posterior a esa. Node-RED recuerda el id de la última sesión cerrada: un resultado en vuelo de esa sesión (o de una anterior) que llegue después del cierre no vuelve a guardar fotos. **Pantalla y demás consumidores:** ninguno lee `aula/vision/resultado` aparte de Node-RED (por la ACL, la pantalla lee solo `aula/sesion/estado`, `aula/sesion/asistencia`, `aula/vision/enrolamiento`, `aula/actuador/estado` y `aula/pantalla/reinicio`, y la XIAO solo `aula/actuador/cmd` y `aula/sesion/estado`), así que el campo nuevo no les llega. Las herramientas de diagnóstico que escuchan ese tópico (`mqtt_probe` y `tools/humo.sh` de la visión) leen `decision`, `modo` y `motivo` y no se enteran del campo, y `mqtt_probe` enmascara `miniatura` al imprimir (`"miniatura":"<N caracteres>"`). **Tamaños:** el mensaje con miniatura pesa unos 11 KB, por debajo del `max_packet_size` del broker (256 KiB), del tope de 64 KiB de la capa JSON de la visión y del de 16 384 caracteres de Node-RED; el `max_inbox_events` de la visión limita los eventos que entran, no los resultados que salen; y el límite de 5 resultados por segundo del pase de lista acota las miniaturas por segundo, no cambia.

**Color en el LED (v0.14).** Los `led` de la tabla anterior (verde, rojo, ámbar) se ven 3 s con la sesión activa, que es el caso del pase de lista, y el LED vuelve al azul; ver la sección 3.

Un límite de entrada deja pasar a lo más 1 resultado cada 200 ms (5 por segundo) hacia la base y descarta los que lleguen antes (no los encola; la visión tarda unos 240 ms por ciclo, así que un resultado legítimo no se pierde): acota `intentos` a unas 430 000 filas al día (unos 43 MB). Si la escritura en la base falla no se publica retroalimentación: se registra el SQLSTATE y la persona puede volver a intentarlo. Cuando el resultado es `presente` o `retardo`, Node-RED publica además `aula/sesion/asistencia` (retenido, sección 5) para actualizar los conteos y el último registro en la pantalla ILI9341 (v0.13).

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
| `motivo` | string | no | Con `rechazada`: `sin_rostro`, `varios_rostros`, `rostro_chico`, `borrosa`, `giro_excesivo`, `pose_incorrecta` (la cabeza no está en la pose pedida; v0.16), `inconsistente` (la pose no se parece a la de `frente`, puede ser otra persona; v0.16, y la pose se repite), `fallo_captura`. Con `error`: `ocupado`, `inconsistente` (las fotos no se parecen entre sí, o se acumularon demasiados rechazos por identidad en el registro; v0.16), `npu` (un fallo real del pipeline: NPU, embedder o reloj; **ya no** significa "se agotaron las poses"), `cuenta_invalida` (la cuenta del comando no es válida; extensión de este documento), `duplicado` (el rostro final ya pertenece a otra cuenta de la galería; `parecido_a` lleva esa cuenta; v0.16), `galeria_no_disponible` (la galería en RAM no es de fiar —nunca cargó, la última recarga falló o no se pudo consultar—, así que no se puede saber si el rostro ya está registrado; el servicio falla cerrado, y como rechazo de una orden lleva `paso: 0`; v0.16). Un registro que se agota (demasiadas poses rechazadas, `max_attempts_per_pose`) se cierra con `estado: "error"` y **el `motivo` del último rechazo** (`sin_rostro`, `varios_rostros`, `rostro_chico`, `borrosa`, `giro_excesivo`, `pose_incorrecta`, `inconsistente` o `fallo_captura`; v0.16, antes era `npu`): por eso esos motivos también valen con `error`. |
| `parecido_a` | string | no | Con `error` y `motivo: "duplicado"` (v0.16): la cuenta de la galería a la que ya pertenece el rostro (coseno de la media final >= el umbral de duplicado del servicio, 0.45 por omisión y nunca mayor que el de "dudoso", sin contar la cuenta que se registra). **[propuesta]** Con `advertencia`: cuenta del alumno al que se parece. Nunca incluye embeddings. La advertencia es informativa (plan, sección 5.4, paso 6): la visión no espera confirmación y sigue con `completado` y la publicación del embedding. Si el profesor no está de acuerdo, aplica la baja del rostro. El servicio de visión todavía no emite `advertencia`. |
| `miniatura` | string | no | JPEG en base64, como máximo 160×120 y unos 8 KB. Solo con `aceptada` o `rechazada`. La pantalla la ignora. Es efímera: el servicio de visión la emite en el mensaje de esa pose, Node-RED la guarda solo en memoria mientras el registro está abierto y sale al navegador por `GET /api/enrolamiento/estado`. No se persiste (ni base de datos, ni archivos, ni logs) y no viaja a la pantalla. |

**Rechazo de `probar` (v0.15).** Si la visión está ocupada cuando llega `accion: "probar"`, responde en este tópico con `estado: "error"`, `motivo: "ocupado"`, `paso: 0`, `pose: ""` y `cuenta: ""`. Node-RED lo toma como resultado de la prueba de reconocimiento solo mientras hay una prueba pendiente; un `ocupado` de un `enrolar` lleva cuenta y paso, y no entra en la prueba (`Guardar resultado de la prueba`, pestaña `Registro`).

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

**Campos vacíos y modelo (v0.15).** La visión escribe `ts` como `""` si no tiene hora y `modelo` como `""` si no tiene modelo configurado (`orangepi/vision/src/json_payload.cpp`). Node-RED acepta esos valores: no guarda un `ts` vacío y sí guarda un `modelo` vacío. Un `modelo` con texto solo puede llevar ASCII imprimible (hasta 100 caracteres); con cualquier otro carácter, Node-RED descarta el estado completo (`Guardar estado de un dispositivo`, pestaña `Sistema`).

**Las dos formas del estado.** El Will que registra el broker lleva la forma **mínima**, `{"en_linea": false}`: no puede llevar `ts` ni el resto, porque el Will se registra al conectar, antes de que el servicio exista. El estado que publica el servicio, y el estado final de la salida ordenada, llevan el **esquema completo** (`ts`, `en_linea`, `modo`, `galeria` y `modelo`).

La forma mínima es válida para los consumidores: `en_linea` es el único campo obligatorio del estado y el resto es opcional cuando `en_linea` es `false`. Un consumidor que use la capa JSON del contrato (`orangepi/vision/include/vision/json_payload.hpp`) acepta las dos formas y no descarta el Will por faltar campos.

**Cuándo se publica.** El servicio publica `aula/vision/estado` en cada cambio de modo (`inactivo`, `pase`, `enrolando`), con el número de embeddings en RAM y el nombre del modelo. En la salida ordenada publica el estado final con `en_linea: false` **antes** de cerrar la conexión: la publicación es asíncrona y, como la desconexión es limpia, el Will no salta. Si el estado final no llegara al broker, el valor retenido se quedaría en `en_linea: true` para siempre (un "en línea" fantasma). Para eso el proceso vacía la cola de salida del bus antes de pararlo.

### `aula/vision/galeria/alta` y `aula/vision/galeria/baja`
La galería en RAM solo cambia por orden de Node-RED, que publica estos tópicos **después** de escribir en Postgres (plan, sección 5.4). La visión nunca agrega una entrada por su cuenta, ni siquiera tras publicar `aula/vision/embedding`. Así la base es la única fuente de verdad. Los dos van con QoS 1 y sin retención.

```json
{"cuenta": "320068234"}
```

- **`alta`:** Node-RED guardó el embedding de esa cuenta; también la publica para forzar una recarga de reconciliación cuando un retiro tardío (embedding guardado con el registro ya cerrado) no afectó ninguna fila, porque ya se había retirado o porque la cuenta tiene otro consentimiento. En ese caso la cuenta puede no tener rostro: la visión no debe suponer que existe. La visión recarga la galería completa con `GET /api/galeria` (unas 40 entradas). El mensaje no lleva el embedding: así no viaja otra vez por MQTT y la galería siempre sale de la base.
- **`baja`:** se retiró el consentimiento de esa cuenta. La visión borra su entrada de la galería en RAM.
- **Recarga completa** con `GET /api/galeria` también al arrancar y en cada reconexión al broker; cubre una `baja` perdida mientras la visión estaba desconectada.
- `GET /api/galeria` exige el encabezado `X-Galeria-Token` igual a `GALERIA_TOKEN`, que solo tienen Node-RED y la visión, y **no** acepta las credenciales HTTP Basic del profesor (el resto de `/api/*` sí las exige; Node-RED lo resuelve con un `httpNodeMiddleware` propio y no con `httpNodeAuth`, que bloquearía a la visión). Sin token o con uno erróneo, 401. Es de solo lectura (`GET`): la cabecera `X-Pase-Lista: 1`, que el resto de la API exige en todo método que cambia datos (defensa contra CSRF, v0.6), no afecta a la visión. Implementado desde v0.6, desplegado y verificado en la placa el 2 de noviembre de 2026 (plan, secciones 6.2 y 6.7).
- **Respuesta (200, `application/json`):** `{"embeddings":[{"cuenta":"320068234","embedding":[...]}]}`, un elemento por alumno con consentimiento y embedding no nulo, ordenado por cuenta; sin ninguno, `{"embeddings":[]}`. `cuenta` es el string de 9 dígitos y `embedding` es un arreglo de 512 números JSON. Es la primera de las formas que acepta `orangepi/vision/src/gallery_store.cpp`, así que la visión no necesita cambios.

## 5. Edge (Node-RED, pantalla, Orange Pi)

### `aula/sesion/cmd`
```json
{"accion": "iniciar", "materia": "FSE", "grupo": "1", "tol_min": 10}
{"accion": "terminar"}
{"accion": "marcar", "cuenta": "320068234"}
```
`marcar` **[propuesta]** es el registro manual del profesor desde la pantalla para la zona gris (paso 7 de la sección 5.1 del plan); se guarda con `metodo: "manual"`. **Node-RED todavía no lo implementa (v0.10): lo descarta con un aviso.**

**Validación en Node-RED (v0.10).** Se descartan, sin publicar nada, los mensajes con retain, con payload vacío, con JSON inválido o que no sean un objeto, y los de una `accion` que no sea `iniciar` ni `terminar` (en minúsculas). `iniciar` exige `materia` y `grupo` como texto no vacío de hasta 40 caracteres (cuentan los caracteres, no los bytes), sin caracteres de control ni invisibles (se normaliza a NFC y se recortan los espacios de los extremos), y `tol_min` como entero de 0 a 600; todos son obligatorios. Si el estado resultante no cupiera en el búfer de 256 bytes de la XIAO (sección 1), también se descarta: con materia y grupo ASCII de 40 caracteres cabe. **Un segundo `iniciar` válido que llegue a menos de 5 s del último aceptado se descarta** (con aviso en el log de Node-RED; lo mide con el reloj de la placa, así que un salto de reloj solo puede dejarlo pasar). El límite frena un goteo de órdenes y cubre la reentrega duplicada de un QoS 1; la pantalla ya pide doble toque para iniciar. `terminar` no tiene ese límite y ignora los campos de más. Quien publica (la pantalla) no recibe respuesta: el resultado se ve en `aula/sesion/estado`.

### `aula/sesion/estado` (retenido)
```json
{"activa": true, "id": 12, "materia": "FSE", "grupo": "1", "inicio": "…", "tol_min": 10}
{"activa": false, "id": 12}
```
Va retenido, con QoS 1, para que la XIAO y la pantalla recuperen el estado al reconectar (LED azul). Al publicarse `activa: false` se genera el reporte.

| Campo | Tipo | Notas |
|---|---|---|
| `activa` | bool | `true` con una sesión abierta |
| `id` | int | `sesiones.id`. Con `activa: false` es el de la última sesión cerrada; se omite si la base no tiene ninguna |
| `materia`, `grupo` | string | Solo con `activa: true` |
| `inicio` | string | Solo con `activa: true`. ISO 8601 en UTC con desfase explícito, sin fracción de segundo (`"2026-11-04T15:10:00+00:00"`) |
| `tol_min` | int | Solo con `activa: true`. Tolerancia en minutos (`sesiones.tolerancia_min`) |

Node-RED lo publica **después** de escribir en la base, así que el retenido nunca adelanta a la base. Se publica `{"activa": true, ...}` al iniciar y `{"activa": false, "id": N}` al terminar (el reporte se genera de la sesión cerrada `N`). Si `iniciar` cierra una sesión olvidada, también se genera el reporte de esa. **`terminar` sin sesión abierta** no cambia la base ni genera reporte; Node-RED lee el estado real de la base y lo vuelve a publicar (inactivo con el `id` de la última sesión), lo que corrige un retenido viejo. Si una escritura falla (por ejemplo, dos `iniciar` simultáneos que violan el índice de una sola sesión abierta), no se publica el estado que se esperaba: se publica el que dice la base. **Reconciliación:** en cada conexión de Node-RED al broker (al arrancar, al desplegar y al reconectar, por ejemplo tras reiniciar Mosquitto) y mientras la consulta falle (cada 15 s), Node-RED publica el estado de la base: la sesión abierta, o el inactivo.

### `aula/sesion/asistencia` (retenido)
```json
{"activa": true, "id": 12, "presentes": 5, "retardos": 1, "faltantes": 4, "ultimo": {"nombre": "Ana López", "estado": "presente"}}
{"activa": false}
```
Resumen en vivo de la asistencia de la sesión abierta para las vistas **Sesión** y **Lista** de la pantalla ILI9341 (v0.13). Va en un tópico aparte de `aula/sesion/estado` para no comprometer el búfer de 256 bytes de PubSubClient en la XIAO, que no se suscribe a este tópico. QoS 1 y retenido: al conectar o reiniciar, la pantalla recupera el estado actual.

| Campo | Tipo | Notas |
|---|---|---|
| `activa` | bool | `true` si hay sesión abierta; `false` si no |
| `id` | int | Solo con `activa: true`: `sesiones.id` |
| `presentes`, `retardos`, `faltantes` | int | Solo con `activa: true`: enteros `>= 0` sobre el grupo de la sesión |
| `ultimo` | object \| null | Solo con `activa: true`: `{"nombre": string, "estado": "presente" \| "retardo"}` del último registro de esa sesión, o `null` si aún no ha registrado nadie. Nunca lleva número de cuenta ni embeddings |

Node-RED lo publica **después** de escribir en la base: en `0 / 0 / <total>` con `ultimo: null` al iniciar sesión, actualizado tras cada `presente` o `retardo` en `Pase de lista`, `{"activa": false}` al terminar sesión, y reconciliado junto con `aula/sesion/estado` en cada conexión al broker.

### `aula/pantalla/reinicio`
```json
{"reiniciar": 1}
```
QoS 1, sin retain. Lo publica Node-RED a mano, con el nodo `inject` "Reiniciar pantalla" del editor (pestaña `Sesiones`); no hay API. La pantalla descarta todo reinicio con retain (sección 1).

### `aula/sistema/metricas`
Cada 10 s, QoS 0, sin retain. Lo publica la pestaña `Sistema` de Node-RED (usuario `nodered`, que ya tiene escritura en `aula/#`) con la temperatura, la CPU y la RAM de la placa (flujo implementado en v0.15). Node-RED no lo suscribe. Condición futura: si lo publica otro proceso del host, ese proceso necesita un usuario MQTT propio, y eso es un cambio de este contrato (sección 6) y de la ACL.
```json
{"ts": "…", "temp": 52.1, "cpu": 34, "ram": 61}
```
`temp` en °C del SoC, con un decimal: la zona térmica más caliente de `/sys/class/thermal`. `cpu` y `ram` en porcentaje, enteros. `cpu` sale de la diferencia de `/proc/stat` entre dos ciclos, así que la primera publicación llega en el segundo ciclo. Si falta una de las tres lecturas, no se publica el ciclo.

## 6. Control de acceso (ACL de Mosquitto)

La aplica `orangepi/mosquitto/acl` (versionado, sin secretos) con `acl_file`, y los usuarios salen del archivo de contraseñas que genera el entrypoint del broker. Los nombres de usuario son fijos; cada uno tiene su propia contraseña en `orangepi/.env` (`MQTT_<USUARIO>_PASSWORD`).

| Usuario | Lee | Escribe |
|---|---|---|
| `xiao` | `aula/actuador/cmd`, `aula/sesion/estado` | `aula/camara/pir`, `aula/camara/energia`, `aula/camara/estado`, `aula/actuador/estado` |
| `vision` | `aula/camara/pir`, `aula/vision/cmd`, `aula/vision/galeria/#` | `aula/vision/resultado`, `aula/vision/enrolamiento`, `aula/vision/embedding`, `aula/vision/estado`, `aula/actuador/cmd` |
| `nodered` | `aula/#` | `aula/#` |
| `pantalla` | `aula/sesion/estado`, `aula/sesion/asistencia`, `aula/vision/enrolamiento`, `aula/actuador/estado`, `aula/pantalla/reinicio` | `aula/sesion/cmd`, `aula/actuador/cmd` |
| `simulador` | nada | `aula/camara/energia` |

Notas:

- **No hay usuario `frontend`.** El navegador no habla MQTT (decisión de Yordi, v0.5): todo pasa por la API HTTP de Node-RED, que publica en `aula/actuador/cmd` y `aula/sesion/cmd` con el usuario `nodered`. Eso deja un solo punto de control y de registro de las órdenes manuales, y resuelve el **[decidir]** de v0.4. El progreso en vivo del registro llega al frontend por sondeo: Node-RED guarda el último estado de `aula/vision/enrolamiento` y el frontend consulta `GET /api/enrolamiento/estado` (decidido en v0.7; los endpoints están en el plan, sección 6.7, no en este contrato). Desde v0.15 Node-RED también publica en nombre del frontend las órdenes manuales a la XIAO (`POST /api/actuadores`, en `aula/actuador/cmd`) y la prueba de reconocimiento (`POST /api/enrolamiento/probar`, que publica `{"accion":"probar"}` en `aula/vision/cmd`).
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
| v0.12 | La XIAO ya tiene el firmware completo del nodo de campo: tira LED, cámara con endpoint `/capture` firmado, PIR, servo, LED RGB y buzzer. **Sin cambios en los tópicos, la ACL ni los nombres de los campos**; lo que cambia es qué hace la XIAO con ellos: (1) `servo`, `led` y `buzzer` ya no se ignoran y el estado de `aula/actuador/estado` pasa de parcial (`{"luz": N}`) a completo (`{"servo", "led", "luz"}`), lo que cierra la desviación 3 anotada en v0.8; (2) la XIAO se suscribe a `aula/sesion/estado` (retenido) y enciende el LED azul con la sesión activa, y ese azul manda sobre el color de retroalimentación mientras dure; (3) el estado del buzzer queda aclarado: `0` = silencio (la semántica de `1` y `2` ya la fijó la v0.11). La firma HMAC-SHA256 de la petición de captura **no es un cambio de este contrato**: es HTTP, entre la XIAO y el servicio de visión (plan, sección 6.1) |
| v0.8 | La XIAO ya tiene firmware de la tira LED (`firmware/xiao`; Yordi, con el acuerdo de Gustavo). **Sin cambios en los tópicos, la ACL ni la forma de los payloads de la tabla.** Se documentan sus límites: (1) la XIAO publica `aula/actuador/estado` y `aula/camara/estado` en vivo con QoS 0 por la limitación de PubSubClient 2.8 (el LWT va con QoS 1); (2) el estado es parcial, `{"luz": N}`, hasta que entren el servo y los LEDs; (3) descarta los mensajes de `aula/actuador/cmd` que llegan en los 1500 ms siguientes a suscribirse, porque la biblioteca no expone la bandera retain; (4) la tira se apaga sola a los 360 s del último `luz:1` (cubre con margen el vencimiento de 5 min del registro; la visión publica `luz:1` una sola vez) y también al perder el enlace con el broker; (5) hasta que entre el firmware de Gustavo, `aula/camara/estado` se publica solo al conectar, sin latido de 30 s ni `rssi`, `heap` ni `temp`. **Afecta al carril de Gustavo (visión y orquestación luz–captura):** no asumir que `luz:1` se aplicó y reenviarlo antes de 360 s si hace falta más tiempo |
| v0.9 | **Sin cambios en los tópicos, la ACL ni la forma de los payloads.** Cambia la semántica de `aula/vision/galeria/alta` (sección 4): además de "se guardó el embedding de esa cuenta", Node-RED la publica tras un retiro tardío que no afectó ninguna fila, para que la visión recargue la galería completa desde la base y quede igual que ella (si el rostro se retiró, desaparece; si hay un registro posterior válido, se conserva). La visión ya recarga con cualquier `alta` y no necesita cambios (`gallery_store.cpp`, `apply_add`). Con una fila afectada se sigue publicando `baja`. Se corrige además el estado de `GET /api/galeria` con token: desplegado y verificado en la placa el 2 de noviembre de 2026. |
| v0.10 | **Sin cambios en los tópicos, la ACL ni la forma de los payloads de la tabla.** Node-RED implementa las sesiones (pestaña `Sesiones`) y precisa el contrato de la sección 5: (1) `aula/sesion/cmd` solo trae `iniciar` y `terminar`; `marcar` sigue como propuesta y se descarta con un aviso; la validación de `iniciar` (`materia` y `grupo` de 1 a 40 caracteres sin caracteres de control, `tol_min` entero de 0 a 600, todo obligatorio, y que el estado quepa en el búfer de 256 bytes de la XIAO); (2) la forma de `aula/sesion/estado` (`materia`, `grupo`, `inicio` y `tol_min` con la sesión activa; `id` de la última cerrada con la inactiva, omitido si no hay ninguna; `inicio` en UTC con `+00:00`); (3) `terminar` sin sesión abierta no genera reporte y republica el estado real; (4) la reconciliación de `aula/sesion/estado` con la base en cada conexión al broker; (5) un segundo `iniciar` válido a menos de 5 s del último aceptado se descarta; (6) `aula/pantalla/reinicio` lo publica un `inject` del editor, con QoS 1 y sin retain. Los consumidores de `aula/pantalla/reinicio` y `aula/sesion/cmd` ya descartaban lo retenido (sección 1); Node-RED descarta retain y lo vacío en `aula/sesion/cmd` |
| v0.11 | **Sin cambios en los tópicos, la ACL ni la forma de los payloads de la tabla.** Node-RED implementa el pase de lista (pestaña `Pase de lista`): consume `aula/vision/resultado` y publica la retroalimentación en `aula/actuador/cmd` (sección 4). (1) Se cierra el **[decidir]** de `buzzer`: `1` = pitido corto de éxito, `2` = doble pitido de error. (2) `ts` es obligatorio en `aula/vision/resultado` y debe estar a menos de 60 s del reloj de Node-RED, hacia atrás o hacia adelante: con el `1970` de reserva de la visión el resultado se descarta (**afecta a la visión (Gustavo)**). (3) Solo `modo: "pase"` registra asistencia; `modo: "prueba"` se ignora (el reenvío al frontend queda pendiente). (4) La retroalimentación sigue la tabla de la sección 4; sin sesión abierta no se registra ni se manda nada. (5) Node-RED descarta retain y lo vacío también en `aula/vision/resultado`, y la limpieza de retenidos agrega ese tópico a su lista (el mensaje vacío en vivo que provoca en cada conexión lo descarta la validación, en silencio; ningún otro consumidor lee ese tópico). (6) `reconocido` exige que la cuenta tenga consentimiento y rostro vigentes en la base, además de ser del grupo de la sesión. (7) Límite de entrada de 5 resultados por segundo (200 ms entre mensajes), con descarte. La lista y el "último registro" de la pantalla quedan pendientes: necesitan un tópico nuevo y un cambio de ACL |
| v0.13 | Se añade `aula/sesion/asistencia` (QoS 1, retenido; sección 5) para alimentar los conteos (`presentes`, `retardos`, `faltantes`) y el `ultimo` registro (`nombre`, `estado`) en las vistas **Sesión** y **Lista** de la pantalla ILI9341, sin tocar `aula/sesion/estado` ni el búfer de 256 bytes de la XIAO. En la ACL (`orangepi/mosquitto/acl`), `pantalla` recibe `topic read aula/sesion/asistencia`. Node-RED lo publica al iniciar, cerrar o reconciliar sesión y en cada `presente` o `retardo` del pase de lista |
| v0.14 | **Sin cambios en los tópicos, la ACL ni los nombres de los campos.** Cambia qué hace la XIAO con `led` mientras la sesión está activa: el azul de la sesión ya no manda sobre la retroalimentación durante toda la sesión, sino que un `led` `verde`, `rojo` o `ambar` se muestra 3 s y luego el LED vuelve al azul (un color nuevo reinicia la ventana; `azul` y `apagado` no destellan). `aula/actuador/estado` se republica al empezar el destello y al volver al azul, y su `led` es el color efectivo, que puede ser transitorio. Al terminar la sesión el LED se apaga (antes mostraba el último color de retroalimentación); si termina con una ventana abierta, ese color se ve hasta que venza y luego se apaga. `aula/actuador/estado` también se republica en esos cambios. Sin sesión, un `led` se aplica y persiste como antes. Corrige lo dicho en v0.12 (azul sobre la retroalimentación durante toda la sesión y regreso al último color al terminar) |
| v0.15 | **Sin cambios en los tópicos, la ACL ni la forma de los campos de la tabla de la sección 2.** Lo que cambia es lo que hacen Node-RED, la visión y la XIAO: (1) `lat_ms` de todo resultado válido se guarda en `metricas_energia` con `origen = 'npu'` (sección 4); (2) `modo: "prueba"` sale a la pestaña `Registro` (`POST /api/enrolamiento/probar`, `GET /api/enrolamiento/prueba`) en vez de ignorarse; (3) el rechazo `ocupado` de `probar` (`cuenta` vacía, `paso` 0) cuenta como resultado de la prueba (sección 4); (4) `aula/sistema/metricas` lo publica la pestaña `Sistema` de Node-RED cada 10 s, QoS 0 y sin retain (sección 5), y ya no queda pendiente el descarte de retain de ese tópico; (5) `aula/actuador/cmd` recibe las órdenes manuales del frontend (`POST /api/actuadores`, una por segundo); (6) `aula/camara/estado` lleva `camara`, `corte` y `nvs`, y el latido de 30 s está implementado en el firmware del repo (sección 3); (7) la visión escribe `ts` y `modelo` vacíos cuando no tiene hora o modelo, y `modelo` solo puede llevar ASCII imprimible (sección 4) |
| v0.16 | **Sin cambios en los tópicos ni en la ACL; amplía el vocabulario de `motivo` de `aula/vision/enrolamiento`** (sección 4). El servicio de visión verifica en el registro la pose y la identidad de cada captura: (1) motivo nuevo `pose_incorrecta` con `rechazada`: la cabeza no está en la pose pedida (giro e inclinación estimados con los 5 landmarks del detector); si el giro es exagerado se sigue usando `giro_excesivo`; (2) `varios_rostros` con `rechazada` cuando, además del principal (el de mayor área), hay otro rostro que no es claramente pequeño ni de score bajo; (3) `inconsistente` vale también con `rechazada`: cada pose después de `frente` se compara con la pose `frente` aceptada, y si no se parece la pose se rechaza y se repite (cuenta para los intentos; **no aborta el registro**); con `error` sigue significando que las fotos no se parecen al cerrar el registro. (4) **duplicado:** al cerrar el registro, la visión busca la media final en la galería (sin la cuenta que se registra); si otra cuenta da coseno >= el umbral de duplicado (0.45 por omisión, nunca mayor que el de "dudoso"), cierra con `error`, `motivo: "duplicado"` y `parecido_a` con esa cuenta, sin publicar el embedding (la galería **falla cerrado**: si nunca cargó, la última recarga falló o no se pudo consultar, el registro se rechaza con `error` y `galeria_no_disponible`; una galería vacía tras una carga correcta es válida); (5) **tope de identidad:** al tercer rechazo `inconsistente` del registro (configurable), el registro termina con `error` e `inconsistente`; (6) **motivo final:** al agotar los intentos de una pose, el `error` lleva el motivo del último rechazo en vez de `npu`, que queda solo para fallos reales del pipeline. Quien consuma `motivo` debe tolerar los valores nuevos: Node-RED ya los pasa tal cual (solo exige minúsculas y guion bajo) y la API los entrega en `poses[].motivo`. **Afecta a la pantalla ILI9341 y al frontend**: la pantalla y el frontend deben tener texto para los motivos nuevos y para los de rechazo cuando llegan con `error` (Node-RED no asume `npu` al agotarse: copia el motivo tal cual y cierra igual que con cualquier `error`) |
| v0.17 | **Sin cambios en los tópicos ni en la ACL; campo nuevo y opcional en `aula/vision/resultado`** (sección 4): `miniatura`, JPEG en base64 del rostro con que se reconoció al alumno (mismo recorte, formato y topes que la de `aula/vision/enrolamiento`: como máximo 160×120 y unos 8 KB). La visión la manda **solo** con `modo: "pase"` y `decision: "reconocido"`, y si no la puede armar el resultado sale sin ella. Es **efímera, como la del registro**: Node-RED la guarda solo en memoria (contexto global, nunca la base, archivos ni logs), asociada a la asistencia que ese resultado insertó (`presente` o `retardo`), y solo mientras la sesión está abierta (se borra al cerrarla, al iniciar otra y al arrancar Node-RED (y con un despliegue completo o que reinicie la pestaña `Pase de lista`; un despliegue que no la toque las conserva, a propósito: quitaría las fotos de la clase en curso); tope de 100: la 101 no se guarda); la vista Asistencia la recibe por `GET /api/sesiones/:id` (campo `foto` de cada alumno; `null` en sesiones cerradas y sin foto). El tope del payload de `aula/vision/resultado` en Node-RED pasa de 2048 a 16 384 caracteres. Regla de privacidad: las fotos del pase de lista ahora también pasan por el navegador, efímeras y solo con la sesión abierta. Los consumidores que ignoran los campos desconocidos (sección 1) no cambian; ninguno aparte de Node-RED lee ese tópico. **Afecta a la visión (Gustavo)** (nuevo campo opcional) |
