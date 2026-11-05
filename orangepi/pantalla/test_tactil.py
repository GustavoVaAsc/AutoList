"""
Pruebas de tactil.py sin hardware: calibracion, conversion de coordenadas
crudas del XPT2046 a pixeles y deteccion de un toque (se entrega al soltar).

Ejecutar desde orangepi/pantalla:  .venv/bin/python -m unittest -v
"""

import unittest

import tactil
from tactil import Calibracion, DetectorToque, a_pixeles, calcular_calibracion

ANCHO, ALTO = 240, 320
CAL = Calibracion(x_min=200, x_max=3900, y_min=300, y_max=3800,
                  cambiar_ejes=False, invertir_x=False, invertir_y=False)


class DeConfig(unittest.TestCase):
    def test_lee_las_claves_tactil(self):
        cfg = {"TACTIL_X_MIN": 100, "TACTIL_X_MAX": 4000, "TACTIL_Y_MIN": 150,
               "TACTIL_Y_MAX": 3950, "TACTIL_CAMBIAR_EJES": 1,
               "TACTIL_INVERTIR_X": 0, "TACTIL_INVERTIR_Y": 1}
        self.assertEqual(tactil.calibracion_de_config(cfg),
                         Calibracion(100, 4000, 150, 3950, True, False, True))


class APixeles(unittest.TestCase):
    def test_esquinas_y_centro(self):
        self.assertEqual(a_pixeles(200, 300, CAL, ANCHO, ALTO), (0, 0))
        self.assertEqual(a_pixeles(3900, 3800, CAL, ANCHO, ALTO), (239, 319))
        self.assertEqual(a_pixeles(2050, 2050, CAL, ANCHO, ALTO), (120, 160))

    def test_fuera_de_rango_se_recorta(self):
        self.assertEqual(a_pixeles(0, 0, CAL, ANCHO, ALTO), (0, 0))
        self.assertEqual(a_pixeles(4095, 4095, CAL, ANCHO, ALTO), (239, 319))

    def test_invertir(self):
        cal = CAL._replace(invertir_x=True, invertir_y=True)
        self.assertEqual(a_pixeles(200, 300, cal, ANCHO, ALTO), (239, 319))
        self.assertEqual(a_pixeles(3900, 3800, cal, ANCHO, ALTO), (0, 0))

    def test_cambiar_ejes(self):
        # el eje X de la pantalla lo da el canal Y crudo, y al reves
        cal = CAL._replace(cambiar_ejes=True)
        self.assertEqual(a_pixeles(300, 200, cal, ANCHO, ALTO), (0, 0))
        self.assertEqual(a_pixeles(3800, 3900, cal, ANCHO, ALTO), (239, 319))


class Calcular(unittest.TestCase):
    MARGEN = 20

    def esquinas(self, crudo):
        """Esquinas TL, TR, BR, BL en pixeles -> lo que mediria el tactil."""
        m = self.MARGEN
        puntos = ((m, m), (ANCHO - m, m), (ANCHO - m, ALTO - m), (m, ALTO - m))
        return [crudo(x, y) for x, y in puntos]

    def comprobar(self, cal, crudo):
        r = calcular_calibracion(self.esquinas(crudo), ANCHO, ALTO, self.MARGEN)
        self.assertEqual(r["cambiar_ejes"], int(cal.cambiar_ejes))
        self.assertEqual(r["invertir_x"], int(cal.invertir_x))
        self.assertEqual(r["invertir_y"], int(cal.invertir_y))
        calculada = Calibracion(r["x_min"], r["x_max"], r["y_min"], r["y_max"],
                                cal.cambiar_ejes, cal.invertir_x, cal.invertir_y)
        # con la calibracion calculada, cada esquina cae en su pixel (+-1)
        m = self.MARGEN
        for (cx, cy), (px, py) in zip(
                self.esquinas(crudo),
                ((m, m), (ANCHO - m, m), (ANCHO - m, ALTO - m), (m, ALTO - m))):
            ox, oy = a_pixeles(cx, cy, calculada, ANCHO, ALTO)
            self.assertLessEqual(abs(ox - px), 1)
            self.assertLessEqual(abs(oy - py), 1)

    def test_sin_cambios(self):
        self.comprobar(CAL, lambda x, y: (
            200 + x * 3700 / ANCHO, 300 + y * 3500 / ALTO))

    def test_invertido(self):
        cal = CAL._replace(invertir_x=True, invertir_y=True)
        self.comprobar(cal, lambda x, y: (
            3900 - x * 3700 / ANCHO, 3800 - y * 3500 / ALTO))

    def test_ejes_cambiados(self):
        cal = CAL._replace(cambiar_ejes=True, invertir_y=True)
        # el canal X crudo sigue a la y de la pantalla (invertida) y el Y a la x
        self.comprobar(cal, lambda x, y: (
            3800 - y * 3500 / ALTO, 300 + x * 3700 / ANCHO))

    def test_esquinas_incoherentes(self):
        with self.assertRaises(ValueError):
            calcular_calibracion([(2000, 2000)] * 4, ANCHO, ALTO, self.MARGEN)

    def test_faltan_esquinas(self):
        with self.assertRaises(ValueError):
            calcular_calibracion([(200, 300)] * 3, ANCHO, ALTO, self.MARGEN)


class Detector(unittest.TestCase):
    def pulsar(self, d, t0, muestras, paso=0.02, crudo=(2000, 2000)):
        """Alimenta `muestras` lecturas con el dedo puesto."""
        resultado = None
        for i in range(muestras):
            resultado = d.actualizar(True, crudo, t0 + i * paso) or resultado
        return resultado

    def soltar(self, d, t0, duracion=0.3, paso=0.02):
        """Dedo fuera durante `duracion`; devuelve el primer toque entregado."""
        t = t0
        while t < t0 + duracion:
            toque = d.actualizar(False, None, t)
            if toque:
                return toque
            t += paso
        return None

    def test_entrega_el_toque_al_soltar(self):
        d = DetectorToque()
        self.assertIsNone(self.pulsar(d, 0, 5))
        self.assertEqual(self.soltar(d, 0.1), (2000, 2000))

    def test_entrega_una_sola_vez(self):
        d = DetectorToque()
        self.pulsar(d, 0, 5)
        self.assertIsNotNone(self.soltar(d, 0.1))
        self.assertIsNone(self.soltar(d, 0.5))

    def test_usa_la_mediana_y_descarta_rebotes(self):
        d = DetectorToque()
        for i, c in enumerate([(2000, 2100), (2010, 2090), (4095, 0), (2005, 2095)]):
            d.actualizar(True, c, i * 0.02)
        self.assertEqual(self.soltar(d, 0.1), (2005, 2095))

    def test_un_pulso_de_ruido_no_cuenta(self):
        d = DetectorToque()
        d.actualizar(True, (2000, 2000), 0)
        self.assertIsNone(self.soltar(d, 0.02))

    def test_un_rebote_al_soltar_no_parte_el_toque(self):
        d = DetectorToque()
        self.pulsar(d, 0, 5)
        self.assertIsNone(d.actualizar(False, None, 0.12))      # rebote de 20 ms
        self.assertIsNone(d.actualizar(True, (2000, 2000), 0.14))
        self.assertIsNone(self.soltar(d, 0.16, duracion=0.03))
        self.assertIsNotNone(self.soltar(d, 0.2))

    def test_lecturas_invalidas_no_hacen_toque(self):
        d = DetectorToque()
        self.pulsar(d, 0, 6, crudo=(0, 0))
        self.assertIsNone(self.soltar(d, 0.2))
        self.pulsar(d, 1, 6, crudo=(4095, 4095))
        self.assertIsNone(self.soltar(d, 1.2))

    def test_dos_toques_seguidos(self):
        d = DetectorToque()
        self.pulsar(d, 0, 5, crudo=(1000, 1000))
        self.assertEqual(self.soltar(d, 0.1), (1000, 1000))
        self.pulsar(d, 1, 5, crudo=(3000, 3000))
        self.assertEqual(self.soltar(d, 1.1), (3000, 3000))


if __name__ == "__main__":
    unittest.main()
