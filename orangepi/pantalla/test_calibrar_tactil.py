"""
Pruebas de calibrar_tactil.py sin hardware: la pantalla y el tactil son falsos
y el "usuario" es un guion de toques.

Ejecutar desde orangepi/pantalla:  .venv/bin/python -m unittest -v
"""

import io
import threading
import unittest
from contextlib import redirect_stderr, redirect_stdout
from unittest import mock

import calibrar_tactil as cal
import main
import tactil as tactil_mod
import vistas
from test_main import PantallaFalsa, TactilFalso


class Reloj:
    def __init__(self):
        self.t = 0.0

    def __call__(self):
        return self.t

    def dormir(self, s):
        self.t += s


class UsuarioFalso(TactilFalso):
    """Toca, en orden, los puntos crudos de `toques`; cada uno dura unas
    lecturas con el dedo puesto y otras tantas sin el."""

    def __init__(self, toques, reloj, parar=None):
        super().__init__()
        self.toques = list(toques)
        self.lecturas = 0
        self.reloj = reloj
        self.parar = parar

    def is_pressed(self):
        self.lecturas += 1
        ciclo = self.lecturas % 14                  # 7 con el dedo y 7 sin el
        if 1 <= ciclo <= 7:
            if ciclo == 1 and self.toques:
                self.crudo = self.toques.pop(0)
            self.presionado = bool(self.crudo) and (self.crudo != (0, 0))
        else:
            self.presionado = False
        if not self.toques and ciclo == 13 and self.parar:
            self.parar.set()
        return self.presionado


ESQUINAS = [(300, 400), (3800, 420), (3790, 3700), (310, 3690)]


class Salida(unittest.TestCase):
    def test_lineas_para_pegar_en_la_unidad_o_en_config(self):
        lineas = cal.formato_salida({
            "x_min": 210, "x_max": 3890, "y_min": -20, "y_max": 4100,
            "cambiar_ejes": 0, "invertir_x": 1, "invertir_y": 0})
        self.assertEqual(lineas, [
            "PANTALLA_TACTIL_X_MIN=210", "PANTALLA_TACTIL_X_MAX=3890",
            "PANTALLA_TACTIL_Y_MIN=-20", "PANTALLA_TACTIL_Y_MAX=4100",
            "PANTALLA_TACTIL_CAMBIAR_EJES=0", "PANTALLA_TACTIL_INVERTIR_X=1",
            "PANTALLA_TACTIL_INVERTIR_Y=0"])

    def test_el_objetivo_se_dibuja_en_cada_esquina(self):
        f = vistas.cargar_fuentes()
        imgs = [cal.dibujar_objetivo(i, f) for i in range(4)]
        for img in imgs:
            self.assertEqual(img.size, (240, 320))
        self.assertEqual(len({i.tobytes() for i in imgs}), 4)


class TomarEsquina(unittest.TestCase):
    def test_devuelve_el_crudo_del_toque(self):
        r = Reloj()
        t = UsuarioFalso([(1234, 2345)], r)
        self.assertEqual(cal.tomar_esquina(t, threading.Event(), r, r.dormir),
                         (1234, 2345))

    def test_si_hay_que_parar_devuelve_none(self):
        r = Reloj()
        parar = threading.Event()
        parar.set()
        self.assertIsNone(cal.tomar_esquina(UsuarioFalso([], r), parar, r, r.dormir))


class Calibrar(unittest.TestCase):
    def test_cuatro_esquinas_dan_la_calibracion(self):
        r = Reloj()
        p, t = PantallaFalsa(), UsuarioFalso(ESQUINAS, r)
        out = io.StringIO()
        resultado = cal.calibrar(p, t, vistas.cargar_fuentes(), threading.Event(),
                                 out, r, r.dormir)
        esperado = tactil_mod.calcular_calibracion(ESQUINAS, 240, 320, cal.MARGEN_PX)
        self.assertEqual(resultado, esperado)
        self.assertEqual(len(p.cuadros), 4)             # un objetivo por esquina
        self.assertIn("PANTALLA_TACTIL_X_MIN=%d" % esperado["x_min"], out.getvalue())

    def test_esquinas_incoherentes_se_dicen_y_no_calibran(self):
        r = Reloj()
        p, t = PantallaFalsa(), UsuarioFalso([(2000, 2000)] * 4, r)
        out = io.StringIO()
        self.assertIsNone(cal.calibrar(p, t, vistas.cargar_fuentes(),
                                       threading.Event(), out, r, r.dormir))
        self.assertIn("coherentes", out.getvalue())

    def test_cancelar_a_la_mitad(self):
        r = Reloj()
        parar = threading.Event()
        p, t = PantallaFalsa(), UsuarioFalso(ESQUINAS[:2], r, parar=parar)
        self.assertIsNone(cal.calibrar(p, t, vistas.cargar_fuentes(), parar,
                                       io.StringIO(), r, r.dormir))


class ModoCrudo(unittest.TestCase):
    def test_imprime_el_crudo_mientras_se_toca(self):
        r = Reloj()
        parar = threading.Event()
        t = UsuarioFalso([(1500, 2500)], r, parar=parar)
        out = io.StringIO()
        cal.modo_crudo(t, parar, out, r, r.dormir)
        lineas = out.getvalue().splitlines()
        self.assertTrue(lineas)
        self.assertIn("x=1500", lineas[-1])
        self.assertIn("y=2500", lineas[-1])


class Probar(unittest.TestCase):
    def test_dibuja_un_punto_donde_detecta_el_toque(self):
        r = Reloj()
        parar = threading.Event()
        t = UsuarioFalso([(1000 + 100, 2000 + 200)], r, parar=parar)
        p = PantallaFalsa()
        p.fill_rectangle = mock.Mock()
        c = tactil_mod.Calibracion(1000, 1240, 2000, 2320, False, False, False)
        cal.probar(p, t, c, parar, r, r.dormir)
        (args, _), = p.fill_rectangle.call_args_list[:1]
        self.assertEqual(args[:2], (100 - 3, 200 - 3))


class Main(unittest.TestCase):
    def correr(self, argv, entorno=None, abrir_tactil=None, abrir_pantalla=None):
        out, err = io.StringIO(), io.StringIO()
        with mock.patch.object(main, "abrir_tactil", abrir_tactil or
                               mock.Mock(return_value=TactilFalso())), \
                mock.patch.object(main, "abrir_pantalla", abrir_pantalla or
                                  mock.Mock(return_value=PantallaFalsa())), \
                mock.patch.dict(main.os.environ, entorno or {}, clear=True), \
                mock.patch.object(cal.signal, "signal"), \
                redirect_stdout(out), redirect_stderr(err):
            return cal.main(argv), out.getvalue(), err.getvalue()

    def test_config_invalida(self):
        codigo, _, err = self.correr(["--crudo"], {"PANTALLA_TACTIL_IRQ": "x"})
        self.assertEqual(codigo, 78)
        self.assertIn("PANTALLA_TACTIL_IRQ", err)

    def test_no_se_puede_abrir_el_tactil(self):
        codigo, _, err = self.correr(["--crudo"], abrir_tactil=mock.Mock(
            side_effect=PermissionError("Permission denied")))
        self.assertEqual(codigo, 74)
        self.assertIn("Permission denied", err)

    def test_argumento_desconocido(self):
        with self.assertRaises(SystemExit):
            self.correr(["--otra-cosa"])

    def test_crudo_no_abre_la_pantalla_y_cierra_el_tactil(self):
        t = TactilFalso()
        abrir_p = mock.Mock()
        parar = threading.Event()
        parar.set()                       # termina de inmediato
        with mock.patch.object(cal.threading, "Event", return_value=parar):
            codigo, _, _ = self.correr(["--crudo"], abrir_pantalla=abrir_p,
                                       abrir_tactil=mock.Mock(return_value=t))
        self.assertEqual(codigo, 0)
        abrir_p.assert_not_called()
        self.assertEqual(t.cerrado, 1)

    def test_guiada_libera_pantalla_y_tactil(self):
        t, p = TactilFalso(), PantallaFalsa()
        parar = threading.Event()
        parar.set()
        with mock.patch.object(cal.threading, "Event", return_value=parar):
            codigo, _, _ = self.correr([], abrir_pantalla=mock.Mock(return_value=p),
                                       abrir_tactil=mock.Mock(return_value=t))
        self.assertEqual(codigo, 0)
        self.assertEqual((t.cerrado, p.cerrada, p.llenados), (1, 1, [0]))


if __name__ == "__main__":
    unittest.main()
