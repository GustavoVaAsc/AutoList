"""Pruebas de landmarks_retinaface.py con la biblioteca estándar.

No necesitan onnxruntime, numpy ni OpenCV: solo ejercitan la lógica pura y la
CLI hasta antes de cargar el modelo. Se corren con:

    cd orangepi/vision/conversion && python3 -B -m unittest
"""

import contextlib
import io
import math
import os
import tempfile
import unittest

import landmarks_retinaface as lr


class PruebasAnclas(unittest.TestCase):
    def test_numero_de_anclas(self):
        self.assertEqual(len(lr.generar_anclas(320, 320)), 4200)
        self.assertEqual(len(lr.generar_anclas(640, 640)), 16800)

    def test_orden_de_recorrido(self):
        a = lr.generar_anclas(320, 320)
        # stride 8, fila 0, columna 0: las dos anclas (16 y 32 px)
        self.assertAlmostEqual(a[0][0], 0.0125)
        self.assertAlmostEqual(a[0][1], 0.0125)
        self.assertAlmostEqual(a[0][2], 16 / 320)
        self.assertAlmostEqual(a[1][2], 32 / 320)
        self.assertAlmostEqual(a[1][0], a[0][0])
        # la columna avanza antes que la fila
        self.assertAlmostEqual(a[2][0], 1.5 * 8 / 320)
        self.assertAlmostEqual(a[2][1], 0.0125)
        self.assertAlmostEqual(a[80][0], 0.0125)
        self.assertAlmostEqual(a[80][1], 1.5 * 8 / 320)
        # primer ancla del stride 16 (40*40*2 = 3200)
        self.assertAlmostEqual(a[3200][0], 8 / 320)
        self.assertAlmostEqual(a[3200][2], 64 / 320)
        self.assertAlmostEqual(a[3201][2], 128 / 320)
        # primer ancla del stride 32 (3200 + 20*20*2 = 4000)
        self.assertAlmostEqual(a[4000][0], 16 / 320)
        self.assertAlmostEqual(a[4000][2], 256 / 320)
        self.assertAlmostEqual(a[4001][2], 512 / 320)

    def test_anclas_normalizan_por_ancho_y_alto(self):
        a = lr.generar_anclas(160, 320)  # alto 160, ancho 320
        self.assertAlmostEqual(a[0][2], 16 / 320)
        self.assertAlmostEqual(a[0][3], 16 / 160)
        self.assertAlmostEqual(a[0][1], 4 / 160)


class PruebasDecodificacion(unittest.TestCase):
    ANCLA = (0.5, 0.5, 0.2, 0.2)

    def test_deltas_cero_dan_el_ancla(self):
        caja, lms = lr.decodificar([0, 0, 0, 0], [0] * 10, self.ANCLA)
        x, y, w, h = caja
        self.assertAlmostEqual(x, 0.4)
        self.assertAlmostEqual(y, 0.4)
        self.assertAlmostEqual(w, 0.2)
        self.assertAlmostEqual(h, 0.2)
        for px, py in lms:
            self.assertAlmostEqual(px, 0.5)
            self.assertAlmostEqual(py, 0.5)

    def test_deltas_conocidos(self):
        # cx = 0.5 + 1*0.1*0.2 = 0.52; cy = 0.5 - 2*0.1*0.2 = 0.46
        # w = 0.2*exp(5*0.2) = 0.2e; h = 0.2*exp(0) = 0.2
        (x, y, w, h), _ = lr.decodificar([1, -2, 5, 0], [0] * 10, self.ANCLA)
        self.assertAlmostEqual(w, 0.2 * math.e)
        self.assertAlmostEqual(h, 0.2)
        self.assertAlmostEqual(x, 0.52 - 0.1 * math.e)
        self.assertAlmostEqual(y, 0.46 - 0.1)

    def test_landmarks_relativos_al_ancla(self):
        # La caja cambia mucho, pero los landmarks solo dependen del ancla:
        # p = centro + d*0.1*tamaño_ancla.
        d = [1, 2, -1, -2, 0, 0, 3, -3, 5, 5]
        _, lms = lr.decodificar([9, 9, 9, 9], d, self.ANCLA)
        esperado = [(0.52, 0.54), (0.48, 0.46), (0.5, 0.5), (0.56, 0.44), (0.6, 0.6)]
        self.assertEqual(len(lms), 5)
        for (px, py), (ex, ey) in zip(lms, esperado):
            self.assertAlmostEqual(px, ex)
            self.assertAlmostEqual(py, ey)

    def test_landmarks_con_ancla_no_cuadrada(self):
        _, lms = lr.decodificar([0] * 4, [1, 1] + [0] * 8, (0.5, 0.5, 0.1, 0.4))
        self.assertAlmostEqual(lms[0][0], 0.5 + 0.1 * 0.1)
        self.assertAlmostEqual(lms[0][1], 0.5 + 0.1 * 0.4)


class PruebasNms(unittest.TestCase):
    def test_suprime_solapadas_y_ordena_por_score(self):
        cajas = [(0.1, 0.1, 0.2, 0.2), (0.11, 0.1, 0.2, 0.2), (0.6, 0.6, 0.2, 0.2)]
        scores = [0.6, 0.9, 0.5]
        self.assertEqual(lr.nms(cajas, scores, 0.45), [1, 2])

    def test_sin_solape_se_conservan_todas(self):
        cajas = [(0.0, 0.0, 0.1, 0.1), (0.5, 0.5, 0.1, 0.1)]
        self.assertEqual(lr.nms(cajas, [0.5, 0.7], 0.45), [1, 0])

    def test_umbral_es_estricto(self):
        # IoU exacto 1/3 (mitad de ancho solapada): con umbral 1/3 se conserva
        cajas = [(0.0, 0.0, 0.2, 0.2), (0.1, 0.0, 0.2, 0.2)]
        self.assertEqual(lr.nms(cajas, [0.9, 0.8], 1 / 3 + 1e-9), [0, 1])
        self.assertEqual(lr.nms(cajas, [0.9, 0.8], 0.3), [0])

    def test_vacio(self):
        self.assertEqual(lr.nms([], [], 0.45), [])


class PruebasLetterbox(unittest.TestCase):
    def test_apaisada(self):
        lb = lr.calcular_letterbox(480, 640, 320, 320)  # 480 filas x 640 columnas
        self.assertEqual((lb.ancho_redim, lb.alto_redim), (320, 240))
        self.assertEqual((lb.arriba, lb.abajo, lb.izquierda, lb.derecha), (40, 40, 0, 0))
        self.assertEqual((lb.pad_ancho, lb.pad_alto), (0, 80))
        self.assertAlmostEqual(lb.ratio_x, 2.0)
        self.assertAlmostEqual(lb.ratio_y, 2.0)

    def test_vertical(self):
        lb = lr.calcular_letterbox(640, 480, 320, 320)
        self.assertEqual((lb.ancho_redim, lb.alto_redim), (240, 320))
        self.assertEqual((lb.izquierda, lb.derecha), (40, 40))
        self.assertEqual(lb.pad_ancho, 80)

    def test_relleno_impar_como_la_referencia(self):
        # 51 filas x 100 columnas: escala 3.2, redim 320x163 (163.2), dh = 78.5
        # top = round(78.4) = 78 y bot = round(78.6) = 79; pad/2 es entero (78).
        lb = lr.calcular_letterbox(51, 100, 320, 320)
        self.assertEqual((lb.ancho_redim, lb.alto_redim), (320, 163))
        self.assertEqual((lb.arriba, lb.abajo), (78, 79))
        self.assertEqual(lb.pad_alto, 157)

    def test_ida_y_vuelta(self):
        for filas, cols in ((480, 640), (640, 480), (300, 300), (720, 1280), (1000, 333)):
            lb = lr.calcular_letterbox(filas, cols, 320, 320)
            for fx, fy in ((0.1, 0.2), (0.5, 0.5), (0.9, 0.75)):
                x, y = fx * cols, fy * filas
                nx, ny = lr.hacia_letterbox(x, y, lb, 320, 320)
                rx, ry = lr.deshacer_letterbox(nx, ny, lb, 320, 320)
                # la referencia usa pad/2 entero: el error es menos de 1 px
                self.assertAlmostEqual(rx, x, delta=lb.ratio_x)
                self.assertAlmostEqual(ry, y, delta=lb.ratio_y)

    def test_centro_apaisado(self):
        lb = lr.calcular_letterbox(480, 640, 320, 320)
        self.assertEqual(lr.deshacer_letterbox(0.5, 0.5, lb, 320, 320), (320.0, 240.0))

    def test_entrada_invalida(self):
        with self.assertRaises(ValueError):
            lr.calcular_letterbox(0, 640, 320, 320)


class PruebasMejorCara(unittest.TestCase):
    def _salidas(self):
        n = len(lr.generar_anclas(320, 320))
        loc = [[0, 0, 0, 0] for _ in range(n)]
        conf = [[1.0, 0.0] for _ in range(n)]
        lms = [[0] * 10 for _ in range(n)]
        return loc, conf, lms

    def test_elige_el_mayor_score_sobre_el_umbral(self):
        loc, conf, lms = self._salidas()
        conf[3200] = [0.1, 0.6]   # stride 16, ancla de 64 px
        conf[4000] = [0.1, 0.9]   # stride 32, ancla de 256 px, el mejor
        puntos = lr.mejor_cara(loc, conf, lms, 320, 320, 320, 320, 0.4, 0.45)
        self.assertEqual(len(puntos), 5)
        # imagen cuadrada de 320: la salida son los centros del ancla 4000
        self.assertAlmostEqual(puntos[0][0], 0.05 * 320)
        self.assertAlmostEqual(puntos[0][1], 0.05 * 320)

    def test_sin_caras(self):
        loc, conf, lms = self._salidas()
        conf[10] = [0.5, 0.4]  # igual al umbral: no lo supera
        self.assertIsNone(lr.mejor_cara(loc, conf, lms, 320, 320, 320, 320, 0.4, 0.45))

    def test_tamano_inconsistente(self):
        loc, conf, lms = self._salidas()
        with self.assertRaises(ValueError):
            lr.mejor_cara(loc[:-1], conf, lms, 320, 320, 320, 320, 0.4, 0.45)


class PruebasMedia(unittest.TestCase):
    def test_bgr_por_omision(self):
        self.assertEqual(lr.media_efectiva(False), (104.0, 117.0, 123.0))

    def test_rgb_invierte_la_media(self):
        self.assertEqual(lr.media_efectiva(True), (123.0, 117.0, 104.0))


class PruebasProcesarLista(unittest.TestCase):
    class Img:
        shape = (320, 320, 3)

    def _correr(self, rutas, inferir, maximo=None):
        salida = io.StringIO()
        res = lr.procesar_lista(
            rutas, salida, lambda r: None if r == "mala.jpg" else self.Img(),
            inferir, 320, 320, 0.4, maximo,
        )
        return res, salida.getvalue()

    def _inferir_con(self, score):
        n = len(lr.generar_anclas(320, 320))

        def inferir(_img):
            conf = [[1.0, 0.0] for _ in range(n)]
            conf[4000] = [0.0, score]
            return [[0] * 4 for _ in range(n)], conf, [[0] * 10 for _ in range(n)]
        return inferir

    def test_cuenta_escritas_y_omitidas(self):
        (esc, omi), texto = self._correr(
            ["/a.jpg", "mala.jpg", "/b.jpg"], self._inferir_con(0.9))
        self.assertEqual((esc, omi), (2, 1))
        self.assertEqual(len(texto.splitlines()), 2)

    def test_sin_cara_se_omite(self):
        (esc, omi), texto = self._correr(["/a.jpg"], self._inferir_con(0.1))
        self.assertEqual((esc, omi, texto), (0, 1, ""))

    def test_max_corta_tras_n_escritas(self):
        (esc, omi), _ = self._correr(["/a.jpg"] * 5, self._inferir_con(0.9), maximo=2)
        self.assertEqual((esc, omi), (2, 0))

    def test_desbordamiento_de_exp_omite_la_imagen(self):
        n = len(lr.generar_anclas(320, 320))

        def inferir(_img):
            conf = [[1.0, 0.0] for _ in range(n)]
            conf[4000] = [0.0, 0.9]
            loc = [[0, 0, 1e6, 0] for _ in range(n)]
            return loc, conf, [[0] * 10 for _ in range(n)]
        (esc, omi), texto = self._correr(["/a.jpg"], inferir)
        self.assertEqual((esc, omi, texto), (0, 1, ""))

    def test_decodificar_con_delta_enorme_lanza_aritmetico(self):
        with self.assertRaises(ArithmeticError):
            lr.decodificar([0, 0, 1e6, 0], [0] * 10, (0.5, 0.5, 0.2, 0.2))


class PruebasManifiesto(unittest.TestCase):
    def test_linea_tab(self):
        pts = [(1.0, 2.0), (3.5, 4.25), (5, 6), (7, 8), (9, 10)]
        self.assertEqual(
            lr.linea_manifiesto("/d/a b.jpg", pts),
            "/d/a b.jpg\t1.00 2.00 3.50 4.25 5.00 6.00 7.00 8.00 9.00 10.00",
        )

    def test_exige_cinco_puntos_finitos(self):
        with self.assertRaises(ValueError):
            lr.linea_manifiesto("a.jpg", [(1, 2)] * 4)
        with self.assertRaises(ValueError):
            lr.linea_manifiesto("a.jpg", [(1, 2)] * 4 + [(float("nan"), 1)])

    def test_ruta_con_tab_o_salto_se_rechaza(self):
        for ruta in ("a\tb.jpg", "a\nb.jpg"):
            with self.assertRaises(ValueError):
                lr.linea_manifiesto(ruta, [(1, 2)] * 5)

    def test_ruta_relativa_se_escribe_absoluta(self):
        # alinear_lfw resuelve las rutas respecto a su propio directorio de trabajo
        linea = lr.linea_manifiesto("rel/a.jpg", [(1, 2)] * 5)
        ruta = linea.split("\t")[0]
        self.assertEqual(ruta, os.path.abspath("rel/a.jpg"))
        self.assertTrue(os.path.isabs(ruta))

    def test_leer_lista(self):
        lineas = ["# comentario\n", "\n", "  \n", "a.jpg\n", "dir con espacio/b.jpg\r\n", "#c\n"]
        self.assertEqual(lr.leer_lista(lineas), ["a.jpg", "dir con espacio/b.jpg"])


class PruebasBarrera(unittest.TestCase):
    def test_dentro_del_repo(self):
        raiz = lr.raiz_repo()
        self.assertTrue(lr.salida_dentro_del_repo(os.path.join(raiz, "x.tsv"), raiz))
        self.assertTrue(lr.salida_dentro_del_repo(os.path.join(raiz, "a", "b", "x.tsv"), raiz))
        self.assertTrue(lr.salida_dentro_del_repo(raiz, raiz))

    def test_rodeo_con_punto_punto(self):
        raiz = lr.raiz_repo()
        self.assertTrue(
            lr.salida_dentro_del_repo(os.path.join(raiz, "..", os.path.basename(raiz), "x"), raiz)
        )

    def test_fuera_del_repo(self):
        raiz = lr.raiz_repo()
        self.assertFalse(lr.salida_dentro_del_repo("/tmp/x.tsv", raiz))
        # mismo prefijo de texto, otro directorio
        self.assertFalse(lr.salida_dentro_del_repo(raiz + "-otro/x.tsv", raiz))

    def test_enlace_simbolico_hacia_el_repo(self):
        raiz = lr.raiz_repo()
        with tempfile.TemporaryDirectory() as tmp:
            enlace = os.path.join(tmp, "enlace")
            os.symlink(raiz, enlace)
            try:
                self.assertTrue(
                    lr.salida_dentro_del_repo(os.path.join(enlace, "x.tsv"), raiz)
                )
            finally:
                os.unlink(enlace)


class PruebasCli(unittest.TestCase):
    def _correr(self, argv):
        err = io.StringIO()
        with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
            try:
                codigo = lr.main(argv)
            except SystemExit as e:
                codigo = e.code
        return codigo, err.getvalue()

    def test_faltan_argumentos(self):
        codigo, _ = self._correr([])
        self.assertEqual(codigo, 2)

    def test_rgb_se_acepta(self):
        codigo, _ = self._correr(["--rgb", "--onnx", "/no/m.onnx", "--lista", "/no/l.txt",
                                  "--salida", "/tmp/s.tsv"])
        self.assertEqual(codigo, 1)  # llega a validar --onnx: --rgb es un argumento válido

    def test_umbral_invalido(self):
        for u in ("0", "1.5", "-1", "abc"):
            codigo, _ = self._correr(
                ["--onnx", "m.onnx", "--lista", "l.txt", "--salida", "/tmp/s.tsv", "--umbral", u]
            )
            self.assertNotEqual(codigo, 0, u)

    def test_max_invalido(self):
        for m in ("0", "-3", "x"):
            codigo, _ = self._correr(
                ["--onnx", "m.onnx", "--lista", "l.txt", "--salida", "/tmp/s.tsv", "--max", m]
            )
            self.assertNotEqual(codigo, 0, m)

    def test_salida_dentro_del_repo_se_rechaza_antes_de_todo(self):
        destino = os.path.join(lr.raiz_repo(), "no_debe_existir.tsv")
        codigo, err = self._correr(
            ["--onnx", "/no/existe.onnx", "--lista", "/no/existe.txt", "--salida", destino]
        )
        self.assertEqual(codigo, 1)
        self.assertIn("dentro del repositorio", err)
        self.assertFalse(os.path.exists(destino))

    def test_onnx_inexistente(self):
        with tempfile.TemporaryDirectory() as tmp:
            codigo, err = self._correr(
                ["--onnx", os.path.join(tmp, "no.onnx"), "--lista", os.path.join(tmp, "l.txt"),
                 "--salida", os.path.join(tmp, "s.tsv")]
            )
            self.assertEqual(codigo, 1)
            self.assertIn("--onnx", err)
            self.assertFalse(os.path.exists(os.path.join(tmp, "s.tsv")))

    def test_lista_inexistente(self):
        with tempfile.TemporaryDirectory() as tmp:
            modelo = os.path.join(tmp, "m.onnx")
            open(modelo, "wb").close()
            codigo, err = self._correr(
                ["--onnx", modelo, "--lista", os.path.join(tmp, "l.txt"),
                 "--salida", os.path.join(tmp, "s.tsv")]
            )
            self.assertEqual(codigo, 1)
            self.assertIn("--lista", err)


if __name__ == "__main__":
    unittest.main()
