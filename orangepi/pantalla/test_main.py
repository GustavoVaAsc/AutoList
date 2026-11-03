"""
Pruebas de main.py sin hardware (unittest de la biblioteca estandar).

Ejecutar desde orangepi/pantalla:  .venv/bin/python -m unittest -v
Usan una pantalla falsa: no abren /dev/spidev* ni /dev/gpiochip*.
"""

import io
import threading
import unittest
from contextlib import redirect_stderr
from datetime import datetime
from unittest import mock

import main

AHORA = datetime(2026, 11, 4, 9, 5, 7)


class PantallaFalsa:
    """Registra lo que main le manda, en lugar de hablar con el SPI."""

    def __init__(self, ancho=240, alto=320, parar=None, cuadros_max=None):
        self.width, self.height = ancho, alto
        self.cuadros = []
        self.llenados = []
        self.cerrada = 0
        self._parar, self._max = parar, cuadros_max

    def image(self, img):
        self.cuadros.append(img)
        if self._parar and len(self.cuadros) >= self._max:
            self._parar.set()       # como si llegara un SIGTERM

    def fill(self, color=0):
        self.llenados.append(color)

    def cerrar(self):
        self.cerrada += 1


class LeerConfig(unittest.TestCase):
    def test_valores_por_defecto(self):
        cfg = main.leer_config({})
        self.assertEqual(cfg["SPI_BUS"], 3)
        self.assertEqual(cfg["SPI_DISPOSITIVO"], 0)
        self.assertEqual((cfg["DC"], cfg["RST"], cfg["CS"]), (139, 33, None))
        self.assertEqual(cfg["GPIOCHIP"], "/dev/gpiochip0")
        self.assertEqual(cfg["BAUDRATE"], 40000000)

    def test_variables_de_entorno(self):
        cfg = main.leer_config({"PANTALLA_DC": "261", "PANTALLA_CS": "229",
                                "PANTALLA_GPIOCHIP": "/dev/gpiochip1"})
        self.assertEqual(cfg["DC"], 261)
        self.assertEqual(cfg["CS"], 229)
        self.assertEqual(cfg["GPIOCHIP"], "/dev/gpiochip1")

    def test_cs_vacio_es_kernel(self):
        self.assertIsNone(main.leer_config({"PANTALLA_CS": ""})["CS"])

    def test_entero_invalido(self):
        with self.assertRaisesRegex(ValueError, "PANTALLA_RST"):
            main.leer_config({"PANTALLA_RST": "abc"})


class Dibujo(unittest.TestCase):
    def setUp(self):
        self.fuentes = main.cargar_fuentes(240)

    def test_cuadro_rgb_del_tamano_de_la_pantalla(self):
        img = main.dibujar_arranque(240, 320, AHORA, self.fuentes)
        self.assertEqual(img.mode, "RGB")
        self.assertEqual(img.size, (240, 320))

    def test_cuadro_horizontal(self):
        img = main.dibujar_arranque(320, 240, AHORA, main.cargar_fuentes(320))
        self.assertEqual(img.size, (320, 240))

    def test_dibuja_texto_sobre_el_fondo(self):
        img = main.dibujar_arranque(240, 320, AHORA, self.fuentes)
        colores = {c for _, c in img.getcolors(240 * 320)}
        self.assertIn(main.FONDO, colores)
        self.assertGreater(len(colores), 2)     # texto con antialias

    def test_la_hora_cambia_el_cuadro(self):
        a = main.dibujar_arranque(240, 320, AHORA, self.fuentes)
        b = main.dibujar_arranque(240, 320, AHORA.replace(second=8),
                                  self.fuentes)
        self.assertNotEqual(a.tobytes(), b.tobytes())

    def test_la_misma_hora_da_el_mismo_cuadro(self):
        a = main.dibujar_arranque(240, 320, AHORA, self.fuentes)
        b = main.dibujar_arranque(240, 320, AHORA, self.fuentes)
        self.assertEqual(a.tobytes(), b.tobytes())


class Ciclo(unittest.TestCase):
    def test_apagar_limpia_y_libera(self):
        p = PantallaFalsa()
        main.apagar(p)
        self.assertEqual(p.llenados, [0])
        self.assertEqual(p.cerrada, 1)

    def test_apagar_libera_aunque_falle_el_borrado(self):
        p = PantallaFalsa()
        p.fill = mock.Mock(side_effect=OSError("spi"))
        with self.assertRaises(OSError):
            main.apagar(p)
        self.assertEqual(p.cerrada, 1)

    def test_ejecutar_dibuja_hasta_parar(self):
        parar = threading.Event()
        p = PantallaFalsa(parar=parar, cuadros_max=2)
        main.ejecutar(p, parar, main.cargar_fuentes(240))
        self.assertEqual(len(p.cuadros), 2)

    def test_ejecutar_no_dibuja_si_ya_hay_que_parar(self):
        parar = threading.Event()
        parar.set()
        p = PantallaFalsa()
        main.ejecutar(p, parar, main.cargar_fuentes(240))
        self.assertEqual(p.cuadros, [])


class Main(unittest.TestCase):
    def correr(self, pantalla=None, abrir=None, entorno=None):
        """Corre main() con la pantalla falsa; devuelve (codigo, stderr)."""
        abrir = abrir or mock.Mock(return_value=pantalla)
        err = io.StringIO()
        with mock.patch.object(main, "abrir_pantalla", abrir), \
                mock.patch.dict(main.os.environ, entorno or {}), \
                mock.patch.object(main.signal, "signal"), \
                redirect_stderr(err):
            return main.main(), err.getvalue()

    def test_no_se_puede_abrir_el_dispositivo(self):
        codigo, err = self.correr(
            abrir=mock.Mock(side_effect=PermissionError("Permission denied")))
        self.assertEqual(codigo, main.EX_IOERR)
        self.assertIn("/dev/spidev3.0", err)
        self.assertIn("/dev/gpiochip0", err)
        self.assertIn("Permission denied", err)

    def test_config_invalida(self):
        codigo, err = self.correr(entorno={"PANTALLA_DC": "x"})
        self.assertEqual(codigo, main.EX_CONFIG)
        self.assertIn("PANTALLA_DC", err)

    def test_senal_limpia_libera_y_sale_con_cero(self):
        parar = threading.Event()
        p = PantallaFalsa(parar=parar, cuadros_max=1)
        with mock.patch.object(main.threading, "Event", return_value=parar):
            codigo, _ = self.correr(pantalla=p)
        self.assertEqual(codigo, 0)
        self.assertEqual(len(p.cuadros), 1)
        self.assertEqual(p.llenados, [0])
        self.assertEqual(p.cerrada, 1)

    def test_fallo_de_escritura_libera_y_sale_con_error(self):
        p = PantallaFalsa()
        p.image = mock.Mock(side_effect=OSError("spi"))
        codigo, err = self.correr(pantalla=p)
        self.assertEqual(codigo, main.EX_IOERR)
        self.assertEqual(p.cerrada, 1)
        self.assertIn("spi", err)


if __name__ == "__main__":
    unittest.main()
