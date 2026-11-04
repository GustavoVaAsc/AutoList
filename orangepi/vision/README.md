# Servicio de visión (C++, nativo)

Servicio de visión del plan (`docs/HIGH-LEVEL-PLAN.md`, secciones 5.1, 5.4, 6.2 y 7.1). Sigue siendo agnóstico a una decisión: el modelo concreto (el detector y el reconocedor, y la dimensión de su embedding), que vive detrás de interfaces y detrás de la NPU. Las librerías de MQTT y HTTP ya no son una de esas decisiones: hay adaptadores reales (libmosquitto y cpp-httplib) detrás de las mismas interfaces, y los adaptadores del detector y el embedder sobre la NPU también existen (`NpuFaceDetector` y `NpuFaceEmbedder`), aunque los `.nb` todavía no. El contrato MQTT (`docs/contrato-mqtt.md`) está codificado en los payloads tipados y en la capa JSON.

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
  process_config.hpp  la configuración del proceso (archivo JSON) y su mapeo a ServiceConfig
  face_detector.hpp   interfaz FaceDetector (imagen -> bbox, score, 5 landmarks)
  face_embedder.hpp   interfaz FaceEmbedder (recorte 112x112 -> vector<float>)
  npu_face_detector.hpp  FaceDetector real sobre la NPU (preproceso + decodificacion)
  npu_face_embedder.hpp  FaceEmbedder real sobre la NPU (preproceso + desquantizacion)
  npu_preprocess.hpp  preproceso de imagen hacia el tensor de entrada ([w, h, c, n])
  retinaface_decode.hpp  decodificador de RetinaFace en CPU: anclas, cajas, landmarks y NMS
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
  process_config.cpp  análisis del archivo JSON de configuración
  wall_clock.cpp      implementación del reloj de pared
  mqtt_bus.cpp        implementación del bus MQTT
  httplib_client.cpp  implementación del cliente HTTP
  gallery_store.cpp   implementación del store de la galería
  main.cpp            el proceso de producción (binario `vision_service`)
  npu_face_detector.cpp  el detector sobre la NPU
  npu_face_embedder.cpp  el embedder sobre la NPU
  npu_preprocess.cpp  el preproceso de imagen (resize, color, escala, cuantizacion)
  retinaface_decode.cpp  el decodificador de RetinaFace (anclas, cajas, landmarks, NMS)
  dry_run.cpp         recorrido en seco con los dobles (binario `vision_dry_run`)
apps/
  camara_falsa.cpp    servidor de prueba: una JPEG sintética en GET /capture
  mqtt_probe.cpp      sonda MQTT: publica y/o escucha un tópico
  npu_smoke.cpp       carga un .nb, corre N inferencias y mide
tools/
  humo.sh             prueba de humo de punta a punta (broker propio, cámara falsa, PIR)
tests/
  minitest.hpp             mini-runner propio
  test_json_payload.cpp    la capa JSON del contrato
  test_process_config.cpp  la configuración del proceso
  test_wall_clock.cpp      la validación de la hora del reloj de pared
  test_httplib_client.cpp  el cliente HTTP (servidor httplib en el propio proceso)
  test_gallery_store.cpp   la galería en RAM
  test_mqtt_bus.cpp        el bus MQTT (integración contra un broker real)
vision.conf.example  ejemplo del archivo de configuración (sin secretos)
vision.env.example   ejemplo del archivo de credenciales (con placeholders)
```

### Las cuatro bibliotecas

| Biblioteca | Qué lleva | Dependencias |
|---|---|---|
| `vision_core` | La lógica pura: máquina de estados, galería, alineación, cabecera de imagen, nitidez, tensores, capa JSON y `WallClock`. Sin red. | OpenCV, nlohmann/json |
| `vision_npu` | El wrapper RAII de VIPLite (`npu::Driver`, `npu::Network`). | `vision_core`, runtime del fabricante |
| `vision_models` | Los adaptadores reales sobre la NPU: `NpuFaceDetector` y `NpuFaceEmbedder`. | `vision_npu` |
| `vision_transport` | Los tres adaptadores reales: `MosquittoBus`, `HttplibClient` y `GalleryStore`. | `vision_core`, libmosquitto, cpp-httplib, nlohmann/json, pthread |

`vision_transport` está aparte a propósito: `vision_core` se queda sin dependencias de red, para que las pruebas del núcleo sigan siendo rápidas y no dependan de que haya un broker o un servidor HTTP en la máquina. Los adaptadores implementan las interfaces (`MessageBus`, `HttpClient`) que el núcleo consume, así que el servicio no sabe si detrás hay un bus real o un doble en memoria.

`vision_models` va aparte por lo mismo: los adaptadores de la NPU necesitan `vision_npu`, que ya depende de `vision_core`; meterlos en `vision_core` crearía una dependencia circular. Lo que sí vive en `vision_core` es el preproceso de imagen y el decodificador de RetinaFace, porque son funciones puras y se prueban sin NPU.

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

Las pruebas `npu`, `npu_adapters` y `npu_smoke_vendor_model` necesitan `/dev/vipcore` (y los archivos de `/opt/vpm_run`): solo se registran si existe, llevan la etiqueta `board` y se pueden elegir o excluir con `ctest -L board` y `ctest -LE board`. `npu_adapters` comprueba el cableado de punta a punta de los adaptadores (preproceso, `set_input`, ejecución y lectura de la salida) con el modelo de ejemplo del fabricante. Si el runtime VIPLite no está en `/usr`, se indica con `-DVIPLITE_ROOT=<prefijo>` (o la variable de entorno del mismo nombre). La configuración exige OpenCV 4.6.x.

## Probar la NPU

```
build/npu_smoke <modelo.nb> <entrada.dat> [iteraciones=100]
build/npu_smoke /opt/vpm_run/network_binary.nb /opt/vpm_run/input_0.dat 200
```

Imprime los tensores (forma `w x h x c x n`, formato, cuantización) y el tiempo promedio de copia de entrada, ejecución y lectura de salida. El archivo de entrada debe medir exactamente lo que ocupa el tensor. Los `.nb` no se versionan (plan, sección 11).

## Congelar los paquetes del kernel

El `.nb` (Acuity v2.0.10.2), el driver `vipcore` 1.13.0 y las librerías VIPLite tienen que ser compatibles entre sí. Si el kernel cambia, la NPU deja de funcionar y hay que rehacer la conversión del modelo (plan, sección 13, riesgos).

**Estado en la placa (3 de noviembre de 2026).** `apt-mark showhold` solo retiene `libawispapi-isp-602-arm64` y `wiringpi`. **Los paquetes del kernel no están retenidos.**

El kernel que corre es el del fabricante, `6.6.98-sun60iw2`, del paquete `linux-image-current-sun60iw2` (1.0.0). Ese paquete no viene de ningún repositorio configurado en `apt` (su origen es `/var/lib/dpkg/status`), así que `apt upgrade` no lo actualiza por sí solo. El riesgo real es que una actualización de Debian instale otro kernel y lo deje por delante en el orden de arranque, con otro driver `vipcore` o sin él.

**Retenerlos.** Hace falta `sudo` y el agente no lo usa: lo aplica una persona a mano.

```bash
sudo apt-mark hold linux-image-current-sun60iw2
# Si llega a instalarse el paquete de cabeceras del fabricante:
sudo apt-mark hold linux-headers-current-sun60iw2
```

**Comprobación.** `apt-mark showhold` tiene que listarlos:

```bash
apt-mark showhold
```

**Después de cualquier cambio de kernel**, hay que volver a verificar la NPU antes de dar por bueno el servicio. El modelo de ejemplo del fabricante es la prueba rápida:

```bash
cd /opt/vpm_run && ./vpm_run -s sample.txt -l 2 -d 0
```

El índice de dispositivo es `0`: con `-d 1` el programa falla con `value is more than device count=1`. Si termina con `vpm run ret=0`, la NPU sigue viva.

## Los adaptadores sobre la NPU

`NpuFaceDetector` y `NpuFaceEmbedder` (biblioteca `vision_models`) son los adaptadores reales detrás de las interfaces `FaceDetector` y `FaceEmbedder`: preprocesan la imagen, la echan al tensor de entrada, corren la red y leen la salida. Con `--dry-run` siguen sin usarse (mandan los dobles en memoria).

**El modo de escala se deduce del tensor, no se elige a mano.** Por omisión es `ScaleMode::Auto`, que deduce el dominio real de la entrada a partir de su cuantización:

| Tensor de entrada | Modo deducido |
|---|---|
| 8 bits con cuantización afín, `scale ≈ 1/255` y `zero_point = 0` | `Unit` (x/255): el dominio real es [0,1] |
| 8 bits sin cuantización | `None`: el valor real es el byte de la imagen |
| 8 bits con otra cuantización afín | **falla cerrado**: hay que confirmar la convención |
| punto flotante, o punto fijo dinámico | **falla cerrado**: hay que elegir el modo a mano |

El motivo de que esto sea automático: con el modo equivocado, `quantize` satura la entrada a 255 en silencio y la red ve una imagen casi blanca. Con la galería poblada, los embeddings salen casi idénticos y la primera entrada de la galería gana todos los empates: cualquier cara se reconoce como el alumno registrado primero. Por eso `quantize` además cuenta los valores recortados y falla si saturan más del 1%: una imagen que satura es un error de configuración, no un dato de entrada.

**Todo se valida al arrancar, no en pleno registro.** Los constructores comprueban el número de entradas (debe ser 1), el formato de la salida (que tiene que estar soportado por `dequantize`), el número de elementos de la salida frente a lo que espera el layout, los índices de salida del layout, el tamaño de la entrada (con un techo de 4096 px por lado y lote 1) y que los bytes necesarios quepan en el tensor **antes** de reservar memoria. Si algo no cuadra, el proceso avisa del motivo y sale con error: es preferible no arrancar a fallar con el alumno posando y la luz encendida.

**El decodificador de RetinaFace** (funciones puras, sin NPU, probadas con tensores sintéticos) genera las anclas de tres strides (8, 16, 32) con dos anclas por posición, decodifica cajas y landmarks, y aplica NMS. Dos decisiones de rendimiento, porque el servicio tiene un solo hilo de trabajo con `poll_ms = 20`:

- Las anclas se **cachean** en el detector: dependen solo del tamaño de entrada, no de la imagen.
- El NMS recibe solo las **500 cajas de mejor score** (como el RetinaFace de referencia con "nms top 500"), en vez de comparar entre sí las 16 800 anclas.

Medido en la placa: decodificar las 16 800 anclas bajó de **487 ms a 2,2 ms**, y un fotograma completo (preproceso 15 ms + inferencia 4-8 ms + decode 2 ms) queda por debajo de los 30 ms.

**Lo que sigue siendo PROVISIONAL**, porque el `.nb` todavía no existe: la disposición de la salida del detector (15 valores por ancla), la convención de decodificación (varianzas 0.1/0.2/0.3/0.3 aplicadas al valor predicho **sin** factor de stride, al estilo de InsightFace; el stride solo fija la rejilla y el tamaño del ancla), el color por omisión (RGB) y el tamaño de entrada (640x640 el detector, 112x112 el embedder). El código falla cerrado si el `.nb` no encaja con lo pedido; hay que contrastar estos supuestos con el modelo real en cuanto exista.

## El proceso de producción

`build/vision_service` es el binario de producción: el proceso que corre en la Orange Pi, el que lanza systemd y el que se comunica con el broker y con la cámara. `build/vision_dry_run` es el recorrido en seco (ver la sección siguiente).

El proceso (plan 6.2, un solo hilo de trabajo):

- **Configuración**: lee el archivo JSON (por omisión `/etc/pase-lista/vision.conf`, o el que se pase con `--config`). Un archivo inválido (un tipo equivocado, un valor fuera de rango o una clave desconocida) hace fallar el arranque. Si el archivo no existe, el proceso **falla cerrado**: en producción termina con error, porque arrancar a ciegas con los valores por omisión es peor que no arrancar. Con `--dry-run` avisa por stderr y sigue con los valores por omisión, para poder probarlo sin instalar nada.
- **Credenciales**: la contraseña del broker y el token de la galería se leen de variables de entorno, nunca del archivo de configuración (ver "La configuración"). En producción, si falta la contraseña del broker o el token de la galería, el proceso **falla cerrado** y termina con error: es preferible que el servicio no esté a que esté sin autenticar.
- **Modo anónimo**: conectar sin usuario y sin contraseña solo tiene sentido con el broker de pruebas. En producción no se permite; con `--dry-run` es posible, y el modo tolerante exige `--permitir-anonimo` explícito. Sin ese flag, `--dry-run` avisa por stderr y conecta sin usuario.
- **Bus MQTT**: `MosquittoBus` contra el broker del borde. El proceso se suscribe a `aula/camara/pir` y `aula/vision/cmd` (lo hace la propia `VisionService`) y además a `aula/vision/galeria/alta` y `aula/vision/galeria/baja` (las suscripciones las hace el proceso, no el servicio).
- **Galería**: al arrancar y en cada reconexión al broker, el proceso recarga la galería completa con `GalleryStore::reload()` (lo dice el contrato v0.3, sección 4). El aviso de conexión lo da el bus (`MosquittoBus::set_connect_listener`); el manejador solo marca una bandera y la recarga se hace en el hilo de trabajo, no en el hilo de red del bus. Si la recarga falla, se registra en stderr y el servicio sigue: se reintenta en la próxima reconexión. Las altas y las bajas que llegan por MQTT también se aplican en el hilo de trabajo (la galería en RAM la lee el servicio en ese hilo y no debe cambiar mientras la lee).
- **Bucle**: `VisionService::poll()` cada `poll_ms` (20 ms por omisión) y una espera corta entre llamadas. Todo el trabajo ocurre en el hilo que llama a `poll()`; el hilo de red de libmosquitto solo encola mensajes.
- **Señales**: `SIGINT` y `SIGTERM` paran el bucle. El manejador solo pone una bandera (`volatile std::sig_atomic_t`): no hace trabajo bloqueante ni lanza.
- **Salida ordenada**: al parar, el proceso (1) cancela el registro en curso y publica su estado `cancelado`, apaga la luz y deja el modo del servicio en `inactivo` (`VisionService::shutdown()`); (2) publica `aula/vision/estado` con `en_linea: false` y el esquema completo (retenido); (3) vacía la cola de salida del bus (`MosquittoBus::flush()`) y solo entonces cierra el bus. Sin el paso (1), un `SIGTERM` en mitad de un registro dejaba la luz encendida y el enrolamiento a medias. Así el frontend no se queda con el servicio "en línea" y no queda retenido un "en línea" fantasma (ver "El bus MQTT"). El Will no hace falta en la salida ordenada: sigue puesto para la caída sin cerrar limpio.
- **Detector y embedder**: con `--dry-run` son los dobles en memoria de `fakes.hpp`. Sin `--dry-run`, si existen los `.nb`, el proceso construye los adaptadores reales sobre la NPU (`NpuFaceDetector` y `NpuFaceEmbedder`, biblioteca `vision_models`) y los enchufa en el servicio. Si los `.nb` no están, termina con el código 78; si están pero no se pueden cargar, avisa del motivo y termina con error: en producción nunca se arranca con los dobles. El driver de la NPU se crea antes que las redes para que viva más que ellas, y la NPU se usa solo desde el hilo de trabajo (el que llama a `poll()`): `npu::Network` no es seguro entre hilos.

**Falla cerrado en producción.** Sin el archivo de configuración, sin la contraseña del broker o sin el token de la galería, el proceso **termina con error** (código 1): es preferible que el servicio no esté a que esté sin autenticar. El modo tolerante y el modo anónimo quedan para `--dry-run`, y el anónimo exige `--permitir-anonimo` explícito. El código 78 (`EX_CONFIG`) es el de "faltan los modelos `.nb`".

```bash
build/vision_service --config /etc/pase-lista/vision.conf
build/vision_service --help
```

Sin `--dry-run`, si no encuentra los modelos, el proceso **no arranca** y lo dice claro por stderr:

```
[vision] faltan los modelos .nb (/opt/autolist/models/detector.nb, /opt/autolist/models/w600k_mbf-uint8-v1.nb); usa --dry-run para verificar el cableado
```

Las rutas de los modelos son fijas: `/opt/autolist/models/detector.nb` y `/opt/autolist/models/<servicio.modelo>.nb` (el nombre del modelo sale de la configuración). Los `.nb` no se versionan (plan, sección 11).

## La configuración

El archivo de configuración es JSON, con las claves en español como el resto del proyecto. El ejemplo completo está en `vision.conf.example`:

```json
{
  "mqtt": {"host": "127.0.0.1", "port": 1883, "usuario": "vision", "clave_env": "MQTT_PASSWORD", "client_id": "vision"},
  "captura": {"url": "http://192.168.1.50/capture", "rafaga": 4, "espera_luz_ms": 300, "http_timeout_ms": 2000, "max_body_bytes": 2097152},
  "galeria": {"url": "https://127.0.0.1:1880/api/galeria", "token_env": "GALERIA_TOKEN", "timeout_ms": 5000},
  "umbrales": {"min_sharpness": 0.0, "min_recognized_score": 0.6, "min_dudoso_score": 0.45, "min_similitud_pose": 0.5},
  "servicio": {"poll_ms": 20, "max_inbox_events": 16, "modelo": "w600k_mbf-uint8-v1", "acepta_png": false},
  "http": {"allow_invalid_certificates": false, "ca_file": ""}
}
```

| Bloque | Clave | Qué es | Omisión |
|---|---|---|---|
| `mqtt` | `host`, `port` | broker MQTT | `127.0.0.1`, `1883` |
| | `usuario` | usuario MQTT (`""` = sin usuario) | `vision` |
| | `clave_env` | **nombre** de la variable de entorno con la contraseña | `MQTT_PASSWORD` |
| | `client_id` | id de cliente MQTT | `vision` |
| `captura` | `url` | GET que devuelve la foto | `http://192.168.1.50/capture` |
| | `rafaga` | fotos por intento | `4` |
| | `espera_luz_ms` | espera tras encender la luz | `300` |
| | `http_timeout_ms` | plazo de la petición de foto | `2000` |
| | `max_body_bytes` | tope del cuerpo de la respuesta | `2097152` |
| `galeria` | `url` | `GET /api/galeria` de Node-RED | `https://127.0.0.1:1880/api/galeria` |
| | `token_env` | **nombre** de la variable de entorno con el token | `GALERIA_TOKEN` |
| | `timeout_ms` | plazo de la petición de la galería | `5000` |
| `umbrales` | `min_sharpness` | varianza mínima del Laplaciano | `0.0` |
| | `min_recognized_score` | umbral de "reconocido" | `0.6` |
| | `min_dudoso_score` | umbral de "dudoso" | `0.45` |
| | `min_similitud_pose` | similitud mínima (coseno) de la peor pose con la media de las candidatas; por debajo se descarta esa pose y se recalcula la media. Con `0` el descarte queda desactivado | `0.5` |
| `servicio` | `poll_ms` | periodo del bucle | `20` |
| | `max_inbox_events` | tope de la cola de eventos (mínimo 4) | `16` |
| | `modelo` | nombre y versión del modelo | `w600k_mbf-uint8-v1` |
| | `acepta_png` | aceptar PNG además de JPEG (pruebas) | `false` |
| `http` | `allow_invalid_certificates` | aceptar cualquier certificado (modo inseguro) | `false` |
| | `ca_file` | ruta al CA (o al certificado de Node-RED) con el que se valida el servidor | `""` (los CA del sistema) |

El mapeo a `ServiceConfig` está en `ProcessConfig::to_service_config()` (`src/process_config.cpp`).

### Dónde están las credenciales

**El archivo de configuración no es un secreto.** Solo lleva el *nombre* de las variables de entorno donde están las credenciales:

- `mqtt.clave_env` (por omisión `MQTT_PASSWORD`): la contraseña del usuario MQTT.
- `galeria.token_env` (por omisión `GALERIA_TOKEN`): el token del encabezado `X-Galeria-Token`.

Si la variable no está, el comportamiento depende del modo:

- **Producción (sin `--dry-run`):** el proceso **falla cerrado** y termina con error. Sin contraseña del broker, la conexión sería anónima; sin token, la galería se pediría sin autenticar.
- **Con `--dry-run`:** el proceso avisa por stderr y sigue. Con `--permitir-anonimo` se acepta explícitamente conectar sin usuario; sin ese flag, solo avisa. Si hay usuario MQTT pero no hay contraseña, se conecta sin usuario (un usuario sin contraseña no pasa la autenticación del broker).

El token de la galería **solo se manda por `https://`**, o por `http://` contra `127.0.0.1`, `localhost` o `::1`. Con una URL de otro tipo y con token configurado, la carga de la galería se rechaza: el token no puede viajar en claro por la red.

El archivo de credenciales de la instalación real es `/etc/pase-lista/vision.env` (permisos 600), que systemd pasa al proceso con `EnvironmentFile`. El ejemplo con placeholders está en `vision.env.example`.

### El certificado autofirmado

`http.allow_invalid_certificates` viene en `false` por omisión: **no** se acepta ningún certificado inválido. El certificado de Node-RED es autofirmado, así que con `false` la conexión a `https://127.0.0.1:1880` se rechaza y la galería no carga.

Hay dos caminos:

- **Confiar en el certificado concreto (recomendado):** `http.ca_file` con la ruta al certificado de Node-RED. El cliente valida el servidor contra ese certificado (`set_ca_cert_path`), sin desactivar toda la verificación. El certificado está en el volumen de Node-RED (`/data/certs/nodered.crt`); la llave privada se queda ahí y no sale. Exportarlo lo hace **una persona**, por ejemplo con `docker compose cp nodered:/data/certs/nodered.crt ~/nodered.crt`, y la ruta se pone en `http.ca_file` (por omisión, vacía: se usan los CA del sistema).
- **Modo inseguro (desaconsejado):** `allow_invalid_certificates: true` acepta cualquier certificado. Cuando está activo, el cliente lo avisa por log al arrancar:

  ```
  [vision] ATENCION: se aceptan certificados invalidos
  ```

  (Si además hay un `ca_file` configurado, el aviso añade que el CA no se usa, porque la validación está desactivada.)

El modo inseguro es un riesgo conocido y aceptado (ver "Limitaciones conocidas"), no un olvido; con `ca_file` bien puesto ya no hace falta.

## Recorrido en seco

`build/vision_dry_run` es un arnés de recorrido en seco sobre los dobles: cablea la máquina de estados real con los dobles en memoria y reproduce un pase de lista y un registro. No hay broker, cámara ni modelos. Los adaptadores reales ya existen (`MosquittoBus`, `HttplibClient`, `GalleryStore`, `NpuFaceDetector` y `NpuFaceEmbedder`) y el proceso de producción los usa; lo que sigue sin existir son los modelos `.nb`.

## La unidad systemd

La unidad del servicio de visión es `orangepi/systemd/orangepi-vision.service` (la de la pantalla, que es de otro carril, todavía no existe). La instala **una persona** (el agente no usa `sudo`):

```bash
cd orangepi/vision
cmake -S . -B build && cmake --build build -j4

# 1. El binario
sudo install -m 0755 build/vision_service /usr/local/bin/vision_service

# 2. La configuración y las credenciales
sudo install -d -m 0755 /etc/pase-lista
sudo install -m 0644 vision.conf.example /etc/pase-lista/vision.conf
sudo install -m 0600 vision.env.example /etc/pase-lista/vision.env
sudoedit /etc/pase-lista/vision.conf   # la IP de la XIAO y el certificado autofirmado
sudoedit /etc/pase-lista/vision.env    # MQTT_PASSWORD y GALERIA_TOKEN de verdad

# 3. La unidad
sudo install -m 0644 ../systemd/orangepi-vision.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now orangepi-vision.service

# 4. Ver el log
journalctl -u orangepi-vision.service -f
```

Notas:

- `WorkingDirectory=/var/lib/autolist`: el servicio no escribe nada, pero queda en un directorio propio (por debajo de `/var`, que con `ProtectSystem=full` sigue siendo escribible). Si no existe, `sudo install -d -o orangepi -g orangepi -m 0755 /var/lib/autolist`.
- El usuario `orangepi` tiene que poder abrir `/dev/vipcore` (la NPU): `ls -l /dev/vipcore`. La unidad **no** lleva reglas de dispositivo a propósito (ni `ProtectSystem=strict`), para no impedirle el acceso a la NPU.
- El endurecimiento es moderado: `NoNewPrivileges=yes`, `PrivateTmp=yes`, `ProtectHome=yes` y `ProtectSystem=full`. Con `ProtectSystem=full`, `/usr`, `/boot` y `/etc` son de solo lectura: el servicio lee su configuración de ahí y no necesita escribir nada.
- `EnvironmentFile=/etc/pase-lista/vision.env` tiene que existir: si falta, systemd no arranca la unidad. Para probarlo sin credenciales, se puede dejar el archivo con los placeholders.
- `Restart=on-failure` y `RestartSec=5`: el servicio vuelve a levantarse solo si se cae. La salida por `SIGTERM` (código 0) no lo rearranca.
- `TimeoutStopSec=30`: la salida ordenada necesita margen (el bucle puede estar en una recarga de la galería o en una captura), y 30 s lo dan.
- `RestartPreventExitStatus=78`: el código 78 (`EX_CONFIG`) es el de "faltan los modelos `.nb`". Es una configuración que falta, no un fallo del proceso: sin esta línea systemd lo relanzaría cada 5 s para siempre y llenaría el journal.
- Endurecimiento del núcleo (`LimitCORE=0` para no dejar volcados de core y `UMask=0077` para que los archivos que cree el servicio solo los vea su usuario): está decidido, pero todavía no está en la unidad. Queda pendiente.
- Sin los modelos `.nb`, el proceso no arranca: avisa por stderr y sale con el código 78. Es lo esperado hasta que existan (ver "Lo que falta"). Con `--dry-run` en el `ExecStart` se puede verificar el cableado sin modelos.

## Las herramientas de prueba

### `mqtt_probe`: hacer sonar el broker a mano

En la placa no hay `mosquitto_pub` ni `mosquitto_sub`: la sonda `build/mqtt_probe` (libmosquitto) publica y escucha.

```bash
# Publicar un PIR (lo que hace la XIAO)
build/mqtt_probe --host 127.0.0.1 --puerto 1883 --publicar aula/camara/pir '{"mov":1}'

# Escuchar el resultado del pase de lista durante 10 s
build/mqtt_probe --host 127.0.0.1 --puerto 1883 --escuchar aula/vision/resultado --segundos 10

# Con usuario (el broker real no acepta anónimos)
build/mqtt_probe --usuario vision --clave "$MQTT_PASSWORD" --escuchar aula/vision/estado

# Mejor: la contraseña se lee del entorno, no de la línea de órdenes
MQTT_PASSWORD=... build/mqtt_probe --usuario vision --clave-env MQTT_PASSWORD --escuchar aula/vision/estado
```

Cada mensaje que llega se imprime como `fecha tópico cuerpo`. Con `--publicar` y `--escuchar` a la vez, se suscribe primero y después publica.

`--clave-env NOMBRE` lee la contraseña de la variable de entorno `NOMBRE`. Es la forma recomendada: escrita en la línea de órdenes, la contraseña queda en la tabla de procesos y la puede leer cualquiera del equipo.

### `camara_falsa`: la cámara sintética

`build/camara_falsa` es un servidor httplib que devuelve una JPEG sintética (ruido y bordes, generada con OpenCV) en `GET /capture`. Simula a la XIAO sin la placa de campo:

```bash
build/camara_falsa --puerto 18080
# camara falsa escuchando en http://127.0.0.1:18080/capture
```

En la configuración del servicio, `captura.url` apunta a `http://127.0.0.1:18080/capture`.

### `tools/humo.sh`: la prueba de humo de punta a punta

```bash
cd orangepi/vision
tools/humo.sh
```

La prueba:

1. levanta su **propio** broker MQTT de pruebas (contenedor `eclipse-mosquitto:2`, puerto 1884, con su propio archivo de configuración en `/tmp` y `allow_anonymous true`); si el puerto 1884 ya está ocupado, reutiliza el broker de pruebas que haya y no lo borra al salir, y si lo que lo ocupa no es un broker de pruebas, falla con un mensaje claro;
2. levanta la cámara falsa en el puerto 18080;
3. arranca `build/vision_service --dry-run` con una configuración temporal que apunta a ese broker y a esa cámara;
4. se suscribe a `aula/vision/resultado` con `mqtt_probe` y publica un PIR en `aula/camara/pir`;
5. espera el resultado y lo imprime;
6. limpia todo lo que levantó (la cámara, el servicio, la sonda y su broker), aunque falle.

Además del resultado, comprueba tres cosas:

- que el mensaje de `aula/vision/resultado` trae `decision`, `modo` y `motivo` con los valores esperados (en `--dry-run` la galería está vacía, así que el veredicto es `desconocido` con motivo `galeria_vacia`, en modo `pase`);
- que se publicó `aula/vision/estado` con `en_linea: true`;
- que la luz se enciende y se apaga: tiene que verse el encendido (`luz: 1`) antes que el apagado (`luz: 0`).

No toca los contenedores del proyecto `pase-lista`: el broker real corre en el 1883 y ahí no se entra. Sale con código 0 si llegó el resultado y con código distinto de cero si no.

### `herramientas/alinear_lfw.sh`: alinear un dataset a 112x112

```bash
herramientas/alinear_lfw.sh --manifest RUTA --out DIR
```

Alinea las imagenes de un manifiesto a 112x112 con **el mismo warp de 5 puntos que usa el servicio**: la transformada de similitud (Umeyama) de los 5 landmarks a `kArcFaceReference` y el `warpAffine` de `vision::align_face`. La herramienta llama a esa funcion, no la reimplementa, asi que el recorte es identico al de produccion (se comprobo pixel a pixel).

Sirve para dos cosas: el set de calibracion del `.nb` (Acuity/Pegasus) y el ajuste de los umbrales con LFW (18 de noviembre).

El manifiesto tiene una linea por imagen, con la ruta y los 5 landmarks en pixeles de la imagen. Acepta tres formatos:

```
ruta_de_imagen<TAB>x1 y1 x2 y2 x3 y3 x4 y4 x5 y5
ruta_de_imagen<TAB>x1<TAB>y1<TAB>...
ruta_de_imagen x1 y1 x2 y2 x3 y3 x4 y4 x5 y5
```

Los puntos van en el orden de `FaceDetection::landmarks`: ojo izquierdo, ojo derecho, nariz, boca izquierda, boca derecha. El formato con TAB es el que permite una ruta con espacios. Las lineas vacias y las que empiezan por `#` se ignoran; **una linea mal formada (un campo de mas o de menos, o una coordenada no finita) es un error del manifiesto y aborta todo el lote** (codigo de salida 1), porque un manifiesto a medias no sirve. Lo que se omite con un aviso y el script sigue es la imagen: la que no se puede leer, la de mas de 4000 px por lado y la que tiene los landmarks degenerados (puntos coincidentes). Los avisos dicen el numero de linea del manifiesto, no la ruta: la ruta suele llevar el nombre de la persona y eso es informacion biometrica que no tiene que salir en un log. La salida es un PNG de 112x112 por imagen, con el nombre base de la original.

El script compila el programa en un directorio temporal: no toca el repositorio, solo escribe en el directorio de salida.

**La salida no puede estar dentro del repositorio.** Los recortes de rostros son datos biometricos irreversibles: un `git add .` los meteria en el historial de forma permanente. Si el directorio de salida cae dentro de la raiz del repo, la herramienta lo rechaza antes de escribir nada (codigo de salida 1). Usa un directorio de fuera (`/tmp/salida` o un volumen externo). El `.gitignore` de la raiz ignora ademas `/dataset/`, `/datos/`, `/lfw/` y `/herramientas/salida*/`, por si se.worktree el dataset en otra parte del arbol.

`herramientas/pruebas_alinear_lfw.sh` comprueba los tres formatos, la ruta con espacios, los caminos de error y la barrera del repositorio:

```bash
herramientas/pruebas_alinear_lfw.sh
```

**De donde salen los landmarks importa.** Para calibrar el `.nb` el recorte tiene que ser **el mismo que vera la placa**, o sea el que produce el detector con sus landmarks. Los landmarks que trae LFW sirven para el ajuste de umbrales, pero no para la cuantizacion del modelo: hay que regenerar el manifiesto con el detector cuando exista.

### Probar el servicio sin modelos

`--dry-run` usa el bus MQTT **real** y el cliente HTTP **real**, pero con el detector y el embedder falsos: verifica el cableado completo (broker, cámara, galería, contrato MQTT) sin los `.nb`.

```bash
build/vision_service --dry-run --config /tmp/vision-prueba.conf
```

Es lo que usa `tools/humo.sh`.

## El contrato y la capa JSON

Los payloads tipados de `include/vision/mqtt_contract.hpp` son el único lugar con tópicos y claves. `include/vision/json_payload.hpp` los convierte a JSON y de vuelta, con las reglas del contrato (`docs/contrato-mqtt.md`, sección 1):

- los campos desconocidos se ignoran;
- un campo obligatorio que falta, o que tiene el tipo equivocado, hace fallar la conversión: `deserialize` devuelve un error (`kMissingField`, `kWrongType`, `kInvalidValue`, `kInvalidJson`) y el nombre del campo, nunca el contenido del mensaje;
- `cuenta` es siempre un string (nunca un número: se perderían los ceros a la izquierda), `conf` y `margen` son flotantes, `lat_ms` son enteros o `null` si la etapa no corrió, y `embedding` es un arreglo de flotantes.

El servicio estampa `ts` (ISO 8601 con zona) en los cuatro mensajes que publica: `resultado`, `enrolamiento`, `embedding` y `estado`. La hora la da el `WallClock` inyectado (`SystemWallClock` usa `system_clock` y `localtime_r`); el `Clock` de milisegundos no lleva fecha.

**Contrato v0.7: el `ts` es obligatorio y tiene que salir de un reloj correcto.** Node-RED descarta los mensajes de `aula/vision/enrolamiento` y `aula/vision/embedding` cuyo `ts` falte, sea anterior al inicio del registro (con 2 s de margen) o posterior a la hora actual más 5 minutos. Un embedding con `ts` ilegible, vacío o anterior al año 2000 cierra el registro como `embedding_invalido` y **revierte el consentimiento**; además, rechaza el embedding si su norma L2 se aleja más de 0.01 de 1 (el servicio publica el promedio normalizado, y la serialización a 4 decimales lo deja a menos de 2e-3 de 1, comprobado en `tests/test_json_payload.cpp`). Por eso:

- **Rechazo temprano.** Al recibir `enrolar`, el servicio comprueba el `ts` con `is_plausible_timestamp` (`wall_clock.hpp`: formato ISO 8601 con zona y año 2000 o posterior, el año escrito en la hora local). Si no es válido, rechaza la orden sin empezar el registro, como `ocupado` o `cuenta_invalida`: estado `error`, paso 0, sin pose y motivo `npu` (no se añade un motivo nuevo, para no cambiar el contrato). No enciende la luz ni hace posar al alumno. Deja en stderr una línea `reloj invalido` con el `ts` recibido, sin datos del alumno.
- **Guarda del embedding.** Se repite la comprobación al aceptar la quinta pose, por si el reloj se rompe a mitad del registro. Con hora inválida no se publica `completado` ni el embedding: el registro termina con `error` y motivo `npu`, y el servicio vuelve a inactivo como en cualquier fin de registro.
- **Con el reloj inválido, Node-RED también descarta el `error` de la visión**, porque lleva el mismo `ts` malo. El registro se cierra en Node-RED por vencimiento, a los 5 minutos sin progreso. Es un comportamiento aceptado.
- **Aviso al arrancar.** `vision_service`, también con `--dry-run`, avisa por stderr si el reloj del sistema marca un año anterior a 2000 (una placa sin NTP ni RTC arranca en 1970). Solo avisa: el proceso no se detiene, porque el pase de lista no depende del registro. Mientras no haya hora correcta, los registros fallarán con `npu`.
- **`completado` no cierra el registro en Node-RED.** Lo publica la visión antes que el embedding, por otro tópico y otra QoS, sin garantía de orden; el registro lo cierra el embedding guardado, `cancelado`, `error`, la cancelación del profesor o el vencimiento (5 minutos sin progreso).
- **Cancelar.** `{"accion":"cancelar"}` sin cuenta cancela el registro en curso; con cuenta, solo si es la del registro en curso. Node-RED publica la forma con cuenta al vencer el registro.

`aula/vision/estado` lo publica el servicio en cada cambio de modo (`inactivo`, `pase`, `enrolando`), con el número de embeddings en RAM y el nombre del modelo. El LWT `{"en_linea": false}` lo pone el adaptador MQTT, no el servicio.

En `aula/vision/estado` hay **dos formas**: la que publica el servicio lleva el esquema completo (`ts`, `en_linea`, `modo`, `galeria` y `modelo`), y el Will que registra el broker lleva la forma **mínima**, `{"en_linea": false}`: no puede llevar `ts` ni el resto, porque el Will se registra al conectar. En el estado, `en_linea` es el único campo obligatorio y el resto se acepta ausente, así que un consumidor con esta capa no descarta el Will. El estado final de la salida ordenada lleva el esquema completo.

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

### `flush()`: vaciar la cola de salida antes de cerrar

`MosquittoBus::flush(timeout)` espera a que se vacíen las publicaciones en vuelo (las de QoS 1, con su PUBACK recibido, y las de QoS 0 que ya salieron hacia el sistema operativo). Devuelve `true` si se vaciaron dentro del plazo y `false` si se agotó.

Hace falta en la salida ordenada: la publicación es asíncrona, así que el estado final `en_linea: false` puede quedarse en la cola de salida y no llegar nunca al broker. Y como la desconexión es limpia, el Will no salta. **El costo de no hacerlo es un "en línea" fantasma**: el valor retenido en `aula/vision/estado` se queda en `en_linea: true` para siempre, aunque el servicio ya no esté.

Por eso el proceso, al parar, hace en este orden: `VisionService::shutdown()`, publicar el estado final, `bus.flush()` y solo entonces `bus.stop()`. El hilo de red tiene que seguir vivo durante el `flush()`, así que `flush()` va antes que `stop()`.

### `mosquitto_unsubscribe` no se llama nunca

El bus no llama nunca a `mosquitto_unsubscribe`. La baja de una suscripción se hace solo en memoria, sin tocar el broker. El motivo es medido: llamar a `mosquitto_unsubscribe` desde un hilo distinto del bucle de red provoca un ABBA entre los mutex internos de libmosquitto y cuelga el proceso. En la placa se midió 10 de 10 colgones con la llamada y 0 de 10 sin ella.

El costo es tráfico inútil hasta la desconexión: el broker sigue mandando mensajes a un tópico que ya no se consume. Como la sesión es limpia, al desconectar el broker olvida las suscripciones. `resubscribe_all()` vuelve a suscribir los tópicos vivos en cada reconexión.

## Entradas no fiables

Lo que llega por MQTT y por HTTP se trata como no fiable (ver `ServiceConfig`):

- **Límites de entrada.** El JSON que llega de la red tiene dos topes, comprobados antes de parsearlo: 64 KiB de tamaño y 32 niveles de anidamiento (`kMaxDocumentBytes` y `kMaxDocumentDepth` en `json_payload.hpp`). El parser de nlohmann es descendente y recursivo, así que un documento muy anidado podría agotar la pila; un documento que supera los topes se rechaza sin lanzar y sin tumbar el proceso. El cliente MQTT pone su propio tope de paquete, también de 64 KiB, por debajo del que permite el broker (256 KiB).

- Cola de eventos acotada (`max_inbox_events`, 16, mínimo 4). Lo que llega con la cola llena se descarta sin responder y se deja una línea en stderr por poll, sin el contenido. Un PIR, un `enrolar` y un `probar` en cola cuentan uno cada uno; un `enrolar` con cuenta inválida se descarta antes de ocupar lugar (Node-RED valida al alumno antes de publicar, plan 5.4 paso 2); un lugar queda reservado para `cancelar`.
- `no_cuenta` de exactamente 9 dígitos ASCII (`is_valid_account`); `enrolar` con otra cosa se rechaza con `cuenta_invalida`, y `Gallery::upsert` la rechaza con `std::invalid_argument`. Los rechazos de órdenes (`ocupado`, `cuenta_invalida`) salen con estado `error`, paso 0 y sin pose; un registro en curso que falla sale con estado `error` y motivo `npu` (también si el reloj no da una hora válida para el embedding; o `inconsistente` si las poses dan embeddings de distinta dimensión).
- Fotos: el adaptador HTTP debe cortar la transferencia al pasar `max_body_bytes` y respetar el plazo de toda la petición (`HttpClient::get`). El servicio vuelve a comprobar el tamaño y lee el formato y las dimensiones de la cabecera antes de decodificar; solo acepta JPEG baseline o secuencial extendido de 8 bits con un único scan entrelazado (nunca progresivo, aritmético ni de varios scans, que se pueden construir para gastar CPU al decodificar) y PNG solo con `accept_png` (pruebas).

## El cliente HTTP

`HttplibClient` (`include/vision/httplib_client.hpp`, `src/httplib_client.cpp`) es el `HttpClient` real sobre cpp-httplib. `get` nunca lanza: cualquier fallo de red, un estado distinto de 200 o una excepción de la biblioteca devuelven status 0 y cuerpo vacío. El plazo de `get` es el de la petición entera (conexión, cabeceras y cuerpo), no el de una operación: cpp-httplib solo da plazos por operación, así que el adaptador acota el plazo de socket y además vigila el reloj desde el hilo de la petición; en cuanto se pasa el plazo total, `get` devuelve status 0. Tampoco sigue redirecciones: una redirección a otro host sería una fuga de la petición. Acepta cabeceras adicionales (`Nombre: valor`), que es como viaja el token de la galería.

**No quedan hilos huérfanos.** Cuando se pasa el plazo total, el adaptador cierra el socket (`stop()`) y espera al hilo de la petición (`join()`): el hilo termina solo y no queda colgado. Antes el hilo se desacoplaba (`detach`) y se quedaba colgado para siempre con un servidor que gotea bytes, porque los plazos de cpp-httplib son por operación: con ráfaga 4 quedaban cuatro hilos colgados por intento, sin cota.

**El token de la galería solo se manda por `https://`**, o por `http://` contra `127.0.0.1`, `localhost` o `::1` (`url_admite_token` en `process_config.cpp`). Con una URL de otro tipo, la petición de la galería no se hace: el token no puede viajar en claro por la red.

### Certificado autofirmado

La comunicación con Node-RED por HTTPS usa un certificado autofirmado. `HttpClientOptions::allow_invalid_certificates` viene en `false` por omisión: no se acepta ningún certificado inválido. En su lugar está `HttpClientOptions::ca_file`: con esa ruta se valida el servidor contra ese certificado concreto (`set_ca_cert_path`), sin desactivar toda la verificación. Es la opción recomendada frente a `allow_invalid_certificates`, que es el modo inseguro y avisa por log al arrancar (ver "El certificado autofirmado", en la sección de la configuración).

## La galería en RAM

`GalleryStore` (`include/vision/gallery_store.hpp`, `src/gallery_store.cpp`) mantiene la galería en RAM alimentada por el borde.

- **Cabecera `X-Galeria-Token`.** `GET /api/galeria` exige solo ese encabezado (`GALERIA_TOKEN`, plan, secciones 6.2 y 6.7), sin HTTP Basic: el resto de `/api/*` sí exige Basic del profesor, y Node-RED lo resuelve con un `httpNodeMiddleware` propio (no con `httpNodeAuth`, que bloquearía a la visión). Sin token o con uno erróneo responde 401. Es de solo lectura (`GET`), así que la cabecera `X-Pase-Lista: 1` que el resto de la API exige al cambiar datos no afecta a la visión (contrato v0.6). El token viene de la configuración del servicio, nunca del repo: no se versiona en ningún archivo de este checkout.
- **Alta.** `aula/vision/galeria/alta` solo trae la cuenta, no el embedding: así el embedding no viaja otra vez por MQTT y la galería siempre sale de la base. Por eso el alta recarga la galería entera con `GET /api/galeria`.
- **Baja.** `aula/vision/galeria/baja` borra esa cuenta de la galería en RAM.
- **Recarga completa.** `reload()` deja la galería en RAM igual que la que devuelve el servidor (quita las cuentas que ya no están). Si la petición falla o la respuesta no se entiende, devuelve `false` y la galería en RAM no se toca. Las recargas se serializan: la última que termina es la que se queda.
- **Dimensión mayoritaria.** Al cargar, la dimensión no la fija la primera entrada, sino la que trae más entradas (la mayoritaria; en un empate se queda la menor). Las entradas cuya dimensión no cuadra con esa se descartan, y se avisa por stderr con cuántas son. Si la fijara la primera entrada, una entrada corrupta al principio dejaría la galería con una sola entrada y con la dimensión equivocada, y cada pase de lista acabaría en `decision: error`.

### Formato de la respuesta

Desde el contrato v0.6 (`docs/contrato-mqtt.md`, sección 4) el formato de `GET /api/galeria` es fijo:

```
{"embeddings":[{"cuenta":"320068234","embedding":[...]}]}
```

El parser sigue aceptando también las formas antiguas (`{"alumnos":[...]}` y un arreglo plano), por tolerancia; el contrato solo garantiza la primera.

Una entrada con `"embedding": null` es el alumno pendiente de rostro: se omite de la galería en RAM.

## Limitaciones conocidas

- **La cámara no se autentica.** Las fotos se piden por HTTP plano, sin autenticación ni comprobación de integridad. Quien esté en la LAN puede inyectar fotos (fraude de asistencia, envenenamiento del registro) y también leerlas. El adaptador real de `HttpClient` y el firmware de la XIAO no deben salir a producción sin una de estas tres: HMAC-SHA256 sobre un nonce más el cuerpo, un punto de acceso dedicado o TLS. No protegen lo mismo: el HMAC solo garantiza integridad y frescura, así que las fotos siguen viajando en claro por la LAN; el punto de acceso dedicado y TLS también cubren la confidencialidad. Sea cual sea la opción, `capture_url` debe apuntar a una IP fija, no a un nombre mDNS que se pueda suplantar. La decisión está pendiente con el equipo; este esqueleto no la resuelve.
- **El certificado de Node-RED es autofirmado.** La comunicación por HTTPS con el borde usa un certificado autofirmado. Se puede confiar en el certificado concreto (`http.ca_file`, que valida el servidor contra ese certificado) en vez de desactivar toda la verificación (`allow_invalid_certificates`, modo inseguro que avisa por log). Ver las secciones "El certificado autofirmado" y "El cliente HTTP".

## Lo que falta

- Los modelos `.nb` (RetinaFace y `w600k_mbf`). Los adaptadores sobre la NPU ya existen (`NpuFaceDetector` y `NpuFaceEmbedder`, con el preproceso de imagen y el decodificador de RetinaFace en CPU), pero los supuestos sobre el `.nb` (disposición de la salida del detector, convenciones de decodificación, color y normalización de la entrada) están **PROVISIONALES** en los comentarios: hay que confirmarlos contra el modelo real en cuanto exista. Sin los modelos, el proceso de producción no arranca: solo arranca en `--dry-run`.
- Scripts de prueba en la placa (broker de pruebas propio, arranque y parada del servicio) más allá de `tools/humo.sh` y `mqtt_probe`.
- Miniaturas en el progreso del registro.
- Chequeo antifraude.
- Validaciones por pose del registro (rostro único, tamaño, giro).
- Mejora de imagen (CLAHE y gamma).
- Umbrales calibrados: `min_sharpness`, `min_recognized_score`, `min_dudoso_score` y `min_pose_similarity` de `ServiceConfig` son valores provisionales (la calibración con LFW es el hito del 18 de noviembre).
- Endurecimiento del núcleo en la unidad systemd: `LimitCORE=0` y `UMask=0077` (decididos, pendientes de aplicarlos a `orangepi/systemd/orangepi-vision.service`).
