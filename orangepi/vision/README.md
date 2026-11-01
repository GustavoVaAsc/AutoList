# Servicio de visión (C++, nativo)

Esqueleto del servicio de visión del plan (`docs/HIGH-LEVEL-PLAN.md`, secciones 5.1, 5.4, 6.2 y 7.1). Es agnóstico a tres decisiones: el detector, el reconocedor (y la dimensión de su embedding) y las librerías de MQTT, HTTP y JSON. Esas partes viven detrás de interfaces y solo tienen dobles en memoria.

## Estructura

```
CMakeLists.txt
include/vision/
  mqtt_contract.hpp   ÚNICO lugar con tópicos (sec. 7.1) y payloads tipados
  message_bus.hpp     interfaz MessageBus (publish/subscribe)
  http_client.hpp     interfaz HttpClient (GET -> status + bytes)
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
  fakes.hpp           dobles en memoria: bus, HTTP, detector, embedder, reloj manual
src/                  implementaciones; main.cpp es un recorrido en seco con los dobles
apps/npu_smoke.cpp    carga un .nb, corre N inferencias y mide
tests/                mini-runner propio (minitest.hpp) y pruebas
```

Bibliotecas: `vision_core` (todo menos la NPU; solo OpenCV) y `vision_npu` (wrapper de VIPLite).

## Compilar

Requisitos en la placa: g++ 12 (C++20), CMake >= 3.16, `libopencv-dev` 4.6 (se encuentra con `pkg-config opencv4`) y el runtime del fabricante (`/usr/include/vip_lite.h`, `/usr/lib/libNBGlinker.so`, `/usr/lib/libVIPhal.so`).

```
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

Las `libopencv_*.so.407` sueltas de `/usr/lib` no se usan; el enlace usa `-Wl,--as-needed` y se puede comprobar con `ldd build/vision_service | grep opencv` (todo debe ser `.so.406`).

Las pruebas `npu` y `npu_smoke_vendor_model` necesitan `/dev/vipcore` (y los archivos de `/opt/vpm_run`): solo se registran si existe, llevan la etiqueta `board` y se pueden elegir o excluir con `ctest -L board` y `ctest -LE board`. Si el runtime VIPLite no está en `/usr`, se indica con `-DVIPLITE_ROOT=<prefijo>` (o la variable de entorno del mismo nombre). La configuración exige OpenCV 4.6.x.

## Probar la NPU

```
build/npu_smoke <modelo.nb> <entrada.dat> [iteraciones=100]
build/npu_smoke /opt/vpm_run/network_binary.nb /opt/vpm_run/input_0.dat 200
```

Imprime los tensores (forma `w x h x c x n`, formato, cuantización) y el tiempo promedio de copia de entrada, ejecución y lectura de salida. El archivo de entrada debe medir exactamente lo que ocupa el tensor. Los `.nb` no se versionan (plan, sección 11).

## Recorrido en seco

`build/vision_service` es un arnés de recorrido en seco sobre los dobles, no el binario de producción: cablea la máquina de estados real con los dobles en memoria y reproduce un pase de lista y un registro. No hay broker, cámara ni modelos; se queda así hasta que estén los adaptadores reales de MQTT y HTTP y los modelos, y entonces se cablean en su lugar.

## Entradas no fiables

Lo que llega por MQTT y por HTTP se trata como no fiable (ver `ServiceConfig`):

- Cola de eventos acotada (`max_inbox_events`, 16, mínimo 2). Lo que llega con la cola llena se descarta sin responder y se deja una línea en stderr por poll, sin el contenido. Un PIR, un `enrolar` y un `probar` en cola cuentan uno cada uno; un `enrolar` con cuenta inválida se descarta antes de ocupar lugar (Node-RED valida al alumno antes de publicar, plan 5.4 paso 2); un lugar queda reservado para `cancelar`.
- `no_cuenta` de exactamente 9 dígitos ASCII (`is_valid_account`); `enrolar` con otra cosa se rechaza con `cuenta_invalida`, y `Gallery::upsert` la rechaza con `std::invalid_argument`. Los rechazos de órdenes (`ocupado`, `cuenta_invalida`) salen con estado `error`, paso 0 y sin pose; `Failed` queda para un registro en curso que falla.
- Fotos: el adaptador HTTP debe cortar la transferencia al pasar `max_body_bytes` y respetar el plazo de toda la petición (`HttpClient::get`). El servicio vuelve a comprobar el tamaño y lee el formato y las dimensiones de la cabecera antes de decodificar; solo acepta JPEG baseline o secuencial extendido de 8 bits con un único scan entrelazado (nunca progresivo, aritmético ni de varios scans, que se pueden construir para gastar CPU al decodificar) y PNG solo con `accept_png` (pruebas).

## Limitaciones conocidas

- **La cámara no se autentica.** Las fotos se piden por HTTP plano, sin autenticación ni comprobación de integridad. Quien esté en la LAN puede inyectar fotos (fraude de asistencia, envenenamiento del registro) y también leerlas. El adaptador real de `HttpClient` y el firmware de la XIAO no deben salir a producción sin una de estas tres: HMAC-SHA256 sobre un nonce más el cuerpo, un punto de acceso dedicado o TLS. No protegen lo mismo: el HMAC solo garantiza integridad y frescura, así que las fotos siguen viajando en claro por la LAN; el punto de acceso dedicado y TLS también cubren la confidencialidad. Sea cual sea la opción, `capture_url` debe apuntar a una IP fija, no a un nombre mDNS que se pueda suplantar. La decisión está pendiente con el equipo; este esqueleto no la resuelve.

## Lo que falta a propósito

- Adaptadores concretos de MQTT y HTTP, y la serialización a JSON (librerías ya elegidas: libmosquitto, cpp-httplib y nlohmann/json; ver la sección 6.2 del plan). El bus ya es tipado: el adaptador convierte cada struct de `mqtt_contract.hpp` a JSON y de vuelta.
- Detector y embedder concretos (decodificación de salidas, preproceso, `.nb`). Se construyen sobre `npu::Network`.
- Bucle principal del proceso (`poll()` periódico), unidad systemd y configuración desde archivo o línea de comandos.
- Carga de la galería (`GET /api/galeria`) y su actualización con `aula/vision/embedding`.
- Validaciones por pose del registro, descarte de outliers, chequeo antifraude y miniaturas en el progreso.
- Mejora de imagen (CLAHE y gamma).
- Umbrales calibrados: `min_sharpness` y `min_match_score` de `ServiceConfig` son valores provisionales.
