# Spec: la cámara de la XIAO por ZeroTier, de punta a punta

Estado: **borrador para revisión**, 13 nov. Nada de esto está implementado todavía.

## Contenido

- [Objetivo y alcance](#objetivo-y-alcance)
- [Qué cambia respecto a la prueba con webcam](#qué-cambia-respecto-a-la-prueba-con-webcam)
- [Punto de partida](#punto-de-partida)
- [Topología](#topología)
- [El modo seguro de la cámara](#el-modo-seguro-de-la-cámara)
- [Workstreams](#workstreams)
- [Plan de pruebas de punta a punta](#plan-de-pruebas-de-punta-a-punta)
- [Decisiones abiertas](#decisiones-abiertas)

## Objetivo y alcance

**Objetivo.** El servicio de visión de la Orange Pi saca fotos reales de la XIAO ESP32S3 Sense igual que hoy las saca de `camara_falsa` con la webcam, y el registro de 5 poses y el pase de lista pasan de punta a punta con la XIAO. Todo eso sin volver a poner la placa en el régimen que quemó la anterior.

**Dentro del alcance:**

- El firmware de la cámara en modo seguro.
- La firma HMAC del lado de la visión.
- Un relay en la PC de desarrollo que une ZeroTier con la LAN de la XIAO.
- La configuración y las pruebas de punta a punta.

**Fuera del alcance (v1):**

- La vista previa del profesor: `GET /api/camara/previa` en Node-RED y el panel de `borde/web`. El firmware deja preparada la resolución baja que necesitará, pero nadie la usa todavía.
- El PIR, el servo, los LEDs, la tira, el buzzer y el INA219 conectados físicamente.
- La XIAO en MQTT. El pase de lista se dispara a mano publicando en `aula/camara/pir`, como en las pruebas con webcam (`docs/RESULTADOS-PRUEBAS.md`, fase 3).
- La migración a esp-mqtt y el cifrado de las fotos.

**Cómo se hace.** Rama nueva desde `main`. **No se fusionan** `feature/vision-firma-captura` (26 commits por detrás de `main` y con conflicto en `orangepi/vision/src/process_config.cpp`), `archive/vision-firma-captura` ni `prueba/camara-pc`. Esas ramas sirven **solo de referencia**: la firma se reimplementa sobre el código actual, con las dos correcciones de `archive/vision-firma-captura` (`579e09f`: una instalación sin secreto arranca; `22de4d6`: el dry-run arranca sin credenciales).

## Qué cambia respecto a la prueba con webcam

| | Webcam (hasta ahora) | XIAO (este spec) |
|---|---|---|
| Quién sirve `/capture` | `camara_falsa --dispositivo` en un equipo con webcam | La XIAO, en la LAN de la PC |
| Cómo llega la Orange Pi | Directo por ZeroTier al proceso de la webcam | Por ZeroTier a un **relay** en la PC, que reenvía por la LAN a la XIAO |
| Firma | No hay: `camara_falsa` ignora `X-Captura-*` y la visión no las manda | La visión **firma** cada petición y la XIAO la **verifica**. El relay solo reenvía y no conoce el secreto |
| Resolución | 640×480, la que daba la webcam | La que fija una lista blanca del firmware, con un tope duro (decisión D1) |
| Ritmo | Sin límite: la webcam aguanta | Limitado **en la placa**: tope de capturas por ventana y corte por temperatura |
| Cámara en reposo | Irrelevante | **Apagada o en espera**: hoy el firmware la deja capturando sin parar desde el arranque (ver abajo) |
| Disparo del pase de lista | `aula/camara/pir` publicado a mano | Igual. La XIAO no entra al MQTT en v1 |

Para el servicio de visión el cambio es pequeño: otra `captura.url`, el secreto en `vision.env` y las tres cabeceras de la firma. La lógica del registro y del pase de lista no cambia, igual que no cambió con `camara_falsa` (`docs/RESULTADOS-PRUEBAS.md`, sección 2).

## Punto de partida

Lo que hay hoy en `main` (13 nov):

- **Firmware:** todo el de `firmware/xiao/nodo-campo` está en `main`. Incluye `/capture` con firma (`captura.cpp`, `logica_camara.cpp`, `hmac_sha256.cpp`) y sus pruebas de host. **Nunca se ha flasheado.**
- **Visión:** `vision_service.cpp:472` hace el GET a `captura.url` **sin cabeceras**, y `orangepi/vision/src/` no tiene `hmac_sha256.cpp`. Con el firmware actual, toda petición recibiría 401.
- **El modo seguro está escrito, pero solo en la rama `docs/vista-previa`** (commit `0e799fc`, sin fusionar). En `docs/HIGH-LEVEL-PLAN.md` de esa rama:
  - Captura bajo demanda, nunca streaming continuo, y antena externa (sección 6.1).
  - Si hay vista previa, sondeo de 1 fps a HVGA, solo cuando alguien mira y con el reconocimiento en pausa (sección 6.7).
  - Vigilar `temp` (tabla de riesgos, "La XIAO se calienta").
- **El firmware de `main` no cumple ese modo seguro:**
  - `captura::iniciar()` arranca la cámara en `setup()` con `FRAMESIZE_QXGA` (2048×1536), `jpeg_quality = 8`, `fb_count = 2` y `CAMERA_GRAB_LATEST` (`captura.cpp:170-174`). Con más de un búfer, el driver `esp32-camera` trabaja en **modo continuo**: el sensor captura cuadros QXGA sin parar mientras la placa está encendida, aunque nadie pida fotos. Es el mismo régimen de streaming en alta resolución que quemó la placa anterior, solo que sin que se note.
  - `res=` se ignora (`captura.cpp:73`).
  - No hay límite de ritmo, ni lectura de temperatura, ni corte térmico.
- **El incidente.** `~/ESP32-Test` es un firmware de ESP-IDF que manda JPEG por el USB Serial/JTAG a 10 fps continuos, con la cámara siempre encendida. Lo que no está escrito en ningún lado es qué resolución se usó cuando la placa se quemó: anotarlo en la sección de pruebas cuando se sepa.
- **El calor está documentado por terceros.** Seeed vende disipadores para la XIAO y cita su propia prueba de una hora con WebCamera: 63.6 °C en la cara trasera sin disipador y 53.5 °C con dos, a unos 27 °C de ambiente. El rango de operación que publican es de -40 a 65 °C.

## Topología

```
Orange Pi (servicio de visión)
   │  ZeroTier: http://<ip-zt-pc>:<puerto>/capture?res=<R>
   │  con X-Captura-TS, X-Captura-Nonce y X-Captura-HMAC
   ▼
PC de desarrollo (relay; solo escucha en su IP de ZeroTier)
   │  LAN Wi-Fi: http://<ip-lan-xiao>/capture?res=<R>  (cabeceras intactas)
   ▼
XIAO ESP32S3 Sense (verifica la firma, saca la foto y responde)
```

- **La firma va de punta a punta:** la pone la visión y la comprueba la XIAO. El relay no tiene el secreto: quien llegue al relay sin firmar recibe el 401 de la placa.
- **El `ts` es el reloj de la Orange Pi** (NTP con systemd-timesyncd). Un reinicio de la XIAO borra su memoria de nonces: es la limitación que ya documenta `firmware/xiao/README.md`.
- **La IP de la XIAO en la LAN tiene que ser fija:** una reserva DHCP en el router, o IP estática en `secrets.h`.

## El modo seguro de la cámara

Requisitos de firmware. Cada uno tiene que poder comprobarse en la placa (sección de pruebas).

| Id | Requisito |
|---|---|
| S1 | **Nada de captura continua.** `fb_count = 1` y `CAMERA_GRAB_WHEN_EMPTY`: el sensor solo entrega un cuadro cuando se pide. |
| S2 | **Cámara apagada en reposo.** No se inicializa en `setup()`: arranca con la primera petición válida (firmada) y se apaga con `esp_camera_deinit()` tras `T_reposo` sin peticiones (por omisión 30 s, configurable). Al arrancar descarta los cuadros iniciales, como hoy. La primera foto de una ráfaga tarda más: se mide (prueba E2) y el plazo del cliente se ajusta con ese dato, no se adivina. |
| S3 | **Resolución con lista blanca y tope duro.** `res=` acepta solo los valores de la lista (por ejemplo `HVGA`, `VGA`, `SVGA` y `XGA`). Un valor desconocido o ausente usa el valor por omisión del reconocimiento (decisión D1). Ninguna petición puede pedir más que el tope. Calidad JPEG fija por resolución. |
| S4 | **Límite de ritmo en la placa.** Como mucho `N` capturas por ventana deslizante (por omisión, 4 en 2 s: una ráfaga) y un mínimo entre ráfagas. Lo que pase el límite recibe **429** sin tocar la cámara. Así ni un cliente con un error puede convertir `/capture` en un stream. |
| S5 | **Corte térmico.** Lee la temperatura interna del ESP32-S3 (`temperatureRead()`) antes de cada captura y cada 30 s. Por encima de `T_corte` (decisión D4) responde **503 `temperatura`**, apaga la cámara y no la vuelve a arrancar hasta bajar de `T_corte - histéresis`. Se imprime en el monitor serie. |
| S6 | **Sin `/stream`** ni ningún otro endpoint de video. Una sola petición de captura a la vez: una segunda concurrente recibe 503 `ocupada`. |
| S7 | **Modo `SOLO_CAMARA` en la compilación.** Con el flag no se inicializan actuadores, PIR, tira ni MQTT: solo Wi-Fi, `/capture` y la vigilancia térmica, y los pines de los headers quedan como los deja el arranque. Se compila con `./compilar.sh` sin avisos. |
| S8 | **Hardware de la prueba.** La XIAO se alimenta solo del USB-C de la PC, sin nada en los headers. Antena externa (U.FL) puesta y disipador si se tiene. Nada más se conecta en v1. |

La lógica pura (la lista de resoluciones, el limitador de ritmo y la decisión térmica con histéresis) va en un módulo sin Arduino, con pruebas de host como las de `logica_camara`.

## Workstreams

W1, W2 y W3 son independientes: van en carpetas distintas y pueden avanzar en paralelo. W4 necesita los tres terminados y W5 necesita W4. W6 va al final.

### W1. Firmware: el modo seguro de la cámara

- **Archivos:** `firmware/xiao/captura.{h,cpp}`, `xiao.ino`, un módulo puro nuevo (por ejemplo `logica_captura_segura.{h,cpp}`) con su `pruebas/test_*.cpp`, y `secrets.h.example` si la IP estática va ahí.
- **Requisitos:** S1 a S7.
- **Hecho cuando:**
  - Pasan todas las pruebas de host del firmware (comandos en `firmware/xiao/README.md`, sección Pruebas de host).
  - `./compilar.sh` compila con y sin `SOLO_CAMARA`, sin avisos.
  - La firma y el anti-replay actuales no cambian: misma cadena canónica y mismos vectores.
- **Revisión:** `kosmos-code-reviewer` y `kosmos-security-auditor` (toca la autenticación y la entrada HTTP).

### W2. Visión: firmar la petición de captura

- **Archivos:** `orangepi/vision/`: `hmac_sha256.{hpp,cpp}` (**la misma** implementación que `firmware/xiao/hmac_sha256.cpp`, con los mismos cinco vectores), el cliente HTTP con cabeceras, `process_config` y `vision_service`, y `vision.env.example` y `vision.conf.example`.
- **Requisitos:**
  - Firma cada GET a `captura.url` con `X-Captura-TS` (epoch de la Orange Pi), `X-Captura-Nonce` (64 bits de una fuente criptográfica) y `X-Captura-HMAC` sobre `GET\n/capture\n<ts>\n<nonce>`.
  - El secreto `CAPTURA_HMAC_SECRETO` se lee de `vision.env`. Si falta: con `captura.hmac_requerido=false` el servicio arranca sin firmar y avisa (así sigue funcionando `camara_falsa`); con `true` no arranca.
  - Rechaza el placeholder y los secretos de menos de 32 caracteres. El dry-run arranca sin credenciales.
  - Distingue en el log 401, 403, 429 y 503 de la placa, sin imprimir nunca el secreto ni la firma.
- **Hecho cuando:** `ctest` pasa en la Orange Pi. Este equipo tiene OpenCV 4.6, así que las pruebas sin NPU también pueden correr aquí.
- **Revisión:** `kosmos-code-reviewer` y `kosmos-security-auditor` (criptografía, secretos y red).

### W3. Relay en la PC

- **Forma:** decisión D2. Recomiendo un script de Python con solo la biblioteca estándar en `herramientas/` (nada que compilar en la PC).
- **Requisitos:**
  - Escucha solo en una IP dada (la de ZeroTier) y se niega a `0.0.0.0` salvo con un flag explícito.
  - Acepta conexiones solo de una lista de IPs de origen (la de ZeroTier de la Orange Pi).
  - Reenvía solo `GET /capture`, con la query y las tres cabeceras `X-Captura-*` intactas.
  - Devuelve el código y el cuerpo de la placa sin cachear, sin guardar nada en disco y sin registrar cuerpos ni cabeceras de firma.
  - Tiene un tope de 2 MB y plazos de conexión y lectura, y deja una sola petición en vuelo hacia la placa: las demás esperan o reciben 503.
- **Herramienta de prueba aparte**, para la LAN y sin pasar por la visión: un script que firma una petición con el secreto, leído de un archivo con permisos 600 y nunca de la línea de comandos.
- **Hecho cuando:** pasan sus pruebas (`python3 -m unittest`) con una placa simulada: cabeceras reenviadas, IPs rechazadas, tope de tamaño y plazos.
- **Revisión:** `kosmos-code-reviewer` y `kosmos-security-auditor` (red y exposición de imágenes).

### W4. Configuración y despliegue

Lo hace Gustavo a mano: ningún agente lee ni escribe secretos.

1. Generar el secreto (`openssl rand -hex 32`) y ponerlo en `firmware/xiao/secrets.h` y en `/etc/pase-lista/vision.env` (permisos 600).
2. Reservar la IP de la XIAO en el router, o ponerla estática.
3. En la Orange Pi: `captura.url = http://<ip-zt-pc>:<puerto>/capture?res=<R>`, `captura.hmac_requerido = true` y `http_timeout_ms` según la medición de E2.
4. Comprobar que la Orange Pi alcanza `<ip-zt-pc>:<puerto>` por ZeroTier y que la PC alcanza la XIAO por la LAN.

### W5. Pruebas de punta a punta

Ver la sección siguiente. Es manual, con la placa delante.

### W6. Documentación

Se pide aprobación antes de delegarla.

- `firmware/xiao/README.md`: estado, modo seguro, `SOLO_CAMARA` y relay. Corregir también la línea 3, que dice que solo existe el código de la tira.
- `orangepi/vision/README.md`: la firma.
- `docs/RESULTADOS-PRUEBAS.md`: una fase nueva con la XIAO.
- Decidir si se fusiona `docs/vista-previa` (decisión D5).

## Plan de pruebas de punta a punta

Cada etapa se hace con la anterior en verde. Ante cualquier temperatura por encima de `T_corte`, olor o calor al tacto en la placa: **desconectar el USB y parar.**

| Etapa | Dónde | Qué | Criterio |
|---|---|---|---|
| E0 | PC y Orange Pi | Pruebas de host del firmware, `ctest` de la visión y pruebas del relay | Todas verdes |
| E1 | PC, USB | Flashear con `SOLO_CAMARA` y abrir el monitor serie. Dejarla 10 min en reposo | El sensor se detecta (OV3660 u OV5640); la cámara **no** está inicializada en reposo; la temperatura queda estable y se anota |
| E2 | PC, LAN | `curl` sin firma y luego la herramienta de prueba firmada | Sin firma: 401. Firmada: 200 con una JPEG de la resolución pedida. Mismo nonce: 403. Se anotan el tiempo de la primera foto (arranque de la cámara) y el de las siguientes |
| E3 | PC, LAN | Ráfagas por encima del límite y `res=` fuera de la lista | 429 sin tocar la cámara; resolución acotada al tope |
| E4 | Orange Pi → relay → XIAO | `curl` sin firma desde la Orange Pi y luego el servicio de visión con firma | 401 a través del relay; con el servicio, 200. Desde una IP fuera de la lista, el relay rechaza |
| E5 | Orange Pi | Prueba de calor: una ráfaga de 4 cada 5 s durante 15 min | La temperatura no llega a `T_corte`; si llega, el corte térmico actúa (503) y la cámara se recupera al enfriarse |
| E6 | Orange Pi | Registro de 5 poses desde la web y pase de lista disparado con `aula/camara/pir`, como en la fase 7 de `RESULTADOS-PRUEBAS.md` | Embedding guardado; pase de lista `presente` con su similitud; latencias `cap`, `det` y `emb` anotadas |

## Decisiones abiertas

| Id | Decisión | Opciones | Recomendación |
|---|---|---|---|
| D1 | Resolución del reconocimiento en v1 | VGA, SVGA, XGA, o mantener QXGA | **SVGA (800×600)**, con tope en XGA. La webcam a 640×480 ya dio 0.6449 de similitud, y QXGA por ZeroTier en ráfagas de 4 con 2 s de plazo es arriesgado por calor y por ancho de banda. Hay que comprobar que la cara mide al menos ~100 px a la distancia real |
| D2 | Forma del relay | Script de Python en `herramientas/`, o un modo `--upstream` de `camara_falsa` | **Python**: no depende de que `prueba/camara-pc` llegue a `main` ni de compilar en la PC |
| D3 | La XIAO en MQTT en v1 | Fuera (`SOLO_CAMARA`), o reenviar el 1883 por la PC | **Fuera.** La luz no se usa sin tira, y el PIR se dispara a mano |
| D4 | Umbral térmico | `T_corte` e histéresis | Empezar con una cifra conservadora (por ejemplo, 65 °C de lectura interna y 10 °C de histéresis) y ajustarla con E1 y E5. La lectura interna del ESP32-S3 no es la temperatura de la carcasa: la cifra es provisional |
| D5 | La rama `docs/vista-previa` | Fusionarla antes, o después de este trabajo | **Antes**: es donde está escrito el modo seguro que este spec implementa |
