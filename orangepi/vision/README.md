# Servicio de visión (C++, nativo)

Servicio de visión del plan (`docs/HIGH-LEVEL-PLAN.md`, secciones 5.1, 5.4, 6.2 y 7.1). Sigue siendo agnóstico a una decisión: el modelo concreto (el detector y el reconocedor, y la dimensión de su embedding), que vive detrás de interfaces y detrás de la NPU. Las librerías de MQTT y HTTP usan adaptadores reales (libmosquitto y cpp-httplib) detrás de las mismas interfaces, y los adaptadores del detector y el embedder sobre la NPU (`NpuFaceDetector` y `NpuFaceEmbedder`) operan sobre modelos binarios compilados `.nb` (RetinaFace `detector.nb` y ArcFace / MobileFaceNet `w600k_mbf-uint8-v1.nb`), verificados en hardware y validados en pruebas de integración de punta a punta (`docs/RESULTADOS-PRUEBAS.md`). El contrato MQTT (`docs/contrato-mqtt.md`) está codificado en los payloads tipados y en la capa JSON.

## Estructura

```
CMakeLists.txt
include/vision/
  mqtt_contract.hpp   ÚNICO lugar con tópicos (contrato v0.3) y payloads tipados
  json_payload.hpp    capa JSON del contrato (nlohmann/json): payload <-> documento; ignora campos desconocidos y falla sin lanzar ante campos obligatorios ausentes o con tipo equivocado
  wall_clock.hpp      WallClock inyectable: la hora ISO 8601 con zona del "ts" y el epoch en segundos de la firma de la captura; el Clock existente es monótono y no lleva fecha
  hmac_sha256.hpp     SHA-256 y HMAC-SHA256 en C++ puro: COPIA de firmware/xiao/hmac_sha256.h (la placa verifica lo que firma el servicio)
  captura_firma.hpp   la firma de la petición de captura: cadena canónica, las tres cabeceras X-Captura-* y el nonce; y la respuesta cifrada (AES-256-GCM)
  reloj_guardia.hpp   la guardia del reloj de pared antes de firmar: salto contra CLOCK_BOOTTIME y ancla de la primera firma (RTC o archivo de estado)
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
  npu_preprocess.hpp  preproceso de imagen hacia el tensor de entrada (planar [w, h, c, n] o intercalado [c, w, h, n], deducido del tensor)
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
  hmac_sha256.cpp     copia de firmware/xiao/hmac_sha256.cpp (solo cambian el espacio de nombres y el include)
  captura_firma.cpp   implementación de la firma de la captura (nonce con getrandom)
  reloj_guardia.cpp   implementación de la guardia del reloj y de las anclas del sistema
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
  camara_falsa.cpp    servidor HTTP de prueba: GET /capture (sintética o imagen fija con --imagen <ruta>)
  mqtt_probe.cpp      sonda MQTT: publica y/o escucha un tópico
  npu_smoke.cpp       carga un .nb, corre N inferencias y mide
  detector_prueba.cpp NpuFaceDetector sobre un .nb y una imagen: imprime las detecciones, bboxes y landmarks
tools/
  humo.sh             prueba de humo de punta a punta (broker propio, cámara falsa, PIR)
tests/
  minitest.hpp             mini-runner propio
  test_json_payload.cpp    la capa JSON del contrato
  test_process_config.cpp  la configuración del proceso
  test_wall_clock.cpp      la validación de la hora del reloj de pared
  test_hmac_sha256.cpp     los cinco vectores del HMAC compartidos con el firmware y la comparación en tiempo constante
  test_captura_firma.cpp   la cadena canónica, las cabeceras de la firma, el nonce y la respuesta cifrada (AES-256-GCM)
  test_reloj_guardia.cpp   la guardia del reloj: BOOTTIME, RTC, archivo de estado y reinicios simulados
  gcm_apoyo.hpp            apoyo de las pruebas: cifra como la placa para armar respuestas
  test_httplib_client.cpp  el cliente HTTP (servidor httplib en el propio proceso)
  test_gallery_store.cpp   la galería en RAM
  test_mqtt_bus.cpp        el bus MQTT (integración contra un broker real)
vision.conf.example  ejemplo del archivo de configuración (sin secretos)
vision.env.example   ejemplo del archivo de credenciales (con placeholders)
```

### Las cuatro bibliotecas

| Biblioteca | Qué lleva | Dependencias |
|---|---|---|
| `vision_core` | La lógica pura: máquina de estados, galería, alineación, cabecera de imagen, nitidez, tensores, capa JSON, `WallClock` y la firma de la captura (`hmac_sha256`, `captura_firma`). Sin red. | OpenCV, nlohmann/json |
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
| `libssl-dev` (OpenSSL) | AES-256-GCM de la respuesta de la captura (`captura_firma.cpp`); `vision_core` enlaza `OpenSSL::Crypto` explícitamente | `find_package(OpenSSL)` |
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

Imprime los tensores (`dims=`, en el orden de VIPLite, de la dimensión más rápida a la más lenta: en una entrada intercalada la primera es el canal; formato y cuantización) y el tiempo promedio de copia de entrada, ejecución y lectura de salida. El archivo de entrada debe medir exactamente lo que ocupa el tensor. Los `.nb` no se versionan (plan, sección 11).

Para el detector hay una prueba con una imagen real, con el mismo adaptador que usa el servicio (letterbox, preproceso, NPU y decodificación):

```
build/detector_prueba <detector.nb> <imagen> [bgr|rgb] [iteraciones=1]
build/detector_prueba /opt/autolist/models/detector.nb /tmp/retinaface_test.jpg bgr 20
```

Imprime el tiempo medio de `detect` y cada detección en píxeles de la imagen original: la línea con el formato de la demo del fabricante (`100%, [ 244,   46,  363,  209], face`) y, debajo, la caja y los 5 landmarks con dos decimales. La imagen no se versiona: el repo no lleva fotos de personas.

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

El motivo de que esto sea automático: con el modo equivocado, `quantize` satura la entrada a 255 en silencio y la red ve una imagen casi blanca. Con la galería poblada, los embeddings salen casi idénticos y la primera entrada de la galería gana todos los empates: cualquier cara se reconoce como el alumno registrado primero.

**El rango se comprueba contra el tensor, no contra la imagen.** Además de deducir el modo, al arrancar se comprueba que el dominio que ese modo implica (`[0,1]` en `Unit`, `[0,255]` en `None`, `[-1,1]` en `ArcFace`) quepa en el rango que el tensor representa (`[(min−zp)·scale, (max−zp)·scale]`). Un `int8` con escala 1/255 representa `[-0.502, 0.498]`, que **no** cubre el `[0,1]` de `Unit`: ese modelo se rechaza al arrancar, con los dos rangos en el mensaje. La comprobación es estática (el rango del tensor es un dato del `.nb`), así que no depende de la luz de la sala: con la comprobación dependiente de la imagen, un tensor mal cuantizado pasaba inadvertido con poca luz y tumbaba el servicio con luz. El porcentaje de valores recortados queda como red secundaria para valores sueltos fuera de rango, no como la barrera principal.

**Todo se valida al arrancar, no en pleno registro.** Los constructores comprueban: el número de entradas (debe ser 1), el formato del tensor de **entrada** (tiene que estar soportado), sus canales (3), su lote (1), los techos de tamaño (4096 px por lado, 4 M de elementos, 32 MB de preproceso), el rango representable frente al dominio del modo de escala, el formato de la **salida** (que tiene que estar soportado por `dequantize`), el número de elementos y la forma de cada salida del detector frente a lo que espera el layout (4, 2 y 10 valores por ancla, seguidos: `4x4200x1`, `2x4200x1` y `10x4200x1` a 320), los índices de salida del layout y el lado mínimo de la entrada (8 px: un lado menor casi seguro es un tensor mal interpretado). Todo eso **antes** de reservar memoria y antes del primer fotograma. Si algo no cuadra, el proceso avisa del motivo y sale con error: es preferible no arrancar a fallar con el alumno posando y la luz encendida.

**Los parámetros que dependen del modelo están en la configuración**, en el bloque `npu`, para no tener que recompilar cuando llegue el `.nb`:

```json
"npu": {"color": "bgr", "escala": "auto", "ancho": 0, "alto": 0,
        "embed_color": "rgb", "embed_escala": "arcface", "embed_ancho": 0, "embed_alto": 0}
```

- `color`: `rgb` o `bgr`, **obligatoria** (sin valor por omisión). El RetinaFace del fabricante necesita **`bgr`**: su conversión usa `REVERSE_CHANNEL = True` (`config_yml.py:17`) y su demo le pasa el `cv::Mat` BGR tal cual. `vision.conf.example` ya lo pone.
- `escala`: `auto` (deducir del tensor), `ninguna` (el byte crudo), `unidad` (x/255) o `arcface` ((x−127.5)/127.5). Por omisión `auto`. Con el RetinaFace del fabricante (`uint8` sin cuantizar, con la media dentro del `.nb`), `auto` deduce `ninguna`.
- `ancho` y `alto`: tamaño de entrada del detector; `0` (por omisión) significa "el que tenga el `.nb`". Las anclas se generan para el tamaño de la entrada (su lado es fijo, en píxeles de la entrada, como en la demo).

El embedder EB (`w600k_mbf` sin nodo de preproceso) declara una entrada `uint8` afín con `scale` 0.0078431 y `zero_point` 127. Con `auto` el dominio no se puede deducir (el arranque falla cerrado), así que necesita **`"embed_escala": "arcface"`** y **`"embed_color": "rgb"`** (el ONNX de InsightFace espera RGB). Con eso el rango del tensor cubre [−1, 1] y el borde negro del recorte alineado no satura.

El embedder es otro modelo, con su propia cuantización y su propio tamaño de entrada, así que tiene sus propias claves: `embed_color`, `embed_escala`, `embed_ancho` y `embed_alto`. **No heredan nada del detector.** `embed_color` es obligatoria como `color`; `embed_escala` es `auto` por omisión (se deduce del tensor o el arranque falla cerrado: con EB falla, por eso el ejemplo pone `arcface`); `embed_ancho` y `embed_alto` son `0` (el tamaño del `.nb`).

**Los dos colores son obligatorios** porque el orden de color no se deduce del modelo y equivocarlo no hace fallar nada: la detección o el embedding se degradan en silencio. Fuera de `--dry-run`, si falta el bloque `npu`, `color` o `embed_color`, el proceso sale con el **78** (como con un archivo inválido) y el mensaje dice qué clave falta y qué valor llevan los modelos actuales (`bgr` el detector, `rgb` el embedder). Con `--dry-run` solo avisa por stderr: los modelos no se cargan. La escala no es obligatoria porque `auto` nunca se equivoca en silencio: o la deduce de la cuantización del tensor o falla cerrado. Un color o una escala desconocidos hacen fallar la carga de la configuración, como las demás claves. Al arrancar, el proceso registra una línea con lo resuelto, por ejemplo `[vision] npu: detector: color bgr, escala auto; embedder: color rgb, escala arcface`.

**El detector sigue la demo del fabricante** (`awnpu_model_zoo`, `examples/retinaface`), que es la que decodifica los `.nb` del RetinaFace de biubug6 (R50 y MobileNet 0.25 a 320). El R50 está verificado en la placa contra la demo; el MobileNet 0.25 carga, corre y detecta la cara de la foto de la demo, sin una referencia del fabricante con la que comparar.

| Punto | Lo que hace el servicio | Fuente |
|---|---|---|
| Redimensionado | Letterbox: reduce sin deformar (escala = la menor de las dos), centra y rellena con gris 114; con relleno impar, el píxel de más va abajo o a la derecha. Al final se deshace, y la caja se recorta a la imagen (los landmarks no) | `retinaface_pre.cpp:47-76`, `retinaface_post.cpp:236-285` |
| Entrada | El byte crudo BGR, intercalado (`3x320x320x1`, `uint8` sin cuantizar): la media [104, 117, 123] va dentro del `.nb` | `config_yml.py:12-23` |
| Salidas | Tres tensores `fp32`: cajas (4 por ancla), scores (2 por ancla: [fondo, cara], con el softmax dentro) y landmarks (10 por ancla) | `retinaface_post.cpp:172-174, 227-229` |
| Anclas | Cuadradas, de lado fijo en píxeles de la entrada: {16, 32}, {64, 128} y {256, 512} para los strides 8, 16 y 32; centro en el medio de la celda, `((k + 0.5)·stride, (j + 0.5)·stride)`; por stride, fila, columna y tamaño. A 320 son 4200 | `retinaface_post.cpp:130-164` |
| Cajas | Centro = ancla + d·0.1·lado del ancla; lado = lado del ancla·exp(d·0.2) | `retinaface_post.cpp:188, 202-205` |
| Landmarks | Relativos al **ancla** (no a la caja): ancla + l·0.1·lado del ancla | `retinaface_post.cpp:218-219` |
| Score y NMS | Columna 1 del score, que tiene que ser **mayor** que 0.4 y como mucho 1 (la demo no dibuja las que pasan de 1); NMS con IoU 0.45 | `model_config.h:19-20`, `retinaface_post.cpp:200, 317-321` |

Dos decisiones propias de rendimiento, porque el servicio tiene un solo hilo de trabajo con `poll_ms = 20`: las anclas se **cachean** en el detector (dependen solo del tamaño de entrada) y el NMS recibe solo las **500 cajas de mejor score** (la demo no acota el NMS). La demo además corta a 100 detecciones y las ordena por área; el servicio no lo necesita, porque se queda con la de mayor score. Las anclas cuya caja o cuyos landmarks no son finitos, también tras decodificar (un valor enorme desborda `exp`), se descartan en vez de abortar el fotograma.

Los umbrales 0.4 y 0.45 siguen al fabricante por paridad con su demo y no están afinados para el aula: afinarlos es parte de la calibración con LFW.

Medido en la placa (`build/detector_prueba`, media de 20 fotogramas sobre la foto de la demo, de 592x503): un `detect` completo (letterbox, preproceso, inferencia y decodificación) tarda **19,9 ms** con el R50 y **5,4 ms** con el MobileNet 0.25. Con el R50 la caja coincide con la de la demo (`[244, 46, 363, 209]`) y los landmarks quedan a 1,3 px como mucho.

## El proceso de producción

`build/vision_service` es el binario de producción: el proceso que corre en la Orange Pi, el que lanza systemd y el que se comunica con el broker y con la cámara. `build/vision_dry_run` es el recorrido en seco (ver la sección siguiente).

El proceso (plan 6.2, un solo hilo de trabajo):

- **Configuración**: lee el archivo JSON (por omisión `/etc/pase-lista/vision.conf`, o el que se pase con `--config`). Un archivo inválido (un tipo equivocado, un valor fuera de rango o una clave desconocida) hace fallar el arranque. Si el archivo no existe, el proceso **falla cerrado**: en producción termina con error, porque arrancar a ciegas con los valores por omisión es peor que no arrancar. Con `--dry-run` avisa por stderr y sigue con los valores por omisión, para poder probarlo sin instalar nada.
- **Credenciales**: la contraseña del broker, el token de la galería y el secreto de la firma de la captura se leen de variables de entorno, nunca del archivo de configuración (ver "La configuración"). En producción, si falta la contraseña del broker o el token de la galería, el proceso **falla cerrado** y termina con error: es preferible que el servicio no esté a que esté sin autenticar. El secreto de la firma falla cerrado solo con `captura.hmac_requerido` (ver "La firma de la captura").
- **Modo anónimo**: conectar sin usuario y sin contraseña solo tiene sentido con el broker de pruebas. En producción no se permite; con `--dry-run` es posible, y el modo tolerante exige `--permitir-anonimo` explícito. Sin ese flag, `--dry-run` avisa por stderr y conecta sin usuario.
- **Bus MQTT**: `MosquittoBus` contra el broker del borde. El proceso se suscribe a `aula/camara/pir` y `aula/vision/cmd` (lo hace la propia `VisionService`) y además a `aula/vision/galeria/alta` y `aula/vision/galeria/baja` (las suscripciones las hace el proceso, no el servicio).
- **Galería**: al arrancar y en cada reconexión al broker, el proceso recarga la galería completa con `GalleryStore::reload()` (lo dice el contrato v0.3, sección 4). El aviso de conexión lo da el bus (`MosquittoBus::set_connect_listener`); el manejador solo marca una bandera y la recarga se hace en el hilo de trabajo, no en el hilo de red del bus. Si la recarga falla, se registra en stderr y el servicio sigue: se reintenta en la próxima reconexión. Las altas y las bajas que llegan por MQTT también se aplican en el hilo de trabajo (la galería en RAM la lee el servicio en ese hilo y no debe cambiar mientras la lee).
- **Bucle**: `VisionService::poll()` cada `poll_ms` (20 ms por omisión) y una espera corta entre llamadas. Todo el trabajo ocurre en el hilo que llama a `poll()`; el hilo de red de libmosquitto solo encola mensajes.
- **Señales**: `SIGINT` y `SIGTERM` paran el bucle. El manejador solo pone una bandera (`volatile std::sig_atomic_t`): no hace trabajo bloqueante ni lanza.
- **Salida ordenada**: al parar, el proceso (1) cancela el registro en curso y publica su estado `cancelado`, apaga la luz y deja el modo del servicio en `inactivo` (`VisionService::shutdown()`); (2) publica `aula/vision/estado` con `en_linea: false` y el esquema completo (retenido); (3) vacía la cola de salida del bus (`MosquittoBus::flush()`) y solo entonces cierra el bus. Sin el paso (1), un `SIGTERM` en mitad de un registro dejaba la luz encendida y el enrolamiento a medias. Así el frontend no se queda con el servicio "en línea" y no queda retenido un "en línea" fantasma (ver "El bus MQTT"). El Will no hace falta en la salida ordenada: sigue puesto para la caída sin cerrar limpio.
- **Detector y embedder**: con `--dry-run` son los dobles en memoria de `fakes.hpp`. Sin `--dry-run`, si existen los `.nb`, el proceso construye los adaptadores reales sobre la NPU (`NpuFaceDetector` y `NpuFaceEmbedder`, biblioteca `vision_models`) y los enchufa en el servicio. Si los `.nb` no están, o no encajan con el código (otra disposicion de salidas, otra entrada, otro formato), o el driver los rechaza (archivo truncado o corrupto), el proceso avisa del motivo y sale con el **78**; el 1 queda para los fallos transitorios (arrancar el bus MQTT, inicializar el driver). En producción nunca se arranca con los dobles. El driver de la NPU se crea antes que las redes para que viva más que ellas, y la NPU se usa solo desde el hilo de trabajo (el que llama a `poll()`): `npu::Network` no es seguro entre hilos.

**Falla cerrado en producción.** Sin el archivo de configuración, sin la contraseña del broker o sin el token de la galería, el proceso **termina con error**: es preferible que el servicio no esté a que esté sin autenticar. El modo tolerante y el modo anónimo quedan para `--dry-run`, y el anónimo exige `--permitir-anonimo` explícito. El secreto de la firma de la captura es la excepción controlada: con `captura.hmac_requerido: true` (el ejemplo) también es obligatorio; con `false` (la omisión) solo avisa. El código de salida distingue los dos tipos de fallo:

| Código | Qué es | Qué hace systemd |
|---|---|---|
| **78** (`EX_CONFIG`) | Error de configuración: el archivo de configuración no existe o no es válido, falta una variable de entorno (o, con `captura.hmac_requerido`, el secreto de la firma falta, es el placeholder o es corto), faltan los modelos `.nb`, el `.nb` no encaja con el código o el driver lo rechaza | No reinicia (está en `RestartPreventExitStatus=78`): el fallo queda a la vista en el journal en vez de quedar enterrado en un bucle de reinicios |
| **1** | Fallo transitorio: no se pudo arrancar el bus MQTT o inicializar el driver de la NPU | Reinicia en 5 s |

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

El archivo de configuración es JSON, con las claves en español como el resto del proyecto. Admite comentarios (`//` y `/* */`): `vision.conf.example` los usa para explicar algunos valores. El ejemplo completo está en `vision.conf.example` (aquí sin sus comentarios):

```json
{
  "mqtt": {"host": "127.0.0.1", "port": 1883, "usuario": "vision", "clave_env": "MQTT_PASSWORD", "client_id": "vision"},
  "captura": {"url": "http://<ip-zerotier-del-puente>:<puerto>/capture?res=SVGA", "rafaga": 4, "espera_luz_ms": 300, "http_timeout_ms": 5000, "max_body_bytes": 2097152, "intervalo_rafagas_ms": 3000, "hmac_secreto_env": "CAPTURA_HMAC_SECRETO", "hmac_requerido": true},
  "galeria": {"url": "https://127.0.0.1:1880/api/galeria", "token_env": "GALERIA_TOKEN", "timeout_ms": 5000},
  "umbrales": {"min_sharpness": 0.0, "min_recognized_score": 0.6, "min_dudoso_score": 0.45, "min_similitud_pose": 0.5},
  "servicio": {"poll_ms": 20, "max_inbox_events": 16, "modelo": "w600k_mbf-uint8-v1", "acepta_png": false},
  "npu": {"color": "bgr", "escala": "auto", "ancho": 0, "alto": 0, "embed_color": "rgb", "embed_escala": "arcface", "embed_ancho": 0, "embed_alto": 0},
  "http": {"allow_invalid_certificates": false, "ca_file": ""}
}
```

| Bloque | Clave | Qué es | Omisión |
|---|---|---|---|
| `mqtt` | `host`, `port` | broker MQTT | `127.0.0.1`, `1883` |
| | `usuario` | usuario MQTT (`""` = sin usuario) | `vision` |
| | `clave_env` | **nombre** de la variable de entorno con la contraseña | `MQTT_PASSWORD` |
| | `client_id` | id de cliente MQTT | `vision` |
| `captura` | `url` | GET que devuelve la foto; puede llevar `?res=SVGA` (la query no se firma). En el ejemplo es un placeholder del puente que reexpone la XIAO por ZeroTier: hay que sustituirlo | `http://192.168.1.50/capture` |
| | `rafaga` | fotos por intento | `4` |
| | `espera_luz_ms` | espera tras encender la luz | `300` |
| | `http_timeout_ms` | plazo de la petición de foto. El ejemplo pone `5000`: la primera captura tras un rato de inactividad de la placa incluye el arranque en frío de la cámara, que todavía no está medido (prueba E2); hay que ajustarlo tras medirlo | `2000` |
| | `max_body_bytes` | tope del cuerpo de la respuesta | `2097152` |
| | `intervalo_rafagas_ms` | tiempo mínimo entre el inicio de una ráfaga y el de la siguiente (la placa limita el ritmo; ver "La firma de la captura") | `3000` |
| | `estado_reloj` | archivo donde el servicio guarda el último `ts` firmado (`ts`, `boot_id`, `CLOCK_BOOTTIME`) para anclar la primera firma tras un reinicio cuando no hay RTC; `""` = no se guarda. Lo crea `StateDirectory=autolist` | `/var/lib/autolist/reloj-captura.estado` |
| | `hmac_secreto_env` | **nombre** de la variable de entorno con el secreto de la firma de la captura | `CAPTURA_HMAC_SECRETO` |
| | `hmac_requerido` | `true`: sin un secreto válido el proceso no arranca (78). `false`: solo avisa y la petición va sin firma | sin fijar: `true`, salvo que el host de `captura.url` sea de loopback (127.0.0.0/8, `::1`, `localhost`: ahí corre `camara_falsa`). `--dry-run` nunca es fatal. El ejemplo pone `true` |
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
| `npu` | `color` | orden de color de la entrada del detector (`rgb` o `bgr`) | **obligatoria** fuera de `--dry-run` (el detector actual: `bgr`) |
| | `escala` | modo de escala del detector (`auto`, `ninguna`, `unidad` o `arcface`) | `auto` |
| | `ancho` | ancho de la entrada del detector (`0` = el del `.nb`) | `0` |
| | `alto` | alto de la entrada del detector (`0` = el del `.nb`) | `0` |
| | `embed_color` | orden de color de la entrada del embedder (no hereda el del detector) | **obligatoria** fuera de `--dry-run` (el embedder actual: `rgb`) |
| | `embed_escala` | modo de escala del embedder (no hereda la del detector) | `auto` (con EB hay que poner `arcface`) |
| | `embed_ancho` | ancho de la entrada del embedder (`0` = el del `.nb`) | `0` |
| | `embed_alto` | alto de la entrada del embedder (`0` = el del `.nb`) | `0` |
| `http` | `allow_invalid_certificates` | aceptar cualquier certificado (modo inseguro) | `false` |
| | `ca_file` | ruta al CA (o al certificado de Node-RED) con el que se valida el servidor | `""` (los CA del sistema) |

El mapeo a `ServiceConfig` está en `ProcessConfig::to_service_config()` (`src/process_config.cpp`).

### Dónde están las credenciales

**El archivo de configuración no es un secreto.** Solo lleva el *nombre* de las variables de entorno donde están las credenciales:

- `mqtt.clave_env` (por omisión `MQTT_PASSWORD`): la contraseña del usuario MQTT.
- `galeria.token_env` (por omisión `GALERIA_TOKEN`): el token del encabezado `X-Galeria-Token`.
- `captura.hmac_secreto_env` (por omisión `CAPTURA_HMAC_SECRETO`): el secreto con el que se firma la petición de captura. Es el mismo valor que lleva la placa (`firmware/xiao/secrets.h`).

Si la variable no está, el comportamiento depende del modo:

- **Producción (sin `--dry-run`):** el proceso **falla cerrado** y termina con error. Sin contraseña del broker, la conexión sería anónima; sin token, la galería se pediría sin autenticar.
- **Con `--dry-run`:** el proceso avisa por stderr y sigue (también sin el secreto de la firma, aunque `hmac_requerido` sea `true`). Con `--permitir-anonimo` se acepta explícitamente conectar sin usuario; sin ese flag, solo avisa. Si hay usuario MQTT pero no hay contraseña, se conecta sin usuario (un usuario sin contraseña no pasa la autenticación del broker).

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

`build/vision_dry_run` es un arnés de recorrido en seco sobre los dobles: cablea la máquina de estados real con los dobles en memoria y reproduce un pase de lista y un registro. No hay broker, cámara ni modelos. Los adaptadores reales ya existen (`MosquittoBus`, `HttplibClient`, `GalleryStore`, `NpuFaceDetector` y `NpuFaceEmbedder`) y el proceso de producción los usa con los modelos compilados `detector.nb` y `w600k_mbf-uint8-v1.nb` validados en hardware NPU real.

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
sudoedit /etc/pase-lista/vision.conf   # la URL del puente de la XIAO y el certificado autofirmado
sudoedit /etc/pase-lista/vision.env    # MQTT_PASSWORD, GALERIA_TOKEN y CAPTURA_HMAC_SECRETO de verdad
                                       # (el secreto, el mismo que firmware/xiao/secrets.h)
# El bloque "npu" del vision.conf instalado tiene que llevar, con los modelos actuales,
# "color": "bgr", "embed_color": "rgb" y "embed_escala": "arcface" (el ejemplo ya los
# trae). Sin "color" o sin "embed_color" el servicio sale con el 78.
grep -E '"(color|embed_color|embed_escala)"' /etc/pase-lista/vision.conf

# 3. La unidad
sudo install -m 0644 ../systemd/orangepi-vision.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now orangepi-vision.service

# 4. Ver el log
journalctl -u orangepi-vision.service -f
```

Notas:

- `StateDirectory=autolist` (modo 0700) y `WorkingDirectory=/var/lib/autolist`: systemd crea el directorio (del usuario del servicio) al arrancar; no hace falta `install -d`. Es donde el servicio guarda `reloj-captura.estado` (ver "El reloj del borde"), lo único que escribe. Con `ProtectSystem=full`, `/var` sigue siendo escribible.
- `After=time-sync.target` y `Wants=time-sync.target`: el servicio firma con la hora del reloj de pared, así que tiene que arrancar con la hora ya sincronizada. **Hay que habilitar `sudo systemctl enable systemd-time-wait-sync.service`** (la imagen usa `systemd-timesyncd`; con chrony sería `chrony-wait.service`): sin él, `time-sync.target` se alcanza al instante y el orden no protege de nada. Comprobar con `systemctl is-enabled systemd-time-wait-sync.service` y `timedatectl` (`System clock synchronized: yes`).
- El usuario `orangepi` tiene que poder abrir `/dev/vipcore` (la NPU): `ls -l /dev/vipcore`. La unidad **no** lleva reglas de dispositivo a propósito (ni `ProtectSystem=strict`), para no impedirle el acceso a la NPU.
- El endurecimiento es moderado: `NoNewPrivileges=yes`, `PrivateTmp=yes`, `ProtectHome=yes` y `ProtectSystem=full`. Con `ProtectSystem=full`, `/usr`, `/boot` y `/etc` son de solo lectura: el servicio lee su configuración de ahí y no necesita escribir nada.
- `EnvironmentFile=/etc/pase-lista/vision.env` tiene que existir: si falta, systemd no arranca la unidad. Para probarlo sin credenciales, se puede dejar el archivo con los placeholders.
- `Restart=on-failure` y `RestartSec=5`: el servicio vuelve a levantarse solo si se cae. La salida por `SIGTERM` (código 0) no lo rearranca.
- `TimeoutStopSec=30`: la salida ordenada necesita margen (el bucle puede estar en una recarga de la galería o en una captura), y 30 s lo dan.
- `RestartPreventExitStatus=78`: el código 78 (`EX_CONFIG`) es el de los errores de configuración (faltan los modelos `.nb`, el `.nb` no encaja con el código, el archivo de configuración no es válido, falta una variable de entorno). Son fallos de configuración, no del proceso: sin esta línea systemd lo relanzaría cada 5 s para siempre y llenaría el journal.
- Endurecimiento del núcleo (`LimitCORE=0` para no dejar volcados de core y `UMask=0077` para que los archivos que cree el servicio solo los vea su usuario): está decidido, pero todavía no está en la unidad. Queda pendiente.
- Sin los modelos `.nb`, el proceso no arranca: avisa por stderr y sale con el código 78 (los modelos ya compilados deben residir en `/opt/autolist/models/`). Con `--dry-run` en el `ExecStart` se puede verificar el cableado sin invocar la NPU.

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

En la configuración del servicio, `captura.url` apunta a `http://127.0.0.1:18080/capture`. **La cámara falsa ignora las cabeceras de la firma y sirve la JPEG en claro** (no verifica ni cifra nada): hay que usarla **sin** `CAPTURA_HMAC_SECRETO` en el entorno (con `--dry-run`, o con una URL de loopback y `hmac_requerido` sin fijar o en `false`). Si el servicio tiene un secreto válido, firma la petición y descarta como "respuesta no autentica" la JPEG sin cifrar de la cámara falsa.

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
- **Espera antes de cada foto.** Tras publicar `esperando_pose` (al empezar, al pasar a la pose siguiente y tras un rechazo, que reintenta la misma pose), el servicio espera `pose_delay` (5000 ms, fijo en `ServiceConfig`, no sale de `vision.conf`) y solo entonces captura. El frontend cuenta ese plazo en pantalla (`ESPERA_POSE_MS`, `web/src/api/registroRespuestas.ts`): si se cambia uno, hay que cambiar el otro.
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

## La firma de la captura

La XIAO solo sirve `GET /capture` si la petición viene firmada (`firmware/xiao/README.md`, "La cámara y el endpoint /capture"). Sin firma contesta 401. Y la **respuesta va cifrada y autenticada** con AES-256-GCM (ver más abajo): el secreto protege en los dos sentidos. El servicio manda tres cabeceras en **cada** foto de la ráfaga (`include/vision/captura_firma.hpp`):

| Cabecera | Valor |
|---|---|
| `X-Captura-TS` | epoch en segundos del reloj del borde (`WallClock::now_epoch_seconds()`, inyectable en las pruebas) |
| `X-Captura-Nonce` | entero aleatorio de 64 bits sin signo, en decimal |
| `X-Captura-HMAC` | 64 caracteres hex en minúscula: HMAC-SHA256 de `GET\n/capture\n<ts>\n<nonce>` (UTF-8, sin salto final) con el secreto compartido |

**El camino firmado es siempre `/capture`, fijo, sin la query.** No se deriva de `captura.url`: la placa firma ese camino sea cual sea el de la petición, y la URL solo dice a dónde se envía. Así `captura.url` puede llevar `?res=SVGA` (viaja intacta, pero no se firma, como en la placa) y apuntar a un puente que reexponga la placa bajo otro camino sin romper la firma. La contrapartida: el servicio no comprueba que el camino de la URL llegue a `/capture` de la placa; si el puente no lo traduce, la petición no llegará al endpoint y el log mostrará el código que conteste (no un 401).

**El secreto** (`CAPTURA_HMAC_SECRETO`, de `/etc/pase-lista/vision.env`; el nombre de la variable sale de `captura.hmac_secreto_env`) tiene que ser el mismo que `firmware/xiao/secrets.h`. Sirve si tiene al menos 32 caracteres, al menos 8 distintos entre sí (la misma regla que `GALERIA_TOKEN` en `nodered/autenticacion-api.js`) y no **contiene** `cambiar_` (el placeholder `cambiar_esta_clave` y sus variantes; igual de estricto que la placa y que Node-RED). Un secreto que no sirve (ausente, vacío, placeholder o corto) se trata según `captura.hmac_requerido`:

| `hmac_requerido` | Sin secreto válido en producción | Con `--dry-run` |
|---|---|---|
| `true` (el ejemplo; también la omisión, salvo una URL de loopback) | no arranca, salida 78 | arranca, avisa y va sin firma |
| `false` | arranca, avisa y va sin firma; si el secreto aparece, firma | igual |

**Sin `captura.hmac_requerido` en el archivo, la omisión es `true`** (falla cerrado): lo no firmado lo rechaza la placa con 401, así que un servicio sin secreto no sirve contra la placa real. La excepción es una URL de loopback (`127.0.0.0/8`, `::1` o `localhost`), donde corre `camara_falsa` (que ignora las cabeceras) y no hay placa que exija la firma: ahí la omisión es `false` y el servicio arranca sin secreto, avisando. `--dry-run` nunca es fatal. Un valor explícito en el archivo siempre manda sobre la URL. El servicio **nunca** imprime el secreto ni la firma: los avisos solo nombran la variable.

**El nonce sale de `getrandom(2)`**, no de `/dev/urandom` ni de un generador pseudoaleatorio: no abre ningún descriptor (no falla por falta de descriptores ni en un entorno sin `/dev`), con 8 bytes devuelve todo o nada, y es la misma fuente criptográfica del kernel. Se pide con `GRND_NONBLOCK`: si el generador del kernel aún no está listo (arranque temprano) la captura falla y se reintenta en la siguiente, en vez de bloquear el hilo de trabajo. No hay reserva con otro generador: un nonce predecible anula el anti-replay de la placa.

**El `ts` es el del reloj del borde** (la placa no tiene uno confiable), y se vigila antes de firmar: una hora anterior al 1 de enero de 2026 (`kMinEpochPlausible`, 1767225600) o posterior al 1 de enero de 2100 (`kMaxEpochPlausible`, 4102444800) no se firma; y un salto del reloj hacia adelante tampoco, medido contra `CLOCK_BOOTTIME`, con la primera firma tras arrancar anclada al RTC de hardware o a un archivo de estado. Todo eso, con el motivo y la recuperación, está en "El reloj del borde".

**La respuesta de la placa va cifrada y autenticada (AES-256-GCM).** En un 200 la placa manda `Content-Type: application/octet-stream` con la foto cifrada. Sin esto, cualquiera en la LAN podría leer la foto o, suplantando la IP de la placa, contestar una petición firmada con una foto cualquiera (fraude de asistencia).

| Pieza | Valor |
|---|---|
| Clave `K` | `HMAC-SHA256(clave = bytes del secreto, mensaje = "AUTOLIST-CAPTURA-RESP-AES256GCM-V1")`, 32 bytes; el servicio la deriva una vez, al arrancar, y la borra al destruirse |
| IV | cabecera `X-Captura-IV`: 24 caracteres hex en minúscula (12 bytes aleatorios de la placa) |
| AAD | `RESP\n<ts>\n<nonce>` (decimal canónico, sin salto final), con el `ts` y el `nonce` que el servicio mandó en **esa** petición |
| Cuerpo | el texto cifrado seguido de la etiqueta de 16 bytes |

Cuando el servicio firmó la petición, exige un IV válido y un cuerpo de al menos 16 bytes, descifra y verifica con OpenSSL (`EVP` AES-256-GCM, `descifrar_respuesta` en `captura_firma.hpp`). **Falla cerrado:** ante cualquier fallo descarta la foto antes de decodificarla, y la foto descifrada se borra de memoria tras decodificarla. El AAD ata la respuesta a su petición: la de otra petición (otro `nonce` o `ts`), con otro IV, otra clave o un byte cambiado no verifica. `captura.max_body_bytes` se aplica a lo que viaja (cifrado más etiqueta). Sin secreto (`camara_falsa`) no se descifra: la JPEG viaja en claro. El cliente HTTP deja el IV en `HttpResponse::resp_iv`. **Este protocolo lo tiene que implementar igual el firmware**: hasta entonces, una placa que no cifre sus respuestas hace que el servicio con secreto descarte todas las fotos.

**El ritmo de la placa.** La placa limita las capturas: 4 por ventana deslizante de 2000 ms y después una pausa de 1000 ms, así que una ráfaga solo puede empezar 3000 ms después de la primera foto de la anterior. Con el antirrebote del PIR (2 s), dos ráfagas seguidas daban 4 respuestas 429 y `captura_fallida`. Por eso:

- **Intervalo entre ráfagas** (`captura.intervalo_rafagas_ms`, por omisión 3000): antes de empezar una ráfaga demasiado cerca de la anterior, el servicio espera lo que falta (`Clock::sleep_for`). Cuenta desde el inicio de la ráfaga anterior, no desde su final. **La captura de una pose del registro también cuenta como inicio de ráfaga**, para que una ráfaga del pase de lista que venga justo después se espacie.
- **Un reintento ante un 429:** esa foto se repite **una vez** (con `ts` y `nonce` nuevos) tras esperar lo que diga `Retry-After` (segundos enteros, `HttpResponse::retry_after_s`; el puente lo reenvía) o 1100 ms si no viene. La espera extra total de una ráfaga no pasa de 3 s: una espera que ya no cabe no se hace y la foto cuenta como fallida. Un 429 que no cede acaba en la vía de fallo de siempre (`captura_fallida`).

**Lo que dice el log** cuando la placa no devuelve la foto: **una línea por código y por ráfaga**, con la cuenta (por ejemplo `la placa rechazo 4/4 fotos con 401: ...`), y una línea `respuesta no autentica: N/M fotos descartadas sin decodificar (...)` si hubo respuestas que no verifican, con la cuenta por motivo (sin `X-Captura-IV`, IV mal formado, menos de 16 bytes o etiqueta que no verifica). Nunca se vuelca el cuerpo ni la firma. Al arrancar hay además una línea que dice si la firma de la captura está activa o inactiva.

| Código | Causa en la placa | Qué hacer |
|---|---|---|
| 401 | falta una cabecera o la firma no cuadra | el secreto de la placa y el del borde no coinciden |
| 403 | nonce repetido, o `ts` más antiguo que el último aceptado (ventana de 300 s) | el reloj del borde retrocedió |
| 429 | la placa limita el ritmo de capturas (se reintentó una vez; ver "El ritmo de la placa") | subir `captura.intervalo_rafagas_ms` o bajar `rafaga` |
| 503 | cámara no lista, ocupada o por encima de la temperatura | esperar; no es un problema de firma |
| otro | cualquier otra respuesta que no es 200 | |

Un fallo de red o un plazo vencido no llevan código: los registra el cliente HTTP (ver "El cliente HTTP").

**El HMAC es el mismo de la placa.** `include/vision/hmac_sha256.hpp` y `src/hmac_sha256.cpp` son una copia de `firmware/xiao/hmac_sha256.h` y `.cpp`: solo cambian el espacio de nombres (`vision::hmac_sha256`) y la ruta del `#include`. Si cambia una, hay que cambiar la otra. Las dos tienen los mismos cinco vectores de prueba (dos de la cadena canónica del protocolo y tres de HMAC: RFC 4231 casos 1 y 2, y la clave de 128 bytes) y la comparación en tiempo constante. En `tests/test_hmac_sha256.cpp` y `tests/test_captura_firma.cpp`.

**Lo que no hace.** No evita un salto del reloj del borde de menos de 10 minutos, ni la primera firma de la vida de la instalación si no hay RTC ni archivo de estado (ver "El reloj del borde"): la placa recuerda el último `ts` aceptado y rechaza con 403 lo que sea más de 300 s más antiguo, hasta que se borre su piso. No firma la query (la resolución). El estado de la placa vive en RAM: tras un reinicio, una petición capturada vuelve a ser aceptable. El secreto sigue legible en `/proc/<pid>/environ` del propio usuario del servicio, como la contraseña MQTT y el token de la galería (pendiente, aparte).

## El reloj del borde

La firma de la captura lleva el `ts` del reloj de pared de la Orange Pi, y la placa **guarda el último `ts` que aceptó** (también al reiniciarse) y rechaza con 403 todo lo que sea más de 300 s anterior. Un solo `ts` en el futuro basta para dejarla rechazando las capturas buenas: pasa con un NTP suplantado, o con un reloj que arranca mal (una placa sin RTC con pila arranca en 1970, o con la hora vieja que restauró el sistema) y se corrige después. Por eso el servicio **no firma un `ts` en el que no confía** (`GuardiaReloj`, `include/vision/reloj_guardia.hpp`):

| Qué comprueba | Cómo |
|---|---|
| Rango | anterior al 1 de enero de 2026 o posterior al 1 de enero de 2100: no se firma |
| Salto dentro del proceso | desde la última firma, el reloj de pared no puede avanzar más de 10 minutos (`kSaltoRelojMaxS`) por encima de `CLOCK_BOOTTIME`. Se usa BOOTTIME y no el reloj monotónico porque BOOTTIME sigue contando durante una suspensión: dormir el equipo no es un salto |
| **La primera firma tras arrancar** | no tiene firma anterior con la que compararse, así que se ancla a algo absoluto (ver abajo) |
| Confirmación por el RTC | si el RTC de hardware coincide (a menos de 10 minutos) con la hora nueva, un salto hacia adelante se acepta **sin reiniciar nada** |

**El ancla de la primera firma.**

1. **El RTC de hardware**, si existe y es legible: `/sys/class/rtc/rtc0/since_epoch`. Mientras la hora de pared difiera del RTC en más de 10 minutos, no se firma. Un RTC con una hora imposible (1970, sin pila) no cuenta como referencia. **No he podido comprobar si la Orange Pi tiene `rtc0`**: en la placa, `ls /sys/class/rtc/` y `cat /sys/class/rtc/rtc0/since_epoch` (y `timedatectl`, `RTC time`) lo dicen. Con RTC, el archivo de estado se escribe igual, por si el RTC deja de servir.
2. **El archivo de estado** (`captura.estado_reloj`, por omisión `/var/lib/autolist/reloj-captura.estado`, modo 0600, escrito de forma atómica como mucho cada 30 s): `v1 <ts> <boot_id> <boottime_s>`. En el **mismo arranque** (mismo `/proc/sys/kernel/random/boot_id`), la hora no puede haberse adelantado más de 10 minutos por encima de lo que cuenta BOOTTIME desde que se escribió: un reinicio del servicio no esquiva la guardia. En **otro arranque**, la hora de pared no puede ser anterior al `ts` guardado (un reloj que arrancó en 1970 o con una hora vieja, todavía sin sincronizar); no hay límite superior, porque no se sabe cuánto estuvo apagado el equipo.

**Sin RTC y sin estado utilizable (primera instalación, archivo borrado o corrupto) se firma, con un aviso fuerte.** Es una decisión: rechazar ahí sería permanente (el archivo solo se escribe al firmar, así que nunca habría uno, y una placa sin RTC con el directorio de estado sin escritura no podría firmar jamás). El riesgo que se acepta es el de la primera firma de la vida de la instalación; lo cubren el orden de arranque (`After=time-sync.target`) y el aviso, que dice que si la hora no es correcta la placa quedará rechazando con 403. Un archivo de estado que no se puede guardar también avisa (una vez), porque sin él el siguiente reinicio vuelve a no tener ancla.

**Qué tiene que habilitar el usuario en la Orange Pi.**

- `sudo systemctl enable systemd-time-wait-sync.service` (la imagen usa `systemd-timesyncd`; con chrony, `chrony-wait.service`). Sin él, `After=time-sync.target` no espera a nada. Comprobar: `systemctl is-enabled systemd-time-wait-sync.service` y `timedatectl` (`System clock synchronized: yes`).
- Reinstalar la unidad (`orangepi/systemd/orangepi-vision.service`) para que lleve `StateDirectory=autolist`, y `sudo systemctl daemon-reload`.

**Recuperación.** El log dice `revisa NTP y la sincronizacion de la hora`; no basta con reiniciar el servicio. En orden:

1. **El servicio se negó a firmar** (nada se mandó a la placa; la placa está bien): revisar la hora (`timedatectl status`, `journalctl -u systemd-timesyncd`, `hwclock`). Si el RTC coincide con la hora nueva, el servicio retoma solo; si la hora se corrige y vuelve a su sitio, también. Si la hora nueva es la correcta pero no hay RTC que la confirme (se corrigió hacia adelante más de 10 minutos), reiniciar **solo el servicio** no sirve en el mismo arranque (el archivo de estado sigue anclado al valor anterior): **reiniciar la Orange Pi** (otro `boot_id`: basta con que la hora sea posterior a la guardada) o, sabiendo que la hora es correcta, borrar `/var/lib/autolist/reloj-captura.estado` y reiniciar el servicio.
2. **La placa ya aceptó un `ts` equivocado** (se firmó antes de que existiera esta guardia, o fue la primera firma sin ancla, o una corrección legítima hacia adelante que la placa aceptó con un `ts` viejo): la placa contesta 403 a todo lo anterior a su piso menos 300 s. Corregir la hora de la Orange Pi y **reiniciar el servicio y reiniciar la placa**, o, para borrar el piso de la placa, mantener pulsado **BOOT durante 1 s dentro de los 3 s posteriores al arranque** de la placa. Reiniciar solo el servicio no cambia el piso de la placa.

## El cliente HTTP

`HttplibClient` (`include/vision/httplib_client.hpp`, `src/httplib_client.cpp`) es el `HttpClient` real sobre cpp-httplib. `get` nunca lanza: cualquier fallo de red, un estado distinto de 200 o una excepción de la biblioteca devuelven status 0 y cuerpo vacío. El plazo de `get` es el de la petición entera (conexión, cabeceras y cuerpo), no el de una operación: cpp-httplib solo da plazos por operación, así que el adaptador acota el plazo de socket y además vigila el reloj desde el hilo de la petición; en cuanto se pasa el plazo total, `get` devuelve status 0. Tampoco sigue redirecciones: una redirección a otro host sería una fuga de la petición. Acepta cabeceras adicionales (`Nombre: valor`), que es como viajan el token de la galería y las tres cabeceras de la firma de la captura. Ante una respuesta que no es 200 el contrato no cambia (status 0 y cuerpo vacío), pero el código HTTP queda en `HttpResponse::rejected_status` solo para el log: así el servicio distingue el 401, 403, 429 y 503 de la placa (ver "La firma de la captura"). Un fallo de red, el plazo o el tope del cuerpo lo dejan en 0. Además lee dos cabeceras de la placa: `Retry-After` de un rechazo (`HttpResponse::retry_after_s`, solo segundos enteros) y `X-Captura-IV` de un 200 (`HttpResponse::resp_iv`).

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

- **La petición va firmada y la respuesta, cifrada y autenticada.** El servicio firma cada petición y la placa devuelve la foto cifrada con AES-256-GCM (ver "La firma de la captura"): quien esté en la LAN no puede leerla, y una foto inyectada o alterada en el camino se descarta. Lo que sigue a la vista es el tráfico en sí (que hay una captura, su tamaño y sus tiempos) y las cabeceras de la petición, que viajan por HTTP plano. `captura.url` debe apuntar a una IP fija, no a un nombre mDNS que se pueda suplantar. El secreto compartido protege la clave y la firma: si se filtra, hay que cambiarlo en la placa y en el borde.
- **El certificado de Node-RED es autofirmado.** La comunicación por HTTPS con el borde usa un certificado autofirmado. Se puede confiar en el certificado concreto (`http.ca_file`, que valida el servidor contra ese certificado) en vez de desactivar toda la verificación (`allow_invalid_certificates`, modo inseguro que avisa por log). Ver las secciones "El certificado autofirmado" y "El cliente HTTP".

## Lo que se completó en este hito

- Modelos `.nb` en hardware NPU verificados: RetinaFace (`detector.nb`, ~22 ms) y MobileFaceNet ArcFace (`w600k_mbf-uint8-v1.nb`, ~3.5 ms), con preproceso en tensor, alineación afín y similitud coseno. Guía y scripts de conversión documentados en [conversion/README.md](conversion/README.md).
- Validación de punta a punta documentada en `docs/RESULTADOS-PRUEBAS.md`: enrolamiento desde web (5 tomas), pase de lista, sincronización con pantalla física ILI9341, control de duplicados y reporte CSV al cerrar sesión.
- Soporte de inyección de fotos fijas con `--imagen` en `camara_falsa`.
- Endurecimiento del servicio en la unidad systemd (`LimitCORE=0` y `UMask=0077` aplicados en `orangepi/systemd/orangepi-vision.service`).

## Lo que falta

- Miniaturas en el progreso del registro.
- Chequeo antifraude.
- Validaciones por pose del registro (rostro único, tamaño, giro).
- Mejora de imagen (CLAHE y gamma).
- Umbrales calibrados: `min_sharpness`, `min_recognized_score`, `min_dudoso_score` y `min_pose_similarity` de `ServiceConfig` son valores provisionales (la calibración con LFW es el hito del 18 de noviembre).
