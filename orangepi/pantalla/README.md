# Pantalla ILI9341

Programa de la pantalla ILI9341 de AutoList (plan, sección 6.5). Corre en el host como unidad systemd, no en Docker (`../systemd/orangepi-pantalla.service`). La mantiene el carril de Yordi; el hardware (overlays de SPI y la unidad) es de Arnau.

**Estado.** El 4 nov se probó por primera vez la versión mínima (la pantalla de arranque con la hora) con la pantalla conectada: Yordi confirmó que se ve bien (texto, colores, orientación vertical y la hora avanza) y que, al recibir SIGTERM, el programa suelta las líneas DC (139) y RST (33). Esa prueba también dio por buena, de forma implícita, la hipótesis del CS0: la imagen se vio, así que el chip select que baja el kernel basta para la pantalla.

Esta versión es el **menú base** del plan (sección 6.5): las vistas Sesión, Lista y Guía de registro, navegación con el táctil XPT2046 y un cliente MQTT con el usuario `pantalla`. Está verificada **sin la pantalla delante**: las pruebas unitarias (sin hardware ni broker), una corrida de 20 s sobre los dispositivos reales con una clave ficticia (abrió el SPI de la pantalla y del táctil y los GPIO, mostró "sin conexión" sin caerse, salió con 0 al recibir SIGTERM y soltó las líneas 32, 33 y 139) y el cliente MQTT contra un broker de pruebas mínimo. **Nadie ha visto aún cómo se ven las vistas ni ha tocado la pantalla**: el táctil y su calibración siguen sin probar. Ver [Sin verificar](#sin-verificar).

Aún no hay I²C ni RTC. La vista Lista y el "último registro" y el contador de presentes de Sesión no tienen fuente de datos (el contrato no define un tópico y Node-RED no tiene flujos de sesiones): muestran "sin datos". Ver [Huecos de datos](#huecos-de-datos).

## Contenido

- [Archivos](#archivos)
- [Pines y bus](#pines-y-bus)
- [Crear el venv](#crear-el-venv)
- [Correrlo a mano](#correrlo-a-mano)
- [Vistas y controles](#vistas-y-controles)
- [MQTT](#mqtt)
- [Decisiones de diseño](#decisiones-de-diseño)
- [Huecos de datos](#huecos-de-datos)
- [Configuración](#configuración)
- [Calibrar el táctil](#calibrar-el-táctil)
- [Pruebas](#pruebas)
- [Instalar la unidad systemd](#instalar-la-unidad-systemd)
- [Cambios que necesita la unidad (propuesta para Arnau)](#cambios-que-necesita-la-unidad-propuesta-para-arnau)
- [Sin verificar](#sin-verificar)

## Archivos

| Archivo | Qué es |
|---|---|
| `main.py` | Programa: configuración (`CONFIG`), bucle, apagado limpio y códigos de salida |
| `estado.py` | Estado de la pantalla, lectura y descarte de los mensajes MQTT, y qué publica cada botón. Sin hardware ni red |
| `vistas.py` | Las tres vistas (imágenes PIL de 240x320) y la geometría de los botones. Sin hardware ni red |
| `cliente_mqtt.py` | Cliente MQTT (paho) con el usuario `pantalla`: reconexión, suscripciones y cola de eventos |
| `tactil.py` | Calibración, conversión de coordenadas crudas a píxeles y detección del toque. Sin hardware |
| `calibrar_tactil.py` | Herramienta para medir la calibración del táctil con la pantalla delante |
| `ili9341_opi.py` | Controlador de la pantalla sobre `python-periphery` (copiado de `~/Documents/1_Pantalla`, con los comentarios corregidos para la Zero 3W) |
| `xpt2046_opi.py` | Controlador del táctil XPT2046 |
| `pantalla.env.example` | Plantilla de `/etc/pase-lista/pantalla.env` (solo variables `PANTALLA_*`, sin secretos); el real está en `.gitignore` |
| `test_*.py` | Pruebas sin hardware (`unittest`) |
| `requirements.txt` | Dependencias con versión fija |

## Pines y bus

| Señal | Pin físico | Línea GPIO (`/dev/gpiochip0`) |
|---|---|---|
| DC (LCD_RS) | 15 | 139 |
| RST | 13 | 33 |
| TP_IRQ (táctil) | 11 | 32 |
| LCD_CS | 24 | La baja el kernel (CS0 del bus SPI3) |

| Dispositivo | Nodo |
|---|---|
| Pantalla ILI9341 | `/dev/spidev3.0` |
| Táctil XPT2046 | `/dev/spidev3.1` |

El overlay es `spi3-cs0-cs1-spidev` (`/boot/orangepiEnv.txt`). La pantalla va a 40 MHz, modo SPI 0.

Estos números son los de la Zero 3W (SoC A733), los que usan `diagnostico.py` y los valores por defecto de los controladores de `~/Documents/1_Pantalla`. **No son los del `LEEME.md` de esa carpeta** (DC 261, RST 227, LCD_CS 229, TP_IRQ 226): son de la Zero 2W (H616). El controlador de esa carpeta los traducía a los de la 3W en silencio, así que cualquier programa que pasara los de la 2W funcionaba igual. Aquí no hay traducción: lo que se configura es lo que se usa. Los números de línea de la 3W vienen del código ya probado; los nombres de puerto del A733 no se comprobaron y no se documentan.

`PANTALLA_CS` permite pasar una línea GPIO para que el código maneje LCD_CS, si algún overlay creara el nodo sin asignar el pin. Con el overlay actual no hace falta.

## Crear el venv

Desde `orangepi/pantalla/` (el venv está en `.gitignore`):

```bash
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
```

Necesita red. No se usa `--system-site-packages`: numpy no viene como paquete del sistema y el Pillow del sistema (9.4, `apt`) es distinto del fijado (12.3). Un venv aislado instala las ruedas arm64 de `requirements.txt` (las mismas versiones que ya se probaron en `~/Documents/venv_embebidos`) y no depende de lo que traiga la imagen. La fuente es DejaVu, del sistema: `/usr/share/fonts/truetype/dejavu/`.

## Correrlo a mano

Con la unidad detenida: dos procesos no pueden tomar las mismas líneas GPIO, y dos programas con el usuario MQTT `pantalla` se expulsan entre sí (el broker usa el usuario como client id; contrato, sección 6).

```bash
cd orangepi/pantalla
( read -rs MQTT_PANTALLA_PASSWORD && export MQTT_PANTALLA_PASSWORD && exec .venv/bin/python main.py )   # pega la clave; Ctrl+C para salir
```

`read -rs` pide la clave sin mostrarla y sin dejarla en el historial del shell (escribirla en la línea de comandos sí la dejaría). Los paréntesis la corren en un subshell y `exec` lo reemplaza por el programa: la variable existe solo en ese proceso y desaparece al salir, así que no queda en el shell interactivo (ni hace falta un `unset`). La clave es la `MQTT_PANTALLA_PASSWORD` de `orangepi/.env`. El programa la busca primero en la credencial `mqtt_pantalla` que systemd deja en `$CREDENTIALS_DIRECTORY` (`LoadCredential=`, la [propuesta para la unidad](#cambios-que-necesita-la-unidad-propuesta-para-arnau)) y, si no existe, en la variable `MQTT_PANTALLA_PASSWORD`. Si están las dos (la credencial y la variable), avisa por stderr, sin el valor: bajo systemd la clave no debe ir en el entorno. Rechaza el placeholder `cambiar_esta_clave` y una clave vacía (78), y no la imprime ni la registra, ni en los errores. Un archivo de credencial con salto de línea al final (el que deja un editor) se lee sin el salto. Si `PANTALLA_MQTT_HOST` no es `127.0.0.1`, `::1` ni `localhost`, avisa al arrancar: el broker no usa TLS y la clave viajaría en claro.

Con SIGTERM o SIGINT apaga la luz de prueba si estaba encendida, cierra MQTT, deja la pantalla en negro, libera el SPI y los GPIO, y sale con código 0. Con un mensaje en `aula/pantalla/reinicio` hace lo mismo y sale con 0, para que systemd (`Restart=always`) la relance. Códigos de salida:

| Código | Cuándo |
|---|---|
| 0 | SIGTERM, SIGINT o reinicio por MQTT |
| 74 | No pudo abrir o escribir `/dev/spidev*` (pantalla o táctil) o `/dev/gpiochip*`, o cargar la fuente DejaVu. `PANTALLA_TACTIL=0` arranca sin táctil |
| 78 | Configuración inválida: una variable `PANTALLA_*` fuera de rango o que no es un entero, o **falta la clave MQTT** (credencial `mqtt_pantalla` o `MQTT_PANTALLA_PASSWORD`), está vacía o es el placeholder |

Un fallo al apagar (por ejemplo, no poder dejar la pantalla en negro) después de que se pidió parar sale con 0 y lo avisa por stderr; un fallo de escritura durante el funcionamiento sigue siendo 74. Si al apagar no se pudo enviar o confirmar el `luz:0` de una luz de prueba encendida, imprime `luz de prueba posiblemente encendida`. El firmware de la XIAO prevé apagar la tira sola a los 360 s del último `luz:1`, pero la pantalla no depende de eso.

El usuario debe estar en los grupos `spi` y `gpio` (`id`).

Al terminar, el kernel conserva DC y RST configuradas como salida (`gpioinfo gpiochip0`), aunque ya no estén tomadas.

Para comprobar que arranca y sale limpio sin la clave real, con una ficticia (el broker la rechaza y la pantalla debe mostrar "Sin conexión" sin caerse). `--preserve-status` hace que `timeout` devuelva el código del programa en lugar de 124:

```bash
MQTT_PANTALLA_PASSWORD=ficticia timeout --preserve-status -s TERM 20 .venv/bin/python main.py; echo $?   # 0 (la ficticia no es secreta)
gpioinfo gpiochip0 | grep -E 'line +(32|33|139):'                                                        # unused
```

## Vistas y controles

Pantalla vertical de 240x320 (`PANTALLA_ROTACION` 0 o 2). Todas llevan arriba el nombre, la hora, el estado de la conexión con el broker (punto verde "En línea"; rojo "Sin conexión", o "Credenciales rechazadas" si el broker rechazó el usuario o la clave) y la fecha.

| Vista | Contenido y controles |
|---|---|
| **Sesión** (la de arranque) | Estado de la sesión (`aula/sesion/estado`): "Sesión activa" con materia, grupo, tolerancia y hora de inicio, o "Sin sesión activa" con lo que se usaría al iniciar. "Presentes" y "Último", **sin datos** por ahora. Botón **Iniciar sesión** / **Terminar sesión** (se confirma con un segundo toque; apagado mientras no llegue el primer `aula/sesion/estado`, y unos segundos tras confirmar, hasta que llegue la respuesta), **Abrir puerta** y **Probar luz LED** (cada uno ignora un segundo toque en los 2 s siguientes, salvo el que apaga la luz de prueba, que nunca se bloquea). Debajo, el estado de los actuadores (`aula/actuador/estado`: puerta, LED y luz) o un aviso de lo último que se pidió. Sin conexión, los botones se ven apagados y un toque avisa "Sin conexión: no se envió" |
| **Lista** | Presentes, retardos y faltantes de la sesión. **Sin datos** por ahora |
| **Guía de registro** | Se activa sola con `aula/vision/enrolamiento`: nombre del alumno, progreso (3/5) con una casilla por pose, la pose que debe adoptar y el resultado (por ejemplo "Otra vez: la imagen salió borrosa"). El registro se controla desde el frontend: el único botón es **Salir de la guía**, que vuelve a Sesión y no la reabre con los mensajes de ese mismo registro. Al terminar (captura completa, cancelado o error) muestra el desenlace unos segundos y vuelve a Sesión |

Las pestañas **Sesión** y **Lista** van abajo; la Guía las cambia por "Salir de la guía". Un toque se entrega al soltar el dedo (las primeras lecturas al apoyar son ruidosas).

## MQTT

Cliente paho-mqtt 2.1.0, MQTT 3.1.1, usuario `pantalla`, sin TLS (el listener 1883 va en texto plano; `orangepi/README.md`, "Riesgos aceptados hoy"). Tópicos y permisos según la ACL (`orangepi/mosquitto/acl`) y el contrato (`docs/contrato-mqtt.md`, secciones 2, 3, 5 y 6):

| Tópico | Dirección | QoS | Qué hace la pantalla |
|---|---|---|---|
| `aula/sesion/estado` | lee (retenido) | 1 | Estado de la sesión. Acepta lo retenido: así se recupera al conectar |
| `aula/actuador/estado` | lee (retenido) | 1 | Estado de los actuadores. Acepta lo retenido |
| `aula/vision/enrolamiento` | lee | 0 | Activa y actualiza la Guía. Descarta lo retenido y lo vacío |
| `aula/pantalla/reinicio` | lee | 1 | `{"reiniciar": 1}` hace salir con 0. Descarta lo retenido y lo vacío |
| `aula/sesion/cmd` | escribe | 1 | `{"accion": "iniciar", "materia", "grupo", "tol_min"}` o `{"accion": "terminar"}` |
| `aula/actuador/cmd` | escribe | 1 | `{"servo": "abrir"}`, `{"luz": 1}` y `{"luz": 0}` |

Nunca publica con retain. Los mensajes que no son un objeto JSON válido, que superan los 64 KB o cuyos campos no cumplen el contrato (por ejemplo `paso` mayor que `total`) se descartan sin tumbar el programa. No registra el contenido de ningún mensaje (trae nombres de alumnos).

**Lo que llega por MQTT no es de fiar al dibujarlo.** Los textos se limpian: los caracteres de control, de formato (como U+202E), privados y los separadores de línea y de párrafo se sustituyen por un espacio, y se conservan a lo más 2 marcas combinantes seguidas. La fecha de inicio solo vale entre los años 2000 y 2100, `tol_min` entre 0 y 600 e `id` entre 1 y 2³¹-1; fuera de eso, el campo se ignora. Aun así, si dibujar una vista falla por un dato raro, el programa no se cae: dibuja un cuadro fijo de "Error de datos" (sin botones: mientras está, los toques se ignoran) y registra solo el tipo de la excepción, una vez por cambio. La cola de mensajes pendientes tiene tope (256): con la cola llena se descartan mensajes y se avisa una vez; los eventos de conexión no se pierden (hacen sitio expulsando primero el progreso de registro más viejo, luego un estado de actuadores, y al final la sesión o el reinicio), y al terminar la racha de descartes el cliente se vuelve a suscribir (como mucho cada 5 s) para que el broker reenvíe los estados retenidos que pudieron perderse.

**Retenidos.** Los consumidores ignoran lo retenido en los tópicos con Retain = no (contrato, sección 1). Con MQTT 3.1.1 la bandera `retain` solo llega en la entrega inicial al (re)conectar; un retenido publicado en vivo llega sin ella y no se distingue de un mensaje normal (limitación descrita en el contrato). Por eso un `reinicio` retenido no reinicia la pantalla en cada reconexión (sería un bucle), pero uno falso publicado en vivo sí la reiniciaría.

**Sin conexión.** Si el broker no responde, rechaza la clave o se cae, el programa **no sale**: muestra "Sin conexión" (o "Credenciales rechazadas", si el CONNACK fue "Bad user name or password" o "Not authorized") y reintenta con espera creciente de 1 a 30 s, sin límite. La marca de credenciales rechazadas se limpia si el siguiente fallo ya no es de credenciales (por ejemplo, el broker dejó de responder). Lo último que se supo sigue en pantalla, con los botones de envío apagados. Los avisos de conexión salen a stderr (al journal) una vez por cambio, sin la clave. Al reconectar, vuelve a suscribirse y recibe el estado retenido.

**Nunca se publica sin conexión.** Con QoS 1, paho encola lo que se publica sin conexión y lo envía al reconectar: una orden "Abrir puerta" de hace minutos se ejecutaría de golpe. Por eso la pantalla no publica si no está conectada y avisa. Queda una carrera de milisegundos entre mirar si hay conexión y publicar (si se cae justo ahí, paho encola el mensaje y lo envía al reconectar) y la retransmisión de QoS 1 si se cae antes del acuse; ambas se aceptan (documentadas en `ClienteMQTT.publicar`).

## Decisiones de diseño

- **Iniciar sesión usa valores de `CONFIG`**, no una selección en pantalla. El contrato exige `materia`, `grupo` y `tol_min` en `iniciar`; la pantalla manda `PANTALLA_SESION_MATERIA` (`FSE`), `PANTALLA_SESION_GRUPO` (`1`) y `PANTALLA_SESION_TOL_MIN` (`10`), los del ejemplo del contrato, y los muestra en la tarjeta antes de iniciar. *Alternativa descartada:* elegirlos en la pantalla (selectores o teclado táctil). No hay una lista de materias ni de grupos que ofrecer (el contrato no define de dónde saldría), exige más lógica de interfaz que no se puede probar sin la pantalla, y en 240x320 es incómodo; si el profesor necesita otros valores, los pone el frontend, que ya tiene la sesión.
- **Iniciar y terminar piden un segundo toque** (3 s para confirmar; el botón pasa a ámbar con "¿Seguro? Toca otra vez"): iniciar cierra una sesión olvidada y terminar genera el reporte. Tras confirmar, el botón se apaga hasta que llegue un `aula/sesion/estado` o pasen 5 s (evita un `iniciar` doble), y mientras no haya llegado el primer estado de la sesión no actúa. **Dependencia del flujo de sesiones de Node-RED** (aún no existe): debe publicar `aula/sesion/estado` retenido también cuando no hay sesión (por ejemplo `{"activa": false}`); con un broker sin ese mensaje, el botón queda apagado. "Abrir puerta" y "Probar luz LED" son de un toque, con 2 s de enfriamiento entre toques del mismo botón (un toque bloqueado avisa "Espera un momento"; si el envío falla, el reintento inmediato sí pasa; el toque que apaga la luz de prueba queda exento: apagar nunca se bloquea): el servo se cierra solo y la luz se apaga sola. El panel no tiene autorización local (ver "Riesgos aceptados hoy" de `../README.md`).
- **La luz de prueba la apaga la pantalla.** "Probar luz LED" publica `{"luz": 1}` y, `PANTALLA_LUZ_PRUEBA_S` (3) segundos después, `{"luz": 0}`. Un segundo toque durante la prueba la apaga de inmediato (el botón dice "Apagar luz"). También se apaga al recibir SIGTERM o un reinicio (y si no se pudo enviar, lo avisa por stderr), y si no hay conexión al vencer, se envía al reconectar. Si no se pudo enviar el encendido, el botón no queda como "Apagar luz". Un `luz:0` automático puede apagar una luz que la visión encendió para capturar: el contrato no define prioridad. *Alternativas descartadas:* un interruptor (la tira de 12 V se queda encendida si se olvida) y depender de que la XIAO la apague: el firmware de la tira (rama `firmware/xiao`, contrato v0.8, aún sin fusionar) la apaga sola a los 360 s del último `luz:1`, pero esa rama no está en `main` y 360 s es demasiado para una prueba de 3 s; la pantalla no depende de ello.
- **Falta la clave: sale con 78.** La clave ausente, vacía o igual al placeholder es una configuración inválida que ningún reintento arregla (el broker no admite anónimos). Con 78, la unidad no la relanza (`RestartPreventExitStatus=78`) y el journal dice qué falta; el programa lo comprueba antes de abrir el hardware. *Alternativa descartada:* seguir mostrando "sin conexión" para siempre, que esconde el error de configuración detrás de una pantalla que parece viva. En cambio, **un broker caído no es un error**: el programa sigue y reconecta.
- **La Guía vuelve sola a Sesión**: unos 4 s tras `completado`, `cancelado` o `error`; `PANTALLA_GUIA_VENCE_S` (330 s, un poco más que los 5 min del vencimiento de Node-RED) sin ningún mensaje de progreso, por si se perdió el último (QoS 0); o `PANTALLA_GUIA_MAX_S` (900 s) desde el primer mensaje de ese registro, aunque sigan llegando (un publicador atorado o hostil no la deja puesta). Mientras hay una Guía en curso, los mensajes de otra cuenta se ignoran, y un mensaje sin `cuenta` se descarta (salvo el rechazo de una orden, que puede ir sin ella). Una excepción deliberada: `esperando_pose` en el paso 0 o 1 es el **inicio de un registro** y siempre se atiende, aunque haya una Guía atorada de otra cuenta o una cerrada con "Salir": así un mensaje final perdido (QoS 0) no oculta el registro siguiente. Repetir ese inicio de la misma cuenta no alarga el techo. Un `error` con `paso` 0 es el rechazo de una orden (por ejemplo `ocupado`): no pisa un registro en curso y, sin registro, no abre la Guía (solo un aviso breve). El botón "Salir de la guía" la cierra a mano. Dice "Captura completa", no "Registro completo": el embedding lo guarda Node-RED después y puede fallar.
- **Solo pantalla vertical.** `PANTALLA_ROTACION` 1 y 3 (horizontal) ahora dan 78: las vistas están trazadas para 240x320.
- **Sin teclado ni selección de alumnos ni "marcar" manual** (`aula/sesion/cmd` `marcar`, propuesta del contrato para la zona gris): no está en el alcance del menú base.

## Huecos de datos

Dos datos no tienen fuente, y la pantalla no inventa tópicos ni cambia el contrato ni la ACL:

| Dato | Dónde se muestra | Atributo de `Estado` |
|---|---|---|
| Presentes, retardos y faltantes | Lista; "Presentes" en Sesión | `asistencia` (`{"presentes": n, "retardos": n, "faltantes": n}`, forma provisional) |
| Último registro (nombre y estado) | Sesión | `ultimo_registro` (`{"nombre": ..., "estado": ...}`, forma provisional) |

Mientras valgan `None`, las vistas dicen "sin datos". Para conectarlos cuando exista la fuente: agregar el tópico a `SUSCRIPCIONES` y un lector a `_LECTORES` en `estado.py`, y un caso en `Estado.aplicar` que los asigne (y, si el tópico es nuevo, cambiar el contrato, la ACL y el plan, sección 7.1, en el mismo cambio). Las vistas ya dibujan los datos si llegan (hay pruebas con ellos).

## Configuración

Los valores por defecto están en la tabla `CONFIG` de `main.py`, el único lugar donde se definen. Cada uno se cambia con una variable de entorno `PANTALLA_<NOMBRE>`; en la unidad, con una línea `Environment=` o en su archivo de entorno ([propuesta](#cambios-que-necesita-la-unidad-propuesta-para-arnau)). Una variable fuera de rango o que no es un entero hace salir con 78.

| Variable | Por defecto | Qué es |
|---|---|---|
| `PANTALLA_SPI_BUS` | `3` | Bus: `/dev/spidev<bus>.<dispositivo>` |
| `PANTALLA_SPI_DISPOSITIVO` | `0` | Chip select de la pantalla |
| `PANTALLA_GPIOCHIP` | `/dev/gpiochip0` | Chip GPIO (el de la pantalla y el de TP_IRQ) |
| `PANTALLA_DC` | `139` | Línea de DC |
| `PANTALLA_RST` | `33` | Línea de RST |
| `PANTALLA_CS` | vacío | Línea de LCD_CS si lo maneja el código; vacío, lo maneja el kernel |
| `PANTALLA_ROTACION` | `0` | 0 o 2 (vertical). 1 y 3 (horizontal) no están soportadas por las vistas |
| `PANTALLA_BAUDRATE` | `40000000` | Hz |
| `PANTALLA_MQTT_HOST` | `127.0.0.1` | Broker. Mosquitto corre en Docker en la misma placa con el 1883 publicado en el host |
| `PANTALLA_MQTT_PUERTO` | `1883` | Puerto del broker |
| `PANTALLA_SESION_MATERIA` | `FSE` | `materia` de "Iniciar sesión" |
| `PANTALLA_SESION_GRUPO` | `1` | `grupo` de "Iniciar sesión" |
| `PANTALLA_SESION_TOL_MIN` | `10` | `tol_min` de "Iniciar sesión" (1 a 600) |
| `PANTALLA_LUZ_PRUEBA_S` | `3` | Segundos que dura "Probar luz LED" (1 a 60) |
| `PANTALLA_GUIA_VENCE_S` | `330` | Segundos sin progreso para dejar la Guía de registro (10 a 3600) |
| `PANTALLA_GUIA_MAX_S` | `900` | Segundos máximos de una Guía desde el primer mensaje de ese registro (60 a 7200, no menor que `GUIA_VENCE_S`) |
| `PANTALLA_TACTIL` | `1` | `0` arranca sin táctil (solo se muestra) |
| `PANTALLA_TACTIL_BUS` | `3` | Bus del táctil |
| `PANTALLA_TACTIL_DISPOSITIVO` | `1` | `/dev/spidev3.1` |
| `PANTALLA_TACTIL_IRQ` | `32` | Línea de TP_IRQ (pin 11); baja al tocar |
| `PANTALLA_TACTIL_BAUDRATE` | `2000000` | Hz |
| `PANTALLA_TACTIL_X_MIN`, `_X_MAX`, `_Y_MIN`, `_Y_MAX` | `200`, `3900`, `200`, `3900` | Calibración: el valor crudo del XPT2046 (0 a 4095) en los bordes de la pantalla, **después** de aplicar `CAMBIAR_EJES`. Valores de partida **sin calibrar** |
| `PANTALLA_TACTIL_CAMBIAR_EJES` | `0` | `1` si el eje X de la pantalla lo da el canal Y crudo del táctil |
| `PANTALLA_TACTIL_INVERTIR_X`, `_INVERTIR_Y` | `0`, `0` | `1` si el crudo crece hacia la izquierda o hacia arriba |

La clave MQTT no está en esta tabla: llega por la credencial `mqtt_pantalla` o por `MQTT_PANTALLA_PASSWORD` (ver [Correrlo a mano](#correrlo-a-mano)).

## Calibrar el táctil

Los valores de partida son genéricos: cada panel y cada montaje dan otros. Se calibra con la pantalla delante y la unidad detenida (usan los mismos GPIO y el mismo bus). Desde `orangepi/pantalla/`:

```bash
.venv/bin/python calibrar_tactil.py --crudo   # solo imprime el crudo mientras tocas; Ctrl+C sale
.venv/bin/python calibrar_tactil.py           # guiada
```

- **`--crudo`** no usa la pantalla: imprime `x=... y=...` cada 100 ms mientras haya un dedo puesto. Sin tocar no debe imprimir nada (si imprime sin tocar, TP_IRQ está bajo todo el tiempo: revisa el cableado). Sirve para ver el rango de cada canal y qué eje corresponde a cuál tocando las esquinas.
- **La guiada** dibuja una cruz verde en cada esquina (arriba a la izquierda, arriba a la derecha, abajo a la derecha, abajo a la izquierda), a 20 px del borde, y pide tocarla. Con las cuatro toma el crudo, deduce si hay que cambiar o invertir ejes, extrapola a los bordes e imprime las siete líneas `PANTALLA_TACTIL_*=valor`. Si las esquinas no son coherentes (por ejemplo, tocaste dos veces la misma), lo dice y no calibra. Después dibuja un punto verde donde detecta cada toque con esa calibración, para comprobarla: el punto debe aparecer bajo el dedo, también en los bordes.

Las siete líneas se pegan en el archivo de entorno de la unidad (o en `Environment=`), o, cuando estén verificadas, como valores por defecto de `CONFIG` en `main.py` (hay una sola pantalla). Hay que repetirla si cambia `PANTALLA_ROTACION` o se remonta el panel.

## Pruebas

`unittest` de la biblioteca estándar, sin hardware ni red: la pantalla, el táctil y el cliente de paho son objetos falsos y no se abre ningún dispositivo ni el puerto 1883. Desde `orangepi/pantalla/`, con el venv creado:

```bash
.venv/bin/python -m unittest -v
.venv/bin/python -m py_compile main.py ili9341_opi.py xpt2046_opi.py estado.py vistas.py tactil.py cliente_mqtt.py calibrar_tactil.py test_*.py
```

| Archivo | Cubre |
|---|---|
| `test_estado.py` | Lectura y descarte de los mensajes (vacíos, retenidos, JSON inválido, fuera de contrato), cambio de vista, la Guía (activación, desenlace, vencimiento, rechazo de órdenes), y cada botón con el payload que publica (iniciar y terminar con confirmación, abrir puerta, luz de prueba y su apagado), sin conexión incluido; el bloqueo del botón de sesión, la Guía (otra cuenta, techo, salir), el enfriamiento, `sucio` solo si cambia, la validación de `inicio`, `tol_min` e `id` y la limpieza de textos hostiles |
| `test_cliente_mqtt.py` | Credenciales y suscripciones, reconexión, descarte de mensajes, no publicar sin conexión, que la clave no salga en el log y el cierre limpio; las credenciales rechazadas, la cola acotada, `detener()` con confirmación y un cliente de paho real contra un puerto cerrado |
| `test_vistas.py` | Geometría de los botones (dentro de la pantalla, sin solaparse, blancos de al menos 38 px), textos de cada vista y que el dibujo funcione con textos largos o raros; `ajustar_texto` sin costo cuadrático, textos hostiles de punta a punta y el cuadro de error |
| `test_tactil.py` | Calibración (ida y vuelta), conversión a píxeles y detección del toque (rebotes, ruido, lecturas inválidas) |
| `test_calibrar_tactil.py` | La herramienta de calibración con un usuario simulado |
| `test_main.py` | Configuración (rangos, 78), falta de clave, el bucle (redibuja solo cuando cambia algo o cada segundo), toques que llegan a los botones, apagado por señal y por reinicio (limpia, libera, sale con 0) y los fallos de apertura o escritura (74); la clave (credencial de systemd, reserva, placeholder), el aviso de la luz de prueba al cerrar, el dibujo defensivo y el aviso del broker no local |

No prueban el protocolo del ILI9341 ni del XPT2046, ni cómo se ven las vistas: eso requiere la pantalla.

## Instalar la unidad systemd

La unidad apunta al checkout de producción, `/home/orangepi/ProyectoFinal/AutoList` (rama `main`). Requisitos: el código ya fusionado en ese checkout y el [venv](#crear-el-venv) creado ahí. Para probar desde otro checkout, cambia esas dos rutas (`WorkingDirectory` y `ExecStart`) en la copia que instales, no en el repo.

**Con el menú base, la unidad actual no basta:** sin la clave MQTT el programa sale con 78 y systemd no lo relanza. Antes de instalarla, aplica los [cambios propuestos](#cambios-que-necesita-la-unidad-propuesta-para-arnau) (la credencial con la clave).

La unidad corre como `orangepi` con los grupos `spi` y `gpio`, que la regla udev `/etc/udev/rules.d/99-gpio-spi.rules` ya abre con modo 0660. Instalarla requiere `sudo` y lo hace una persona:

```bash
cd /home/orangepi/ProyectoFinal/AutoList
sudo cp orangepi/systemd/orangepi-pantalla.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now orangepi-pantalla.service
systemctl status orangepi-pantalla.service
journalctl -u orangepi-pantalla -n 50 --no-pager
```

Para seguir el journal en vivo, `journalctl -u orangepi-pantalla -f`. Para detenerla antes de correr el programa a mano, `sudo systemctl stop orangepi-pantalla.service`.

Si `status` muestra `Permission denied` al abrir un dispositivo, la causa más probable es `DevicePolicy=closed` con `DeviceAllow=char-spi` y `char-gpiochip` (nombres de `/proc/devices`), que no se pudo probar sin instalar la unidad. Para descartarla, comenta esas tres líneas, `daemon-reload` y reinicia la unidad; si funciona, hay que corregir los nombres de la unidad, no dejarla sin la política.

## Cambios que necesita la unidad (propuesta para Arnau)

`../systemd/orangepi-pantalla.service` es del carril de Arnau y esta versión no la toca. Lo que necesita el menú base:

1. **La clave MQTT, con `LoadCredential=`** (no con `EnvironmentFile=`). Con `EnvironmentFile=` la clave queda en el entorno del proceso, y `/proc/<pid>/environ` es legible por el mismo uid (`orangepi`). Con `LoadCredential=`, systemd la deja en un directorio del servicio (`/run/credentials/orangepi-pantalla.service/`, expuesto como `$CREDENTIALS_DIRECTORY`; desde systemd 247, la placa tiene la 252) y el programa la lee de ahí: **no queda en el entorno ni se hereda a otros procesos, pero la puede leer el mismo uid** (el usuario del servicio, `orangepi`). No es aislamiento frente a otros procesos de ese usuario. Agregar a `[Service]`:

   ```ini
   LoadCredential=mqtt_pantalla:/etc/pase-lista/pantalla.clave
   EnvironmentFile=-/etc/pase-lista/pantalla.env
   ```

   El archivo `pantalla.clave` contiene solo la clave (la `MQTT_PANTALLA_PASSWORD` de `orangepi/.env`), es de root y de modo 600. Lo crea una persona con `sudo`, sin dejar la clave en el historial ni en la línea de comandos:

   ```bash
   sudo install -d -m 755 -o root -g root /etc/pase-lista
   sudo install -m 600 -o root -g root /dev/null /etc/pase-lista/pantalla.clave
   sudoedit /etc/pase-lista/pantalla.clave      # pega la clave y guarda
   ```

   `/etc/pase-lista` ya lo usa la unidad de visión (`vision.env`), así que el `install -d` solo hace falta si no existe. `EnvironmentFile` queda **solo para las variables `PANTALLA_*`** (calibración del táctil, materia y grupo; sin secretos, a partir de `pantalla.env.example`, `sudo install -m 644 -o root -g root pantalla.env.example /etc/pase-lista/pantalla.env`); lleva `-` para que su ausencia no impida arrancar. Sin `pantalla.clave`, systemd no arranca la unidad; con el archivo vacío o con el placeholder, el programa sale con 78 y no se relanza. La calibración del táctil es de esta placa, no del repo: el archivo de entorno es su lugar natural hasta que se fije en `CONFIG`.

   **Opción para un aislamiento real:** correr la unidad con un usuario de sistema propio (por ejemplo `User=pantalla`, creado con `sudo useradd --system --no-create-home --shell /usr/sbin/nologin pantalla`, con `SupplementaryGroups=spi gpio`) y un checkout legible por él. Así la clave, el proceso y su entorno ya no son del uid `orangepi` (el del equipo y de Docker), y `pantalla.clave` se puede dejar legible solo por root. Cuesta: otro usuario que mantener, que el venv y el checkout de producción sean legibles por `pantalla`, y revisar `ProtectHome=read-only`. La decisión es de Arnau.
2. **Red.** Nada: la unidad no restringe la red (`RestrictAddressFamilies`, `IPAddressDeny` ni `PrivateNetwork`), así que el TCP a `127.0.0.1:1883` ya funciona. Si se endurece, el cliente necesita `AF_INET` y también `AF_UNIX` (paho crea un `socketpair` para despertar su hilo de red).
3. **Dispositivos.** Nada que se sepa: el táctil es `/dev/spidev3.1`, del mismo tipo y mayor (`spi`, 153) que la pantalla (`/dev/spidev3.0`), así que `DeviceAllow=char-spi rw` lo cubre. La línea de TP_IRQ está en el mismo `gpiochip0`. No se pudo probar bajo la unidad.
4. **Orden de arranque.** Nada: el programa reintenta la conexión sin límite, así que no hace falta `After=docker.service`. La parada es rápida: espera como máximo 1 s la última orden publicada y, si había un intento de conexión en curso, a lo más los 5 s del tiempo de espera de paho (no medido; con el broker inalcanzable la parada tardó poco más de 1 s), dentro de los 10 s de `TimeoutStopSec`.
5. **Reinicio.** `Restart=always` con `RestartSec=5` ya sirve para el reinicio por MQTT (sale con 0 y vuelve en unos 5 s). Falta el flujo de Node-RED que publica `aula/pantalla/reinicio` (plan, sección 6.4, punto 4).

## Sin verificar

Falta la prueba con la pantalla delante. Lo que Yordi debe comprobar:

- **Cómo se ve cada vista** (Sesión con y sin sesión, y sin conexión; Lista; Guía de registro, incluidos un nombre largo y los mensajes de rechazo): legibilidad del texto a 240x320, que nada se corte ni se solape, el contraste, y que los blancos de los botones (de 38 a 42 px) se puedan pulsar con el dedo.
- **El táctil**: que el TP_IRQ (línea 32) baje al tocar y que las lecturas sean estables; la calibración (ver [Calibrar el táctil](#calibrar-el-táctil)); que cada botón responda donde se ve, que un toque cuente una sola vez y que no haya toques fantasma con la pantalla en reposo.
- **La fluidez**: el bucle redibuja la pantalla completa cada segundo y en cada cambio (unos 150 KB por el SPI a 40 MHz). Si al tocar se nota lento, es lo primero a medir.
- **"Credenciales rechazadas"**: con una clave equivocada contra el broker real debe salir ese texto (con la clave ficticia, el CONNACK de Mosquitto es "Not authorized"; no se vio en pantalla).
- **Guía**: que "Salir de la guía" quepa y responda, y qué hace un registro real si el último mensaje se pierde.
- **Con un broker real y la clave real** (el 11 nov o cuando la unidad esté instalada): que conecte y se suscriba, que la Guía siga un registro real, que "Abrir puerta" y "Probar luz LED" lleguen a la XIAO, que "Iniciar sesión" publique (hoy no hay flujo de sesiones en Node-RED que lo consuma: no cambiará nada en pantalla) y que el reinicio por MQTT relance el programa.
- **Convención de las poses**: si "izquierda" en el contrato es la izquierda del alumno o la de la imagen de la cámara. La guía dice "gira un poco la cabeza a la izquierda" sin flechas; si la visión usa la otra convención, hay que invertir el texto.
- **Bajo systemd**: que la unidad arranque con `DevicePolicy=closed`, con `ProtectHome=read-only` y con los grupos `spi` y `gpio`, con el archivo de entorno de la propuesta. Con `Restart=always`, un `systemctl stop` no relanza el servicio y el código 78 tampoco, por `RestartPreventExitStatus=78`.
