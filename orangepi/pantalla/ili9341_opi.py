"""
Controlador ILI9341 para la Orange Pi Zero 3W

Sustituye a adafruit_rgb_display, que no reconoce esta placa. Habla directo
con /dev/spidevX.Y y /dev/gpiochipN mediante python-periphery.

Conexiones por defecto     Pin fisico   Linea GPIO (gpiochip0)
  LCD_RS (DC)                15           139
  RST                        13           33
  MOSI / MISO / SCLK         19 / 21 / 23 bus SPI3 (/dev/spidev3.0)
  LCD_CS                     24           lo maneja el kernel (cs=None)

El chip select de la pantalla es el CS0 del bus SPI3 y lo baja el kernel en
cada transferencia: no se maneja por GPIO. El parametro cs sigue disponible
para el caso contrario (un overlay que cree el nodo sin asignar el pin): con
un numero de linea, este codigo baja CS durante todo el envio de pixeles,
como pide la hoja de datos.

Organizado en tres partes: constantes del chip, arranque de la pantalla
y operaciones de dibujo.
"""

import time

import numpy as np
from periphery import GPIO, SPI

# ----- Geometria, sin rotar -----
ANCHO = 240
ALTO = 320

# ----- Comandos del ILI9341 -----
CMD_MADCTL = 0x36
CMD_CASET = 0x2A
CMD_PASET = 0x2B
CMD_RAMWR = 0x2C

# Un valor de MADCTL por rotacion; el bit 0x08 dice que el panel es BGR
MADCTL = (0x48, 0x28, 0x88, 0xE8)

CHIP = "/dev/gpiochip0"
BLOQUE = 4096                   # bytes por transferencia SPI

# Secuencia de encendido estandar del ILI9341
INICIO = (
    (0xEF, b"\x03\x80\x02"),
    (0xCF, b"\x00\xC1\x30"),
    (0xED, b"\x64\x03\x12\x81"),
    (0xE8, b"\x85\x00\x78"),
    (0xCB, b"\x39\x2C\x00\x34\x02"),
    (0xF7, b"\x20"),
    (0xEA, b"\x00\x00"),
    (0xC0, b"\x23"),
    (0xC1, b"\x10"),
    (0xC5, b"\x3E\x28"),
    (0xC7, b"\x86"),
    (0x3A, b"\x55"),            # 16 bits por pixel
    (0xB1, b"\x00\x18"),
    (0xB6, b"\x08\x82\x27"),
    (0xF2, b"\x00"),
    (0x26, b"\x01"),
    (0xE0, b"\x0F\x31\x2B\x0C\x0E\x08\x4E\xF1\x37\x07\x10\x03\x0E\x09\x00"),
    (0xE1, b"\x00\x0E\x14\x03\x11\x07\x31\xC1\x48\x08\x0F\x0C\x31\x36\x0F"),
    (0x11, None),               # salir de reposo
    (0x29, None),               # encender la pantalla
)


def color565(r, g=0, b=0):
    """Empaca un color de 8 bits por canal en los 16 bits del panel."""
    try:
        r, g, b = r             # acepta tambien una tupla
    except TypeError:
        pass
    return (r & 0xF8) << 8 | (g & 0xFC) << 3 | b >> 3


class ILI9341:
    """Pantalla ILI9341 sobre SPI, con DC y RST por GPIO."""

    def __init__(self, bus=3, dispositivo=0, dc=139, rst=33,
                 cs=None, rotacion=0, baudrate=40000000, chip=CHIP):
        self._spi = self._dc = self._rst = self._cs = None
        try:
            # 1. el bus
            ruta = "/dev/spidev%d.%d" % (bus, dispositivo)
            self._spi = SPI(ruta, 0, baudrate)

            # 2. los pines de control; cs=None se lo deja al kernel
            self._dc = GPIO(chip, dc, "low")
            self._rst = GPIO(chip, rst, "high") if rst is not None else None
            self._cs = GPIO(chip, cs, "high") if cs is not None else None

            # 3. el tamano util y el encendido
            par = (rotacion % 2 == 0)
            self.width = ANCHO if par else ALTO
            self.height = ALTO if par else ANCHO
            self._reiniciar()
            self._encender(rotacion)
        except BaseException:
            self.cerrar()           # no dejar el bus ni las lineas tomados
            raise

    # ----- Arranque -----

    def _reiniciar(self):
        if self._rst is None:
            return
        self._rst.write(True)
        time.sleep(0.05)
        self._rst.write(False)          # pulso de reset
        time.sleep(0.05)
        self._rst.write(True)
        time.sleep(0.15)

    def _encender(self, rotacion):
        for cmd, datos in INICIO:
            self._comando(cmd, datos)
            if datos is None:
                time.sleep(0.12)        # reposo y encendido tardan
        self._comando(CMD_MADCTL, bytes([MADCTL[rotacion % 4]]))

    # ----- Transferencia -----

    def _abrir(self):
        if self._cs is not None:
            self._cs.write(False)       # CS abajo: inicia la transaccion

    def _soltar(self):
        if self._cs is not None:
            self._cs.write(True)

    def _comando(self, cmd, datos=None):
        self._abrir()
        self._dc.write(False)           # 0 = es un comando
        self._spi.transfer([cmd])
        if datos:
            self._dc.write(True)        # 1 = son datos
            self._enviar(datos)
        self._soltar()

    def _enviar(self, buf):
        for i in range(0, len(buf), BLOQUE):
            self._spi.transfer(buf[i:i + BLOQUE])

    def _pintar(self, x0, y0, x1, y1, buf):
        self._comando(CMD_CASET, bytes([x0 >> 8, x0 & 0xFF,
                                        x1 >> 8, x1 & 0xFF]))
        self._comando(CMD_PASET, bytes([y0 >> 8, y0 & 0xFF,
                                        y1 >> 8, y1 & 0xFF]))
        # el comando y los pixeles van en una sola transaccion
        self._abrir()
        self._dc.write(False)
        self._spi.transfer([CMD_RAMWR])
        self._dc.write(True)
        self._enviar(buf)
        self._soltar()

    # ----- Dibujo -----

    def fill(self, color=0):
        self.fill_rectangle(0, 0, self.width, self.height, color)

    def fill_rectangle(self, x, y, ancho, alto, color):
        x, y, ancho, alto = int(x), int(y), int(ancho), int(alto)
        if x < 0:
            ancho, x = ancho + x, 0     # recorta por la izquierda
        if y < 0:
            alto, y = alto + y, 0
        ancho = min(ancho, self.width - x)
        alto = min(alto, self.height - y)
        if ancho <= 0 or alto <= 0:
            return                      # queda fuera de la pantalla
        relleno = bytes([color >> 8, color & 0xFF]) * (ancho * alto)
        self._pintar(x, y, x + ancho - 1, y + alto - 1, relleno)

    def pixel(self, x, y, color):
        self.fill_rectangle(x, y, 1, 1, color)

    def hline(self, x, y, ancho, color):
        self.fill_rectangle(x, y, ancho, 1, color)

    def vline(self, x, y, alto, color):
        self.fill_rectangle(x, y, 1, alto, color)

    def image(self, img, x=0, y=0):
        if img.mode != "RGB":
            img = img.convert("RGB")
        ancho, alto = img.size
        self._pintar(x, y, x + ancho - 1, y + alto - 1, self._a_565(img))

    @staticmethod
    def _a_565(img):
        a = np.asarray(img, dtype=np.uint16)
        v = ((a[:, :, 0] & 0xF8) << 8 |
             (a[:, :, 1] & 0xFC) << 3 |
             a[:, :, 2] >> 3)
        return v.astype(">u2").tobytes()

    def cerrar(self):
        """Libera el bus y las lineas; se puede llamar mas de una vez."""
        for recurso in (self._spi, self._dc, self._rst, self._cs):
            if recurso is not None:
                recurso.close()
        self._spi = self._dc = self._rst = self._cs = None
