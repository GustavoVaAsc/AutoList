"""Pruebas de nodered/reporte_csv.py, el programa de Python que el nodo exec de Node-RED
ejecuta para escribir el CSV de asistencia (plan, secciones 6.4 y 7.3).

Solo biblioteca estándar. Desde esta carpeta: python3 -m unittest
Los nombres y cuentas de aquí son entradas de prueba, no datos de la lista real.
"""
import contextlib
import csv
import importlib.util
import io
import json
import os
import pathlib
import tempfile
import unittest

RUTA = pathlib.Path(__file__).resolve().parent.parent / "reporte_csv.py"
_spec = importlib.util.spec_from_file_location("reporte_csv", RUTA)
reporte = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(reporte)

BOM = b"\xef\xbb\xbf"


def fila(cuenta="320068234", nombre="Ana López", estado="presente", hora="2026-11-05 09:12:33"):
    return {"no_cuenta": cuenta, "nombre": nombre, "estado": estado, "hora": hora}


class PruebaConDirectorio(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.dir = self._tmp.name

    def escribir_entrada(self, sesion, filas, sesion_en_archivo=None):
        ruta = os.path.join(self.dir, ".entrada-%d.json" % sesion)
        with open(ruta, "w", encoding="utf-8") as f:
            json.dump({"sesion": sesion if sesion_en_archivo is None else sesion_en_archivo,
                       "filas": filas}, f)
        return ruta

    def leer_csv(self, sesion):
        ruta = os.path.join(self.dir, "sesion-%d.csv" % sesion)
        with open(ruta, "rb") as f:
            crudo = f.read()
        with open(ruta, newline="", encoding="utf-8-sig") as f:
            return crudo, list(csv.reader(f))


class NeutralizarFormulas(unittest.TestCase):
    def test_cada_prefijo_peligroso_se_neutraliza(self):
        for prefijo in ("=", "+", "-", "@", "\t", "\r"):
            with self.subTest(prefijo=repr(prefijo)):
                valor = prefijo + "cmd|' /C calc'!A0"
                self.assertEqual(reporte.neutralizar(valor), "'" + valor)

    def test_un_nombre_normal_no_cambia(self):
        for valor in ("Ana López", "José-Luis", "O'Brien", "  Ana", "1234", "", "Ñandú"):
            with self.subTest(valor=valor):
                self.assertEqual(reporte.neutralizar(valor), valor)

    def test_solo_cuenta_el_primer_caracter(self):
        self.assertEqual(reporte.neutralizar("Ana=1+1"), "Ana=1+1")

    def test_formas_de_ancho_completo_y_signo_menos_unicode(self):
        for prefijo in ("\uff1d", "\uff0b", "\uff0d", "\uff20", "\u2212"):
            with self.subTest(prefijo=hex(ord(prefijo))):
                valor = prefijo + "1+1"
                self.assertEqual(reporte.neutralizar(valor), "'" + valor)

    def test_un_segmento_tras_punto_y_coma_o_coma_tambien_se_neutraliza(self):
        # Con otro separador de lista (es-ES usa ;) el valor se parte en celdas y cada segmento
        # que empieza con un carácter de fórmula quedaría como fórmula.
        casos = {
            "Ana;=1+1": "Ana;'=1+1",
            "Ana,=1+1": "Ana,'=1+1",
            "Ana; =1+1": "Ana; '=1+1",
            "Ana;   @SUMA(1)": "Ana;   '@SUMA(1)",
            "a;=b;+c,-d": "a;'=b;'+c,'-d",
            "=a;=b": "'=a;'=b",
            "Ana;\uff1d1": "Ana;'\uff1d1",
            "Ana;\u2212 1": "Ana;'\u2212 1",
            "Ana;\t=1": "Ana;'\t=1",
        }
        for entrada, esperado in casos.items():
            with self.subTest(entrada=entrada):
                self.assertEqual(reporte.neutralizar(entrada), esperado)

    def test_separadores_sin_formula_no_cambian(self):
        for valor in ("López, Ana", "Ana; José", "Pérez-Ruiz, Ana", "a;b,c", "Ana ;x", "Ana;-"[:-1]):
            with self.subTest(valor=valor):
                self.assertEqual(reporte.neutralizar(valor), valor)


class IdDeSesion(unittest.TestCase):
    def test_acepta_enteros_positivos(self):
        self.assertEqual(reporte.leer_id("1"), 1)
        self.assertEqual(reporte.leer_id("12"), 12)
        self.assertEqual(reporte.leer_id("2147483647"), 2147483647)

    def test_rechaza_lo_demas(self):
        for malo in ("", "0", "-1", "01", "1.5", "12; rm", "12 13", "../x", "２", "٣", "2147483648",
                     "99999999999", " 12", "12\n", "1e3", "0x10"):
            with self.subTest(malo=malo):
                with self.assertRaises(reporte.ErrorReporte):
                    reporte.leer_id(malo)


class Generar(PruebaConDirectorio):
    def test_escribe_encabezado_y_filas_en_utf8_con_bom(self):
        self.escribir_entrada(12, [fila(), fila("320068235", "Ñandú Pérez", "falta", None)])
        ruta, n = reporte.generar(12, self.dir)
        self.assertEqual(ruta, os.path.join(self.dir, "sesion-12.csv"))
        self.assertEqual(n, 2)
        crudo, filas = self.leer_csv(12)
        self.assertTrue(crudo.startswith(BOM))
        self.assertEqual(filas, [
            ["no_cuenta", "nombre", "estado", "hora"],
            ["320068234", "Ana López", "presente", "2026-11-05 09:12:33"],
            ["320068235", "Ñandú Pérez", "falta", ""],
        ])
        self.assertIn("Ñandú".encode("utf-8"), crudo)

    def test_lineas_terminan_en_crlf(self):
        self.escribir_entrada(1, [fila()])
        reporte.generar(1, self.dir)
        crudo, _ = self.leer_csv(1)
        self.assertEqual(crudo.count(b"\r\n"), 2)
        self.assertEqual(crudo.replace(b"\r\n", b"").count(b"\n"), 0)

    def test_sin_filas_solo_el_encabezado(self):
        self.escribir_entrada(3, [])
        _, n = reporte.generar(3, self.dir)
        self.assertEqual(n, 0)
        _, filas = self.leer_csv(3)
        self.assertEqual(filas, [["no_cuenta", "nombre", "estado", "hora"]])

    def test_neutraliza_formulas_en_el_nombre(self):
        nombres = ["=HYPERLINK(\"http://x\",\"a\")", "+1+1", "-2+3", "@SUMA(1)", "\t=1", "\r=1"]
        self.escribir_entrada(4, [fila("32006823%d" % i, nombre) for i, nombre in enumerate(nombres)])
        reporte.generar(4, self.dir)
        _, filas = self.leer_csv(4)
        for lectura, original in zip(filas[1:], nombres):
            self.assertEqual(lectura[1], "'" + original)

    def test_comas_comillas_y_saltos_de_linea_van_citados(self):
        nombre = 'Ana, "la" López\nSegunda línea'
        self.escribir_entrada(5, [fila(nombre=nombre)])
        reporte.generar(5, self.dir)
        _, filas = self.leer_csv(5)
        self.assertEqual(filas[1][1], nombre)
        self.assertEqual(len(filas[1]), 4)

    def test_borra_la_entrada_y_no_deja_temporales(self):
        entrada = self.escribir_entrada(6, [fila()])
        reporte.generar(6, self.dir)
        self.assertFalse(os.path.exists(entrada))
        self.assertEqual(sorted(os.listdir(self.dir)), ["sesion-6.csv"])

    def test_reemplaza_un_reporte_anterior(self):
        self.escribir_entrada(7, [fila()])
        reporte.generar(7, self.dir)
        self.escribir_entrada(7, [fila(), fila("320068235", "Luis")])
        reporte.generar(7, self.dir)
        _, filas = self.leer_csv(7)
        self.assertEqual(len(filas), 3)

    def test_el_archivo_no_es_legible_por_otros(self):
        self.escribir_entrada(8, [fila()])
        ruta, _ = reporte.generar(8, self.dir)
        self.assertEqual(os.stat(ruta).st_mode & 0o077, 0)

    def test_sin_directorio_falla_con_error_controlado(self):
        # El directorio lo crea el nodo file de Node-RED (createDir); el programa no lo inventa.
        with self.assertRaises(reporte.ErrorReporte):
            reporte.generar(9, os.path.join(self.dir, "no_existe"))


class NoDejaLaEntradaEnDisco(PruebaConDirectorio):
    """La entrada lleva nombres: se borra salga bien o mal, y nada la reintenta."""

    def ruta_entrada(self, sesion=2):
        return os.path.join(self.dir, ".entrada-%d.json" % sesion)

    def test_se_borra_si_la_validacion_falla(self):
        self.escribir_entrada(2, [fila(), fila(estado="inventado")])
        with self.assertRaises(reporte.ErrorReporte):
            reporte.generar(2, self.dir)
        self.assertFalse(os.path.exists(self.ruta_entrada()))
        self.assertEqual(os.listdir(self.dir), [])

    def test_se_borra_si_el_json_es_invalido(self):
        with open(self.ruta_entrada(), "w", encoding="utf-8") as f:
            f.write("{no es json")
        with self.assertRaises(reporte.ErrorReporte):
            reporte.generar(2, self.dir)
        self.assertFalse(os.path.exists(self.ruta_entrada()))

    def test_se_borra_si_la_sesion_no_coincide(self):
        self.escribir_entrada(2, [fila()], sesion_en_archivo=3)
        with self.assertRaises(reporte.ErrorReporte):
            reporte.generar(2, self.dir)
        self.assertFalse(os.path.exists(self.ruta_entrada()))

    def test_se_borra_si_falla_al_escribir_el_csv(self):
        self.escribir_entrada(2, [fila()])
        original = reporte.escribir_csv

        def falla(*args, **kwargs):
            raise OSError("disco lleno")

        reporte.escribir_csv = falla
        self.addCleanup(setattr, reporte, "escribir_csv", original)
        with self.assertRaises(OSError):
            reporte.generar(2, self.dir)
        self.assertFalse(os.path.exists(self.ruta_entrada()))

    def test_sin_entrada_no_hay_nada_que_borrar(self):
        with self.assertRaises(reporte.ErrorReporte):
            reporte.generar(2, self.dir)

    def test_una_entrada_demasiado_grande_se_borra(self):
        with open(self.ruta_entrada(), "w", encoding="utf-8") as f:
            f.write(" " * (reporte.MAX_BYTES_ENTRADA + 1))
        with self.assertRaises(reporte.ErrorReporte):
            reporte.generar(2, self.dir)
        self.assertFalse(os.path.exists(self.ruta_entrada()))


class EnlacesSimbolicos(PruebaConDirectorio):
    def test_una_entrada_que_es_un_enlace_se_rechaza_sin_seguirlo(self):
        objetivo = os.path.join(self.dir, "otro.json")
        with open(objetivo, "w", encoding="utf-8") as f:
            json.dump({"sesion": 2, "filas": [fila()]}, f)
        os.symlink(objetivo, os.path.join(self.dir, ".entrada-2.json"))
        with self.assertRaises(reporte.ErrorReporte):
            reporte.generar(2, self.dir)
        # El enlace se quita; el archivo al que apuntaba no se toca.
        self.assertFalse(os.path.lexists(os.path.join(self.dir, ".entrada-2.json")))
        self.assertTrue(os.path.exists(objetivo))
        self.assertFalse(os.path.exists(os.path.join(self.dir, "sesion-2.csv")))

    def test_una_entrada_que_no_es_un_archivo_regular_se_rechaza(self):
        os.mkdir(os.path.join(self.dir, ".entrada-2.json"))
        with self.assertRaises(reporte.ErrorReporte):
            reporte.leer_entrada(self.dir, 2)

    def test_un_temporal_previo_se_reemplaza_y_un_enlace_no_se_sigue(self):
        objetivo = os.path.join(self.dir, "victima.txt")
        with open(objetivo, "w", encoding="utf-8") as f:
            f.write("intacto")
        os.symlink(objetivo, os.path.join(self.dir, "sesion-2.csv.tmp"))
        reporte.escribir_csv(self.dir, 2, [("320068234", "Ana", "presente", "")])
        with open(objetivo, encoding="utf-8") as f:
            self.assertEqual(f.read(), "intacto")
        self.assertFalse(os.path.lexists(os.path.join(self.dir, "sesion-2.csv.tmp")))
        self.assertTrue(os.path.isfile(os.path.join(self.dir, "sesion-2.csv")))

    def test_un_temporal_previo_regular_se_reemplaza(self):
        with open(os.path.join(self.dir, "sesion-2.csv.tmp"), "w", encoding="utf-8") as f:
            f.write("basura")
        destino = reporte.escribir_csv(self.dir, 2, [("320068234", "Ana", "presente", "")])
        with open(destino, "rb") as f:
            self.assertNotIn(b"basura", f.read())

    def test_los_permisos_son_0600_aunque_el_umask_sea_laxo(self):
        anterior = os.umask(0)
        self.addCleanup(os.umask, anterior)
        destino = reporte.escribir_csv(self.dir, 2, [("320068234", "Ana", "presente", "")])
        self.assertEqual(os.stat(destino).st_mode & 0o777, 0o600)

    def test_el_destino_ya_existente_se_reemplaza_aunque_sea_un_enlace(self):
        objetivo = os.path.join(self.dir, "victima.txt")
        with open(objetivo, "w", encoding="utf-8") as f:
            f.write("intacto")
        os.symlink(objetivo, os.path.join(self.dir, "sesion-2.csv"))
        reporte.escribir_csv(self.dir, 2, [("320068234", "Ana", "presente", "")])
        with open(objetivo, encoding="utf-8") as f:
            self.assertEqual(f.read(), "intacto")
        self.assertFalse(os.path.islink(os.path.join(self.dir, "sesion-2.csv")))


class EntradaInvalida(PruebaConDirectorio):
    def assertRechaza(self, sesion=2):
        with self.assertRaises(reporte.ErrorReporte) as ctx:
            reporte.generar(sesion, self.dir)
        return str(ctx.exception)

    def test_sin_archivo_de_entrada(self):
        self.assertRechaza()

    def test_json_invalido(self):
        with open(os.path.join(self.dir, ".entrada-2.json"), "w", encoding="utf-8") as f:
            f.write("{no es json")
        self.assertRechaza()

    def test_la_sesion_del_archivo_no_coincide(self):
        self.escribir_entrada(2, [fila()], sesion_en_archivo=3)
        self.assertRechaza()

    def test_estructura_inesperada(self):
        ruta = os.path.join(self.dir, ".entrada-2.json")
        for contenido in ([], "texto", {"sesion": 2}, {"sesion": 2, "filas": "x"}, {"sesion": 2, "filas": [1]}):
            with self.subTest(contenido=contenido):
                with open(ruta, "w", encoding="utf-8") as f:
                    json.dump(contenido, f)
                self.assertRechaza()

    def test_campos_invalidos(self):
        casos = {
            "cuenta corta": fila(cuenta="123"),
            "cuenta con letras": fila(cuenta="32006823a"),
            "cuenta numérica": fila(cuenta=320068234),
            "estado desconocido": fila(estado="justificado"),
            "nombre vacío": fila(nombre=""),
            "nombre no texto": fila(nombre=7),
            "hora no texto": fila(hora=5),
            "nombre enorme": fila(nombre="x" * 1000),
        }
        for descripcion, mala in casos.items():
            with self.subTest(descripcion):
                self.escribir_entrada(2, [fila(), mala])
                self.assertRechaza()
                self.assertFalse(os.path.exists(os.path.join(self.dir, "sesion-2.csv")))

    def test_falta_un_campo(self):
        incompleta = fila()
        del incompleta["estado"]
        self.escribir_entrada(2, [incompleta])
        self.assertRechaza()

    def test_demasiadas_filas(self):
        self.escribir_entrada(2, [fila() for _ in range(reporte.MAX_FILAS + 1)])
        self.assertRechaza()

    def test_el_mensaje_nunca_trae_datos_de_alumnos(self):
        self.escribir_entrada(2, [fila(nombre="Nombre Secreto", estado="inventado")])
        mensaje = self.assertRechaza()
        self.assertNotIn("Secreto", mensaje)
        self.assertNotIn("inventado", mensaje)
        self.assertNotIn("320068234", mensaje)


class Principal(PruebaConDirectorio):
    def correr(self, argv):
        salida, error = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(salida), contextlib.redirect_stderr(error):
            codigo = reporte.main(argv, self.dir)
        return codigo, salida.getvalue(), error.getvalue()

    def test_exito(self):
        self.escribir_entrada(12, [fila()])
        codigo, salida, error = self.correr(["12"])
        self.assertEqual((codigo, error), (0, ""))
        self.assertIn("sesion-12.csv", salida)
        self.assertNotIn("Ana", salida)

    def test_argumentos_incorrectos(self):
        for argv in ([], ["1", "2"], ["x"], ["12; rm -rf /"], ["0"]):
            with self.subTest(argv=argv):
                codigo, salida, error = self.correr(argv)
                self.assertEqual(codigo, 2)
                self.assertEqual(salida, "")
                self.assertNotEqual(error, "")

    def test_entrada_invalida_sale_con_1_sin_datos_en_stderr(self):
        self.escribir_entrada(12, [fila(nombre="Nombre Secreto", estado="inventado")])
        codigo, salida, error = self.correr(["12"])
        self.assertEqual(codigo, 1)
        self.assertEqual(salida, "")
        self.assertNotIn("Secreto", error)

    def test_un_error_inesperado_solo_nombra_la_clase(self):
        self.escribir_entrada(12, [fila(nombre="Nombre Secreto")])
        original = reporte.escribir_csv

        def falla(*args, **kwargs):
            raise RuntimeError("Nombre Secreto")

        reporte.escribir_csv = falla
        self.addCleanup(setattr, reporte, "escribir_csv", original)
        codigo, _, error = self.correr(["12"])
        self.assertEqual(codigo, 1)
        self.assertIn("RuntimeError", error)
        self.assertNotIn("Secreto", error)


if __name__ == "__main__":
    unittest.main()
