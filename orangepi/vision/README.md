# Servicio de visión (C++, nativo)

Servicio de visión del plan (`docs/HIGH-LEVEL-PLAN.md`, secciones 5.1, 5.4, 6.2 y 7.1). Sigue siendo agnóstico a dos decisiones: el detector y el reconocedor (y la dimensión de su embedding), que viven detrás de interfaces y solo tienen dobles en memoria. Las librerías de MQTT y HTTP ya no son una de esas decisiones: hay adaptadores reales (libmosquitto y cpp-httplib) detrás de las mismas interfaces. El contrato MQTT (`docs/contrato-mqtt.md`) está codificado en los payloads tipados y en la capa JSON.

## Estructura

```
CMakeLists.txt
include/vision/
  mqtt_contract.hpp   ÚNICO lugar con tópicos (contrato v0.3) y payloads tipados
  json_payload.hpp    capa JSON del contrato (nlohmann/json): payload <-> documento; ignora campos desconocidos y falla sin lanzar ante campos obligatorios ausentes o con tipo equivocado
  wall_clock.hpp      WallClock inyectable: la hora ISO 8601 con zona del "ts"; el Clock existente es monótono y no lleva fecha
  message_bus.hpp     interfaz MessageBus (publish/subscribe)
  http_client.hpp     interfaz HttpClient (GET -> status + bytes)
  mqtt_bus.hpp        MosquittoBus: MessageBus real sobre libmosquitto
  httplib_client.hpp  HttplibClient: HttpClient real sobre cpp-httplib
  gallery_store.hpp   GalleryStore: la galería en RAM alimentada por el borde
  face_detector.hpp   interfaz FaceDetector (imagen -> bbox, score, 5 landmarks)
  face_embedder.hpp   interfaz FaceEmbedder (recorte 112x112 -> vector<float>)
  clock.hpp           Clock inyectable (now / sleep_for)
  alignment.hpp       alineación por similitud a 112x112 (Umeyama, referencia ArcFace)
  image_header.hpp    formato y dimensiones de un JPEG (solo baseline) o PNG leídas de la cabecera, sin decodificar
  sharpness.hpp       varianza del Laplaciano y elección de la foto más nítida
  gallery.hpp         galería en RAM: alta/reemplazo/baja, coseno top-1 y margen
  vision_service.hpp  máquina de estados: reposo, pase de lista, registro
  tensor.hpp          TensorInfo y dequantize (sin dependencia de la NPU)
  npu.hpp             wrapper RAII de VIPLite: Driver y Network
  fakes.hpp           dobles en memoria: bus, HTTP, detector, embedder, relojes
src/
  json_payload.cpp    implementación de la capa JSON
  wall_clock.cpp      implementación del reloj de pared
  mqtt_bus.cpp        implementación del bus MQTT
  httplib_client.cpp  implementación del cliente HTTP
  gallery_store.cpp   implementación del store de la galería
  main.cpp            recorrido en seco con los dobles (no es el binario de producción)
apps/npu_smoke.cpp    carga un .nb, corre N inferencias y mide
tests/
  minitest.hpp             mini-runner propio
  test_json_payload.cpp    la capa JSON del contrato
  test_httplib_client.cpp  el cliente HTTP (servidor httplib en el propio proceso)
  test_gallery_store.cpp   la galería en RAM
  test_mqtt_bus.cpp        el bus MQTT (integración contra un broker real)
```

### Las tres bibliotecas

| Biblioteca | Qué lleva | Dependencias |
|---|---|---|
| `vision_core` | La lógica pura: máquina de estados, galería, alineación, cabecera de imagen, nitidez, tensores, capa JSON y `WallClock`. Sin red. | OpenCV, nlohmann/json |
| `vision_npu` | El wrapper RAII de VIPLite (`npu::Driver`, `npu::Network`). | `vision_core`, runtime del fabricante |
| `vision_transport` | Los tres adaptadores reales: `MosquittoBus`, `HttplibClient` y `GalleryStore`. | `vision_core`, libmosquitto, cpp-httplib, nlohmann/json, pthread |

`vision_transport` está aparte a propósito: `vision_core` se queda sin dependencias de red, para que las pruebas del núcleo sigan siendo rápidas y no dependan de que haya un broker o un servidor HTTP en la máquina. Los adaptadores implementan las interfaces (`MessageBus`, `HttpClient`) que el núcleo consume, así que el servicio no sabe si detrás hay un bus real o un doble en memoria.

## Compilar

Requisitos en la placa:

| Paquete | Para qué | Cómo se encuentra |
|---|---|---|
| g++ 12 (C++20) | compilador | `g++ --version` |
| CMake >= 3.16 | build | `cmake --version` |
| `libopencv-dev` 4.6 | decodificación de imagen, preproceso | `pkg-config opencv4` |
| `nlohmann-json3-dev` 3.11 | capa JSON del contrato MQTT | `find_package(nlohmann_json 3.11)` |
| `libmosquitto-dev` | bus MQTT (`MosquittoBus`) | `pkg-config libmosquitto` |
| `libcpp-httplib-dev` | cliente HTTP (`HttplibClient`), cabecera única `httplib.h` | `pkg-config cpp-httplib` |
| runtime VIPLite | `npu::Driver` y `npu::Network` | `/usr/include/vip_lite.h`, `/usr/lib/libNBGlinker.so`, `/usr/lib/libVIPhal.so` |

```
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

Las `libopencv_*.so.407` sueltas de `/usr/lib` no se usan; el enlace usa `-Wl,--as-needed` y se puede comprobar con `ldd build/vision_service | grep opencv` (todo debe ser `.so.406`).

## Pruebas

La suite se compila y se lanza con los comandos de la sección anterior (`cmake -S . -B build`, `cmake --build build -j4` y `ctest --test-dir build --output-on-failure`).

Sin broker, la prueba de integración del bus MQTT se omite con un mensaje claro y no falla. Para ejecutarla hace falta un broker de pruebas propio, con su propio archivo de configuración y su propio puerto (1884), sin tocar los contenedores del proyecto `pase-lista` (el broker real corre en el 1883).

Configuración del broker de pruebas, en `/tmp/mosquitto-vision-test.conf`:

```
listener 1884
allow_anonymous true
```

Levantar el broker de pruebas:

```
docker run --rm -d --name vision-mosquitto-test -p 127.0.0.1:1884:1884 -v /tmp/mosquitto-vision-test.conf:/mosquitto/config/mosquitto.conf:ro eclipse-mosquitto:2
```

Ejecutar la suite contra ese broker:

```
VISION_BROKER=127.0.0.1:1884 ctest --test-dir build --output-on-failure
```

La dirección también se puede fijar al configurar (`-DVISION_BROKER=host:puerto`). La prueba lleva la etiqueta `integration`, así que se puede dejar fuera con:

```
ctest -LE integration
```

Al terminar, el contenedor se borra con:

```
docker rm -f vision-mosquitto-test
```

Las pruebas `npu` y `npu_smoke_vendor_model` necesitan `/dev/vipcore` (y los archivos de `/opt/vpm_run`): solo se registran si existe, llevan la etiqueta `board` y se pueden elegir o excluir con `ctest -L board` y `ctest -LE board`. Si el runtime VIPLite no está en `/usr`, se indica con `-DVIPLITE_ROOT=<prefijo>` (o la variable de entorno del mismo nombre). La configuración exige OpenCV 4.6.x.

## Probar la NPU

```
build/npu_smoke <modelo.nb> <entrada.dat> [iteraciones=100]
build/npu_smoke /opt/vpm_run/network_binary.nb /opt/vpm_run/input_0.dat 200
```

Imprime los tensores (forma `w x h x c x n`, formato, cuantización) y el tiempo promedio de copia de entrada, ejecución y lectura de salida. El archivo de entrada debe medir exactamente lo que ocupa el tensor. Los `.nb` no se versionan (plan, sección 11).

## Recorrido en seco

`build/vision_service` es un arnés de recorrido en seco sobre los dobles, no el binario de producción: cablea la máquina de estados real con los dobles en memoria y reproduce un pase de lista y un registro. No hay broker, cámara ni modelos. Los adaptadores reales ya existen (`MosquittoBus`, `HttplibClient`, `GalleryStore`), pero el `main.cpp` sigue cableando los dobles: lo que falta es el proceso de producción (bucle, configuración, señales) y el detector y el embedder sobre la NPU.

## El contrato y la capa JSON

Los payloads tipados de `include/vision/mqtt_contract.hpp` son el único lugar con tópicos y claves. `include/vision/json_payload.hpp` los convierte a JSON y de vuelta, con las reglas del contrato (`docs/contrato-mqtt.md`, sección 1):

- los campos desconocidos se ignoran;
- un campo obligatorio que falta, o que tiene el tipo equivocado, hace fallar la conversión: `deserialize` devuelve un error (`kMissingField`, `kWrongType`, `kInvalidValue`, `kInvalidJson`) y el nombre del campo, nunca el contenido del mensaje;
- `cuenta` es siempre un string (nunca un número: se perderían los ceros a la izquierda), `conf` y `margen` son flotantes, `lat_ms` son enteros o `null` si la etapa no corrió, y `embedding` es un arreglo de flotantes.

El servicio estampa `ts` (ISO 8601 con zona) en los cuatro mensajes que publica: `resultado`, `enrolamiento`, `embedding` y `estado`. La hora la da el `WallClock` inyectado (`SystemWallClock` usa `system_clock` y `localtime_r`); el `Clock` de milisegundos no lleva fecha.

`aula/vision/estado` lo publica el servicio en cada cambio de modo (`inactivo`, `pase`, `enrolando`), con el número de embeddings en RAM y el nombre del modelo. El LWT `{"en_linea": false}` lo pone el adaptador MQTT, no el servicio.

## El bus MQTT

`MosquittoBus` (`include/vision/mqtt_bus.hpp`, `src/mqtt_bus.cpp`) es el `MessageBus` real sobre libmosquitto. Habla con el broker del borde (Mosquitto en la Orange Pi) y reparte los mensajes del contrato al hilo de red de libmosquitto, que es distinto del que publica.

### Tópicos

Según la sección 2 de `docs/contrato-mqtt.md`:

| Tópico | QoS | Retenido | Sentido |
|---|---|---|---|
| `aula/vision/resultado` | 1 | no | publica |
| `aula/vision/enrolamiento` | 0 | no | publica |
| `aula/vision/embedding` | 1 | no | publica |
| `aula/vision/estado` | 1 | sí | publica |
| `aula/actuador/cmd` | 1 | no | publica (solo el campo `luz`) |
| `aula/camara/pir` | 0 | no | consume |
| `aula/vision/cmd` | 1 | no | consume |
| `aula/vision/galeria/alta` | 1 | no | consume |
| `aula/vision/galeria/baja` | 1 | no | consume |

El QoS y la retención los aplica el propio bus, con las reglas de `find_topic_rule`. La ACL del contrato (sección 6 de `docs/contrato-mqtt.md`) da al usuario `vision` exactamente esos permisos: lee `aula/camara/pir`, `aula/vision/cmd` y `aula/vision/galeria/#`, y escribe `aula/vision/resultado`, `aula/vision/enrolamiento`, `aula/vision/embedding`, `aula/vision/estado` y `aula/actuador/cmd`. Las ACL del broker las escribe el carril del borde, no este servicio.

### Will

Al caer la conexión sin cerrar limpio, el broker publica `{"en_linea": false}` en `aula/vision/estado`, retenido y con QoS 1. Es el LWT que registra `MosquittoOptions` por omisión. El frontend ve así que la visión ya no está en línea.

### `mosquitto_unsubscribe` no se llama nunca

El bus no llama nunca a `mosquitto_unsubscribe`. La baja de una suscripción se hace solo en memoria, sin tocar el broker. El motivo es medido: llamar a `mosquitto_unsubscribe` desde un hilo distinto del bucle de red provoca un ABBA entre los mutex internos de libmosquitto y cuelga el proceso. En la placa se midió 10 de 10 colgones con la llamada y 0 de 10 sin ella.

El costo es tráfico inútil hasta la desconexión: el broker sigue mandando mensajes a un tópico que ya no se consume. Como la sesión es limpia, al desconectar el broker olvida las suscripciones. `resubscribe_all()` vuelve a suscribir los tópicos vivos en cada reconexión.

## Entradas no fiables

Lo que llega por MQTT y por HTTP se trata como no fiable (ver `ServiceConfig`):

- Cola de eventos acotada (`max_inbox_events`, 16, mínimo 4). Lo que llega con la cola llena se descarta sin responder y se deja una línea en stderr por poll, sin el contenido. Un PIR, un `enrolar` y un `probar` en cola cuentan uno cada uno; un `enrolar` con cuenta inválida se descarta antes de ocupar lugar (Node-RED valida al alumno antes de publicar, plan 5.4 paso 2); un lugar queda reservado para `cancelar`.
- `no_cuenta` de exactamente 9 dígitos ASCII (`is_valid_account`); `enrolar` con otra cosa se rechaza con `cuenta_invalida`, y `Gallery::upsert` la rechaza con `std::invalid_argument`. Los rechazos de órdenes (`ocupado`, `cuenta_invalida`) salen con estado `error`, paso 0 y sin pose; un registro en curso que falla sale con estado `error` y motivo `npu` (o `inconsistente` si las poses dan embeddings de distinta dimensión).
- Fotos: el adaptador HTTP debe cortar la transferencia al pasar `max_body_bytes` y respetar el plazo de toda la petición (`HttpClient::get`). El servicio vuelve a comprobar el tamaño y lee el formato y las dimensiones de la cabecera antes de decodificar; solo acepta JPEG baseline o secuencial extendido de 8 bits con un único scan entrelazado (nunca progresivo, aritmético ni de varios scans, que se pueden construir para gastar CPU al decodificar) y PNG solo con `accept_png` (pruebas).

## El cliente HTTP

`HttplibClient` (`include/vision/httplib_client.hpp`, `src/httplib_client.cpp`) es el `HttpClient` real sobre cpp-httplib. `get` nunca lanza: cualquier fallo de red, un estado distinto de 200 o una excepción de la biblioteca devuelven status 0 y cuerpo vacío. El plazo de `get` es el de la petición entera (conexión, cabeceras y cuerpo), no el de una operación: cpp-httplib solo da plazos por operación, así que el adaptador acota el plazo de socket y además vigila el reloj desde otro hilo; en cuanto se pasa el plazo total, `get` devuelve status 0. Tampoco sigue redirecciones: una redirección a otro host sería una fuga de la petición. Acepta cabeceras adicionales (`Nombre: valor`), que es como viaja el token de la galería.

### Certificado autofirmado

La comunicación con Node-RED por HTTPS usa un certificado autofirmado. `HttpClientOptions::allow_invalid_certificates` viene en `false` por omisión: no se acepta ningún certificado inválido. Hay que activarlo a mano (`allow_invalid_certificates = true`) hasta que haya un certificado válido. Es un riesgo conocido y aceptado, no un olvido: con el certificado autofirmado tal cual, la conexión se rechaza.

## La galería en RAM

`GalleryStore` (`include/vision/gallery_store.hpp`, `src/gallery_store.cpp`) mantiene la galería en RAM alimentada por el borde.

- **Cabecera `X-Galeria-Token`.** Desde el 11 de noviembre, `GET /api/galeria` exige `httpNodeAuth` y ese encabezado (`GALERIA_TOKEN`, plan, secciones 6.2 y 6.7). El token viene de la configuración del servicio, nunca del repo: no se versiona en ningún archivo de este checkout.
- **Alta.** `aula/vision/galeria/alta` solo trae la cuenta, no el embedding: así el embedding no viaja otra vez por MQTT y la galería siempre sale de la base. Por eso el alta recarga la galería entera con `GET /api/galeria`.
- **Baja.** `aula/vision/galeria/baja` borra esa cuenta de la galería en RAM.
- **Recarga completa.** `reload()` deja la galería en RAM igual que la que devuelve el servidor (quita las cuentas que ya no están). Si la petición falla o la respuesta no se entiende, devuelve `false` y la galería en RAM no se toca. Las recargas se serializan: la última que termina es la que se queda.

### Formato provisional

El contrato (`docs/contrato-mqtt.md`, sección 4) fija el endpoint y el encabezado, pero NO el formato de la respuesta de `GET /api/galeria`. El parser es tolerante a las formas que se listaron abajo; el formato es PROVISIONAL y queda pendiente de que el carril del borde lo confirme:

```
{"embeddings":[{"cuenta":"320068234","embedding":[...]}]}
{"alumnos":[...]}
[{"cuenta":"320068234","embedding":[...]}]
```

Una entrada con `"embedding": null` es el alumno pendiente de rostro: se omite de la galería en RAM.

## Limitaciones conocidas

- **La cámara no se autentica.** Las fotos se piden por HTTP plano, sin autenticación ni comprobación de integridad. Quien esté en la LAN puede inyectar fotos (fraude de asistencia, envenenamiento del registro) y también leerlas. El adaptador real de `HttpClient` y el firmware de la XIAO no deben salir a producción sin una de estas tres: HMAC-SHA256 sobre un nonce más el cuerpo, un punto de acceso dedicado o TLS. No protegen lo mismo: el HMAC solo garantiza integridad y frescura, así que las fotos siguen viajando en claro por la LAN; el punto de acceso dedicado y TLS también cubren la confidencialidad. Sea cual sea la opción, `capture_url` debe apuntar a una IP fija, no a un nombre mDNS que se pueda suplantar. La decisión está pendiente con el equipo; este esqueleto no la resuelve.
- **El certificado de Node-RED es autofirmado.** La comunicación por HTTPS con el borde usa un certificado autofirmado, así que `allow_invalid_certificates` tiene que estar en `true` (por omisión está en `false` y la conexión se rechaza). Es un riesgo conocido y aceptado hasta que haya un certificado válido, no un olvido. Ver la sección "El cliente HTTP".

## Lo que falta

- Proceso de producción: bucle principal (`poll()` periódico), configuración desde archivo o línea de comandos y manejo de señales. El `main.cpp` actual sigue siendo el recorrido en seco con los dobles, no el binario de producción.
- Unidad systemd del servicio de visión.
- Scripts de prueba en la placa (broker de pruebas propio, arranque y parada del servicio).
- Detector y embedder concretos sobre la NPU (decodificación de salidas, preproceso, `.nb`). Se construyen sobre `npu::Network`.
- Carga de la galería en el arranque: `GalleryStore` ya sabe recargar (`GET /api/galeria`) y aplicar las altas y las bajas, pero el proceso de producción todavía no lo llama al arrancar ni en cada reconexión al broker.
- Miniaturas en el progreso del registro.
- Chequeo antifraude y descarte de outliers.
- Validaciones por pose del registro (rostro único, tamaño, giro).
- Mejora de imagen (CLAHE y gamma).
- Umbrales calibrados: `min_sharpness`, `min_recognized_score` y `min_dudoso_score` de `ServiceConfig` son valores provisionales (la calibración con LFW es el hito del 18 de noviembre).
