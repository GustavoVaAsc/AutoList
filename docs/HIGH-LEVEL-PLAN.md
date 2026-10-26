# Pase de lista con reconocimiento facial en Edge AI

**Materia:** Fundamentos de Sistemas Embebidos 2027-1
**Profesor:** Eduardo Fragoso Navarro
**Equipo:** Gustavo · Yordi · Arnau
**Plataforma Edge AI:** Orange Pi Zero 3W (Allwinner A733, NPU de 3 TOPS, 4 GB de RAM)

> Documento vivo. Si una decisión cambia, actualízala aquí.

---

## 1. Problema y objetivo

El pase de lista manual consume tiempo de clase y es propenso a errores. Los métodos automatizados que solo registran un número de cuenta no verifican la identidad: permiten que un alumno registre a otro desde dentro del salón.

**Objetivo:** automatizar el pase de lista verificando la presencia física de cada alumno mediante reconocimiento facial ejecutado localmente en la NPU de la Orange Pi. El sistema registra presente o retardo, da retroalimentación física (puerta simulada con servo), almacena y visualiza los datos, y genera reportes sin depender de servicios en la nube.

## 2. Alcance

**Incluye**
- Reconocimiento facial en la NPU: detección, alineación y embedding.
- Registro de alumnos (enrolamiento) desde un frontend web, con la ILI9341 guiando al alumno frente a la cámara.
- Importación de la lista maestra desde CSV.
- Sesiones de clase con tolerancia de retardo.
- Lista maestra y anti-doble registro.
- Retroalimentación física: servo como puerta simulada, LEDs RGB y buzzer.
- Etapa de potencia: MOSFET que enciende una tira LED de 12 V para iluminar la cara.
- Almacenamiento en InfluxDB y Postgres, visualización en Grafana y en la ILI9341.
- Mejora de imagen para la cámara.
- Medición de consumo energético.

**Opcional**
- Detección de rostro en la propia XIAO ESP32S3 (cascada con la NPU).
- Detección de vida (antispoofing).

## 3. Arquitectura

```mermaid
flowchart LR
  subgraph Campo
    CAM["XIAO ESP32S3 Sense<br/>cámara, PIR, INA219"]
    ACT["Pico 2 W<br/>servo, LEDs, buzzer, MOSFET"]
  end
  subgraph OPi["Orange Pi Zero 3W"]
    VIS["Servicio de visión C++<br/>NPU"]
    MQ["Mosquitto"]
    NR["Node-RED"]
    IF["InfluxDB"]
    LCD["Pantalla ILI9341<br/>Python (systemd)"]
  end
  subgraph PC
    PG[("Postgres + pgvector")]
    GF["Grafana"]
  end
  WEB["Frontend web de registro<br/>Vite + React"]

  CAM -->|HTTP fotos| VIS
  CAM -->|MQTT pir y energía| MQ
  VIS <-->|MQTT| MQ
  MQ <-->|MQTT| NR
  NR --> IF
  NR --> PG
  NR -->|exec ssh| LCD
  LCD <-->|MQTT| MQ
  MQ -->|MQTT cmd| ACT
  GF --> IF
  GF --> PG
  WEB -->|HTTP API| NR
  WEB <-->|MQTT WebSocket| MQ
```

| Capa | Dispositivo | Responsabilidad |
|---|---|---|
| Campo (sensores) | XIAO ESP32S3 Sense | Fotos bajo demanda (HTTP), PIR e INA219 (MQTT) |
| Campo (actuadores) | Raspberry Pi Pico 2 W | Servo, LEDs RGB, buzzer, MOSFET de la tira LED (MQTT) |
| Borde | Orange Pi Zero 3W | Servicio de visión (NPU), Docker (Mosquitto, Node-RED, InfluxDB), pantalla ILI9341, RTC DS3231, frontend web (servido por Node-RED) |
| Almacenamiento | PC | Docker (Postgres con pgvector, Grafana) |
| Administración | Navegador de la laptop del profesor | Frontend web de registro de alumnos |

**Protocolos:**
- **HTTP:** imágenes de la cámara.
- **MQTT:** telemetría, resultados y comandos.
- **MQTT sobre WebSocket (puerto 9001):** progreso del registro en el frontend.
- **HTTP (Node-RED):** frontend estático y API de alumnos y registro.
- **SQL por LAN:** de Node-RED a Postgres.

## 4. Flujos

### 4.1 Pase de lista (flujo directo)

```mermaid
sequenceDiagram
  participant X as XIAO (cámara)
  participant V as Visión (NPU)
  participant P as Pico (actuador)
  participant N as Node-RED
  participant D as Postgres / Influx
  participant L as Pantalla

  X->>V: aula/camara/pir
  V->>P: aula/actuador/cmd {luz:1}
  V->>X: GET /capture (ráfaga de 3 a 5)
  V->>V: filtro de calidad, detección, alineación, embedding, comparación
  V->>N: aula/vision/resultado
  N->>N: reglas de sesión (lista, duplicado, retardo)
  N->>D: asistencia y métricas
  N->>P: aula/actuador/cmd {servo:abrir, led:verde, luz:0}
  N->>L: actualización en pantalla
```

1. El PIR de la XIAO detecta movimiento y lo publica.
2. El servicio de visión enciende la tira LED, espera unos 300 ms a que la exposición se estabilice y pide una ráfaga de fotos en alta resolución.
3. Filtro de calidad: elige la foto más nítida (varianza del Laplaciano). Si ninguna pasa el umbral, publica `motivo: imagen_insuficiente`.
4. En la NPU, detecta el rostro y sus 5 puntos clave, alinea la cara a 112×112 y calcula el embedding.
5. Compara contra la galería local por similitud coseno y publica el resultado con sus latencias.
6. Node-RED aplica las reglas de la sesión:
   - el alumno está en la lista del grupo;
   - no está ya registrado en la sesión;
   - se clasifica como presente o retardo según la tolerancia.
7. Node-RED guarda en Postgres e InfluxDB y manda la retroalimentación:
   - **verde:** registrado, y el servo abre;
   - **rojo:** desconocido o ya registrado;
   - **ámbar:** zona gris de confianza; se repite la captura y, si persiste, el profesor lo marca manualmente desde la pantalla.

### 4.2 Control (flujo inverso)

- **Automático:** Node-RED → `aula/actuador/cmd` → Pico 2 W.
- **Manual:** desde la pantalla se puede abrir el servo, probar LEDs y buzzer, e iniciar o terminar la sesión.

### 4.3 Sesión de clase

El profesor inicia la sesión con materia, grupo y tolerancia. Node-RED crea el registro en Postgres y publica `aula/sesion/estado`; la Pico enciende el LED azul. Al terminar, se genera el reporte de presentes, retardos y faltas (sección 6.3).

### 4.4 Registro de alumnos (enrolamiento)

El registro tiene dos partes: primero se da de alta al alumno en la lista maestra y después se le asocia su rostro. Todo se controla desde el frontend web; la captura siempre se hace con la XIAO y el embedding siempre se calcula en la NPU, con el mismo pipeline del pase de lista.

**Parte 1: lista maestra (una vez por grupo).** El profesor sube un CSV (`no_cuenta,nombre,grupo`) desde el frontend. Node-RED lo inserta en `alumnos` con `embedding = NULL`, así que cada alumno queda **pendiente de rostro**.

**Parte 2: registro del rostro.**

```mermaid
sequenceDiagram
  participant W as Frontend web
  participant N as Node-RED
  participant V as Visión (NPU)
  participant X as XIAO (cámara)
  participant L as Pantalla ILI9341
  participant D as Postgres

  W->>N: POST /api/enrolamiento {cuenta, consentimiento}
  N->>V: aula/vision/cmd {accion: enrolar}
  loop 5 poses
    V-->>L: aula/vision/enrolamiento {paso, pose}
    V->>X: GET /capture
    V-->>W: aula/vision/enrolamiento {paso, estado, motivo, miniatura}
  end
  V->>V: consistencia entre fotos y comparación contra la galería
  V->>N: aula/vision/embedding
  N->>D: UPDATE alumnos SET embedding
  W->>N: POST /api/enrolamiento/probar
  N->>V: aula/vision/cmd {accion: probar}
  V-->>W: resultado y confianza
```

> Las flechas punteadas son mensajes MQTT que llegan al frontend y a la pantalla por Mosquitto.

1. El profesor elige a un alumno pendiente en el frontend y marca el consentimiento. Sin consentimiento, el botón de inicio está deshabilitado.
2. Node-RED valida que el alumno exista y publica la orden de registro.
3. El servicio de visión enciende la tira LED y toma 5 fotos, una por pose: de frente, un poco a la izquierda, un poco a la derecha, un poco hacia arriba y de frente otra vez. La ILI9341 le muestra la pose al alumno, que no ve la laptop del profesor.
4. Cada foto se valida: exactamente un rostro, cara de al menos ~100 px de ancho, nitidez mínima y giro de cabeza moderado. Si se rechaza, el frontend muestra el motivo y se repite esa pose.
5. Se calcula el embedding de cada foto en la NPU. Los que se alejan mucho del resto se descartan; los demás se promedian y se normalizan.
6. **Chequeo antifraude:** si el nuevo embedding se parece mucho al de otro alumno ya registrado, el frontend muestra una advertencia y el profesor decide si continúa.
7. Node-RED guarda el embedding en Postgres (es el único que escribe en la base) y el servicio de visión actualiza su galería local. Las fotos se descartan: las miniaturas solo pasan por el navegador y el embedding nunca llega al frontend.
8. **Prueba inmediata:** el alumno se para frente a la cámara y el frontend muestra el resultado y la confianza.

**Reglas del registro:**
- **Mismo pipeline siempre.** El embedding del registro se calcula con el mismo modelo cuantizado en la NPU que el pase de lista. Registrar con el modelo flotante en la PC baja la precisión.
- **Misma cámara.** No se registra con la webcam de la laptop: registrarse con una cámara y reconocerse con otra también baja la precisión.
- **Pase de lista en pausa.** Mientras hay un registro en curso, el servicio de visión ignora el PIR.
- **Re-registro y baja** desde el frontend. Borrar el embedding es la forma de retirar el consentimiento.
- **Sincronización.** Al arrancar, el servicio de visión pide la galería completa a Node-RED (`GET /api/galeria`).

## 5. Componentes de software

### 5.1 XIAO ESP32S3 Sense (`firmware/xiao-camara`)
- **HTTP:** `/capture?res=UXGA` para la foto y `/stream` (MJPEG en QVGA) para vista previa.
- Descarta los primeros cuadros al despertar la cámara.
- **MQTT:** publica `aula/camara/pir`, `aula/camara/energia` y `aula/camara/estado`.
- Usar la antena externa (U.FL) y captura solo bajo demanda: la placa se calienta si transmite video continuo.

### 5.2 Pico 2 W (`firmware/pico-actuador`)
- Arduino-Pico (core de earlephilhower) con PubSubClient, para mantener C++ en todo el firmware.
- Suscrito a `aula/actuador/cmd` y `aula/sesion/estado`; publica `aula/actuador/estado`.
- **Servo:** gira 90° (abierto), espera 3 s y regresa.
- **LEDs:**
  - azul: sesión activa;
  - verde: registrado;
  - rojo: error;
  - ámbar: zona gris.

### 5.3 Servicio de visión (`orangepi/vision`, C++, nativo)
- **Detección:** SCRFD o RetinaFace en versión mobile, con entrada fija.
- **Reconocimiento:** MobileFaceNet o ArcFace, con entrada de 112×112.
- **Conversión:** ONNX → Pegasus/Acuity → cuantización uint8 con imágenes de calibración → `.nb`. Referencia: ejemplos del ai-sdk de Radxa para el A733.
- **Galería local** en SQLite, sincronizada desde Postgres (`GET /api/galeria` al arrancar).
- **Registro:** atiende las órdenes `enrolar`, `cancelar` y `probar`; publica el progreso en `aula/vision/enrolamiento` y el embedding final en `aula/vision/embedding`. Mientras registra, ignora el PIR.
- **Mejora de imagen:** CLAHE y gamma en CPU (OpenCV). Como experimento: Zero-DCE y superresolución del recorte facial en la NPU.
- **Publica:** número de cuenta, confianza y latencias (captura, detección, embedding, total).

### 5.4 Docker en la Orange Pi (`orangepi/docker-compose.yml`)
- Mosquitto, con listener en 1883 y WebSocket en 9001 (para el frontend), y usuario y contraseña. Con ACL, el usuario del frontend no puede leer `aula/vision/embedding`.
- Node-RED.
- InfluxDB.
- **La pantalla no corre en Docker.** Es el único componente que necesita SPI, GPIO e I²C, así que se ejecuta en el host como unidad systemd (`orangepi/systemd/orangepi-pantalla.service`), con el usuario del servicio en los grupos `spi`, `gpio` e `i2c`. Node-RED la reinicia desde un nodo `exec` que corre por SSH contra el host. Así los permisos de `/dev/spidev*` y `/dev/i2c-*` los resuelve el host con sus reglas de udev, sin mapear dispositivos al contenedor, y la pantalla sobrevive a un reinicio de Node-RED.
- La imagen de Node-RED necesita `openssh-client` y una clave restringida montada en `/data`. El arranque y la supervisión reales los hace systemd con `Restart=always`: el nodo `exec` sirve para reiniciar la pantalla a mano, no para vigilarla.

### 5.5 Node-RED (`nodered/flows.json`)
1. **Ingesta:** MQTT → InfluxDB (energía, métricas, latencias).
2. **Pase de lista:** resultado → reglas → Postgres, actuador y pantalla.
3. **Sesiones:** iniciar, terminar y generar el reporte CSV.
4. **Pantalla:** nodo `exec` que reinicia por SSH la unidad systemd de la ILI9341 (`systemctl restart orangepi-pantalla`). Con la pantalla, Node-RED habla solo por MQTT.
5. **Métricas del sistema:** temperatura del SoC, CPU y RAM → InfluxDB.
6. **Registro:** importación de la lista maestra, órdenes de registro y guardado del embedding que llega por `aula/vision/embedding`.
7. **API HTTP y frontend:** endpoints del frontend (ver frontend web) y archivos estáticos de `web/` servidos con `httpStatic` desde `/data/public`. Todo protegido con `httpNodeAuth`.

### 5.6 Pantalla ILI9341 (`orangepi/pantalla`, Python, táctil XPT2046)

- Programa Python ejecutado en el host por systemd. La pantalla y el táctil XPT2046 comparten el bus SPI, con dos líneas CS distintas, y el RTC DS3231 va por I²C: el proceso necesita permisos de `spi`, `gpio` e `i2c`.

| Pantalla | Contenido |
|---|---|
| Inicio | Sesión activa, último registro (nombre y estado), contador de presentes |
| Sesión | Iniciar o terminar, materia, grupo y tolerancia |
| Lista | Presentes, retardos y faltantes |
| Guía de registro | Se activa sola durante un registro: nombre del alumno, pose a adoptar y progreso (3/5). El registro se controla desde el frontend |
| Control | Abrir el servo, probar LEDs, buzzer y tira LED |
| Imagen | Última captura original y mejorada |
| Sistema | Temperatura, latencia de la NPU, potencia, estado de la red |

### 5.7 PC (`pc/docker-compose.yml`)
- Postgres con la imagen `pgvector/pgvector`.
- Grafana con datasources de PostgreSQL e InfluxDB.
- **Paneles de Grafana:**
  - asistencia por sesión;
  - histograma de hora de llegada;
  - presentes contra faltas;
  - latencia de inferencia;
  - confianza;
  - potencia;
  - temperatura.

### 5.8 Frontend web de registro (`web/`, Vite + React + TypeScript)
- Lo usa el profesor desde el navegador de su laptop, en la misma red. Se compila y Node-RED lo sirve desde la Orange Pi (`httpStatic`).
- **mqtt.js** sobre WebSocket (puerto 9001) para el progreso en vivo; la API HTTP de Node-RED para todo lo demás.
- Sin vista previa en vivo en la primera versión: el servidor de cámara de la ESP32 maneja mal un stream abierto mientras recibe capturas. Las miniaturas de cada foto dan la retroalimentación.

| Vista | Contenido |
|---|---|
| Alumnos | Lista por grupo con estado (pendiente o registrado), búsqueda, re-registrar y borrar rostro |
| Importar | Subir el CSV de la lista maestra, vista previa y errores por fila |
| Registro | Consentimiento, iniciar o cancelar, miniaturas de las 5 poses con estado y motivo, advertencia de parecido, resultado |
| Prueba | Reconocimiento de prueba con la confianza obtenida |

**Endpoints (Node-RED, protegidos con `httpNodeAuth`):**

```
GET    /api/alumnos?grupo=          lista con estado de rostro
POST   /api/alumnos/importar        CSV de la lista maestra
POST   /api/enrolamiento            {"cuenta":"320068234","consentimiento":true}
POST   /api/enrolamiento/cancelar
POST   /api/enrolamiento/probar     reconocimiento de prueba
DELETE /api/alumnos/:cuenta/rostro  borra el embedding (retira el consentimiento)
GET    /api/galeria                 embeddings para el servicio de visión (no lo usa el frontend)
```

## 6. Datos

### 6.1 Tópicos MQTT

```
aula/camara/pir          XIAO → borde            {"mov":1}
aula/camara/energia      XIAO → borde            {"v":12.1,"i":0.85,"p":10.3}
aula/camara/estado       XIAO → borde            {"rssi":-58,"heap":120000,"temp":48}
aula/actuador/cmd        borde → Pico            {"servo":"abrir","led":"verde","buzzer":1,"luz":0}
aula/actuador/estado     Pico → borde            {"servo":"cerrado","led":"azul","luz":0}
aula/vision/resultado    visión → Node-RED       {"cuenta":"320068234","conf":0.78,"lat_ms":{"cap":180,"det":12,"emb":6,"total":240}}
aula/vision/cmd          Node-RED → visión       {"accion":"enrolar","cuenta":"320068234"}   (enrolar, cancelar, probar)
aula/vision/enrolamiento visión → frontend, LCD  {"cuenta":"320068234","paso":3,"total":5,"pose":"izquierda","estado":"rechazada","motivo":"borrosa","miniatura":"<base64>"}
aula/vision/embedding    visión → Node-RED       {"cuenta":"320068234","embedding":[...]}
aula/sesion/cmd          pantalla → Node-RED     {"accion":"iniciar","materia":"FSE","grupo":"1","tol_min":10}
aula/sesion/estado       Node-RED → todos        {"activa":true,"id":12}
aula/sistema/metricas    Orange Pi → Node-RED    {"temp":52.1,"cpu":34,"ram":61}
```

> Los valores de ejemplo son ilustrativos, no mediciones.

### 6.2 Esquema de Postgres (`pc/sql/schema.sql`, borrador)

```sql
CREATE EXTENSION IF NOT EXISTS vector;

CREATE TABLE alumnos (
  no_cuenta      VARCHAR(9) PRIMARY KEY,
  nombre         TEXT        NOT NULL,
  grupo          TEXT        NOT NULL,
  embedding      vector(512),            -- NULL = pendiente de rostro; 128 si el modelo lo usa
  consentimiento BOOLEAN     NOT NULL DEFAULT FALSE,
  fecha_alta     TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE sesiones (
  id             SERIAL PRIMARY KEY,
  materia        TEXT        NOT NULL,
  grupo          TEXT        NOT NULL,
  inicio         TIMESTAMPTZ NOT NULL DEFAULT now(),
  fin            TIMESTAMPTZ,
  tolerancia_min INT         NOT NULL DEFAULT 10
);

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
  motivo        TEXT,                    -- desconocido, imagen_insuficiente, duplicado
  embedding     vector(512)
);
```

### 6.3 Reporte de asistencia

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

### 6.4 Auditoría con pgvector

```sql
-- ¿A quién se parece más un intento desconocido?
SELECT no_cuenta, nombre, 1 - (embedding <=> $1) AS similitud
FROM alumnos ORDER BY embedding <=> $1 LIMIT 3;
```

## 7. Hardware

| Componente | Nodo | Uso |
|---|---|---|
| Orange Pi Zero 3W 4 GB + microSD | Borde | Inferencia, Docker, pantalla |
| XIAO ESP32S3 Sense | Campo | Cámara, PIR, INA219 |
| Raspberry Pi Pico 2 W (Pico W de respaldo) | Campo | Actuadores |
| ILI9341 con táctil XPT2046 | Borde (SPI) | Interfaz |
| RTC DS3231 (rescatado) | Borde (I²C) | Hora confiable si no hay NTP |
| PIR HC-SR501 | XIAO | Disparo de captura |
| INA219 | XIAO (I²C) | Potencia del sistema, en serie con la entrada de 12 V |
| Servo SG90 o MG90S + capacitor de 470 µF | Pico | Puerta simulada |
| MOSFET de nivel lógico (IRLZ44N o AO3400) + resistencias | Pico | Etapa de potencia |
| Tira LED de 12 V con difusor | Pico vía MOSFET | Iluminación frontal |
| LEDs RGB + buzzer (rescatados) | Pico | Retroalimentación |
| Fuente de 12 V + buck a 5 V (≥ 3 A) | Todo | Alimentación |

**Pines propuestos (ajustar al cablear):**
- **XIAO:** PIR → D0, INA219 → D4 (SDA) y D5 (SCL).
- **Pico 2 W:** servo → GP15, compuerta del MOSFET → GP16, LEDs → GP17 a GP19, buzzer → GP20.

**Notas de alimentación:**
- El servo se alimenta del buck de 5 V, nunca del pin de 3.3 V. Tierra común en todo el sistema.
- Con el INA219 en la entrada de 12 V se mide el consumo total del sistema (sin la PC), que es lo que necesita la estimación de batería.

## 8. Experimentos

**Participantes:** 10 inscritos más 3 a 5 no inscritos ("impostores"). Mínimo aceptable: 6 inscritos y 3 no inscritos. Todos firman el formato de consentimiento (`docs/consentimiento.md`).

| ID | Experimento | Método | Métrica |
|---|---|---|---|
| E1 | NPU contra CPU | Mismo modelo y mismas imágenes | Latencia (ms), potencia (W) |
| E2 | Distancia | 0.5, 1, 1.5, 2 y 3 m; 5 intentos por persona | Precisión, latencia |
| E3 | Iluminación y mejora de imagen | Normal, baja y contraluz; con y sin mejora | Precisión, FAR, FRR |
| E4 | Extremo a extremo | Del PIR al servo, desglosado por etapa | Latencia (ms) |
| E5 | Consumo | INA219: reposo, captura e inferencia | Potencia promedio, duración estimada de batería |
| E6* | Cascada en la XIAO (opcional) | Detección en la S3 antes de enviar | Latencia, potencia, tráfico |

**Refuerzos estadísticos:**
- **Umbral:** calibrar la similitud con los pares de LFW (Labeled Faces in the Wild) en la PC y elegir el umbral por FAR/FRR.
- **Escala:** rellenar la galería con unas 30 identidades del dataset para simular un grupo de 40 y medir si suben los falsos positivos.

## 9. Análisis de costo y consumo (para el reporte)

- **Costo de materiales:** lista de la sección 7 con precios reales de compra.
- **Costo de ingeniería:** horas por persona (llevar registro semanal) × tarifa de referencia.
- **Consumo:** resultados de E5; estimar la duración con una batería de 12 V de capacidad conocida.

## 10. Estructura del repositorio

```
/firmware
  /xiao-camara
  /pico-actuador
  /pruebas-unitarias     camara, pir, ina219, servo, mosfet, leds, rtc
/orangepi
  /vision                servicio C++, scripts de conversión (.nb fuera de git)
  /pantalla              programa Python de la ILI9341
  /systemd               unidad systemd de la pantalla (orangepi-pantalla.service)
  docker-compose.yml
/nodered
  flows.json
/pc
  docker-compose.yml
  /sql/schema.sql
  /grafana/dashboards
/web                     frontend de registro (Vite + React + TypeScript)
/herramientas            enrolamiento por lote (experimentos), calibración LFW, reporte CSV
/docs                    diagramas, mediciones, consentimiento, reporte
.env.example             credenciales de MQTT y Postgres (nunca subir el .env)
```

**Convenciones:**
- Una rama por módulo (`firmware/xiao`, `vision/npu`, `borde/nodered`, etc.) y merge a `main` cuando funcione.
- Exportar `flows.json` y los dashboards de Grafana al repo después de cada cambio.
- Modelos `.nb` y datasets fuera de git: usar releases o una carpeta compartida.
- No subir fotos de personas al repo.
- El build de `web/` no se versiona: se genera y se copia a `/data/public` de Node-RED.

## 11. Roles y plan semanal

| Persona | Rol | Módulos |
|---|---|---|
| **Gustavo** | Visión | Servicio C++, conversión de modelos, mejora de imagen, E1–E3, calibración LFW |
| **Arnau** | Campo, pantalla y Grafana | Firmware XIAO y Pico, etapa de potencia, pruebas unitarias, pantalla ILI9341, paneles de Grafana, E4–E5 |
| **Yordi** | Borde, datos y frontend | Docker, Node-RED, Postgres, API HTTP, frontend web de registro |

| Fecha | Entregable | Gustavo | Arnau | Yordi |
|---|---|---|---|---|
| **28 oct** | Proyecto y planeación | Spec y exposición | Lista de compras | Repo y estructura |
| **4 nov** | Primer avance | Armbian, verificar `/dev/vipcore`, ejemplo de detección en la NPU, toolchain Pegasus en la PC | XIAO: `/capture`, PIR e INA219 por MQTT; Pico: servo y LEDs por MQTT; pruebas unitarias; overlays de SPI (`ls /dev/spidev*`) y unidad systemd de la pantalla | Compose de la Orange Pi y la PC; esquema de Postgres; ingesta a Influx; primer panel de energía; imagen de Node-RED con `openssh-client` y clave SSH |
| **11 nov** | Segundo avance | Detección, alineación y embedding en la NPU; galería; publicar resultado; orden de enrolar con captura de 5 poses | MOSFET y tira LED; orquestación luz–captura; menú base de la pantalla (inicio, sesión, lista) | Flujos de pase de lista y sesiones; nodo `exec` por SSH; reporte CSV; API de alumnos e importación de CSV; esqueleto del frontend |
| **18 nov** | Tercer avance | Filtro de calidad, mejora de imagen; validaciones y chequeo antifraude del registro; E1–E3; umbral con LFW | Pantalla completa (guía de registro, control, imagen, sistema); Grafana completo; E4–E5 | Frontend de registro completo (alumnos, importar, registrar, probar, borrar); `httpNodeAuth` y ACL de Mosquitto |
| **25 nov** | Entrega | Integración, video, reporte (costo, consumo, conclusiones) y comprimido de código (todos) | | Pulido del frontend |

## 12. Riesgos

| Riesgo | Mitigación |
|---|---|
| La NPU no funciona en Armbian | En la imagen de Armbian de la Zero 3W el nodo `/dev/vipcore` aparece, pero la aceleración es opt-in: hay que instalar los blobs del fabricante. Plan B: imagen oficial de Orange Pi (kernel de fabricante 6.6) |
| Conversión de modelos difícil | Empezar por los ejemplos del ai-sdk de Radxa para el A733; los modelos de rostro son CNN estándar |
| El profesor exige ESP32 también para los actuadores | Plan B: una ESP32-C3 económica; el código Arduino de la Pico se porta casi sin cambios |
| El profesor exige MariaDB | Esquema portable (sección 6.2) |
| Mala calidad de imagen | Foto bajo demanda en alta resolución, tira LED frontal, filtro de calidad |
| La XIAO se calienta | Sin streaming continuo, solo captura bajo demanda |
| Suplantación con foto | Detección de vida por parpadeo entre cuadros (opcional) o documentarlo como limitación |
| Privacidad de datos biométricos | Consentimiento, solo embeddings, sin fotos en el repo |
| La pantalla necesita SPI, GPIO e I²C | Corre en el host como servicio systemd, fuera de Docker; Node-RED la reinicia por SSH |
| Los overlays de SPI no exponen `/dev/spidev*` | Verificar en la semana 1 con `ls /dev/spidev*` y `armbian-config`; plan B: imagen oficial del fabricante |
| Hora incorrecta sin internet | RTC DS3231 en la Orange Pi |
| El frontend queda en la ruta crítica del registro | Probar los endpoints con `curl` desde la semana 2; la herramienta de enrolamiento por lote sirve de respaldo |
| Acceso no autorizado a datos biométricos desde el frontend | `httpNodeAuth` en Node-RED y ACL en Mosquitto; los embeddings nunca llegan al navegador |
| Un registro dispara el pase de lista | El servicio de visión ignora el PIR mientras hay un registro en curso |
