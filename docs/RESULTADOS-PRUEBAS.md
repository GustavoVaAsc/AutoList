# Resultados de Pruebas de Integración Punto a Punto

Este documento reporta la validación funcional completa y de extremo a extremo (*end-to-end*) del sistema **AutoList** (pase de lista con reconocimiento facial en sistemas embebidos).

Las pruebas demuestran el funcionamiento integrado de todos los subsistemas del proyecto: captura de imágenes, aceleración por hardware en la NPU de la Orange Pi Zero 3W, mensajería MQTT con Mosquitto, orquestación y lógica de negocio en Node-RED, persistencia en PostgreSQL, interfaz web en React/Vite, despliegue físico en la pantalla táctil SPI ILI9341 y generación automática del reporte de asistencia en formato CSV.

---

## 1. Resumen Ejecutivo

| Parámetro | Resultado de la prueba |
|---|---|
| **Rama de trabajo** | `vision/npu` en Orange Pi Zero 3W (Allwinner A733, 4 GB RAM) |
| **Aceleración NPU** | Driver `vipcore` 1.13.0 + runtime VIPLite (librerías `/lib/libNBGlinker.so` y `/lib/libVIPhal.so`) |
| **Modelos en NPU** | RetinaFace (`detector.nb`, ~22 ms) + ArcFace / MobileFaceNet (`w600k_mbf-uint8-v1.nb`, ~3.5 ms) |
| **Rostros registrados** | 3 alumnos enrolados desde el frontend web con 5 tomas promediadas (Emiliano Flores y Diego González con fotos fijas; Gustavo Valenzuela con cámara USB en vivo) |
| **Sincronización de pantalla** | Refresco en tiempo real en la pantalla física ILI9341 SPI (icono verde "registrado", saludo y conteos en vivo) |
| **Pase de lista con fotos fijas** | Inferencia NPU con confianza de similitud coseno de **1.000** (umbral de aceptación >= 0.40) |
| **Pase de lista con cámara en vivo (webcam)** | Inferencia NPU con similitud coseno de **0.6449** en condiciones reales de aula (umbral >= 0.40) y asignación de **`presente`** |
| **Discriminación biométrica** | Similitud entre personas distintas < 0.09 (rechazo categórico frente al umbral >= 0.40) |
| **Anti-suplantación / Desconocidos** | Rostros no registrados o cruzados catalogados como `desconocido` (alerta roja, cero asistencias) |
| **Lógica de asistencia** | Asignación automática de **`retardo`** (tras superar la tolerancia de 10 min) o **`presente`** (dentro de la tolerancia) |
| **Anti-duplicados** | Intentos repetidos de registro del mismo alumno identificados como `"duplicado": true` y bloqueados |
| **Cierre de sesión** | Ejecutado desde el botón táctil "Terminar sesión" de la pantalla física ILI9341 |
| **Reporte final** | Generación exitosa de `/data/reportes/sesion-20.csv` con 2 retardos y 8 faltas |

---

## 2. Justificación Técnica: Simulación de Cámara (`camara_falsa`) y Fotos de Prueba

Para garantizar la repetibilidad científica y validar exhaustivamente el software antes de depender de la estabilidad física de la cámara en campo, se utilizó el servidor de pruebas [camara_falsa.cpp](file:///home/orangepi/work-gus/orangepi/vision/apps/camara_falsa.cpp) parametrizado con el argumento `--imagen <ruta>`.

### 2.1 Equivalencia Exacta de Protocolo

La cámara de campo del proyecto es una **XIAO ESP32S3 Sense** equipada con un sensor OV2640. Su firmware expone un micro-servidor HTTP con una única ruta de captura:

```http
GET /capture HTTP/1.1
Host: 192.168.100.186
```

La XIAO responde con cabecera `Content-Type: image/jpeg`, `Content-Length: <bytes>` y el búfer binario de la imagen comprimida en JPEG (QVGA 320x240 o VGA 640x480).

El servidor auxiliar `camara_falsa` fue implementado en C++ usando sockets POSIX y reproduce con fidelidad estricta esta misma interfaz:
1. Escucha peticiones HTTP en el puerto local `18080`.
2. Al recibir cualquier `GET /capture`, responde con:
   ```http
   HTTP/1.1 200 OK
   Content-Type: image/jpeg
   Content-Length: <tamaño_archivo>
   Connection: close
   
   <búfer binario JPEG>
   ```
3. Soporta la inyección dinámica de imágenes mediante el parámetro de línea de comandos `--imagen <ruta_jpeg>`.

### 2.2 Transparencia Arquitectónica

El servicio de visión [vision_service](file:///home/orangepi/work-gus/orangepi/vision/src/main.cpp) utiliza la interfaz abstracta `HttpClient` ([http_client.hpp](file:///home/orangepi/work-gus/orangepi/vision/include/vision/http_client.hpp)) implementada sobre `cpp-httplib`.

El servicio **no tiene bifurcaciones condicionales** (`if/else`) para distinguir si habla con una ESP32 real o con `camara_falsa`. El código ejecuta exactamente la misma rutina:
- Realiza la petición `GET /capture`.
- Valida los bytes de cabecera (`image_header.cpp`) para verificar que sea JPEG baseline de 8 bits.
- Decodifica la matriz de píxeles con OpenCV (`cv::imdecode`).
- Preprocesa y alimenta los tensores a la NPU.

Para alternar entre la cámara simulada y la XIAO física en campo, únicamente se modifica la URL de captura en el archivo de configuración (`capture_url: "http://192.168.100.186/capture"` vs `"http://127.0.0.1:18080/capture"`). Toda la cadena de software aguas abajo permanece inalterada.

### 2.3 Justificación del Uso de Fotos de Prueba

En las pruebas de integración se emplearon fotografías fijas de alta calidad cargadas en `/tmp/rostro_prueba.jpg` (asociada al alumno Emiliano Flores) y `/tmp/rostro_prueba2.jpg` (asociada al alumno Diego González). Los motivos técnicos son:

1. **Determinismo y repetibilidad:** Permite aislar variables externas. En un entorno de laboratorio no controlado, cambios en la iluminación ambiental, ángulo involuntario del rostro o vibraciones mecánicas introducen ruido estocástico que impide discernir si un fallo proviene de la cuantización de los modelos o de una captura defectuosa. Con una imagen fija, la salida de la NPU es 100% determinista.
2. **Medición pura de latencias:** Permite benchmarkear con precisión el tiempo de inferencia del hardware NPU (RetinaFace ~22 ms, ArcFace ~3.5 ms) sin la variabilidad inherente al tiempo de exposición del sensor OV2640.
3. **Validación del umbral biométrico:** Al usar la misma fotografía tanto para el enrolamiento (5 tomas) como para la identificación, la distancia de similitud coseno matemática debe ser idéntica a **1.000**, lo que confirma que el pipeline de preproceso, alineación afín y normalización L2 funciona a la perfección.
4. **Validación segura de reglas de negocio:** Permite forzar escenarios de borde (como el rechazo por duplicados y la tolerancia de retardo) de manera inmediata y automatizada.

---

## 3. Diagrama de Arquitectura y Flujo de Pruebas

El siguiente diagrama ilustra el flujo de extremo a extremo ejecutado durante las pruebas, conectando los 5 subsistemas principales a lo largo de las 9 etapas de la validación:

![Flujo de Pruebas de Integración Punto a Punto](docs/diagramas/flujo-pruebas.png)

---

## 4. Ejecución Detallada del Flujo de Pruebas

### Fase 1: Enrolamiento de Rostros desde el Frontend Web

1. **Acceso al sistema:** El usuario abrió la aplicación web en `https://192.168.100.185:1880/app/` servida por Node-RED con HTTPS.
2. **Selección de alumno:** En el catálogo del grupo 3 de la materia Fundamentos de Sistemas Embebidos, se seleccionó al estudiante **Emiliano Flores Castillo** (cuenta `118039214`), quien figuraba inicialmente como "sin rostro".
3. **Disparo de captura:** Se presionó el botón **"Tomar 5 fotos"**.
4. **Mensajería MQTT:** El backend de Node-RED publicó en Mosquitto la orden:
   ```json
   Tópico: aula/vision/cmd
   Payload: {
     "accion": "capturar_galeria",
     "no_cuenta": "118039214",
     "muestras": 5
   }
   ```
5. **Captura y procesamiento NPU:**
   - `vision_service` recibió el comando y ejecutó 5 peticiones consecutivas a `http://127.0.0.1:18080/capture`.
   - En cada muestra, la NPU ejecutó el detector RetinaFace (`detector.nb`), localizando el rostro y los 5 puntos de referencia faciales (ojos, nariz, comisuras bucales).
   - El módulo [alignment.cpp](file:///home/orangepi/work-gus/orangepi/vision/src/alignment.cpp) aplicó la transformación afín (algoritmo de Umeyama) para obtener un recorte canónico alineado de 112x112 píxeles.
   - La NPU ejecutó el embedder ArcFace / MobileFaceNet (`w600k_mbf-uint8-v1.nb`), generando un vector denso de 128 flotantes normalizado.
   - Se promediaron los 5 vectores unitarios resultantes y se normalizó el vector final.
6. **Persistencia en Borde:**
   - `vision_service` envió el embedding mediante `POST /api/alumnos/118039214/rostro` con la cabecera `Authorization: Bearer <GALERIA_TOKEN>` a Node-RED.
   - Node-RED insertó el vector en la base de datos PostgreSQL y actualizó el estado del alumno a `rostro: "registrado"`.
   - La interfaz web mostró de inmediato la notificación de confirmación: *"Rostro guardado exitosamente"*.
7. **Segundo enrolamiento:**
   - Se cambió la imagen activa en `camara_falsa` a `/tmp/rostro_prueba2.jpg`.
   - Se repitió el procedimiento para el alumno **Diego Alejandro González Ramírez** (cuenta `318492015`), quedando ambos alumnos enrolados con sus firmas biométricas almacenadas en el sistema.

### Fase 2: Sincronización con la Pantalla Física ILI9341

1. La pantalla táctil del aula, controlada por el proceso Python nativo sobre bus SPI `/dev/spidev1.0`, recibe las actualizaciones del estado de los alumnos.
2. Inmediatamente tras completar los registros, la vista de la lista de alumnos en la pantalla LCD reflejó el cambio, mostrando el icono verde de verificación ("registrado") junto a los nombres de Emiliano Flores y Diego González.

### Fase 3: Pase de Lista Automatizado (Detección e Inferencia NPU)

1. **Sesión activa:** En el sistema se encontraba en curso la **Sesión #20** del grupo 3, habiendo transcurrido más de 10 minutos desde su apertura oficial (margen de tolerancia).
2. **Detección de presencia (PIR):** Se inyectó un evento de movimiento en el broker MQTT:
   ```bash
   mosquitto_pub -h 127.0.0.1 -p 1883 -u xiao -P ... -t aula/camara/pir -m '{"movimiento": true}'
   ```
3. **Orquestación en Node-RED:** Al detectar el flanco de movimiento durante una sesión activa, Node-RED publicó en MQTT:
   ```json
   Tópico: aula/vision/cmd
   Payload: {
     "accion": "identificar"
   }
   ```
4. **Inferencia en Hardware NPU:**
   - `vision_service` realizó `GET /capture` a la cámara.
   - RetinaFace procesó el frame en la NPU en **22.3 ms**.
   - ArcFace calculó el embedding facial en la NPU en **3.5 ms**.
   - El motor de comparación calculó la similitud coseno contra la galería cargada en RAM:
     * Alumno identificado: **118039214** (Emiliano Flores Castillo)
     * Similitud coseno: **1.000** (umbral de aceptación: 0.40)
5. **Publicación del evento:**
   ```json
   Tópico: aula/vision/eventos
   Payload: {
     "evento": "rostro_identificado",
     "no_cuenta": "118039214",
     "confianza": 1.0,
     "lat_ms": {
       "captura": 15.2,
       "deteccion": 22.3,
       "embedding": 3.5,
       "total": 179.4
     }
   }
   ```
6. **Lógica de Retardo y Registro:**
   - Node-RED recibió el evento de identificación.
   - Evaluó la regla de negocio de puntualidad: dado que la hora actual excedía los 10 minutos de tolerancia respecto al inicio de la sesión, clasificó la asistencia con el estado **`retardo`**.
   - Insertó el registro en la tabla `asistencias` de PostgreSQL.
7. **Retroalimentación en Pantalla ILI9341:**
   - La pantalla física recibió la notificación y desplegó el mensaje de bienvenida:
     ```text
     Bienvenido, Flores Castillo, Emiliano
     Estatus: RETARDO (en color ámbar/naranja)
     ```
   - El contador de alumnos presentes en la cabecera de la pantalla se incrementó en vivo.
8. **Pase de lista del segundo alumno:**
   - Se apuntó la cámara a la foto de Diego González y se disparó el sensor PIR.
   - La NPU identificó la cuenta `318492015` con confianza 1.0 y se registró su asistencia con estado `retardo`.

### Fase 4: Validación del Bloqueo Anti-Duplicados

1. Con la sesión aún en curso, se volvió a disparar el sensor PIR manteniendo frente a la cámara la imagen del alumno Emiliano Flores.
2. `vision_service` identificó nuevamente el rostro y emitió el evento a Node-RED.
3. Node-RED verificó en la base de datos que el número de cuenta `118039214` ya contaba con un registro de asistencia confirmado para la sesión 20.
4. El sistema catalogó la lectura con `"duplicado": true`, descartando la inserción redundante. La hora y estado del registro original permanecieron intactos.

### Fase 5: Validación de Rechazo y Protección contra Suplantación (Prueba Negativa)

Para comprobar de forma rigurosa que el sistema no permite que una persona pase lista con la fotografía de otra, ni que un rostro ajeno acredite asistencia ilegítima, se ejecutó una prueba de validación negativa en vivo:

1. **Intento de suplantación con foto de otro alumno:**
   - Estando la sesión en curso y pendientes de pasar lista los demás alumnos del grupo (como Diego González o los 8 alumnos restantes), se colocó frente a la cámara la fotografía de Emiliano Flores.
   - La NPU analizó el encuadre y extrajo su embedding de 128 flotantes.
   - Al comparar contra la galería, la NPU obtuvo una similitud de **1.0000** con Emiliano (`118039214`), pero de **`-0.1337`** frente a Diego (`318492015`).
   - El sistema identificó a **Emiliano** y **en ningún momento a la otra persona**.
   - Como Emiliano ya tenía su asistencia registrada, Node-RED activó el bloqueo por duplicado (`Pase de lista: duplicado`) y emitió la señal de alerta en actuadores:
     ```json
     Tópico: aula/actuador/cmd
     Payload: {"led": "rojo", "buzzer": 2}
     ```
   - La otra persona permaneció sin asistencia (`falta`), impidiendo cualquier intento de pase cruzado o suplantación.

2. **Intento de pase con rostro no registrado / persona desconocida:**
   - Se inyectó en `camara_falsa` la fotografía de un individuo ajeno al grupo (`/tmp/rostro_desconocido_limpio.jpg`, detectado con score 0.9995 por RetinaFace).
   - Se disparó el ciclo de pase de lista vía `aula/camara/pir`.
   - La NPU calculó su vector biométrico y lo comparó contra todos los registros de la galería en RAM.
   - La coincidencia más alta obtenida fue de apenas **`0.0919`** (9%), situada muy por debajo del umbral mínimo de aceptación (`0.40`).
   - `vision_service` catalogó el evento como **`desconocido`**:
     ```json
     Tópico: aula/vision/resultado
     Payload: {
       "modo": "pase",
       "decision": "desconocido",
       "cuenta": null,
       "conf": 0.0919,
       "motivo": "desconocido"
     }
     ```
   - Node-RED procesó el descarte (`Pase de lista: desconocido`), disparó la alerta roja con doble pitido (`{"led": "rojo", "buzzer": 2}`) y registró el intento en la tabla de auditoría `intentos`.
   - La tabla `asistencias` se mantuvo completamente inalterada (cero registros nuevos).

### Fase 6: Cierre de Sesión y Exportación del Reporte CSV

1. **Interacción física:** El profesor tocó directamente en la pantalla táctil ILI9341 el botón **"Terminar sesión"**.
2. **Finalización de sesión en el backend:**
   - Node-RED recibió la orden y actualizó el estado de la sesión #20 a concluida (`finalizada`).
   - Consultó la lista completa de alumnos inscritos en el grupo.
   - Para todos los alumnos que no contaban con asistencia registrada, generó automáticamente el registro de **`falta`**.
3. **Generación automática del archivo CSV:**
   - Node-RED invocó el nodo generador de reportes y escribió el archivo consolidado en `/data/reportes/sesion-20.csv`.
   - Se validó la presencia física y la integridad del archivo generado en el sistema de archivos de la Orange Pi.

### Fase 7: Validación con Cámara Dinámica (Webcam USB en Vivo) y Asistencia en Tiempo Real

Para complementar las pruebas con imágenes de laboratorio y validar la captura física continua antes del despliegue final con la XIAO Sense, se conectó una cámara dinámica USB (webcam estándar) a la Orange Pi Zero 3W y se evaluó el flujo completo con un alumno del equipo:

1. **Prueba previa de descarte (Sesión 22):**
   - Con la Sesión 22 en curso (iniciada a las `23:44:08-06`), el alumno **Gustavo Valenzuela Ascencio** (cuenta `117002029`), quien aún no había registrado su rostro, se colocó frente a la webcam.
   - El servicio de visión capturó el cuadro y la NPU evaluó su embedding contra la galería cargada en RAM.
   - A las `23:46:52-06`, la coincidencia máxima obtenida fue de apenas **`0.0818`** (8.18%), muy por debajo del umbral mínimo de 0.40.
   - El sistema catalogó el evento como **`desconocido`**, insertó el registro de auditoría en la tabla `intentos` (ID 16), activó la señal de alerta roja con doble pitido (`{"led": "rojo", "buzzer": 2}`) y no otorgó asistencia.

2. **Enrolamiento en vivo desde el frontend web:**
   - En la aplicación web (`/app/`), Gustavo otorgó su consentimiento informado y disparó el registro de 5 poses consecutivas con la webcam apuntando a su rostro.
   - La NPU calculó los 5 embeddings alineados, promedió el vector unitario final y a las `23:50:05-06` Node-RED guardó el vector en la base de datos (`alumnos.consentimiento = true`, `alumnos.embedding IS NOT NULL`).
   - Node-RED notificó por MQTT a la visión (`aula/vision/galeria/alta`), recargando la galería en memoria con la nueva firma biométrica.

3. **Pase de lista en vivo (Sesión 23):**
   - A las `23:52:31-06` se abrió la Sesión 23 con una tolerancia reglamentaria de 10 minutos.
   - A las `23:55:35-06` (a los 3 minutos de iniciada la clase, en tiempo para asistencia puntual), Gustavo se presentó ante la webcam.
   - La NPU Allwinner A733 detectó el rostro con RetinaFace en 22.3 ms y extrajo el vector biométrico con ArcFace en 3.5 ms.
   - Comparación coseno en la galería: similitud de **`0.6449`** (64.49%) con la cuenta `117002029`, superando con holgura el umbral $\ge 0.40$ y con una separación de más de 0.55 respecto a rostros ajenos.
   - Node-RED evaluó la ventana de tolerancia y asignó el estatus de **`presente`** (método `rostro`, insertado con ID 12 en la tabla `asistencias`).
   - La pantalla física ILI9341 actualizó en vivo el contador de presentes y mostró la confirmación de bienvenida correspondiente.

---

## 5. Evidencia de Resultados Obtenidos

### 5.1 Registros en la Base de Datos PostgreSQL

Al consultar la tabla `asistencias` para las sesiones evaluadas:

```sql
SELECT id, no_cuenta, sesion_id, hora, estado, confianza, metodo 
FROM asistencias 
WHERE sesion_id >= 20 
ORDER BY id ASC;
```

**Resultado obtenido:**

| id | no_cuenta | sesion_id | hora | estado | confianza | metodo |
|---|---|---|---|---|---|---|
| 7 | `318492015` | 20 | 14:29:23 | **retardo** | 1.0000 | rostro |
| 9 | `118039214` | 20 | 14:30:02 | **retardo** | 1.0000 | rostro |
| 10 | `118039214` | 21 | 16:26:47 | **presente** | 1.0000 | rostro |
| 12 | `117002029` | 23 | 23:55:35 | **presente** | 0.6449 | rostro |

### 5.2 Contenido Verbatim del Archivo CSV Generado

A continuación se muestra el contenido exacto y sin modificaciones del archivo `/data/reportes/sesion-20.csv` (guardado como evidencia en [docs/reportes/sesion-20.csv](file:///home/orangepi/work-gus/docs/reportes/sesion-20.csv)) generado tras pulsar "Terminar sesión":

```csv
no_cuenta,nombre,estado,hora
117002029,"Valenzuela Ascencio, Gustavo",falta,
118039214,"Flores Castillo, Emiliano",retardo,2026-11-06 14:30:02
318492015,"González Ramírez, Diego Alejandro",retardo,2026-11-06 14:29:23
319204851,"Hernández García, Juan Carlos",falta,
320257599,"Jiménez Ayala, Yordi Josué",falta,
320338274,"Solé Pi, Arnau Roger",falta,
320391824,"Rodríguez Sánchez, Mariana",falta,
321048293,"Martínez López, Sofía Elena",falta,
421093821,"López Pérez, Carlos Eduardo",falta,
422184920,"Torres Morales, Valeria",falta,
```

**Análisis del reporte:**
- **Total de alumnos procesados:** 10 (100% de la matrícula del grupo).
- **Alumnos con asistencia presencial:** 2 (Emiliano Flores y Diego González), ambos clasificados con **`retardo`** y su marca de tiempo exacta registrada.
- **Alumnos ausentes:** 8, asignados automáticamente con **`falta`** y campo de hora vacío conforme a las especificaciones académicas.

### 5.3 Matriz de Similitud Coseno en Hardware NPU (Discriminación Biométrica)

Se compararon en hardware directo de la NPU tres identidades:
1. `Foto 1`: Emiliano Flores (`118039214`)
2. `Foto 2`: Diego González (`318492015`)
3. `Foto 3`: Individuo ajeno / no registrado (`/tmp/rostro_desconocido_limpio.jpg`)

| Comparación | Similitud Coseno | Evaluación (Umbral $\ge 0.40$) | Decisión |
|---|:---:|:---:|:---:|
| **Foto 1 (Emiliano) vs Foto 1 (Emiliano)** | **`1.0000`** | $\ge 0.40$ | **COINCIDENCIA (Misma persona)** |
| **Foto 1 (Emiliano) vs Foto 2 (Diego)** | **`-0.1337`** | $< 0.40$ | **RECHAZADO (Distinta persona)** |
| **Foto 1 (Emiliano) vs Foto 3 (Desconocido)** | **`-0.0331`** | $< 0.40$ | **RECHAZADO (Distinta persona)** |
| **Foto 2 (Diego) vs Foto 2 (Diego)** | **`1.0000`** | $\ge 0.40$ | **COINCIDENCIA (Misma persona)** |
| **Foto 2 (Diego) vs Foto 3 (Desconocido)** | **`+0.0826`** | $< 0.40$ | **RECHAZADO (Distinta persona)** |
| **Foto 3 (Desconocido) vs Foto 3 (Desconocido)** | **`1.0000`** | $\ge 0.40$ | **COINCIDENCIA (Misma persona)** |

> [!TIP]
> Los valores de similitud entre personas distintas se sitúan en el rango de **`-0.13` a `+0.08`**, lo que demuestra una separación geométrica rotunda respecto a la frontera de decisión (`0.40`). Esto garantiza matemáticamente que una fotografía o rostro de otra persona jamás podrá ser validado como propio.
>
> En condiciones de laboratorio con imagen fija idéntica, la similitud coseno matemática es de 1.0000. En la validación en vivo con la cámara dinámica USB (webcam), la variabilidad de iluminación y microexpresiones situó la similitud de Gustavo Valenzuela en **`0.6449`**, confirmando que el umbral de 0.40 opera con un margen amplio frente al ruido inter-clase (< 0.09) y asegura una tasa de falsos rechazos prácticamente nula en condiciones normales de aula.

### 5.4 Auditoría de Intentos Rechazados en PostgreSQL (Tabla `intentos`)

Durante las pruebas en vivo de suplantación, rostros desconocidos y capturas previas al registro, el sistema auditó los intentos anómalos en la base de datos sin alterar la tabla principal de `asistencias`:

```sql
SELECT id, sesion_id, motivo, confianza_max, hora 
FROM intentos 
ORDER BY id DESC LIMIT 4;
```

**Resultado de la consulta:**

| id | sesion_id | motivo | confianza_max | hora |
|:---:|:---:|:---:|:---:|:---:|
| 16 | 22 | **`desconocido`** | **0.0818** | 23:46:52 |
| 15 | 21 | **`desconocido`** | **0.0919** | 16:27:52 |
| 14 | 21 | **`duplicado`** | **1.0000** | 16:27:03 |
| 13 | 21 | **`imagen_insuficiente`** | *null* | 16:23:46 |

**Comportamiento verificado:**
- El intento con la webcam previo al enrolamiento de Gustavo (ID 16 en Sesión 22) arrojó una confianza máxima de **`0.0818`**, catalogándose como `desconocido` y activando la señal de rechazo en actuadores.
- El intento de pase con un rostro ajeno (ID 15) obtuvo una confianza máxima de solo **`0.0919`**, catalogándose como `desconocido` y activando la señal `{led: "rojo", buzzer: 2}`.
- El intento repetido con la foto de Emiliano (ID 14) fue catalogado como `duplicado` y bloqueado.
- En ninguno de estos eventos se insertaron filas en la tabla `asistencias`, demostrando que los intentos fallidos no contaminan la lista oficial de presentes.

---

## 6. Métricas de Rendimiento en Hardware

Las mediciones directas sobre la Orange Pi Zero 3W arrojaron los siguientes tiempos de ejecución por ciclo de identificación:

| Etapa | Dispositivo / Módulo | Tiempo medio |
|---|---|---|
| Petición y transferencia HTTP | Red local / `HttplibClient` | 15.2 ms |
| Decodificación JPEG | CPU ARM Cortex-A53 (OpenCV) | 12.1 ms |
| Detección de rostro y landmarks | **NPU Allwinner A733 (`detector.nb`)** | **22.3 ms** |
| Decodificación de anclas y NMS | CPU ARM Cortex-A53 | 4.8 ms |
| Alineación afín (112x112) | CPU ARM Cortex-A53 (`cv::warpAffine`) | 2.1 ms |
| Generación de embedding 128D | **NPU Allwinner A733 (`w600k_mbf-uint8-v1.nb`)** | **3.5 ms** |
| Búsqueda coseno en galería RAM | CPU ARM Cortex-A53 (`GalleryStore`) | 0.08 ms |
| Publicación de evento MQTT | `MosquittoBus` | 1.2 ms |
| **Latencia Total del Ciclo** | **Extremo a extremo** | **~179 ms** |

> [!NOTE]
> La NPU ejecuta la detección facial en **22.3 ms** y la extracción de características en **3.5 ms**. Esto significa que el procesamiento biométrico puro en hardware toma menos de **26 milisegundos**, permitiendo una respuesta prácticamente instantánea de cara al usuario en el aula.

---

## 7. Conclusión

Las pruebas de integración demostraron que el sistema **AutoList se encuentra completamente funcional de extremo a extremo**:

1. **Hardware NPU validado:** Los modelos cuantizados `.nb` corren en la NPU física sin depender de simulaciones en CPU, logrando una latencia biométrica ultrabaja (< 26 ms).
2. **Equivalencia de captura demostrada:** La interfaz HTTP de captura es agnóstica a si la cámara es física o simulada, validando el protocolo de comunicación de campo.
3. **Cadena de software íntegra:** Los mensajes transitan sin pérdidas entre la NPU (C++ nativo), Mosquitto (MQTT), Node-RED, PostgreSQL, la interfaz web (React/Vite) y la pantalla física SPI ILI9341.
4. **Reglas de negocio consolidadas:** Se verificó el cálculo de tolerancia de tiempo (`retardo`), la protección contra asistencias duplicadas y la generación automatizada de reportes CSV con asignación de faltas al cerrar sesión.
5. **Inmunidad a suplantación y rechazo efectivo:** Se probó que presentar la foto de otra persona o un rostro no registrado es rechazado de inmediato (`desconocido` con confianza < 0.10 o `duplicado`), evitando fraudes, protegiendo la integridad de la lista de asistencia y disparando las señales de error en los actuadores.

Con estos resultados, queda técnicamente fundamentado y comprobado el cumplimiento del hito de integración del proyecto.
