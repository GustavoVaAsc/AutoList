"""
Pruebas de vistas.py sin hardware: la geometria de los botones (que se dibujan
y se tocan con el mismo trazado), los textos de cada vista y el dibujo.

No se puede comprobar aqui como se ve en la pantalla: eso requiere mirarla.

Ejecutar desde orangepi/pantalla:  .venv/bin/python -m unittest -v
"""

import json
import unittest
from datetime import datetime

from PIL import ImageChops

import vistas
from estado import Estado, interpretar

AHORA = datetime(2026, 11, 4, 9, 5, 7)
ANCHO, ALTO = vistas.ANCHO, vistas.ALTO


def conectado(**kw):
    e = Estado(**kw)
    e.aplicar(("conexion", True), 0)
    e.aplicar(("sesion", {"activa": False}), 0)
    return e


def con_guia(**campos):
    e = conectado()
    d = {"cuenta": "999000001", "nombre": "Ana López", "paso": 3, "total": 5,
         "pose": "izquierda", "estado": "capturando", "motivo": ""}
    d.update(campos)
    e.aplicar(("enrolamiento", d), 0)
    return e


def ids(lista):
    return [b.id for b in lista]


class Botones(unittest.TestCase):
    def test_sesion_tiene_cinco_botones(self):
        self.assertEqual(sorted(ids(vistas.botones(conectado()))),
                         ["luz", "nav_lista", "nav_sesion", "puerta",
                          "sesion_toggle"])

    def test_lista_solo_pestanas(self):
        e = conectado()
        e.tocar("nav_lista", 0)
        self.assertEqual(sorted(ids(vistas.botones(e))),
                         ["nav_lista", "nav_sesion"])

    def test_guia_solo_con_salir(self):
        lista = vistas.botones(con_guia())
        self.assertEqual(ids(lista), ["guia_salir"])
        b = lista[0]
        self.assertGreaterEqual(b.alto, 38)
        self.assertLessEqual(b.x + b.ancho, ANCHO)
        self.assertLessEqual(b.y + b.alto, ALTO)

    def test_coinciden_con_los_activos_del_estado(self):
        for e in (conectado(), con_guia()):
            self.assertEqual(set(ids(vistas.botones(e))), e.botones_activos())

    def test_caben_en_la_pantalla_sin_solaparse(self):
        lista = vistas.botones(conectado())
        for b in lista:
            self.assertGreaterEqual(b.x, 0)
            self.assertGreaterEqual(b.y, 0)
            self.assertLessEqual(b.x + b.ancho, ANCHO)
            self.assertLessEqual(b.y + b.alto, ALTO)
        for i, a in enumerate(lista):
            for b in lista[i + 1:]:
                separados = (a.x + a.ancho <= b.x or b.x + b.ancho <= a.x
                             or a.y + a.alto <= b.y or b.y + b.alto <= a.y)
                self.assertTrue(separados, "%s se solapa con %s" % (a.id, b.id))

    def test_blancos_tactiles_de_al_menos_38_px(self):
        # unos 7 mm en una ILI9341 de 2.8"
        for b in vistas.botones(conectado()):
            self.assertGreaterEqual(b.alto, 38, b.id)
            self.assertGreaterEqual(b.ancho, 100, b.id)

    def test_boton_en(self):
        lista = vistas.botones(conectado())
        centro = {b.id: (b.x + b.ancho // 2, b.y + b.alto // 2) for b in lista}
        for id_, (x, y) in centro.items():
            self.assertEqual(vistas.boton_en(lista, x, y).id, id_)
        self.assertIsNone(vistas.boton_en(lista, 0, 0))
        self.assertIsNone(vistas.boton_en([], 100, 100))

    def test_boton_en_incluye_el_borde_izquierdo_y_excluye_el_derecho(self):
        b = vistas.botones(conectado())[0]
        self.assertEqual(vistas.boton_en([b], b.x, b.y).id, b.id)
        self.assertIsNone(vistas.boton_en([b], b.x + b.ancho, b.y))


class Etiquetas(unittest.TestCase):
    def test_sesion(self):
        e = conectado()
        self.assertEqual(vistas.etiqueta_sesion(e), "Iniciar sesión")
        e.tocar("sesion_toggle", 1)
        self.assertIn("Toca", vistas.etiqueta_sesion(e))
        e.armado = None
        e.aplicar(("sesion", {"activa": True}), 2)
        self.assertEqual(vistas.etiqueta_sesion(e), "Terminar sesión")

    def test_luz(self):
        e = conectado()
        self.assertEqual(vistas.etiqueta_luz(e), "Probar\nluz LED")
        e.tocar("luz", 1)
        self.assertEqual(vistas.etiqueta_luz(e), "Apagar\nluz")

    def test_estilos(self):
        e = conectado()
        estilos = {b.id: b.estilo for b in vistas.botones(e)}
        self.assertEqual(estilos["nav_sesion"], "activo")
        self.assertEqual(estilos["nav_lista"], "normal")
        self.assertEqual(estilos["puerta"], "normal")
        e.tocar("sesion_toggle", 1)
        e.tocar("luz", 1)
        estilos = {b.id: b.estilo for b in vistas.botones(e)}
        self.assertEqual(estilos["sesion_toggle"], "alerta")
        self.assertEqual(estilos["luz"], "alerta")

    def test_sin_conexion_los_de_envio_se_ven_deshabilitados(self):
        estilos = {b.id: b.estilo for b in vistas.botones(Estado())}
        for id_ in ("sesion_toggle", "puerta", "luz"):
            self.assertEqual(estilos[id_], "deshabilitado")
        self.assertEqual(estilos["nav_lista"], "normal")

    def test_la_sesion_desconocida_deshabilita_solo_su_boton(self):
        e = Estado()
        e.aplicar(("conexion", True), 0)
        estilos = {b.id: b.estilo for b in vistas.botones(e)}
        self.assertEqual(estilos["sesion_toggle"], "deshabilitado")
        self.assertEqual(estilos["puerta"], "normal")

    def test_tras_confirmar_el_boton_de_sesion_se_ve_deshabilitado(self):
        e = conectado()
        e.tocar("sesion_toggle", 1)
        e.tocar("sesion_toggle", 2)
        estilos = {b.id: b.estilo for b in vistas.botones(e)}
        self.assertEqual(estilos["sesion_toggle"], "deshabilitado")


class TextosSesion(unittest.TestCase):
    def textos(self, e):
        return [t for t, _ in vistas.lineas_sesion(e)]

    def test_sin_datos_de_la_sesion(self):
        e = Estado()
        e.aplicar(("conexion", True), 0)
        t = self.textos(e)
        self.assertIn("Sesión: sin datos", t)
        self.assertIn("Presentes: sin datos", t)
        self.assertIn("Último: sin datos", t)

    def test_sin_sesion_activa_muestra_lo_que_iniciaria(self):
        e = conectado(materia="FSE", grupo="1", tol_min=10)
        e.aplicar(("sesion", {"activa": False, "id": 12}), 0)
        t = self.textos(e)
        self.assertIn("Sin sesión activa", t)
        self.assertTrue(any("FSE" in x and "grupo 1" in x and "10 min" in x
                            for x in t), t)

    def test_sesion_activa(self):
        e = conectado()
        e.aplicar(("sesion", {"activa": True, "id": 12, "materia": "Redes",
                              "grupo": "2", "tol_min": 15,
                              "inicio": "2026-11-04T09:00:00-06:00"}), 0)
        t = self.textos(e)
        self.assertIn("Sesión activa", t)
        self.assertTrue(any("Redes" in x and "grupo 2" in x for x in t), t)
        self.assertTrue(any("15 min" in x for x in t), t)

    def test_hueco_conectado_muestra_los_datos(self):
        e = conectado()
        e.asistencia = {"presentes": 12, "retardos": 2, "faltantes": 9}
        e.ultimo_registro = {"nombre": "Ana López", "estado": "presente"}
        t = self.textos(e)
        self.assertIn("Presentes: 12", t)
        self.assertTrue(any("Ana López" in x and "presente" in x for x in t), t)

    def test_siempre_el_mismo_numero_de_lineas(self):
        e = conectado()
        n = len(vistas.lineas_sesion(e))
        e.aplicar(("sesion", {"activa": True, "materia": "FSE"}), 0)
        self.assertEqual(len(vistas.lineas_sesion(e)), n)
        e.aplicar(("sesion", {"activa": False}), 0)
        self.assertEqual(len(vistas.lineas_sesion(e)), n)

    def test_hora_local(self):
        self.assertRegex(vistas.hora_local("2026-11-04T09:00:00-06:00"),
                         r"^\d\d:\d\d$")
        self.assertRegex(vistas.hora_local("2026-11-04T15:00:00Z"), r"^\d\d:\d\d$")
        self.assertIsNone(vistas.hora_local("ayer"))
        self.assertIsNone(vistas.hora_local(None))

    def test_hora_local_con_fechas_que_desbordan(self):
        # astimezone() lanza OverflowError en los extremos del calendario
        for iso in ("0001-01-01T00:00:00+14:00", "9999-12-31T23:59:59-14:00"):
            with self.subTest(iso=iso):
                self.assertIsNone(vistas.hora_local(iso))

    def test_texto_de_conexion(self):
        e = Estado()
        self.assertEqual(vistas.texto_conexion(e), ("Sin conexión", "error"))
        e.aplicar(("credenciales", True), 0)
        self.assertEqual(vistas.texto_conexion(e), ("Credenciales rechazadas", "error"))
        e.aplicar(("conexion", True), 1)
        self.assertEqual(vistas.texto_conexion(e), ("En línea", "ok"))

    def test_actuadores(self):
        e = conectado()
        self.assertEqual(vistas.resumen_actuadores(e),
                         [("Puerta", "–"), ("LED", "–"), ("Luz", "–")])
        e.aplicar(("actuadores", {"servo": "cerrado", "led": "azul", "luz": 1}), 0)
        self.assertEqual(vistas.resumen_actuadores(e),
                         [("Puerta", "cerrado"), ("LED", "azul"),
                          ("Luz", "encendida")])
        e.aplicar(("actuadores", {"luz": 0}), 0)
        self.assertEqual(vistas.resumen_actuadores(e)[2], ("Luz", "apagada"))


class TextosGuia(unittest.TestCase):
    def test_progreso(self):
        g = vistas.texto_guia(con_guia(paso=3, pose="izquierda"))
        self.assertEqual(g["nombre"], "Ana López")
        self.assertEqual(g["contador"], "3/5")
        self.assertIn("izquierda", g["instruccion"])
        self.assertEqual(g["mensaje"], "No te muevas")

    def test_cada_pose_tiene_instruccion(self):
        for pose in ("frente", "izquierda", "derecha", "arriba", "frente_2"):
            with self.subTest(pose=pose):
                self.assertTrue(vistas.texto_guia(con_guia(pose=pose))["instruccion"])

    def test_pose_desconocida_no_rompe(self):
        self.assertEqual(vistas.texto_guia(con_guia(pose="boca_abajo"))["instruccion"], "")

    def test_rechazada_dice_por_que(self):
        g = vistas.texto_guia(con_guia(estado="rechazada", motivo="borrosa"))
        self.assertIn("borrosa", g["mensaje"])
        g = vistas.texto_guia(con_guia(estado="rechazada", motivo="nuevo_motivo"))
        self.assertTrue(g["mensaje"])

    def test_desenlaces(self):
        self.assertIn("completa", vistas.texto_guia(
            con_guia(paso=5, estado="completado"))["mensaje"])
        self.assertIn("cancelado", vistas.texto_guia(
            con_guia(estado="cancelado"))["mensaje"])
        g = vistas.texto_guia(con_guia(estado="error", motivo="inconsistente"))
        self.assertIn("No se pudo", g["mensaje"])
        self.assertEqual(g["instruccion"], "")      # ya no hay pose que adoptar

    def test_sin_nombre(self):
        self.assertEqual(vistas.texto_guia(con_guia(nombre=""))["nombre"],
                         "Registro de rostro")


class Texto(unittest.TestCase):
    def setUp(self):
        self.f = vistas.cargar_fuentes()["texto"]

    def test_ajustar_deja_lo_que_cabe(self):
        self.assertEqual(vistas.ajustar_texto("Hola", self.f, 200), "Hola")

    def test_ajustar_recorta_con_puntos_suspensivos(self):
        r = vistas.ajustar_texto("Ana María de los Ángeles Fernández", self.f, 80)
        self.assertTrue(r.endswith("…"))
        self.assertLessEqual(self.f.getlength(r), 80)

    def test_envolver_en_dos_lineas(self):
        lineas = vistas.envolver("Ana María de los Ángeles Fernández", self.f, 110, 2)
        self.assertEqual(len(lineas), 2)
        for linea in lineas:
            self.assertLessEqual(self.f.getlength(linea), 110)

    def test_envolver_recorta_la_ultima_linea(self):
        lineas = vistas.envolver("palabra " * 30, self.f, 110, 2)
        self.assertEqual(len(lineas), 2)
        self.assertTrue(lineas[-1].endswith("…"))

    def test_envolver_una_palabra_mas_larga_que_la_linea(self):
        lineas = vistas.envolver("A" * 60, self.f, 110, 2)
        for linea in lineas:
            self.assertLessEqual(self.f.getlength(linea), 110)

    def test_ajustar_no_mide_cadenas_largas_ni_muchas_veces(self):
        medidas = []

        class Contada:
            def getlength(self, t):
                medidas.append(len(t))
                return 7.0 * len(t)

        r = vistas.ajustar_texto("W" * 100000, Contada(), 100)
        self.assertLessEqual(7.0 * len(r), 100)
        self.assertLessEqual(max(medidas), vistas.MAX_CARACTERES + 1)
        self.assertLess(len(medidas), 40)

    def test_envolver_texto_enorme(self):
        medidas = []

        class Contada:
            def getlength(self, t):
                medidas.append(len(t))
                return 7.0 * len(t)

        lineas = vistas.envolver("palabra " * 50000, Contada(), 100, 2)
        self.assertEqual(len(lineas), 2)
        self.assertLess(len(medidas), 400)

    def test_envolver_vacio(self):
        self.assertEqual(vistas.envolver("", self.f, 110, 2), [])


class TextosHostiles(unittest.TestCase):
    """Lo que llega por MQTT pasa por interpretar y Estado antes de dibujarse."""

    def guia(self, nombre):
        e = conectado()
        ev = interpretar("aula/vision/enrolamiento", json.dumps({
            "paso": 1, "total": 5, "estado": "capturando", "pose": "frente",
            "cuenta": "999000001", "nombre": nombre}).encode(), False)
        e.aplicar(ev, 0)
        return e

    def test_saltos_de_linea_y_controles_no_llegan_al_texto(self):
        for malo in ("\n", "\u202e", "\u2028", "\u2029"):
            with self.subTest(malo=repr(malo)):
                e = self.guia("Ana%sLopez" % malo)
                self.assertEqual(vistas.texto_guia(e)["nombre"], "Ana Lopez")
                vistas.dibujar(e, AHORA, vistas.cargar_fuentes())

    def test_pila_de_marcas_combinantes(self):
        e = self.guia("a" + "\u0301" * 50)
        self.assertEqual(vistas.texto_guia(e)["nombre"], "a" + "\u0301" * 2)
        vistas.dibujar(e, AHORA, vistas.cargar_fuentes())

    def test_el_dibujo_con_marcas_no_se_sale_de_su_renglon(self):
        fuentes = vistas.cargar_fuentes()
        limpio = vistas.dibujar(self.guia("aaaa"), AHORA, fuentes)
        pila = vistas.dibujar(self.guia("a" + "\u0301" * 50), AHORA, fuentes)
        # solo cambian los pixeles del bloque del nombre (a lo mas ~ 50 px de alto)
        diferencia = ImageChops.difference(limpio, pila).getbbox()
        self.assertIsNotNone(diferencia)
        self.assertLess(diferencia[1], 60)      # no toca nada por encima del nombre


class Dibujo(unittest.TestCase):
    def setUp(self):
        self.fuentes = vistas.cargar_fuentes()

    def cuadro(self, e, ahora=AHORA):
        return vistas.dibujar(e, ahora, self.fuentes)

    def test_las_tres_vistas_son_rgb_de_240x320(self):
        lista = conectado()
        lista.tocar("nav_lista", 0)
        for e in (conectado(), lista, con_guia(), Estado()):
            img = self.cuadro(e)
            self.assertEqual((img.mode, img.size), ("RGB", (240, 320)))

    def test_cada_vista_se_dibuja_distinta(self):
        lista = conectado()
        lista.tocar("nav_lista", 0)
        cuadros = {self.cuadro(e).tobytes()
                   for e in (conectado(), lista, con_guia())}
        self.assertEqual(len(cuadros), 3)

    def test_la_hora_cambia_el_cuadro(self):
        e = conectado()
        self.assertNotEqual(self.cuadro(e).tobytes(),
                            self.cuadro(e, AHORA.replace(second=8)).tobytes())

    def test_misma_entrada_mismo_cuadro(self):
        e = conectado()
        self.assertEqual(self.cuadro(e).tobytes(), self.cuadro(e).tobytes())

    def test_sin_conexion_se_ve_distinto(self):
        self.assertNotEqual(self.cuadro(Estado()).tobytes(),
                            self.cuadro(conectado()).tobytes())

    def test_datos_en_el_hueco_cambian_la_lista(self):
        vacia = conectado()
        vacia.tocar("nav_lista", 0)
        llena = conectado()
        llena.tocar("nav_lista", 0)
        llena.asistencia = {"presentes": 12, "retardos": 2, "faltantes": 9}
        self.assertNotEqual(self.cuadro(vacia).tobytes(),
                            self.cuadro(llena).tobytes())

    def test_el_mensaje_amarillo_deja_aire_con_el_boton(self):
        # El mensaje de rechazo caia pegado al boton "Salir de la guia".
        e = con_guia(estado="rechazada", motivo="borrosa")
        img = self.cuadro(e)
        pixeles = img.load()
        alto_boton = vistas.GEOMETRIA["guia_salir"][1]
        filas = [y for y in range(alto_boton)
                 if any(pixeles[x, y] == vistas.ALERTA for x in range(ANCHO))]
        self.assertTrue(filas)            # el mensaje se ve
        self.assertLessEqual(max(filas), alto_boton - 9)

    def test_el_aviso_de_rechazo_se_dibujo_entero(self):
        # El aviso es de una linea: si no cabe, sale recortado con "...".
        e = Estado()
        e.aplicar(("enrolamiento", {"cuenta": "", "nombre": "", "paso": 0,
                                    "total": 5, "pose": "", "estado": "error",
                                    "motivo": "ocupado"}), 0)
        self.assertEqual(e.aviso[0], "Registro rechazado")
        self.assertEqual(vistas.ajustar_texto(e.aviso[0],
                                              self.fuentes["negrita"],
                                              vistas.INTERIOR), e.aviso[0])

    def test_textos_largos_o_raros_no_rompen_el_dibujo(self):
        e = conectado()
        e.aplicar(("sesion", {"activa": True, "materia": "M" * 80, "grupo": "G" * 80,
                              "tol_min": 10, "inicio": "no es una fecha"}), 0)
        e.aplicar(("actuadores", {"servo": "s" * 80, "led": "l" * 80}), 0)
        self.cuadro(e)
        self.cuadro(con_guia(nombre="Nombre " * 40, motivo="x" * 80,
                             estado="rechazada"))

    def test_sobre_el_fondo_hay_texto_con_antialias(self):
        colores = {c for _, c in self.cuadro(conectado()).getcolors(240 * 320)}
        self.assertIn(vistas.FONDO, colores)
        self.assertGreater(len(colores), 8)

    def test_el_cuadro_de_error_es_fijo(self):
        img = vistas.dibujar_error(self.fuentes)
        self.assertEqual((img.mode, img.size), ("RGB", (240, 320)))
        self.assertEqual(img.tobytes(), vistas.dibujar_error(self.fuentes).tobytes())

    def test_credenciales_rechazadas_se_ve_distinto_de_sin_conexion(self):
        e = Estado()
        base = self.cuadro(e).tobytes()
        e.aplicar(("credenciales", True), 0)
        self.assertNotEqual(self.cuadro(e).tobytes(), base)

    def test_con_credenciales_rechazadas_la_fecha_no_se_encima_al_texto(self):
        e = Estado()
        e.aplicar(("credenciales", True), 0)
        zona = self.cuadro(e).crop((190, 34, 240, 48))
        self.assertEqual(zona.getcolors(), [(50 * 14, vistas.FONDO)])

    def test_aviso_y_confirmacion_se_dibujan(self):
        e = conectado()
        base = self.cuadro(e).tobytes()
        e.tocar("puerta", 1)
        self.assertNotEqual(self.cuadro(e).tobytes(), base)


if __name__ == "__main__":
    unittest.main()
