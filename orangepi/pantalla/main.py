"""
Programa de la pantalla ILI9341 de AutoList (pase de lista)

Version minima: muestra la pantalla de arranque con el nombre del proyecto y
la hora, y la actualiza cada segundo. Todavia no hay MQTT ni tactil.

Con SIGTERM o SIGINT limpia la pantalla, libera el SPI y los GPIO y sale con
codigo 0. Codigos de salida distintos de 0:
  74  no se pudo abrir o escribir un dispositivo (/dev/spidev*, /dev/gpiochip*)
      ni cargar la fuente DejaVu
  78  configuracion invalida (una variable PANTALLA_* que no es un entero)

Configuracion de pines y bus: la tabla CONFIG de abajo es el unico lugar. Cada
valor se puede cambiar con la variable de entorno PANTALLA_<NOMBRE>.
"""

import os
import signal
import sys
import threading
import time
from datetime import datetime

from PIL import Image, ImageDraw, ImageFont

from ili9341_opi import ILI9341, color565

EX_IOERR = 74
EX_CONFIG = 78

# nombre: (valor por defecto, ¿puede ir vacio?). Lineas GPIO de gpiochip0.
# PANTALLA_CS vacio o sin definir deja el chip select al kernel (CS0 del bus).
CONFIG = {
    "SPI_BUS": (3, False),              # /dev/spidev3.x
    "SPI_DISPOSITIVO": (0, False),      # /dev/spidev3.0 es la pantalla
    "GPIOCHIP": ("/dev/gpiochip0", False),
    "DC": (139, False),                 # pin fisico 15
    "RST": (33, False),                 # pin fisico 13
    "CS": (None, True),                 # pin fisico 24, lo maneja el kernel
    "ROTACION": (0, False),             # 0 y 2 vertical, 1 y 3 horizontal
    "BAUDRATE": (40000000, False),
}

DIR_FUENTES = "/usr/share/fonts/truetype/dejavu"
FUENTE_NEGRITA = "DejaVuSans-Bold.ttf"
FUENTE_MONO = "DejaVuSansMono-Bold.ttf"

NOMBRE = "AutoList"
SUBTITULO = "Pase de lista"

FONDO = (16, 24, 40)
TEXTO = (240, 244, 250)
ACENTO = (80, 170, 255)
APAGADO = (140, 150, 165)

DIAS = ("lun", "mar", "mié", "jue", "vie", "sáb", "dom")
MESES = ("ene", "feb", "mar", "abr", "may", "jun",
         "jul", "ago", "sep", "oct", "nov", "dic")

TROZO_ESPERA = 0.1      # s; tope de latencia para atender SIGTERM


def leer_config(entorno=None):
    """Devuelve la configuracion como dict; ValueError si un entero no lo es."""
    entorno = os.environ if entorno is None else entorno
    cfg = {}
    for nombre, (defecto, vacio_ok) in CONFIG.items():
        crudo = entorno.get("PANTALLA_" + nombre)
        if crudo is None or (crudo.strip() == "" and vacio_ok):
            cfg[nombre] = defecto
        elif isinstance(defecto, str):
            cfg[nombre] = crudo
        else:
            try:
                cfg[nombre] = int(crudo)
            except ValueError:
                raise ValueError("PANTALLA_%s debe ser un entero, no %r"
                                 % (nombre, crudo)) from None
    return cfg


def cargar_fuentes(ancho):
    """Fuentes DejaVu con tamaños proporcionales al ancho de la pantalla."""
    def fuente(archivo, tamano):
        return ImageFont.truetype(os.path.join(DIR_FUENTES, archivo), tamano)
    return {
        "nombre": fuente(FUENTE_NEGRITA, ancho // 8),
        "subtitulo": fuente(FUENTE_NEGRITA, ancho // 15),
        "hora": fuente(FUENTE_MONO, ancho // 7),
        "fecha": fuente(FUENTE_NEGRITA, ancho // 17),
    }


def dibujar_arranque(ancho, alto, ahora, fuentes):
    """Genera la pantalla de arranque como imagen RGB; no toca el hardware."""
    img = Image.new("RGB", (ancho, alto), FONDO)
    d = ImageDraw.Draw(img)
    centro = ancho // 2

    d.text((centro, alto * 0.22), NOMBRE, font=fuentes["nombre"],
           fill=TEXTO, anchor="mm")
    d.text((centro, alto * 0.33), SUBTITULO, font=fuentes["subtitulo"],
           fill=ACENTO, anchor="mm")
    d.line((ancho * 0.15, alto * 0.41, ancho * 0.85, alto * 0.41),
           fill=ACENTO, width=2)
    d.text((centro, alto * 0.55), ahora.strftime("%H:%M:%S"),
           font=fuentes["hora"], fill=TEXTO, anchor="mm")
    fecha = "%s %d %s %d" % (DIAS[ahora.weekday()], ahora.day,
                             MESES[ahora.month - 1], ahora.year)
    d.text((centro, alto * 0.65), fecha, font=fuentes["fecha"],
           fill=APAGADO, anchor="mm")
    return img


def abrir_pantalla(cfg):
    return ILI9341(bus=cfg["SPI_BUS"], dispositivo=cfg["SPI_DISPOSITIVO"],
                   dc=cfg["DC"], rst=cfg["RST"], cs=cfg["CS"],
                   rotacion=cfg["ROTACION"], baudrate=cfg["BAUDRATE"],
                   chip=cfg["GPIOCHIP"])


def ejecutar(pantalla, parar, fuentes):
    """Dibuja y envia un cuadro por segundo hasta que parar se active."""
    while not parar.is_set():
        cuadro = dibujar_arranque(pantalla.width, pantalla.height,
                                  datetime.now(), fuentes)
        pantalla.image(cuadro)
        # Hasta el siguiente cambio de segundo, en trozos cortos. No se usa
        # Event.wait: el manejador de señales llama a Event.set en el mismo
        # hilo y podria bloquearse con el candado de wait.
        fin = time.monotonic() + 1 - (time.time() % 1)
        while not parar.is_set() and time.monotonic() < fin:
            time.sleep(min(TROZO_ESPERA, max(fin - time.monotonic(), 0)))


def apagar(pantalla):
    """Deja la pantalla en negro y libera el SPI y los GPIO."""
    try:
        pantalla.fill(color565(0, 0, 0))
    finally:
        pantalla.cerrar()


def main():
    try:
        cfg = leer_config()
    except ValueError as e:
        print("pantalla: configuración inválida: %s" % e, file=sys.stderr)
        return EX_CONFIG

    parar = threading.Event()
    for senal in (signal.SIGTERM, signal.SIGINT):
        signal.signal(senal, lambda *_: parar.set())

    try:
        pantalla = abrir_pantalla(cfg)
    except OSError as e:
        print("pantalla: no se pudo abrir %s (SPI) o %s (GPIO): %s\n"
              "Revisa que existan los dispositivos y que el usuario esté en "
              "los grupos spi y gpio."
              % ("/dev/spidev%d.%d" % (cfg["SPI_BUS"], cfg["SPI_DISPOSITIVO"]),
                 cfg["GPIOCHIP"], e), file=sys.stderr)
        return EX_IOERR

    try:
        try:
            ejecutar(pantalla, parar, cargar_fuentes(pantalla.width))
        finally:
            apagar(pantalla)
    except OSError as e:
        print("pantalla: falló la escritura en la pantalla o la carga de la "
              "fuente: %s" % e, file=sys.stderr)
        return EX_IOERR
    return 0


if __name__ == "__main__":
    sys.exit(main())
