# Pantalla ILI9341

Programa de la pantalla ILI9341 de AutoList (plan, sección 6.5). Corre en el host como unidad systemd, no en Docker (`../systemd/orangepi-pantalla.service`). La mantiene el carril de Yordi; el hardware (overlays de SPI y la unidad) es de Arnau.

**Estado: sin probar en hardware.** Las pruebas de `test_main.py` y la verificación de la unidad se hicieron sin la pantalla conectada. La primera prueba con la pantalla está pendiente.

Hoy es una versión mínima: una pantalla de arranque con el nombre del proyecto y la hora, que se actualiza cada segundo. Aún no hay MQTT, táctil, I²C ni RTC, ni las tres vistas del plan.

## Contenido

- [Archivos](#archivos)
- [Pines y bus](#pines-y-bus)
- [Crear el venv](#crear-el-venv)
- [Correrlo a mano](#correrlo-a-mano)
- [Configuración](#configuración)
- [Pruebas](#pruebas)
- [Instalar la unidad systemd](#instalar-la-unidad-systemd)
- [Sin verificar](#sin-verificar)

## Archivos

| Archivo | Qué es |
|---|---|
| `main.py` | Programa: configuración, dibujo de la pantalla de arranque y bucle con apagado limpio |
| `ili9341_opi.py` | Controlador de la pantalla sobre `python-periphery` (copiado de `~/Documents/1_Pantalla`, con los comentarios corregidos para la Zero 3W) |
| `xpt2046_opi.py` | Controlador del táctil XPT2046. Todavía no lo usa `main.py` |
| `test_main.py` | Pruebas sin hardware (`unittest`) |
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

Con la unidad detenida (dos procesos no pueden tomar las mismas líneas GPIO):

```bash
cd orangepi/pantalla
.venv/bin/python main.py        # Ctrl+C para salir
```

Con SIGTERM o SIGINT deja la pantalla en negro, libera el SPI y los GPIO, y sale con código 0. Si no puede abrir `/dev/spidev*` o `/dev/gpiochip*`, imprime el motivo en stderr y sale con 74. Con una variable `PANTALLA_*` inválida, sale con 78.

El usuario debe estar en los grupos `spi` y `gpio` (`id`).

Al terminar, el kernel conserva DC y RST configuradas como salida (`gpioinfo gpiochip0`), aunque ya no estén tomadas.

## Configuración

Los valores por defecto están en la tabla `CONFIG` de `main.py`, el único lugar donde se definen. Cada uno se cambia con una variable de entorno `PANTALLA_<NOMBRE>`; en la unidad, con una línea `Environment=`.

| Variable | Por defecto | Qué es |
|---|---|---|
| `PANTALLA_SPI_BUS` | `3` | Bus: `/dev/spidev<bus>.<dispositivo>` |
| `PANTALLA_SPI_DISPOSITIVO` | `0` | Chip select de la pantalla |
| `PANTALLA_GPIOCHIP` | `/dev/gpiochip0` | Chip GPIO |
| `PANTALLA_DC` | `139` | Línea de DC |
| `PANTALLA_RST` | `33` | Línea de RST |
| `PANTALLA_CS` | vacío | Línea de LCD_CS si lo maneja el código; vacío, lo maneja el kernel |
| `PANTALLA_ROTACION` | `0` | 0 y 2 vertical, 1 y 3 horizontal |
| `PANTALLA_BAUDRATE` | `40000000` | Hz |

La línea de TP_IRQ y el nodo del táctil (`/dev/spidev3.1`) son los valores por defecto de `xpt2046_opi.py`; pasarán a `CONFIG` cuando `main.py` use el táctil.

## Pruebas

`unittest` de la biblioteca estándar, sin hardware: la pantalla es un objeto falso y no se abre ningún dispositivo. Desde `orangepi/pantalla/`, con el venv creado:

```bash
.venv/bin/python -m unittest -v
.venv/bin/python -m py_compile main.py ili9341_opi.py xpt2046_opi.py test_main.py
```

Cubren la configuración, el dibujo (el cuadro se genera con el tamaño y el texto correctos), el apagado por señal (limpia, libera y sale con 0) y los fallos de apertura y de escritura (74) o de configuración (78). No prueban el protocolo del ILI9341: eso requiere la pantalla.

## Instalar la unidad systemd

La unidad apunta al checkout de producción, `/home/orangepi/ProyectoFinal/AutoList` (rama `main`). Requisitos: el código ya fusionado en ese checkout y el [venv](#crear-el-venv) creado ahí. Para probar desde otro checkout, cambia esas dos rutas (`WorkingDirectory` y `ExecStart`) en la copia que instales, no en el repo.

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

## Sin verificar

Falta la prueba con la pantalla conectada:

- Que la pantalla muestre la pantalla de arranque y la hora, sin fallos de color ni de orientación.
- Que el CS0 que baja el kernel sea suficiente para la pantalla (es el comportamiento que usaban los controladores probados).
- Que la unidad arranque bajo systemd con `DevicePolicy=closed`, con `ProtectHome=read-only` y con los grupos `spi` y `gpio`.
- El reinicio por MQTT del plan: la unidad ya usa `Restart=always` (el programa saldrá con `sys.exit(0)` y systemd lo relanza), pero `main.py` aún no se suscribe a MQTT. Con `Restart=always`, un `systemctl stop` no relanza el servicio y el código 78 (configuración inválida) tampoco, por `RestartPreventExitStatus=78`.
