"""
Controlador del panel tactil XPT2046 para la Orange Pi Zero 3W

Va en el mismo bus SPI3 que la pantalla, con el chip select 1
(/dev/spidev3.1), que maneja el kernel. La interrupcion TP_IRQ (pin fisico
11, linea 32 de gpiochip0) baja al tocar.

Lo usan main.py (los toques) y calibrar_tactil.py (la calibracion).
"""

from periphery import GPIO, SPI

CMD_X = 0xD0
CMD_Y = 0x90

CHIP = "/dev/gpiochip0"


class Touch:
    """Panel tactil XPT2046 sobre SPI, con la interrupcion por GPIO."""

    def __init__(self, bus=3, dispositivo=1, irq=32, baudrate=2000000,
                 chip=CHIP):
        ruta = "/dev/spidev%d.%d" % (bus, dispositivo)
        self._spi = SPI(ruta, 0, baudrate)
        try:
            self._irq = GPIO(chip, irq, "in")
        except BaseException:
            self._spi.close()
            raise

    def is_pressed(self):
        return not self._irq.read()     # la linea baja al tocar

    def get_coordinates(self):
        return self._leer(CMD_X), self._leer(CMD_Y)

    def _leer(self, comando):
        r = self._spi.transfer([comando, 0x00, 0x00])
        return ((r[1] << 8) | r[2]) >> 3    # el dato util son 12 bits

    def cerrar(self):
        self._spi.close()
        self._irq.close()
