# Pase de lista con reconocimiento facial en Edge AI

**Materia:** Fundamentos de Sistemas Embebidos 2027-1
**Profesor:** Eduardo Fragoso Navarro
**Equipo:** Gustavo · Yordi · Arnau
**Plataforma Edge AI:** Orange Pi Zero 3W (Allwinner A733, NPU de 3 TOPS, 4 GB de RAM)

> Documento vivo. Si una decisión cambia, actualízala aquí.

---

## 1. Problema y objetivo

El pase de lista manual consume tiempo de clase y es propenso a errores. El proyecto previo del equipo ([pase de lista por Bluetooth con Arduino Nano](https://github.com/Yordi-JA/Pase-de-Asitencia-Bluetooth-Proyecto-Final-Sistemas-de-comunicaciones)) automatizó el registro, pero no verificaba la identidad: la app permitía enviar cualquier número de cuenta, así que un alumno podía registrar a otro desde dentro del salón.

**Objetivo:** automatizar el pase de lista verificando la presencia física de cada alumno mediante reconocimiento facial ejecutado localmente en la NPU de la Orange Pi. El sistema registra presente o retardo, da retroalimentación física (puerta simulada con servo), almacena y visualiza los datos, y genera reportes sin depender de servicios en la nube.

## 2. Alcance

**Incluye**
- Reconocimiento facial en la NPU: detección, alineación y embedding.
- Registro de alumnos (enrolamiento) desde un frontend web, con la ILI9341 guiando al alumno frente a la cámara. La pantalla tiene tres vistas: sesión, lista y guía de registro.
- Importación de la lista maestra desde CSV.
- Sesiones de clase con tolerancia de retardo.
- Reglas heredadas del proyecto previo: lista maestra y anti-doble registro.
- Retroalimentación física: servo como puerta simulada, LEDs RGB y buzzer.
- Etapa de potencia: MOSFET que enciende una tira LED de 12 V para iluminar la cara.
- Almacenamiento en Postgres, visualización en la pestaña de métricas del frontend y en la ILI9341.
- Flujo inverso desde la pantalla: la vista de Sesión de la ILI9341 tiene los botones táctiles "Abrir puerta" y "Probar luz LED", que publican por MQTT hacia la XIAO.
- Mejora de imagen para la cámara.
- Medición de consumo energético.

**Opcional**
- Detección de rostro en la propia XIAO ESP32S3 (cascada con la NPU).
- Detección de vida (antispoofing).

**Fuera de alcance**
- Cerradura física real.
- Integración con sistemas escolares.
- Bluetooth.
- App móvil.

## 3. Trazabilidad con los requisitos del documento

| Requisito | Cómo se cumple |
|---|---|
| 1–2. Campo de aplicación con los elementos de la Fig. 1 | Pase de lista verificable en el aula |
| 3. La ESP32 lee los sensores y los envía a la Orange Pi | La XIAO ESP32S3 lee cámara, PIR e INA219 y los envía por HTTP y MQTT; además maneja los actuadores |
| 4. Docker: Mosquitto, Node-RED y base de datos en la Orange Pi | Todo el stack vive en `orangepi/docker-compose.yml`: Mosquitto, Node-RED, Postgres con pgvector y Portainer. Las *Notas* del PDF permiten instalar todos los contenedores en la Orange Pi; Grafana no es obligatorio (las gráficas están en el frontend) e InfluxDB se sustituye por la tabla `metricas_energia` de Postgres, un desvío consciente de los puntos 4 y 5 |
| 5. Node-RED redistribuye a la base de datos, con un nodo que ejecuta el Python de la pantalla | Flujos de ingesta y pase de lista hacia Postgres; un nodo `exec` que ejecuta el programa de Python del reporte CSV, y el reinicio de la pantalla por MQTT |
| 6. Programa de Python para la ILI9341 con menú | `orangepi/pantalla/`, complementado con el frontend web de registro (`web/`), que corresponde a la opción "HTML" de la Fig. 1 |
| 7. Base de datos del sistema | Postgres con pgvector en la Orange Pi y las gráficas en la pestaña de métricas del frontend (nota de la fila 4) |
| Nota: flujo inverso hasta el actuador, con etapa de potencia | Pantalla → MQTT → XIAO ESP32S3 → servo, LEDs y MOSFET con tira LED de 12 V; la pantalla tiene los botones táctiles "Abrir puerta" y "Probar luz LED", y el control completo también está en el frontend web |
| Entregables: reporte con costo y consumo | Experimentos E5 y análisis de costo (sección 10) |

## 4. Arquitectura

![Arquitectura del sistema](diagramas/arquitectura-sistema.png)

Los diagramas de este documento están en `diagramas/`. Cada uno tiene su fuente editable `.excalidraw`, que se abre en [excalidraw.com](https://excalidraw.com), y su exportación en `.png`.

| Capa | Dispositivo | Responsabilidad |
|---|---|---|
| Campo | XIAO ESP32S3 | Fotos bajo demanda (HTTP); PIR e INA219 (MQTT); servo, LEDs RGB, buzzer y MOSFET de la tira LED de 12 V (MQTT) |
| Edge | Orange Pi Zero 3W | Servicio de visión (NPU, systemd), Docker (Mosquitto, Node-RED, Postgres con pgvector y Portainer), pantalla ILI9341, RTC DS3231, frontend web (servido por Node-RED) |
| Administración | Navegador de la laptop del profesor | Frontend web de registro de alumnos; la laptop solo abre el navegador |

**Protocolos:**
- **HTTP:** imágenes de la cámara.
- **MQTT:** telemetría, resultados y comandos.
- **MQTT (puerto 1883, TCP):** el frontend no habla MQTT; no hay listener WebSocket. El progreso del registro le llega por la API HTTP de Node-RED (mecanismo por decidir el 11 nov).
- **HTTPS (Node-RED, certificado autofirmado):** frontend estático, API de alumnos y registro, y la galería que pide el servicio de visión (`GET /api/galeria`).
- **SQL por la red del compose:** de Node-RED a Postgres, por el nombre de servicio `postgres`. El puerto 5432 solo se publica en 127.0.0.1.

## 5. Flujos

### 5.1 Pase de lista (flujo directo)

![Secuencia del pase de lista](diagramas/pase-de-lista.png)

1. El PIR de la XIAO detecta movimiento y lo publica.
2. El servicio de visión enciende la tira LED, espera unos 300 ms a que la exposición se estabilice y pide una ráfaga de fotos en alta resolución.
3. Filtro de calidad: elige la foto más nítida (varianza del Laplaciano). Si ninguna pasa el umbral, publica `motivo: imagen_insuficiente`.
4. En la NPU, detecta el rostro y sus 5 puntos clave, alinea la cara a 112×112 y calcula el embedding.
5. Compara contra la galería local por similitud coseno y publica el resultado con sus latencias.
6. Node-RED aplica las reglas de la sesión:
   - el alumno está en la lista del grupo;
   - no está ya registrado en la sesión;
   - se clasifica como presente o retardo según la tolerancia.
7. Node-RED guarda en Postgres y manda la retroalimentación:
   - **verde:** registrado, y el servo abre;
   - **rojo:** desconocido o ya registrado;
   - **ámbar:** zona gris de confianza; se repite la captura y, si persiste, el profesor lo marca manualmente desde la pantalla.

   El marcado manual es un `INSERT ... ON CONFLICT (no_cuenta, sesion_id) DO NOTHING` con `metodo = 'manual'`. Si no se insertó ninguna fila (el alumno ya estaba registrado en la sesión), Node-RED se lo avisa al profesor. Corregir una asistencia ya registrada solo lo hace el superusuario: el rol de Node-RED no tiene UPDATE ni DELETE sobre `asistencias`.

### 5.2 Control (flujo inverso)

- **Automático:** Node-RED → `aula/actuador/cmd` → XIAO ESP32S3.
- **Manual desde la pantalla:** la vista de Sesión de la ILI9341 tiene los botones táctiles **Abrir puerta** (servo) y **Probar luz LED**, que publican por MQTT hacia la XIAO. Es el flujo inverso de la Fig. 1, de la pantalla hasta el actuador. Desde la misma vista se inicia o termina la sesión.
- **Manual desde el frontend:** el control completo (servo, LEDs y buzzer) está en el frontend web, junto con la sesión y el registro.

### 5.3 Sesión de clase

El profesor inicia la sesión con materia, grupo y tolerancia. Node-RED crea el registro en Postgres, en una sola transacción: primero `UPDATE sesiones SET fin = now() WHERE fin IS NULL` (cierra una sesión olvidada) y después el `INSERT` de la nueva. El índice único parcial `sesiones_una_abierta_idx` garantiza que haya a lo más una sesión abierta, incluso si dos inicios llegan a la vez. Después publica `aula/sesion/estado`; la XIAO enciende el LED azul. Al terminar, se genera el reporte de presentes, retardos y faltas (sección 7.3).

### 5.4 Registro de alumnos (enrolamiento)

El registro tiene dos partes: primero se da de alta al alumno en la lista maestra y después se le asocia su rostro. Todo se controla desde el frontend web; la captura siempre se hace con la XIAO y el embedding siempre se calcula en la NPU, con el mismo pipeline del pase de lista.

**Parte 1: lista maestra (una vez por grupo).** El profesor sube un CSV (`no_cuenta,nombre,grupo`) desde el frontend. Node-RED lo inserta en `alumnos` con `embedding = NULL`, así que cada alumno queda **pendiente de rostro**.

**Parte 2: registro del rostro.**

![Secuencia del registro del rostro](diagramas/registro-rostro.png)

> Las flechas continuas son mensajes MQTT por Mosquitto; las discontinuas, HTTP(S), y las punteadas, SQL. Las flechas de Node-RED al frontend son la API HTTP: el frontend no habla MQTT, así que lo que visión publica para él entra por Node-RED y sale por su API (contrato MQTT, sección 2). El mecanismo del progreso en vivo se decide el 11 nov.

1. El profesor elige a un alumno pendiente en el frontend y marca el consentimiento, que el alumno firmó antes. Sin consentimiento, el botón de inicio está deshabilitado. La versión del formato de consentimiento es `docs/consentimiento.md`, aún pendiente de redactar.
2. `POST /api/enrolamiento {"cuenta","consentimiento":true}` va detrás de la autenticación de la API (HTTP Basic del profesor, sección 6.7). Node-RED registra el consentimiento con `UPDATE alumnos SET consentimiento = true, consentimiento_fecha = now() WHERE no_cuenta = $1 AND embedding IS NULL`; si no actualiza ninguna fila (la cuenta no existe o ya tiene rostro), rechaza la petición. Si ya hay una inscripción pendiente para esa cuenta (doble clic, segunda pestaña), responde 409 sin tocar nada. Guarda una inscripción pendiente de esa cuenta en el contexto del flujo, que vence tras 5 minutos sin progreso (cada mensaje `aula/vision/enrolamiento` de esa cuenta renueva el plazo), y publica la orden `enrolar`. Así el consentimiento lo escribe solo el profesor, autenticado por HTTP; nada que llegue por MQTT puede darlo.
3. El servicio de visión enciende la tira LED y toma 5 fotos, una por pose: de frente, un poco a la izquierda, un poco a la derecha, un poco hacia arriba y de frente otra vez. La ILI9341 le muestra la pose al alumno, que no ve la laptop del profesor.
4. Cada foto se valida: exactamente un rostro, cara de al menos ~100 px de ancho, nitidez mínima y giro de cabeza moderado. Si se rechaza, el frontend muestra el motivo y se repite esa pose.
5. Se calcula el embedding de cada foto en la NPU. Los que se alejan mucho del resto se descartan; los demás se promedian y se normalizan.
6. **Chequeo antifraude:** si el nuevo embedding se parece mucho al de otro alumno ya registrado, el frontend muestra una advertencia informativa. El embedding se guarda igual (no hay paso de confirmación); si el profesor no está de acuerdo, aplica la baja del rostro (`DELETE /api/alumnos/:cuenta/rostro`) y repite el registro.
7. Node-RED recibe `aula/vision/embedding` y solo acepta la `cuenta` de la inscripción pendiente. Guarda el embedding con `UPDATE alumnos SET embedding = $1::vector WHERE no_cuenta = $2 AND consentimiento AND embedding IS NULL` (es el único que escribe en la base). Si no actualiza ninguna fila, lo rechaza y registra solo la `cuenta`. Después borra la inscripción pendiente y publica `aula/vision/galeria/alta {"cuenta":...}`; el servicio de visión recarga entonces la galería con `GET /api/galeria`. Las fotos se descartan: las miniaturas solo pasan por el navegador y el embedding nunca llega al frontend.
8. **Prueba inmediata:** el alumno se para frente a la cámara y el frontend muestra el resultado y la confianza.
9. **Cancelar o expirar:** `POST /api/enrolamiento/cancelar`, o el vencimiento (5 minutos sin progreso), borra la inscripción pendiente. Si no se guardó ningún embedding, restablece `consentimiento = false, consentimiento_fecha = NULL` (con `AND embedding IS NULL`).
10. **Reinicio o despliegue de Node-RED:** las inscripciones pendientes viven en memoria y no sobreviven un reinicio. Al arrancar el proceso o al desplegar flujos, el mismo manejador primero borra todas las inscripciones pendientes del contexto y después ejecuta `UPDATE alumnos SET consentimiento = false, consentimiento_fecha = NULL WHERE consentimiento AND embedding IS NULL`, para no dejar consentimientos sin rostro.

**Reglas del registro:**
- **Mismo pipeline siempre.** El embedding del registro se calcula con el mismo modelo cuantizado en la NPU que el pase de lista. Registrar con el modelo flotante en la PC baja la precisión.
- **Misma cámara.** No se registra con la webcam de la laptop: registrarse con una cámara y reconocerse con otra también baja la precisión.
- **Pase de lista en pausa.** Mientras hay un registro en curso, el servicio de visión ignora el PIR.
- **Re-registro y baja** desde el frontend. Retirar el consentimiento es el inverso del registro: Node-RED hace `UPDATE alumnos SET embedding = NULL, consentimiento = false, consentimiento_fecha = NULL WHERE no_cuenta = $1` y publica `aula/vision/galeria/baja` con la cuenta (QoS 1, sin retención), para que el servicio de visión borre esa entrada de su galería en RAM. Si el servicio estaba caído, la baja se aplica al recargar la galería con `GET /api/galeria`. La base lo impone: no puede haber embedding sin consentimiento con fecha. Volver a registrar a alguien con rostro exige primero la baja y un consentimiento nuevo.
- **Sincronización.** El servicio de visión pide la galería completa a Node-RED (`GET /api/galeria`) al arrancar, en cada reconexión MQTT y al recibir `aula/vision/galeria/alta`. La galería solo cambia por orden de Node-RED (`alta` y `baja`).

## 6. Componentes de software

### 6.1 XIAO ESP32S3 (`firmware/xiao`)
- Core Arduino de ESP32 con PubSubClient, en C++.
- **HTTP:** `/capture?res=UXGA` para la foto. `/stream` (MJPEG en QVGA) queda para la v2: en la primera versión no se usa (ver sección 6.7).
- Descarta los primeros cuadros al despertar la cámara.
- **MQTT:** publica `aula/camara/pir`, `aula/camara/energia` y `aula/camara/estado`.
- Usar la antena externa (U.FL) y captura solo bajo demanda: la placa se calienta si transmite video continuo.

- Suscrito a `aula/actuador/cmd` y `aula/sesion/estado`; publica `aula/actuador/estado`.
- **Servo:** gira 90° (abierto), espera 3 s y regresa.
- **LEDs:** mismos patrones que el proyecto previo.
  - azul: sesión activa;
  - verde: registrado;
  - rojo: error;
  - ámbar: zona gris.
- El servo se mueve sin bloquear (temporizado con `millis()`), para que la espera de 3 s no detenga el servidor HTTP de la cámara.
- La cámara no gasta un canal LEDC: en el ESP32-S3 el XCLK lo genera el periférico LCD_CAM. El servo, el buzzer y los LEDs RGB sí tienen que repartirse los 4 timers LEDC, y cada timer fija la frecuencia de sus dos canales.

### 6.2 Servicio de visión (`orangepi/vision`, C++, nativo)
- **Detección (decidido):** RetinaFace del model zoo de Allwinner/Radxa para la A733, con su conversión y post-proceso en C++ ya hechos (R50 a 320×320 en uint8: unos 15.7 ms en la NPU, según Radxa). Pendiente de medir: entrada de 320 o 640 y backbone R50 o Mobilenet0.25. Con la cámara de la XIAO en UXGA, a 3 m la cara queda en unos 12 px en la entrada de 320, así que se detecta en la imagen reducida y el recorte para el embedding sale de la foto en resolución completa. YOLOv8n-face (5 puntos clave) queda como alternativa detrás de la misma interfaz.
- **Reconocimiento (decidido):** InsightFace `w600k_mbf` (MobileFaceNet), entrada de 112×112 y **embedding de 512 dimensiones** (acordado con Yordi para `vector(512)`).
- **Fallback del reconocimiento:** si `w600k_mbf` cuantizado pierde precisión, se prueba primero la cuantización int16 o mixta del mismo modelo y, si no basta, se cambia a `w600k_r50` (IResNet50, también de 512, así que el esquema no cambia). Criterio: comparar con los pares de LFW el ONNX flotante contra el `.nb` y aplicar el fallback si el FAR/FRR en el umbral elegido empeora de forma apreciable. Cambiar de modelo invalida los embeddings registrados: hay que volver a registrar a todos.
- **Conversión:** ONNX → Pegasus/Acuity → cuantización uint8 con imágenes de calibración → `.nb`. Para la A733 se usa Acuity **v2.0.10.2** (Docker `ubuntu-npu:v2.0.10.2`, `env.sh v3`) y se exporta con `pegasus_export_ovx_nbg.sh <modelo> uint8 a733`. El `.nb` tiene que ser compatible con el driver `vipcore` 1.13.0 y con las librerías VIPLite de la imagen: no actualizar el kernel sin volver a probar con `vpm_run`.
- **Librerías (decidido):** CMake, libmosquitto (MQTT), cpp-httplib (cliente HTTP), nlohmann/json y OpenCV 4.6 de Debian. Corre nativo con systemd, igual que la pantalla. Un solo hilo de trabajo atiende la cola de eventos y la NPU.
- **Galería en RAM:** `std::vector` con los embeddings (unos 80 KB para 40 alumnos), se carga con `GET /api/galeria` al arrancar, en cada reconexión MQTT y al recibir `aula/vision/galeria/alta` (recarga completa: son unas 40 entradas, y así hay un solo camino de carga). Nunca añade una entrada por su cuenta, ni siquiera tras calcular un embedding: la galería sigue a Node-RED. Al recibir `aula/vision/galeria/baja`, borra la entrada de esa cuenta. Ambos tópicos se suscriben con QoS 1. Desde el 11 nov, `GET /api/galeria` exige además el encabezado `X-Galeria-Token` (ver la sección 6.7). Sin base de datos local.
- **Registro:** atiende las órdenes `enrolar`, `cancelar` y `probar`; publica el progreso en `aula/vision/enrolamiento` y el embedding final en `aula/vision/embedding`. Mientras registra, ignora el PIR.
- **Mejora de imagen:** CLAHE y gamma en CPU (OpenCV). Como experimento: Zero-DCE y superresolución del recorte facial en la NPU.
- **Publica:** número de cuenta, confianza y latencias (captura, detección, embedding, total).

### 6.3 Docker en la Orange Pi (`orangepi/docker-compose.yml`)
- Mosquitto, con un solo listener, el 1883 (sin WebSocket: el frontend no habla MQTT), sin acceso anónimo y con un usuario por rol (`xiao`, `vision`, `nodered`, `pantalla` y `simulador`, este último solo para `herramientas/simular_energia.sh`). La ACL (`orangepi/mosquitto/acl`, sección 6 de `docs/contrato-mqtt.md`) hace que solo `nodered` pueda publicar en `aula/vision/galeria/#` y que solo `nodered` pueda leer `aula/vision/embedding`. El broker usa el nombre de usuario como client id (`use_username_as_clientid`).
- Node-RED, servido por HTTPS en el puerto 1880 con certificado autofirmado. El editor está protegido por usuario y contraseña (`adminAuth`).
- Postgres con la imagen `pgvector/pgvector`: sesiones, alumnos, asistencias y la tabla `metricas_energia`. Se publica solo en `127.0.0.1:5432`; Node-RED lo alcanza por la red del compose con el nombre `postgres`. Node-RED usa un rol de base de datos con privilegios mínimos sobre las tablas que usa (sin DELETE ni DDL), no el superusuario.
- Portainer CE (`portainer/portainer-ce`), el bloque que aparece en la Fig. 1: se administra desde el navegador. Solo escucha en `127.0.0.1:9443`; desde otra máquina se entra con un túnel SSH (`ssh -L 9443:localhost:9443 ...`) y `https://localhost:9443`.
- **La pantalla no corre en Docker.** Es el único componente que necesita SPI, GPIO e I²C, así que se ejecuta en el host como unidad systemd (`orangepi/systemd/orangepi-pantalla.service`), con el usuario del servicio en los grupos `spi`, `gpio` e `i2c`. Así los permisos de `/dev/spidev*` y `/dev/i2c-*` los resuelve el host con sus reglas de udev, sin mapear dispositivos al contenedor, y la pantalla sobrevive a un reinicio de Node-RED.
- **El reinicio de la pantalla va por MQTT.** Node-RED publica un mensaje de reinicio y el script de Python hace `sys.exit(0)`; systemd, con `Restart=always`, lo vuelve a levantar. El arranque y la supervisión reales los hace systemd, no Node-RED.

### 6.4 Node-RED (`nodered/flows/flows.json`)
1. **Ingesta:** MQTT → tabla `metricas_energia` de Postgres (energía, latencias y temperatura).
2. **Pase de lista:** resultado → reglas → Postgres, actuador y pantalla.
3. **Sesiones:** iniciar (cierra en la misma transacción una sesión abierta que se haya quedado olvidada), terminar y generar el reporte CSV.
4. **Pantalla:** reinicio por MQTT (el script de Python hace `sys.exit(0)` y systemd lo levanta) y un nodo `exec` que ejecuta el programa de Python que genera el reporte CSV. Con la pantalla, Node-RED habla solo por MQTT.
5. **Métricas del sistema:** temperatura del SoC, CPU y RAM → Postgres.
6. **Registro:** importación de la lista maestra; `POST /api/enrolamiento`, que registra el consentimiento del profesor, abre la inscripción pendiente (vence tras 5 min sin progreso; 409 si ya hay una) y publica la orden `enrolar`; la limpieza al arrancar o desplegar (borra las inscripciones pendientes y luego los consentimientos sin rostro); el manejador de `aula/vision/embedding`, que solo acepta la cuenta pendiente y guarda el embedding si hay consentimiento y aún no tiene rostro; y la publicación de `aula/vision/galeria/alta` (tras guardar) y `aula/vision/galeria/baja` (al retirar el consentimiento).
7. **API HTTP y frontend:** endpoints del frontend (ver frontend web) y archivos estáticos de `web/`. El frontend se compila dentro de la imagen de Node-RED (`nodered/Dockerfile`) y se sirve en `/app/` con `httpStatic`, con cabeceras de seguridad. Un `httpNodeMiddleware` propio (`nodered/autenticacion-api.js`) protege `/api` (sección 6.7); los estáticos son públicos porque no llevan secretos. Los comandos del frontend al actuador y a la sesión pasan por la API, y Node-RED los publica en `aula/actuador/cmd` y `aula/sesion/cmd`.

**Reglas de los flujos:**
- No se sirve `/api/galeria` sin la autenticación de la API y `GALERIA_TOKEN`: Node-RED no arranca sin sus credenciales (implementado en el repo; falta desplegarlo y verificarlo en la placa). No se registra a ninguna persona real sin esa autenticación verificada en la placa y sin las ACL de Mosquitto (4 nov); antes de eso, solo identidades de prueba (LFW y miembros del equipo).
- El consentimiento lo escribe solo el endpoint HTTP del profesor. El manejador de `aula/vision/embedding` nunca lo escribe: solo guarda el embedding de la cuenta pendiente cuando ya hay consentimiento.
- Los embeddings viajan siempre como parámetros enlazados (`$1::vector`), nunca concatenados en el SQL. Los nodos catch y debug de las rutas de embeddings registran solo `msg.error.message`, nunca el mensaje completo.

### 6.5 Pantalla ILI9341 (`orangepi/pantalla`, Python, táctil XPT2046)

- Programa Python ejecutado en el host por systemd. La pantalla y el táctil XPT2046 comparten el bus SPI, con dos líneas CS distintas, y el RTC DS3231 va por I²C: el proceso necesita permisos de `spi`, `gpio` e `i2c`. La pantalla (interfaz y programa Python) la mantiene Yordi, junto con el frontend; el hardware de la pantalla (overlays de SPI, unidad systemd y RTC DS3231) lo mantiene Arnau.

| Pantalla | Contenido |
|---|---|
| Sesión | Sesión activa, último registro (nombre y estado), contador de presentes; iniciar o terminar, materia, grupo y tolerancia; botones táctiles "Abrir puerta" (servo) y "Probar luz LED" |
| Lista | Presentes, retardos y faltantes |
| Guía de registro | Se activa sola durante un registro: nombre del alumno, pose a adoptar y progreso (3/5). El registro se controla desde el frontend |

La pantalla conserva los botones "Abrir puerta" y "Probar luz LED" porque el PDF exige el flujo inverso, de la pantalla hasta el actuador. El control completo del servo, los LEDs y el buzzer vive además en el frontend web, que ya tiene la sesión y el registro. Las vistas de imagen y de sistema quedan fuera de la pantalla: esa información vive en la pestaña de métricas del frontend.

### 6.6 Almacenamiento y gráficas (en la Orange Pi)
- Postgres con la imagen `pgvector/pgvector`: alumnos, sesiones, asistencias e intentos, más la tabla `metricas_energia` con las lecturas del INA219, las latencias y la temperatura del SoC.
- Los promedios del reporte salen de consultas SQL sobre esa tabla.
- **Las gráficas están en el frontend**, en la tercera pestaña, "Métricas y Asistencia", que consume la API de Node-RED:
  - asistencia por sesión;
  - latencia de inferencia;
  - potencia;
  - confianza.
- La laptop del profesor solo abre el navegador: no corre ningún contenedor.

### 6.7 Frontend web de registro (`web/`, Vite + React + TypeScript)
- Lo usa el profesor desde el navegador de su laptop, en la misma red. Se compila dentro de la imagen de Node-RED, que lo sirve en `/app/` desde la Orange Pi (`httpStatic`).
- **Todo por la API HTTP de Node-RED, sin MQTT en el navegador** (decisión de Yordi: no hay listener WebSocket ni usuario MQTT para el frontend). Los comandos al actuador y a la sesión pasan por la API, y Node-RED los publica en MQTT. El mecanismo del progreso en vivo del registro (sondeo u otro) se decide el 11 nov.
- Sin vista previa en vivo en la primera versión: el servidor de cámara de la ESP32 maneja mal un stream abierto mientras recibe capturas. Las miniaturas de cada foto dan la retroalimentación. El endpoint `/stream` existe, pero la v1 no lo usa.

| Vista | Contenido |
|---|---|
| Alumnos | Lista por grupo con estado (pendiente o registrado), búsqueda, re-registrar y borrar rostro; importación del CSV de la lista maestra |
| Registro | Consentimiento, iniciar o cancelar, miniaturas de las 5 poses con estado y motivo, advertencia de parecido, resultado, prueba inmediata y control manual del servo, los LEDs y el buzzer |
| Métricas y Asistencia | Asistencia por sesión, latencia de inferencia, potencia y confianza, con los datos de la API de Node-RED |

La importación del CSV de la lista maestra se hace desde la vista de Alumnos, con un `<input type="file" accept=".csv">` que sube el archivo a `POST /api/alumnos/importar` como cuerpo crudo (`Content-Type: text/csv; charset=utf-8`). El profesor la ejecuta una vez por grupo.

**Endpoints (Node-RED, protegidos por el `httpNodeMiddleware` de `settings.js`):**

```
GET    /api/alumnos?grupo=          lista con estado de rostro
POST   /api/alumnos/importar        CSV de la lista maestra
POST   /api/enrolamiento            {"cuenta":"320068234","consentimiento":true}
POST   /api/enrolamiento/cancelar
POST   /api/enrolamiento/probar     reconocimiento de prueba
DELETE /api/alumnos/:cuenta/rostro  retira el consentimiento: embedding NULL en la base y aula/vision/galeria/baja
GET    /api/galeria                 embeddings para el servicio de visión (no lo usa el frontend); exige X-Galeria-Token
```

`POST /api/enrolamiento` registra el consentimiento del profesor y abre la inscripción pendiente; `POST /api/enrolamiento/cancelar` la cierra; el embedding se guarda cuando llega por MQTT (sección 5.4). `DELETE /api/alumnos/:cuenta/rostro` es el retiro del consentimiento, con la baja por MQTT de la sección 7.1.

**Autenticación de la API (implementada en el repo; falta desplegarla y verificarla en la placa).** No se usa `httpNodeAuth`, que es global y bloquearía al servicio de visión: `nodered/settings.js` instala un `httpNodeMiddleware` propio (`nodered/autenticacion-api.js`) que se aplica antes de cualquier ruta `http in`:

- **Todo `/api/*` exige HTTP Basic** con un único usuario de profesor (`NODERED_PROFESOR_USER` y `NODERED_PROFESOR_PASSWORD`), comparado en tiempo constante. Sin credenciales o con credenciales erróneas, 401 con `WWW-Authenticate: Basic`. El frontend, que es del mismo origen, usa el diálogo nativo del navegador: no hay pantalla de inicio de sesión propia.
- **Excepción: `GET /api/galeria` no usa Basic.** Solo acepta el encabezado `X-Galeria-Token` igual a `GALERIA_TOKEN`, comparado en tiempo constante. Ese token solo lo tienen Node-RED y el servicio de visión, nunca el navegador: las credenciales del profesor no bastan para descargar embeddings. Con token ausente o erróneo, 401 sin `WWW-Authenticate: Basic`.
- **Falla cerrado:** si falta `NODERED_PROFESOR_USER`, `NODERED_PROFESOR_PASSWORD` o `GALERIA_TOKEN`, `settings.js` lanza una excepción y Node-RED no arranca (como con `NODERED_ADMIN_*`); Compose tampoco levanta el stack.
- **Contra CSRF**, todo método que no sea `GET` ni `HEAD`, ya autenticado, exige la cabecera propia `X-Pase-Lista: 1` y se rechaza (403) si llega `Origin` o `Sec-Fetch-Site` de otro origen: el navegador reenvía el Basic guardado aunque la petición la dispare otro sitio, pero ese sitio no puede añadir la cabecera sin un preflight CORS. Cada `fetch` del frontend que cambie datos debe mandarla; los `GET` (incluidos los del frontend actual y los de la visión) no la necesitan, y por eso **ningún `http in` GET o HEAD puede cambiar datos ni publicar en MQTT** (lo comprueba `nodered/pruebas/get-solo-lectura.test.js`).
- **Secretos:** Node-RED no arranca si `NODERED_PROFESOR_PASSWORD`, `GALERIA_TOKEN` o `NODERED_ADMIN_PASSWORD` valen `cambiar_esta_clave` o si dos de esos tres son iguales, ni si `GALERIA_TOKEN` tiene menos de 32 caracteres o `NODERED_PROFESOR_PASSWORD` menos de 16, ni si alguna de esas dos tiene menos de 8 caracteres distintos. Un valor que contenga `cambiar_esta_clave` se rechaza.
- Los estáticos de `/app/` siguen públicos y el editor conserva su `adminAuth`.
- Todas las peticiones de la API pasan por un tope de cuerpo de 256 KiB (413 si `Content-Length` lo supera, 411 si no trae `Content-Length`) y se responden sin caché.

**Contrato de los endpoints de alumnos y galería** (los tres están implementados en `nodered/flows/flows.json`, pestaña "Alumnos"):

- `GET /api/alumnos?grupo=`: arreglo `[{"cuenta":"320068234","nombre":"...","grupo":"...","rostro":"pendiente"|"registrado"}]`, ordenado por grupo y nombre. `grupo` es opcional (1 a 40 caracteres, sin caracteres de control); inválido, 400 con `{"error":"..."}`. Nunca incluye embeddings: la consulta ni siquiera los selecciona.
- `POST /api/alumnos/importar[?existentes=actualizar|omitir]`: el cuerpo es el CSV crudo (`Content-Type: text/csv; charset=utf-8`, obligatorio, otro tipo da 415; no multipart; con `X-Pase-Lista: 1`), con encabezado `no_cuenta,nombre,grupo`, de a lo más 500 filas y 256 KiB. Se valida el archivo completo antes de escribir (cuenta de 9 dígitos, nombre de hasta 120 caracteres y grupo de hasta 40, ambos no vacíos, cuentas repetidas, comillas, BOM, UTF-8). Respuestas: **400** `{"error","total_errores","filas":[{"fila":3,"error":"..."}]}` (la fila es la línea del archivo, con el encabezado en la 1; se listan hasta 100) y no se importa nada; **409** si alguna cuenta ya existe y no se indicó `existentes`: `{"escrito":false,"error","existentes":[{"cuenta","actual":{"nombre","grupo"},"csv":{"nombre","grupo"},"cambia","tiene_rostro"}],"opciones":["actualizar","omitir"]}` y no se importa nada, porque la decisión es del profesor (hay un segundo 409, `{"escrito":true,"error","insertadas","actualizadas","omitidas","total"}`, sin `existentes`: otra importación simultánea ganó la carrera por una cuenta del archivo, y las filas nuevas ya se escribieron; reenviar con `existentes` es seguro); **200** `{"insertadas","actualizadas","omitidas","total"}`; **413** si el cuerpo pesa más de 256 KiB; **415** si el `Content-Type` no es `text/csv`; **403** si falta `X-Pase-Lista: 1` o la petición es de otro origen. `actualizar` cambia solo `nombre` y `grupo` de las existentes e inserta las nuevas; `omitir` inserta solo las nuevas; otro valor de `existentes`, 400. Nunca toca `embedding`, `consentimiento` ni `consentimiento_fecha`, y nunca borra: una sola sentencia SQL (una sola transacción) con el rol de Node-RED.
- `GET /api/galeria` (solo con `X-Galeria-Token`): `{"embeddings":[{"cuenta":"320068234","embedding":[...512 números...]}]}` con los alumnos que tienen `consentimiento` y `embedding` no nulo, ordenados por cuenta; sin ninguno, `{"embeddings":[]}`. Es el formato que lee `orangepi/vision/src/gallery_store.cpp` (la primera de las formas que acepta).

## 7. Datos

### 7.1 Tópicos MQTT

![Tópicos MQTT y ACL de Mosquitto](diagramas/mqtt-acl.png)

```
aula/camara/pir          XIAO → visión           {"mov":1}
aula/camara/energia      XIAO (y simulador, solo pruebas) → Node-RED          {"v":12.1,"i":0.85,"p":10.3}
aula/camara/estado       XIAO → Node-RED          {"rssi":-58,"heap":120000,"temp":48}   (retenido)
aula/actuador/cmd        Node-RED, pantalla, visión → XIAO          {"servo":"abrir","led":"verde","buzzer":1,"luz":0}   (Node-RED o pantalla)
aula/actuador/estado     XIAO → Node-RED, pantalla          {"servo":"cerrado","led":"azul","luz":0}   (retenido)
aula/vision/resultado    visión → Node-RED       {"ts":"...","modo":"pase","decision":"reconocido","cuenta":"320068234","conf":0.78,"margen":0.21,"lat_ms":{"cap":180,"det":12,"emb":6,"total":240}}
aula/vision/cmd          Node-RED → visión       {"accion":"enrolar","cuenta":"320068234","nombre":"Ana López"}   (enrolar, cancelar, probar)
aula/vision/enrolamiento visión → LCD, Node-RED  {"ts":"...","cuenta":"320068234","paso":3,"total":5,"pose":"izquierda","estado":"rechazada","motivo":"borrosa","miniatura":"<base64>"}
aula/vision/embedding    visión → Node-RED       {"ts":"...","cuenta":"320068234","dim":512,"modelo":"w600k_mbf-uint8-v1","embedding":[...]}   (QoS 1, retain=false; solo lo consume Node-RED, que acepta únicamente la cuenta con inscripción pendiente)
aula/vision/estado       visión (+ LWT) → Node-RED  {"ts":"...","en_linea":true,"modo":"inactivo","galeria":38,"modelo":"w600k_mbf-uint8-v1"}   (retenido; el LWT del broker lleva la forma mínima {"en_linea":false})
aula/vision/galeria/alta Node-RED → visión       {"cuenta":"320068234"}   (QoS 1, nunca retenido; tras guardar el embedding: el servicio recarga la galería con GET /api/galeria)
aula/vision/galeria/baja Node-RED → visión       {"cuenta":"320068234"}   (QoS 1, nunca retenido; retira a una persona de la galería en RAM)
aula/sesion/cmd          pantalla, Node-RED → Node-RED     {"accion":"iniciar","materia":"FSE","grupo":"1","tol_min":10}
aula/pantalla/reinicio   Node-RED → pantalla     {"reiniciar":1}
aula/sesion/estado       Node-RED → XIAO, pantalla        {"activa":true,"id":12}   (retenido)
aula/sistema/metricas    Node-RED → Node-RED    {"ts":"...","temp":52.1,"cpu":34,"ram":61}   (cada 10 s; temperatura, CPU y RAM de la placa)
```

> Los valores de ejemplo son ilustrativos, no mediciones.

> **Payloads de la visión (contrato MQTT v0.3; la forma del estado, v0.4).** El detalle completo está en [`docs/contrato-mqtt.md`](contrato-mqtt.md), que manda sobre este resumen.
> - `aula/vision/resultado`: lleva `ts`, `modo` (`pase` o `prueba`), `decision` (`reconocido`, `dudoso`, `desconocido`, `sin_rostro`, `imagen_insuficiente`, `error`), `cuenta`, `conf`, `margen` y `lat_ms`, con las etapas que no corrieron en `null`. `decision` es el campo que dice qué pasó; `motivo` (opcional) da el detalle fino, porque el esquema SQL `intentos.motivo` lo necesita.
> - `aula/vision/enrolamiento`: el vocabulario de `estado` es `esperando_pose`, `capturando`, `aceptada`, `rechazada`, `advertencia`, `completado`, `cancelado` y `error`; las cinco poses son `frente`, `izquierda`, `derecha`, `arriba` y `frente_2` (la quinta es otra toma de frente, no una repetición de la primera).
> - `aula/vision/embedding`: `dim` y `modelo` son obligatorios.
> - `aula/vision/estado` (retenido): lo publica la visión en cada cambio de modo y en la salida ordenada del servicio. El Will del broker lleva la forma mínima `{"en_linea": false}`; el estado final de la salida ordenada lleva el esquema completo.

> Con la ACL (v0.5 del contrato), solo el usuario `nodered` puede publicar en `aula/vision/galeria/#`. El frontend no es un cliente MQTT: recibe estos datos por la API de Node-RED, y sus comandos entran por la API, no por `aula/actuador/cmd` ni `aula/sesion/cmd` directamente.
> Los consumidores de todo tópico con Retain = no en la tabla de la sección 2 del contrato ignoran los mensajes con retain y los de payload vacío, en cualquier tópico y no solo en `*/cmd` (contrato v0.5, sección 1; afecta al firmware de la XIAO y al servicio de visión, incluidos `aula/camara/pir` y `aula/vision/cmd`), porque la ACL no puede impedir el retain por tópico. Node-RED borra los retenidos de los `*/cmd`, `aula/camara/pir` y `aula/camara/energia` en cada conexión al broker, lo que provoca un mensaje vacío en vivo (sin retain) en cada uno de esos tópicos. Los flujos del 11 nov deben descartar retain en los demás tópicos con Retain = no que la limpieza no cubre (`aula/vision/resultado`, `aula/vision/embedding`, `aula/vision/enrolamiento`, `aula/vision/galeria/alta`, `aula/vision/galeria/baja`, `aula/pantalla/reinicio` y `aula/sistema/metricas`); la alternativa, ampliar la lista de la limpieza, está descrita en el contrato.
> `aula/sistema/metricas` lo publica Node-RED con el usuario `nodered`, que ya tiene escritura en `aula/#`; el flujo aún no existe (contrato MQTT, sección 5).

### 7.2 Esquema de Postgres (`orangepi/sql/schema.sql`)

![Modelo de datos](diagramas/modelo-datos.png)

```sql
CREATE EXTENSION IF NOT EXISTS vector;

CREATE TABLE alumnos (
  no_cuenta      VARCHAR(9) PRIMARY KEY CHECK (no_cuenta ~ '^[0-9]{9}$'),
  nombre         TEXT        NOT NULL,
  grupo          TEXT        NOT NULL,
  embedding      vector(512),            -- NULL = pendiente de rostro
  consentimiento BOOLEAN     NOT NULL DEFAULT FALSE,
  consentimiento_fecha TIMESTAMPTZ,      -- cuándo el profesor registró el consentimiento; formato: docs/consentimiento.md (pendiente)
  fecha_alta     TIMESTAMPTZ NOT NULL DEFAULT now(),
  CONSTRAINT alumnos_embedding_requiere_consentimiento
    CHECK (embedding IS NULL OR consentimiento),   -- retirar = embedding NULL, consentimiento false y fecha NULL
  CONSTRAINT alumnos_consentimiento_con_fecha
    CHECK (consentimiento = (consentimiento_fecha IS NOT NULL))
);
-- schema.sql añade autovacuum agresivo en alumnos: el embedding borrado no debe
-- quedarse como tupla muerta.

CREATE TABLE sesiones (
  id             SERIAL PRIMARY KEY,
  materia        TEXT        NOT NULL,
  grupo          TEXT        NOT NULL,
  inicio         TIMESTAMPTZ NOT NULL DEFAULT now(),
  fin            TIMESTAMPTZ,
  tolerancia_min INT         NOT NULL DEFAULT 10
);
-- A lo más una sesión abierta a la vez
CREATE UNIQUE INDEX sesiones_una_abierta_idx ON sesiones ((true)) WHERE fin IS NULL;

CREATE TABLE asistencias (
  id        SERIAL PRIMARY KEY,
  no_cuenta VARCHAR(9)  NOT NULL REFERENCES alumnos,
  sesion_id INT         NOT NULL REFERENCES sesiones,
  hora      TIMESTAMPTZ NOT NULL DEFAULT now(),
  estado    TEXT        NOT NULL CHECK (estado IN ('presente','retardo')),
  confianza REAL,
  metodo    TEXT        NOT NULL CHECK (metodo IN ('rostro','manual')),
  UNIQUE (no_cuenta, sesion_id)          -- reemplaza checkIfAlreadyRegistered()
);

CREATE TABLE intentos (
  id            SERIAL PRIMARY KEY,
  hora          TIMESTAMPTZ NOT NULL DEFAULT now(),
  sesion_id     INT REFERENCES sesiones,
  confianza_max REAL,
  motivo        TEXT CHECK (motivo IN ('desconocido','imagen_insuficiente','duplicado'))
  -- sin embedding: quien no se reconoce no dio consentimiento
);

CREATE TABLE metricas_energia (
  id            SERIAL PRIMARY KEY,
  origen        TEXT        NOT NULL,     -- ina219, npu, sistema
  v             REAL,                     -- voltaje del INA219
  i             REAL,                     -- corriente del INA219
  p             REAL,                     -- potencia del INA219
  lat_cap_ms    INT,                      -- latencia de captura
  lat_det_ms    INT,                      -- latencia de detección
  lat_emb_ms    INT,                      -- latencia de embedding
  lat_total_ms  INT,                      -- latencia total de inferencia
  temp_soc      REAL,                     -- temperatura del SoC
  hora          TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- Para GET /api/metricas/energia: filtra por origen y ordena por hora e id
CREATE INDEX IF NOT EXISTS metricas_energia_origen_hora_idx
  ON metricas_energia (origen, hora DESC, id DESC);
```

Zona horaria: `schema.sql` fija `timezone = America/Mexico_City` en la base del proyecto (`ALTER DATABASE ... SET`, sin `ALTER ROLE`: la heredan todos los roles). Los `timestamptz` se guardan en UTC; la zona decide el día que calculan `::date`, `date_trunc('day', ...)` y `CURRENT_DATE`, para que una clase a las 19:00 de CDMX no caiga en el día siguiente. `log_timezone` sigue en UTC, así que los logs no cambian. Un volumen creado antes de este ajuste lo recibe con el paso de `orangepi/README.md`, sección Volúmenes existentes.

Si el profesor exige MariaDB, solo cambian `vector` → `BLOB` y `TIMESTAMPTZ` → `DATETIME`; el resto es SQL estándar.

### 7.3 Reporte de asistencia (port de `Asistencia.py`)

```sql
SELECT a.no_cuenta, a.nombre,
       COALESCE(s.estado, 'falta') AS estado,
       s.hora
FROM alumnos a
LEFT JOIN asistencias s
       ON s.no_cuenta = a.no_cuenta AND s.sesion_id = $1
WHERE a.grupo = (SELECT grupo FROM sesiones WHERE id = $1)
ORDER BY a.no_cuenta;
```

Regla: toda consulta de reporte que agrupe o compare por día (promedios diarios de consumo, asistencia por fecha, presente o retardo por sesión) escribe la zona explícita, `(columna AT TIME ZONE 'America/Mexico_City')::date`, aunque la base ya la tenga por defecto (sección 7.2). Así el resultado no depende de la configuración de la sesión. El reporte de arriba filtra por `sesion_id`, no por día, y no la necesita.

### 7.4 Auditoría con pgvector

```sql
-- Chequeo antifraude del registro (sección 5.4, paso 6): ¿a quién se parece más el embedding nuevo?
SELECT no_cuenta, nombre, 1 - (embedding <=> $1::vector) AS similitud
FROM alumnos
WHERE embedding IS NOT NULL AND no_cuenta <> $2
ORDER BY embedding <=> $1::vector LIMIT 3;
```

Es una consulta ad hoc: `$1` es el embedding recién calculado y `$2` la cuenta que se está registrando. El servicio de visión hace esta comparación en RAM; la versión SQL sirve para auditarla. Los intentos nunca se guardan con embedding para esto (`intentos` no tiene la columna).

## 8. Hardware

| Componente | Nodo | Uso |
|---|---|---|
| Orange Pi Zero 3W 4 GB + microSD | Edge | Inferencia, Docker, pantalla |
| XIAO ESP32S3 | Campo | Cámara (sensor OV3660, hasta 2048×1536), PIR, INA219 y actuadores |
| ILI9341 con táctil XPT2046 | Edge (SPI) | Interfaz |
| RTC DS3231 (rescatado) | Edge (I²C) | Hora confiable si no hay NTP |
| PIR HC-SR501 | XIAO | Disparo de captura |
| INA219 | XIAO (I²C) | Potencia del sistema, en serie con la entrada de 12 V |
| Servo SG90 o MG90S + capacitor de 470 µF | XIAO | Puerta simulada |
| MOSFET de nivel lógico (IRLZ44N o AO3400) + resistencias | XIAO | Etapa de potencia |
| Tira LED de 12 V con difusor | XIAO vía MOSFET | Iluminación frontal |
| LEDs RGB + buzzer (rescatados) | XIAO | Retroalimentación |
| Fuente de 12 V + buck a 5 V (≥ 3 A) | Todo | Alimentación |

**Pines propuestos (ajustar al cablear):**
- **XIAO:** PIR → D0, servo → D1, compuerta del MOSFET → D2, buzzer → D3, INA219 → D4 (SDA) y D5 (SCL), LEDs RGB → D8, D9 y D10. D6 y D7 quedan libres. D8 a D10 los comparte la ranura microSD de la placa Sense, que el proyecto no usa. D2 es GPIO3, un pin de strapping del ESP32-S3 que elige el origen de las señales JTAG al arrancar y que por defecto está flotante: el pull-down de la compuerta del MOSFET lo fija a 0, así que hay que comprobar que el USB Serial/JTAG sigue funcionando.

**Notas de alimentación:**
- El servo se alimenta del buck de 5 V, nunca del pin de 3.3 V. Tierra común en todo el sistema.
- Con el INA219 en la entrada de 12 V se mide el consumo total del sistema (sin la laptop, que solo abre el navegador), que es lo que necesita la estimación de batería.
- Un brownout causado por el servo reinicia también la cámara, así que el capacitor de 470 µF va junto al servo.
- La compuerta del MOSFET lleva una resistencia de pull-down para que la tira quede apagada mientras la XIAO arranca.

## 9. Experimentos

**Participantes:** 10 inscritos más 3 a 5 no inscritos ("impostores"). Mínimo aceptable: 6 inscritos y 3 no inscritos. Todos firman el formato de consentimiento (`docs/consentimiento.md`, pendiente de redactar), y el registro de personas reales sigue la regla de la sección 6.4.

| ID | Experimento | Método | Métrica |
|---|---|---|---|
| E1 | NPU contra CPU | Mismo modelo y mismas imágenes | Latencia (ms), potencia (W) |
| E2 | Distancia | 0.5, 1, 1.5, 2 y 3 m; 5 intentos por persona | Precisión, latencia |
| E3 | Iluminación y mejora de imagen | Normal, baja y contraluz; con y sin mejora | Precisión, FAR, FRR |
| E4 | Extremo a extremo | Del PIR al servo, desglosado por etapa | Latencia (ms) |
| E5 | Consumo | INA219: reposo, captura e inferencia | Potencia promedio, duración estimada de batería |
| E6 | Contra el proyecto previo | Mismos criterios del reporte de Bluetooth | Latencia (BT: 38–96 ms) y fiabilidad |
| E7* | Cascada en la XIAO (opcional) | Detección en la S3 antes de enviar | Latencia, potencia, tráfico |

**Refuerzos estadísticos:**
- **Umbral:** calibrar la similitud con los pares de LFW (Labeled Faces in the Wild) en la PC y elegir el umbral por FAR/FRR.
- **Escala:** rellenar la galería con unas 30 identidades del dataset para simular un grupo de 40 y medir si suben los falsos positivos.

## 10. Análisis de costo y consumo (para el reporte)

- **Costo de materiales:** lista de la sección 8 con precios reales de compra.
- **Costo de ingeniería:** horas por persona (llevar registro semanal) × tarifa de referencia.
- **Consumo:** resultados de E5; estimar la duración con una batería de 12 V de capacidad conocida.

## 11. Estructura del repositorio

```
/firmware
  /xiao                  cámara, sensores y actuadores
  /pruebas-unitarias     camara, ina219, servo
/orangepi
  /vision                servicio C++, scripts de conversión (.nb fuera de git)
  /pantalla              programa Python de la ILI9341
  /systemd               unidades systemd: servicio de visión (orangepi-vision.service) y pantalla (orangepi-pantalla.service)
  /sql/schema.sql        esquema de Postgres (alumnos, sesiones, asistencias, metricas_energia)
  /sql/roles.sh         rol de Node-RED en Postgres (privilegios mínimos)
  /mosquitto             configuración, ACL (`acl`) y entrypoint del broker
  docker-compose.yml     Mosquitto, Node-RED, Postgres y Portainer
  .env.example           credenciales de MQTT, Postgres y Node-RED (copiar a .env; nunca subir el .env)
/nodered
  /flows/flows.json      flujos de Node-RED (el editor los escribe aquí al desplegar)
/web                     frontend de registro (Vite + React + TypeScript)
/herramientas            enrolamiento por lote (experimentos), calibración LFW, reporte CSV
  simular_energia.sh     publica lecturas simuladas de energía en aula/camara/energia
/docs                    diagramas, mediciones, consentimiento, reporte
```

**Convenciones:**
- Una rama por módulo (`firmware/xiao`, `vision/npu`, `borde/nodered`, etc.) y merge a `main` cuando funcione.
- Exportar `flows.json` al repo después de cada cambio.
- Modelos `.nb` y datasets fuera de git: usar releases o una carpeta compartida.
- No subir fotos de personas al repo.
- Las pruebas unitarias cubren la cámara, el INA219 y el servo: lo que no se prueba en la placa no se prueba en ninguna parte.
- El build de `web/` no se versiona: se compila dentro de la imagen de Node-RED (`nodered/Dockerfile`) y se sirve en `/app/`.

## 12. Roles y plan semanal

El trabajo se reparte en tres carriles: **visión y campo** (Gustavo), **hardware de la pantalla** (Arnau) y **edge, datos, frontend y pantalla** (Yordi). Cada hito es un miércoles y cada carril presenta un avance verificable ese día. La sección 12.3 lista las dependencias entre tareas: lo que no depende de nadie arranca de inmediato y lo que sí, espera.

### 12.1 Hitos

| Fecha | Hito | Gustavo | Arnau | Yordi |
|---|---|---|---|---|
| **28 oct** | Proyecto y planeación | Especificación y exposición | — | Repo y estructura |
| **4 nov** | Primer avance | NPU verificada en la imagen oficial de Orange Pi (hecho: `vipcore` 1.13.0, `vpm_run` a 2.76 ms); adaptadores reales de MQTT y HTTP, proceso de producción y unidad systemd del servicio de visión | Lista de compras; overlays de SPI (`ls /dev/spidev*`) y unidad systemd de la pantalla (hecho: `/dev/spidev3.0` y `/dev/spidev3.1`; `orangepi-pantalla.service`) | Compose de la Orange Pi (Mosquitto, Node-RED, Postgres, Portainer); esquema de Postgres; arranque del esqueleto del frontend; ACL de Mosquitto y usuarios por rol |
| **11 nov** | Segundo avance | Toolchain Pegasus en la PC y modelo de la NPU convertido a `.nb`; detección, alineación y embedding en la NPU; galería; publicación del resultado; orden de enrolar con captura de 5 poses; MOSFET y tira LED de 12 V; orquestación luz–captura; XIAO (la placa y su cámara ya están; los sensores y actuadores dependen de las compras): `/capture`, PIR e INA219 por MQTT, y servo y LEDs por MQTT; pruebas unitarias; prueba temprana: servo moviéndose mientras la cámara captura (valida el reparto de canales LEDC y que el servo no frena la captura) | RTC DS3231 e integración del hardware de la pantalla; prueba de la unidad systemd con la pantalla conectada | Flujos de pase de lista y de sesiones; reinicio de la pantalla por MQTT y nodo `exec` del reporte CSV; API de alumnos e importación de CSV; esqueleto del frontend; menú base de la pantalla (sesión, lista y guía de registro); autenticación de la API en Node-RED (`httpNodeMiddleware`); ingesta a `metricas_energia` y primer gráfico de energía con el INA219 |
| **18 nov** | Tercer avance | Filtro de calidad y mejora de imagen; validaciones y chequeo antifraude del registro; E1–E5; umbral con LFW | — | Frontend de registro completo, con la pestaña de Métricas y Asistencia; pantalla completa con sus 3 vistas y los botones de control |
| **25 nov** | Entrega | Integración, video y reporte de costo y consumo; comprimido de código | | Pulido del frontend y de la pantalla; E6 |

La fila del 25 de noviembre es de integración: el comprimido de código lo aporta todo el equipo.

El 11 de noviembre queda cargado para Gustavo: el toolchain, la conversión, la detección y el embedding van en cadena, junto con el firmware de la XIAO. La detección y el embedding siguen atados a los `.nb`. Si la conversión se atrasa, lo primero que se recorre al 18 de noviembre es la detección y el embedding.

### 12.2 Reparto

| Persona | Rol | Módulos |
|---|---|---|
| **Gustavo** | Visión y campo | Servicio de visión en la NPU, conversión de modelos, mejora de imagen, antifraude, firmware de la XIAO (sensores y actuadores), etapa de potencia, pruebas unitarias, E1–E5, calibración LFW |
| **Arnau** | Hardware de la pantalla y documentación | Overlays de SPI, unidad systemd de la pantalla (`orangepi-pantalla.service`), RTC DS3231, y documentación y presentaciones del proyecto (con la colaboración de todo el equipo) |
| **Yordi** | Edge, datos, frontend y pantalla | Docker, Node-RED, Postgres, API HTTP, frontend web de registro con las gráficas de métricas y asistencia, pantalla ILI9341 (3 vistas y botones), E6 |

### 12.3 Dependencias entre tareas

| Tarea | Persona | Depende de | Bloquea a |
|---|---|---|---|
| Verificación de la NPU en la imagen oficial (hecho) | Gustavo | — | El ejemplo de detección en la NPU |
| Adaptadores reales de MQTT y HTTP | Gustavo | El contrato MQTT v0.3 | El proceso de producción del servicio de visión |
| Proceso de producción y unidad systemd | Gustavo | Los adaptadores reales de MQTT y HTTP | El primer pase de lista real, que sigue atado al toolchain de modelos |
| Toolchain Pegasus en la PC | Gustavo | — | La conversión del modelo a `.nb` |
| Modelo convertido a `.nb` | Gustavo | El toolchain Pegasus | La detección y el embedding en la NPU |
| Captura, PIR e INA219 por MQTT en la XIAO (11 nov) | Gustavo | La compra del INA219 y el PIR (la XIAO y su cámara ya están) | La ingesta de energía real a Postgres |
| Servo y LEDs por MQTT (11 nov) | Gustavo | La compra del servo, los LEDs y el MOSFET | La retroalimentación del pase de lista |
| Prueba de servo y captura simultáneas (11 nov) | Gustavo | El servo y el firmware de la XIAO | — |
| Overlays de SPI y unidad systemd (hecho, 4 nov; prueba con la pantalla conectada, 11 nov) | Arnau | — | La pantalla en la Orange Pi |
| Detección, alineación y embedding en la NPU | Gustavo | El modelo `.nb` y `/dev/vipcore` | La galería y la publicación del resultado |
| Galería y publicación del resultado | Gustavo | La detección en la NPU | El pase de lista y las reglas de sesión |
| Orden de enrolar con captura de 5 poses | Gustavo | La galería y la captura de la XIAO | El registro de alumnos en el frontend |
| Filtro de calidad, mejora de imagen y antifraude | Gustavo | El embedding en la NPU | E1–E3 y el umbral con LFW |
| Umbral con LFW | Gustavo | El embedding en la NPU | E1–E3 |
| Esquema de Postgres con pgvector | Yordi | La dimensión del embedding que fija Gustavo | La API de alumnos y la galería local |
| Ingesta MQTT → `metricas_energia` y primer gráfico de energía | Yordi | El compose (el código ya está); la prueba, de la compra del INA219 y de su publicación MQTT desde la XIAO (11 nov). No se usan datos simulados como evidencia | E5 de consumo |
| Flujos de pase de lista y de sesiones | Yordi | La publicación del resultado de la visión | La asistencia en Postgres |
| API de alumnos e importación de CSV | Yordi | El esquema de Postgres | El frontend de registro |
| Reinicio por MQTT y reporte CSV | Yordi | El compose | La pantalla y el reporte final |
| Frontend de registro completo | Yordi | La API de alumnos y el orden de enrolar | — |
| Autenticación de la API en Node-RED (`httpNodeMiddleware`) y `GALERIA_TOKEN` (11 nov) | Yordi | Los flujos de Node-RED | No se sirve `/api/galeria` sin la autenticación de la API y `GALERIA_TOKEN`: Node-RED no arranca sin sus credenciales (implementado en el repo; falta desplegarlo y verificarlo en la placa). No se registra a ninguna persona real sin esa autenticación verificada en la placa y sin las ACL de Mosquitto (4 nov); antes de eso, solo identidades de prueba (LFW y miembros del equipo). |
| ACL de Mosquitto (hecho, 4 nov) | Yordi | El compose | El despliegue seguro y el registro de personas reales (regla de la fila anterior) |
| Pantalla ILI9341 con sus 3 vistas | Yordi | Los overlays de SPI y la unidad systemd | — |

### 12.4 Equipos compartidos

La Orange Pi Zero 3W y la XIAO ESP32S3 son equipos físicos únicos, así que las tareas que los ocupan se turnan. El turno de cada equipo se acuerda al inicio de cada hito, y el miércoles por la mañana se reserva como franja de demostraciones: la Orange Pi se enciende y cada carril muestra lo que trae. Con tres personas, la Orange Pi se reparte entre Gustavo para la NPU, Arnau para los overlays de SPI y la unidad systemd, y Yordi para el stack de Docker y la pantalla.

| Equipo | Tareas que lo ocupan | Quién lo usa |
|---|---|---|
| Orange Pi Zero 3W | NPU; overlays de SPI y unidad systemd; stack de Docker | Gustavo, Arnau y Yordi |
| XIAO ESP32S3 | Firmware de captura y sensores; etapa de potencia | Gustavo |

## 13. Riesgos

| Riesgo | Mitigación |
|---|---|
| La NPU deja de funcionar tras actualizar o recompilar el kernel | La placa corre la imagen oficial de Orange Pi con el kernel de fabricante `6.6.98-sun60iw2` (necesario también para ZeroTier), que trae el driver `vipcore` 1.13.0 y VIPLite; la NPU está verificada con `vpm_run`. El driver, las librerías VIPLite y el `.nb` (Acuity v2.0.10.2) tienen que ser compatibles entre sí: congelar los paquetes del kernel con `apt-mark hold` y volver a probar con `vpm_run` después de cualquier cambio de kernel |
| Conversión de modelos difícil | Empezar por los ejemplos del ai-sdk de Radxa para el A733; los modelos de rostro son CNN estándar |
| El servo, el buzzer y los LEDs RGB tienen que repartirse los 4 timers LEDC del ESP32-S3 | Cada timer fija la frecuencia de sus dos canales y todos los timers comparten reloj: el servo (50 Hz) necesita un timer propio, el buzzer otro, y los tres LEDs RGB pueden ocupar los dos que quedan. Repartir con `ledcAttach`/`ledcWrite` de Arduino 3.x, no con `ledcSetup`, y comprobar en cuanto llegue el servo (hito del 11 nov) que se mueve mientras la cámara captura. La cámara no consume canal LEDC: en el ESP32-S3 el XCLK lo genera el periférico LCD_CAM |
| Las compras del material aún no están hechas | El servo, el INA219, el PIR, el MOSFET y la tira LED de 12 V bloquean el firmware de la XIAO, la etapa de potencia y la lectura de energía. Son el camino crítico: la XIAO y su cámara ya están, pero sus sensores y actuadores dependen de estas compras, así que ese firmware queda en el hito del 11 nov. La captura por HTTP puede avanzar desde ya. La ingesta y el gráfico de energía ya están, pero su prueba espera al INA219 (11 nov): no se usan datos simulados como evidencia, y `herramientas/simular_energia.sh` queda solo como herramienta de desarrollo. No contar con el servo ni el INA219 hasta que lleguen |
| El INA219 y la SCCB de la cámara pueden terminar en el mismo puerto I2C | La SCCB de la cámara usa el puerto I2C0 con los pines GPIO39 y GPIO40 de la placa Sense, y el INA219 en D4/D5 (GPIO5 y GPIO6) también es I2C0. La forma más simple de separarlos es poner el INA219 en `Wire1` (I2C1) con los pines explícitos, `Wire1.begin(5, 6)`. La otra vía es que la cámara use un puerto ya inicializado, pero para eso el driver exige dejar `pin_sccb_sda` y `pin_sccb_scl` a -1 y pasar el número de puerto en `sccb_i2c_port`. Verificar en cuanto llegue el INA219 (hito del 11 nov) que la cámara sigue detectando el sensor con el INA219 conectado |
| Un solo microcontrolador concentra sensores y actuadores: si la XIAO se cuelga, se cae todo el nodo de campo | Watchdog y reconexión automática a Wi-Fi y MQTT; plan B: una ESP32-C3 económica solo para actuadores |
| El profesor exige MariaDB | Esquema portable (sección 7.2) |
| Mala calidad de imagen | Foto bajo demanda en alta resolución, tira LED frontal, filtro de calidad |
| La XIAO se calienta | Sin streaming continuo, solo captura bajo demanda |
| Suplantación con foto | Detección de vida por parpadeo entre cuadros (opcional) o documentarlo como limitación |
| Privacidad de datos biométricos | Consentimiento con fecha en la base (formato en `docs/consentimiento.md`, pendiente), solo embeddings de alumnos registrados (los intentos no reconocidos no guardan embedding), sin fotos en el repo. Retirar el consentimiento borra el embedding de la base al instante y de los archivos de datos tras el autovacuum; los bytes pueden sobrevivir en el WAL o en espacio libre hasta que se sobrescriban, y cualquier `pg_dump` o copia del volumen los conserva |
| La pantalla necesita SPI, GPIO e I²C | Corre en el host como servicio systemd, fuera de Docker; Node-RED la reinicia por MQTT: el script de Python hace `sys.exit(0)` y systemd, con `Restart=always`, lo levanta |
| Todo el stack concentrado en la Orange Pi (4 GB de RAM) | Vigilar la RAM con las métricas del SoC y dejar margen para el servicio de visión y el frontend; a cambio, la laptop dejó de ser una dependencia del sistema |
| Los overlays de SPI no exponen `/dev/spidev*` | Verificar en la semana 1 con `ls /dev/spidev*`; los overlays se activan con `orangepi-config` o en `/boot/orangepiEnv.txt` |
| Hora incorrecta sin internet | RTC DS3231 en la Orange Pi |
| El frontend queda en la ruta crítica del registro | Probar los endpoints desde la semana 2 con un cliente HTTP; la herramienta de enrolamiento por lote sirve de respaldo |
| Acceso no autorizado a datos biométricos desde el frontend | Autenticación de la API y `GALERIA_TOKEN` en Node-RED (11 nov) y ACL en Mosquitto (activa desde la v0.5 del contrato); los embeddings nunca llegan al navegador y el consentimiento solo lo escribe el endpoint HTTP del profesor. No se sirve `/api/galeria` sin la autenticación de la API y `GALERIA_TOKEN`. No se registra a ninguna persona real sin esa autenticación verificada en la placa y sin las ACL de Mosquitto (4 nov); antes de eso, solo identidades de prueba (LFW y miembros del equipo). |
| Un registro dispara el pase de lista | El servicio de visión ignora el PIR mientras hay un registro en curso |
| El firmware y la visión son un solo punto de fallo | Un mismo error de protocolo MQTT rompe a la vez el pase de lista, la ingesta de energía y los flujos de Node-RED. Fijar los payloads de los tópicos en el repositorio antes de escribir el firmware (que ahora empieza el 11 nov), y probar el broker con un cliente de línea de comandos antes de encadenar el resto. Ahora los dos los escribe la misma persona (Gustavo), lo que da coherencia al contrato MQTT, pero concentra la carga |
| D2 (GPIO3) es un pin de strapping del ESP32-S3 | En el arranque elige el origen de las señales JTAG y por defecto está flotante. El pull-down de la compuerta del MOSFET lo fija a 0, así que hay que verificar al conectar el MOSFET (hito del 11 nov) que el USB Serial/JTAG sigue funcionando: es la vía de consola y de programación de la placa |
