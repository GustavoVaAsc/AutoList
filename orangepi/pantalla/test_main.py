"""
Pruebas de main.py sin hardware ni red (unittest de la biblioteca estandar).

Ejecutar desde orangepi/pantalla:  .venv/bin/python -m unittest -v
Usan una pantalla, un tactil y un cliente MQTT falsos: no abren /dev/spidev*,
ni /dev/gpiochip*, ni el puerto 1883.
"""

import io
import os
import queue
import tempfile
import threading
import unittest
from contextlib import redirect_stderr
from datetime import datetime
from unittest import mock

import main
import tactil as tactil_mod
import vistas
from estado import Estado

AHORA = datetime(2026, 11, 4, 9, 5, 7)
CLAVE = "clave-ficticia-de-prueba-123"
ENTORNO = {"MQTT_PANTALLA_PASSWORD": CLAVE}
# Calibracion de prueba: el crudo es 1000 + x y 2000 + y (un pixel por unidad)
CAL = tactil_mod.Calibracion(1000, 1240, 2000, 2320, False, False, False)


class PantallaFalsa:
    """Registra lo que main le manda, en lugar de hablar con el SPI."""

    def __init__(self, ancho=240, alto=320, parar=None, cuadros_max=None,
                 bitacora=None):
        self.width, self.height = ancho, alto
        self.cuadros = []
        self.llenados = []
        self.cerrada = 0
        self._parar, self._max = parar, cuadros_max
        self._bit = bitacora if bitacora is not None else []

    def image(self, img):
        self.cuadros.append(img)
        if self._parar and len(self.cuadros) >= self._max:
            self._parar.set()       # como si llegara un SIGTERM

    def fill(self, color=0):
        self.llenados.append(color)
        self._bit.append("pantalla.fill")

    def cerrar(self):
        self.cerrada += 1
        self._bit.append("pantalla.cerrar")


class TactilFalso:
    def __init__(self, bitacora=None):
        self.presionado = False
        self.crudo = (0, 0)
        self.cerrado = 0
        self._bit = bitacora if bitacora is not None else []

    def is_pressed(self):
        return self.presionado

    def get_coordinates(self):
        return self.crudo

    def cerrar(self):
        self.cerrado += 1
        self._bit.append("tactil.cerrar")


class ClienteFalso:
    """Hace de ClienteMQTT: guarda lo publicado y deja inyectar eventos."""

    def __init__(self, host=None, puerto=None, clave=None, eventos=None,
                 bitacora=None):
        self.args = (host, puerto, clave)
        self.eventos = eventos if eventos is not None else queue.SimpleQueue()
        self.publicado = []
        self.iniciado = self.detenido = 0
        self.acepta = True
        self.confirma = True
        self._bit = bitacora if bitacora is not None else []

    def iniciar(self):
        self.iniciado += 1

    def publicar(self, topico, datos):
        self.publicado.append((topico, datos))
        self._bit.append("cliente.publicar")
        return self.acepta

    def detener(self, espera=1.0):
        self.detenido += 1
        self._bit.append("cliente.detener")
        return self.confirma


class Reloj:
    def __init__(self):
        self.t = 100.0

    def __call__(self):
        return self.t


def crear_app(tactil=None, conectado=True, **kw):
    reloj = Reloj()
    ahora = {"dt": AHORA}
    estado = Estado()
    if conectado:
        estado.aplicar(("conexion", True), 0)
        estado.aplicar(("sesion", {"activa": False}), 0)
    cliente = ClienteFalso()
    pantalla = PantallaFalsa()
    app = main.Aplicacion(pantalla, vistas.cargar_fuentes(), estado, cliente,
                          cliente.eventos, tactil=tactil, calibracion=CAL,
                          reloj=reloj, ahora=lambda: ahora["dt"], **kw)
    app.fake = mock.Mock(reloj=reloj, ahora=ahora, cliente=cliente,
                         pantalla=pantalla)
    return app


class LeerConfig(unittest.TestCase):
    def test_valores_por_defecto(self):
        cfg = main.leer_config({})
        self.assertEqual(cfg["SPI_BUS"], 3)
        self.assertEqual(cfg["SPI_DISPOSITIVO"], 0)
        self.assertEqual((cfg["DC"], cfg["RST"], cfg["CS"]), (139, 33, None))
        self.assertEqual(cfg["GPIOCHIP"], "/dev/gpiochip0")
        self.assertEqual(cfg["BAUDRATE"], 40000000)

    def test_valores_por_defecto_de_mqtt_sesion_y_tactil(self):
        cfg = main.leer_config({})
        self.assertEqual((cfg["MQTT_HOST"], cfg["MQTT_PUERTO"]), ("127.0.0.1", 1883))
        self.assertEqual((cfg["SESION_MATERIA"], cfg["SESION_GRUPO"],
                          cfg["SESION_TOL_MIN"]), ("FSE", "1", 10))
        self.assertEqual(cfg["LUZ_PRUEBA_S"], 3)
        self.assertEqual((cfg["GUIA_VENCE_S"], cfg["GUIA_MAX_S"]), (330, 900))
        self.assertEqual((cfg["TACTIL"], cfg["TACTIL_BUS"],
                          cfg["TACTIL_DISPOSITIVO"], cfg["TACTIL_IRQ"]),
                         (1, 3, 1, 32))

    def test_variables_de_entorno(self):
        cfg = main.leer_config({"PANTALLA_DC": "261", "PANTALLA_CS": "229",
                                "PANTALLA_GPIOCHIP": "/dev/gpiochip1",
                                "PANTALLA_MQTT_HOST": "broker.local",
                                "PANTALLA_TACTIL_X_MIN": "-5"})
        self.assertEqual(cfg["DC"], 261)
        self.assertEqual(cfg["CS"], 229)
        self.assertEqual(cfg["GPIOCHIP"], "/dev/gpiochip1")
        self.assertEqual(cfg["MQTT_HOST"], "broker.local")
        self.assertEqual(cfg["TACTIL_X_MIN"], -5)

    def test_la_clave_no_es_una_variable_de_la_tabla(self):
        # no hay PANTALLA_<algo> para la clave: se lee aparte y no se guarda en cfg
        self.assertFalse([k for k in main.CONFIG if "CLAVE" in k or "PASSWORD" in k])

    def test_cs_vacio_es_kernel(self):
        self.assertIsNone(main.leer_config({"PANTALLA_CS": ""})["CS"])

    def test_calibracion_de_config(self):
        cal = tactil_mod.calibracion_de_config(main.leer_config(
            {"PANTALLA_TACTIL_INVERTIR_Y": "1", "PANTALLA_TACTIL_X_MAX": "3800"}))
        self.assertEqual((cal.x_max, cal.invertir_y, cal.cambiar_ejes),
                         (3800, True, False))

    def test_entero_invalido(self):
        with self.assertRaisesRegex(ValueError, "PANTALLA_RST"):
            main.leer_config({"PANTALLA_RST": "abc"})

    def test_valores_fuera_de_rango(self):
        for var, valor in (("MQTT_PUERTO", "0"), ("MQTT_PUERTO", "70000"),
                           ("SESION_TOL_MIN", "0"), ("LUZ_PRUEBA_S", "0"),
                           ("TACTIL", "2"), ("TACTIL_INVERTIR_X", "-1"),
                           ("TACTIL_X_MIN", "4000"),       # mayor que X_MAX
                           ("TACTIL_Y_MAX", "100"),
                           ("GUIA_VENCE_S", "1"), ("GUIA_MAX_S", "10"),
                           ("GUIA_MAX_S", "9000")):
            with self.subTest(var=var, valor=valor):
                with self.assertRaisesRegex(ValueError, "PANTALLA_"):
                    main.leer_config({"PANTALLA_" + var: valor})

    def test_el_techo_de_la_guia_no_puede_ser_menor_que_el_vencimiento(self):
        with self.assertRaisesRegex(ValueError, "PANTALLA_GUIA_MAX_S"):
            main.leer_config({"PANTALLA_GUIA_VENCE_S": "600",
                              "PANTALLA_GUIA_MAX_S": "500"})

    def test_texto_vacio_no_vale(self):
        for var in ("MQTT_HOST", "SESION_MATERIA", "SESION_GRUPO", "GPIOCHIP"):
            with self.subTest(var=var):
                with self.assertRaisesRegex(ValueError, "PANTALLA_" + var):
                    main.leer_config({"PANTALLA_" + var: "  "})

    def test_rotacion_horizontal_no_esta_soportada(self):
        # las vistas estan hechas para 240x320
        self.assertEqual(main.leer_config({"PANTALLA_ROTACION": "2"})["ROTACION"], 2)
        for valor in ("1", "3", "4", "-2"):
            with self.subTest(valor=valor):
                with self.assertRaisesRegex(ValueError, "PANTALLA_ROTACION"):
                    main.leer_config({"PANTALLA_ROTACION": valor})


class LeerClave(unittest.TestCase):
    def credencial(self, contenido, nombre="mqtt_pantalla"):
        d = tempfile.TemporaryDirectory()
        self.addCleanup(d.cleanup)
        if contenido is not None:
            with open(os.path.join(d.name, nombre), "wb") as f:
                f.write(contenido)
        return d.name

    def test_la_lee_del_entorno(self):
        self.assertEqual(main.leer_clave({"MQTT_PANTALLA_PASSWORD": "abc"}), "abc")

    def test_falta_o_vacia(self):
        for entorno in ({}, {"MQTT_PANTALLA_PASSWORD": ""}):
            with self.subTest(entorno=entorno):
                with self.assertRaisesRegex(ValueError, "MQTT_PANTALLA_PASSWORD"):
                    main.leer_clave(entorno)

    def test_la_credencial_de_systemd_manda_sobre_el_entorno(self):
        d = self.credencial(b"de-la-credencial")
        self.assertEqual(main.leer_clave({
            "CREDENTIALS_DIRECTORY": d, "MQTT_PANTALLA_PASSWORD": "del-entorno"}),
            "de-la-credencial")

    def test_quita_el_salto_de_linea_final_del_editor(self):
        d = self.credencial(b"con-salto\n")
        self.assertEqual(main.leer_clave({"CREDENTIALS_DIRECTORY": d}), "con-salto")
        d = self.credencial(b"con-salto\r\n")
        self.assertEqual(main.leer_clave({"CREDENTIALS_DIRECTORY": d}), "con-salto")

    def test_sin_el_archivo_de_la_credencial_usa_el_entorno(self):
        d = self.credencial(None)
        self.assertEqual(main.leer_clave({
            "CREDENTIALS_DIRECTORY": d, "MQTT_PANTALLA_PASSWORD": "reserva"}),
            "reserva")

    def test_una_credencial_vacia_es_un_error_aunque_haya_entorno(self):
        d = self.credencial(b"\n")
        with self.assertRaisesRegex(ValueError, "mqtt_pantalla"):
            main.leer_clave({"CREDENTIALS_DIRECTORY": d,
                             "MQTT_PANTALLA_PASSWORD": "reserva"})

    def test_el_placeholder_se_rechaza(self):
        with self.assertRaisesRegex(ValueError, "cambiar_esta_clave"):
            main.leer_clave({"MQTT_PANTALLA_PASSWORD": "cambiar_esta_clave"})
        d = self.credencial(b"cambiar_esta_clave\n")
        with self.assertRaisesRegex(ValueError, "cambiar_esta_clave"):
            main.leer_clave({"CREDENTIALS_DIRECTORY": d})

    def test_credencial_ilegible_no_filtra_su_contenido(self):
        d = self.credencial(b"\xff\xfesecreto")
        with self.assertRaises(ValueError) as c:
            main.leer_clave({"CREDENTIALS_DIRECTORY": d})
        self.assertNotIn("secreto", str(c.exception))
        self.assertNotIn("xff", str(c.exception))

    def test_el_mensaje_del_placeholder_lo_dice(self):
        with self.assertRaises(ValueError) as c:
            main.leer_clave({"MQTT_PANTALLA_PASSWORD": "cambiar_esta_clave"})
        self.assertIn("placeholder", str(c.exception))


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

    def test_ejecutar_da_pasos_hasta_parar(self):
        parar = threading.Event()
        app = crear_app()
        pasos = []
        app.paso = lambda: (pasos.append(1), parar.set() if len(pasos) == 3 else None)
        main.ejecutar(app, parar)
        self.assertEqual(len(pasos), 3)

    def test_ejecutar_no_da_pasos_si_ya_hay_que_parar(self):
        parar = threading.Event()
        parar.set()
        app = crear_app()
        app.paso = mock.Mock()
        main.ejecutar(app, parar)
        app.paso.assert_not_called()

    def test_ejecutar_termina_con_el_reinicio(self):
        parar = threading.Event()
        app = crear_app()
        app.estado.reiniciar = True
        app.paso = mock.Mock()
        main.ejecutar(app, parar)
        app.paso.assert_not_called()


class Paso(unittest.TestCase):
    def test_dibuja_la_primera_vez_y_no_repite_en_el_mismo_segundo(self):
        app = crear_app()
        app.paso()
        self.assertEqual(len(app.fake.pantalla.cuadros), 1)
        app.paso()
        self.assertEqual(len(app.fake.pantalla.cuadros), 1)

    def test_redibuja_al_cambiar_el_segundo(self):
        app = crear_app()
        app.paso()
        app.fake.ahora["dt"] = AHORA.replace(second=8)
        app.paso()
        self.assertEqual(len(app.fake.pantalla.cuadros), 2)

    def test_un_evento_de_red_redibuja_y_cambia_el_estado(self):
        app = crear_app(conectado=False)
        app.paso()
        app.fake.cliente.eventos.put(("conexion", True))
        app.paso()
        self.assertTrue(app.estado.conectado)
        self.assertEqual(len(app.fake.pantalla.cuadros), 2)

    def test_el_registro_activa_la_guia(self):
        app = crear_app()
        app.fake.cliente.eventos.put(("enrolamiento", {
            "cuenta": "1", "nombre": "Ana", "paso": 1, "total": 5,
            "pose": "frente", "estado": "esperando_pose", "motivo": ""}))
        app.paso()
        self.assertEqual(app.estado.vista_visible, "guia")

    def test_cada_pasada_atiende_un_numero_acotado_de_eventos(self):
        app = crear_app()
        for _ in range(500):
            app.fake.cliente.eventos.put(("conexion", True))
        app.paso()
        self.assertFalse(app.fake.cliente.eventos.empty())

    def test_vencimientos_publican(self):
        app = crear_app()
        app.estado.tocar("luz", app.fake.reloj.t)
        app.fake.cliente.publicado.clear()
        app.fake.reloj.t += 3
        app.paso()
        self.assertEqual(app.fake.cliente.publicado,
                         [("aula/actuador/cmd", {"luz": 0})])

    def test_si_falla_apagar_la_luz_se_reintenta(self):
        app = crear_app()
        app.estado.tocar("luz", app.fake.reloj.t)
        app.fake.reloj.t += 3
        app.fake.cliente.acepta = False
        app.paso()
        self.assertIsNotNone(app.estado.luz_hasta)
        app.fake.cliente.acepta = True
        app.fake.reloj.t += 1
        app.fake.cliente.publicado.clear()
        app.paso()
        self.assertEqual(app.fake.cliente.publicado,
                         [("aula/actuador/cmd", {"luz": 0})])
        self.assertIsNone(app.estado.luz_hasta)

    def test_si_dibujar_falla_se_muestra_el_cuadro_de_error_y_no_se_cae(self):
        app = crear_app()
        app.estado.sesion = {"activa": True, "tol_min": "no es un numero"}
        err = io.StringIO()
        with redirect_stderr(err):
            app.paso()
        self.assertEqual(len(app.fake.pantalla.cuadros), 1)
        esperado = vistas.dibujar_error(app.fuentes)
        self.assertEqual(app.fake.pantalla.cuadros[0].tobytes(), esperado.tobytes())
        self.assertIn("TypeError", err.getvalue())

    def test_el_error_de_dibujo_se_registra_una_vez_por_cambio(self):
        app = crear_app()
        app.estado.sesion = {"activa": True, "tol_min": "x"}
        err = io.StringIO()
        with redirect_stderr(err):
            for s in range(10, 14):             # cuatro segundos seguidos
                app.fake.ahora["dt"] = AHORA.replace(second=s)
                app.paso()
        self.assertEqual(len(err.getvalue().splitlines()), 1)
        self.assertEqual(len(app.fake.pantalla.cuadros), 4)

    def test_no_registra_el_contenido_ni_el_mensaje_de_la_excepcion(self):
        app = crear_app()
        app.estado.sesion = {"activa": True, "tol_min": "SECRETO-9"}
        err = io.StringIO()
        with redirect_stderr(err):
            app.paso()
        self.assertNotIn("SECRETO", err.getvalue())

    def test_con_el_cuadro_de_error_los_toques_se_ignoran(self):
        # los botones no se ven: un toque no debe abrir la puerta "a ciegas"
        tactil = TactilFalso()
        app = crear_app(tactil=tactil)
        b = next(b for b in vistas.botones(app.estado) if b.id == "puerta")
        x, y = b.x + b.ancho // 2, b.y + b.alto // 2
        app.estado.sesion = {"activa": True, "tol_min": "x"}
        with redirect_stderr(io.StringIO()):
            app.paso()                              # ya se dibuja el cuadro de error
            tactil.crudo = (1000 + x, 2000 + y)
            tactil.presionado = True
            for _ in range(5):
                app.fake.reloj.t += 0.02
                app.paso()
            tactil.presionado = False
            for _ in range(6):
                app.fake.reloj.t += 0.02
                app.paso()
        self.assertEqual(app.fake.cliente.publicado, [])

    def test_cuando_vuelve_a_dibujar_bien_se_olvida_el_error(self):
        app = crear_app()
        err = io.StringIO()
        with redirect_stderr(err):
            app.estado.sesion = {"activa": True, "tol_min": "x"}
            app.paso()
            app.estado.aplicar(("sesion", {"activa": False}), 0)
            app.paso()
            app.estado.sesion = {"activa": True, "tol_min": "x"}
            app.estado.sucio = True
            app.paso()
        self.assertEqual(len(err.getvalue().splitlines()), 2)

    def test_si_falla_un_envio_avisa(self):
        app = crear_app()
        app.fake.cliente.acepta = False
        app._publicar(app.estado.tocar("puerta", app.fake.reloj.t))
        self.assertIn("enviar", app.estado.aviso[0])


class Toques(unittest.TestCase):
    def tocar_en(self, app, tactil, x, y):
        """Pone el dedo en el pixel (x, y) unas lecturas y lo suelta."""
        t = app.fake.reloj
        tactil.crudo = (1000 + x, 2000 + y)
        tactil.presionado = True
        for _ in range(5):
            app.paso()
            t.t += 0.02
        tactil.presionado = False
        for _ in range(6):
            app.paso()
            t.t += 0.02

    def centro(self, app, id_):
        b = next(b for b in vistas.botones(app.estado) if b.id == id_)
        return b.x + b.ancho // 2, b.y + b.alto // 2

    def test_abrir_puerta(self):
        tactil = TactilFalso()
        app = crear_app(tactil=tactil)
        self.tocar_en(app, tactil, *self.centro(app, "puerta"))
        self.assertEqual(app.fake.cliente.publicado,
                         [("aula/actuador/cmd", {"servo": "abrir"})])

    def test_probar_luz_y_se_apaga_sola(self):
        tactil = TactilFalso()
        app = crear_app(tactil=tactil)
        self.tocar_en(app, tactil, *self.centro(app, "luz"))
        self.assertEqual(app.fake.cliente.publicado,
                         [("aula/actuador/cmd", {"luz": 1})])
        app.fake.reloj.t += 3
        app.paso()
        self.assertEqual(app.fake.cliente.publicado[-1],
                         ("aula/actuador/cmd", {"luz": 0}))

    def test_iniciar_sesion_con_dos_toques(self):
        tactil = TactilFalso()
        app = crear_app(tactil=tactil)
        for _ in range(2):
            self.tocar_en(app, tactil, *self.centro(app, "sesion_toggle"))
        self.assertEqual(app.fake.cliente.publicado,
                         [("aula/sesion/cmd", {"accion": "iniciar",
                                               "materia": "FSE", "grupo": "1",
                                               "tol_min": 10})])

    def test_pestana_lista_cambia_de_vista(self):
        tactil = TactilFalso()
        app = crear_app(tactil=tactil)
        self.tocar_en(app, tactil, *self.centro(app, "nav_lista"))
        self.assertEqual(app.estado.vista_visible, "lista")
        self.assertEqual(app.fake.cliente.publicado, [])

    def test_un_toque_en_el_vacio_no_hace_nada(self):
        tactil = TactilFalso()
        app = crear_app(tactil=tactil)
        self.tocar_en(app, tactil, 2, 100)
        self.assertEqual(app.fake.cliente.publicado, [])
        self.assertEqual(app.estado.vista_visible, "sesion")

    def test_sin_tactil_no_se_lee(self):
        app = crear_app(tactil=None)
        app.paso()     # no falla

    def test_mantener_el_dedo_es_un_solo_toque(self):
        tactil = TactilFalso()
        app = crear_app(tactil=tactil)
        x, y = self.centro(app, "puerta")
        tactil.crudo, tactil.presionado = (1000 + x, 2000 + y), True
        for _ in range(100):                # 2 s con el dedo puesto
            app.paso()
            app.fake.reloj.t += 0.02
        self.assertEqual(app.fake.cliente.publicado, [])    # aun no suelta


class Cierre(unittest.TestCase):
    def test_orden_de_cierre(self):
        bit = []
        app = crear_app(tactil=TactilFalso(bitacora=bit))
        app.pantalla = PantallaFalsa(bitacora=bit)
        app.cliente = ClienteFalso(bitacora=bit)
        app.estado.tocar("luz", app.fake.reloj.t)
        app.cerrar()
        self.assertEqual(bit, ["cliente.publicar", "cliente.detener",
                               "tactil.cerrar", "pantalla.fill",
                               "pantalla.cerrar"])
        self.assertEqual(app.cliente.publicado,
                         [("aula/actuador/cmd", {"luz": 0})])

    def apagar_con_luz(self, acepta=True, confirma=True):
        app = crear_app(tactil=TactilFalso())
        app.cliente.acepta, app.cliente.confirma = acepta, confirma
        app.estado.tocar("luz", app.fake.reloj.t)
        err = io.StringIO()
        with redirect_stderr(err):
            app.cerrar()
        return app, err.getvalue()

    def test_si_no_se_puede_apagar_la_luz_lo_avisa(self):
        app, err = self.apagar_con_luz(acepta=False)
        self.assertIn("luz de prueba posiblemente encendida", err)
        self.assertEqual(app.pantalla.cerrada, 1)       # y cierra todo lo demas

    def test_si_el_apagado_no_se_confirma_lo_avisa(self):
        _, err = self.apagar_con_luz(confirma=False)
        self.assertIn("luz de prueba posiblemente encendida", err)

    def test_si_se_apago_bien_no_avisa(self):
        _, err = self.apagar_con_luz()
        self.assertEqual(err, "")

    def test_sin_luz_de_prueba_no_avisa_aunque_no_haya_conexion(self):
        app = crear_app(tactil=TactilFalso(), conectado=False)
        app.cliente.acepta, app.cliente.confirma = False, False
        err = io.StringIO()
        with redirect_stderr(err):
            app.cerrar()
        self.assertEqual(err.getvalue(), "")

    def test_el_aviso_no_lleva_datos_sensibles(self):
        _, err = self.apagar_con_luz(acepta=False)
        for texto in ("clave", "password", "999"):
            self.assertNotIn(texto, err.lower())

    def test_si_falla_un_paso_se_cierra_lo_demas(self):
        tactil = TactilFalso()
        app = crear_app(tactil=tactil)
        app.cliente.detener = mock.Mock(side_effect=RuntimeError("x"))
        with self.assertRaises(RuntimeError):
            app.cerrar()
        self.assertEqual(tactil.cerrado, 1)
        self.assertEqual(app.pantalla.cerrada, 1)


class Main(unittest.TestCase):
    def correr(self, pantalla=None, abrir=None, entorno=ENTORNO, tactil=None,
               abrir_tactil=None, cliente=None):
        """Corre main() con todo falso; devuelve (codigo, stderr, cliente)."""
        abrir = abrir or mock.Mock(return_value=pantalla)
        abrir_tactil = abrir_tactil or mock.Mock(return_value=tactil)
        cliente = cliente or ClienteFalso()
        fabrica = mock.Mock(side_effect=lambda h, p, c, ev: (
            setattr(cliente, "args", (h, p, c)), setattr(cliente, "eventos", ev),
            cliente)[-1])
        err = io.StringIO()
        with mock.patch.object(main, "abrir_pantalla", abrir), \
                mock.patch.object(main, "abrir_tactil", abrir_tactil), \
                mock.patch.object(main, "ClienteMQTT", fabrica), \
                mock.patch.dict(main.os.environ, entorno, clear=True), \
                mock.patch.object(main.signal, "signal"), \
                redirect_stderr(err):
            codigo = main.main()
        self.abrir, self.abrir_tactil = abrir, abrir_tactil
        return codigo, err.getvalue(), cliente

    def parada(self, cuadros=1, **kw):
        parar = threading.Event()
        p = PantallaFalsa(parar=parar, cuadros_max=cuadros, **kw)
        return parar, p

    def test_no_se_puede_abrir_el_dispositivo(self):
        codigo, err, cliente = self.correr(
            abrir=mock.Mock(side_effect=PermissionError("Permission denied")))
        self.assertEqual(codigo, main.EX_IOERR)
        self.assertIn("/dev/spidev3.0", err)
        self.assertIn("/dev/gpiochip0", err)
        self.assertIn("Permission denied", err)
        self.assertEqual(cliente.iniciado, 0)       # sin pantalla no hay red

    def test_no_se_puede_abrir_el_tactil_y_libera_la_pantalla(self):
        p = PantallaFalsa()
        codigo, err, cliente = self.correr(
            pantalla=p, abrir_tactil=mock.Mock(
                side_effect=FileNotFoundError("/dev/spidev3.1")))
        self.assertEqual(codigo, main.EX_IOERR)
        self.assertIn("/dev/spidev3.1", err)
        self.assertEqual(p.cerrada, 1)
        self.assertEqual(cliente.iniciado, 0)

    def test_config_invalida(self):
        codigo, err, _ = self.correr(entorno=dict(ENTORNO, PANTALLA_DC="x"))
        self.assertEqual(codigo, main.EX_CONFIG)
        self.assertIn("PANTALLA_DC", err)

    def test_falta_la_clave_sale_con_78_sin_tocar_el_hardware(self):
        for entorno in ({}, {"MQTT_PANTALLA_PASSWORD": ""}):
            with self.subTest(entorno=entorno):
                codigo, err, cliente = self.correr(entorno=entorno)
                self.assertEqual(codigo, main.EX_CONFIG)
                self.assertIn("MQTT_PANTALLA_PASSWORD", err)
                self.abrir.assert_not_called()
                self.assertEqual(cliente.iniciado, 0)

    def test_senal_limpia_libera_y_sale_con_cero(self):
        bit = []
        parar, p = self.parada(bitacora=bit)
        t = TactilFalso(bitacora=bit)
        c = ClienteFalso(bitacora=bit)
        with mock.patch.object(main.threading, "Event", return_value=parar):
            codigo, err, _ = self.correr(pantalla=p, tactil=t, cliente=c)
        self.assertEqual(codigo, 0)
        self.assertEqual(len(p.cuadros), 1)
        self.assertEqual(p.llenados, [0])
        self.assertEqual((p.cerrada, t.cerrado, c.detenido), (1, 1, 1))
        self.assertEqual(c.iniciado, 1)

    def test_la_clave_va_al_cliente_y_no_al_log(self):
        parar, p = self.parada()
        with mock.patch.object(main.threading, "Event", return_value=parar):
            codigo, err, cliente = self.correr(pantalla=p, tactil=TactilFalso())
        self.assertEqual(cliente.args, ("127.0.0.1", 1883, CLAVE))
        self.assertNotIn(CLAVE, err)

    def test_con_el_broker_caido_la_pantalla_sigue_dibujando(self):
        parar, p = self.parada(cuadros=3)
        # un cliente que nunca conecta: ningun evento llega
        with mock.patch.object(main.threading, "Event", return_value=parar), \
                mock.patch.object(main, "TROZO_ESPERA", 0):
            app_fechas = iter([AHORA.replace(second=s) for s in range(10, 20)])
            with mock.patch.object(main, "datetime") as dt:
                dt.now.side_effect = lambda: next(app_fechas)
                codigo, _, cliente = self.correr(pantalla=p, tactil=TactilFalso())
        self.assertEqual(codigo, 0)
        self.assertEqual(len(p.cuadros), 3)

    def test_el_reinicio_por_mqtt_sale_con_cero_y_limpia(self):
        p = PantallaFalsa()
        t = TactilFalso()
        c = ClienteFalso()

        def fabrica_con_evento(h, pu, cl, ev):
            ev.put(("reinicio", None))
            c.eventos = ev
            return c
        err = io.StringIO()
        with mock.patch.object(main, "abrir_pantalla", return_value=p), \
                mock.patch.object(main, "abrir_tactil", return_value=t), \
                mock.patch.object(main, "ClienteMQTT", side_effect=fabrica_con_evento), \
                mock.patch.dict(main.os.environ, ENTORNO, clear=True), \
                mock.patch.object(main.signal, "signal"), \
                redirect_stderr(err):
            codigo = main.main()
        self.assertEqual(codigo, 0)
        self.assertEqual((p.cerrada, t.cerrado, c.detenido), (1, 1, 1))
        self.assertEqual(p.llenados, [0])
        self.assertIn("reinicio", err.getvalue())

    def test_tactil_desactivado(self):
        parar, p = self.parada()
        with mock.patch.object(main.threading, "Event", return_value=parar):
            codigo, _, _ = self.correr(
                pantalla=p, entorno=dict(ENTORNO, PANTALLA_TACTIL="0"))
        self.assertEqual(codigo, 0)
        self.abrir_tactil.assert_not_called()

    def test_un_fallo_al_apagar_tras_pedir_parar_sale_con_cero_y_avisa(self):
        parar, p = self.parada()
        p.fill = mock.Mock(side_effect=OSError("spi caido"))
        with mock.patch.object(main.threading, "Event", return_value=parar):
            codigo, err, _ = self.correr(pantalla=p, tactil=TactilFalso())
        self.assertEqual(codigo, 0)
        self.assertIn("spi caido", err)
        self.assertEqual(p.cerrada, 1)

    def test_un_fallo_de_escritura_y_otro_al_apagar_sale_con_74(self):
        p = PantallaFalsa()
        p.image = mock.Mock(side_effect=OSError("spi"))
        p.fill = mock.Mock(side_effect=OSError("spi otra vez"))
        codigo, err, _ = self.correr(pantalla=p, tactil=TactilFalso())
        self.assertEqual(codigo, main.EX_IOERR)
        self.assertEqual(p.cerrada, 1)

    def test_si_falla_iniciar_el_cliente_se_libera_todo(self):
        p, t, c = PantallaFalsa(), TactilFalso(), ClienteFalso()
        c.iniciar = mock.Mock(side_effect=RuntimeError("hilo"))
        with self.assertRaises(RuntimeError):
            self.correr(pantalla=p, tactil=t, cliente=c)
        self.assertEqual((p.cerrada, t.cerrado, c.detenido), (1, 1, 1))

    def test_avisa_si_el_broker_no_es_local(self):
        for host in ("192.168.1.20", "broker.local"):
            with self.subTest(host=host):
                parar, p = self.parada()
                with mock.patch.object(main.threading, "Event", return_value=parar):
                    _, err, _ = self.correr(
                        pantalla=p, entorno=dict(ENTORNO, PANTALLA_MQTT_HOST=host))
                self.assertIn(host, err)
                self.assertIn("en claro", err)
                self.assertNotIn(CLAVE, err)

    def test_no_avisa_si_el_broker_es_local(self):
        for host in ("127.0.0.1", "::1", "localhost"):
            with self.subTest(host=host):
                parar, p = self.parada()
                with mock.patch.object(main.threading, "Event", return_value=parar):
                    _, err, _ = self.correr(
                        pantalla=p, entorno=dict(ENTORNO, PANTALLA_MQTT_HOST=host))
                self.assertNotIn("en claro", err)

    def test_la_cola_de_eventos_esta_acotada(self):
        parar, p = self.parada()
        with mock.patch.object(main.threading, "Event", return_value=parar):
            _, _, cliente = self.correr(pantalla=p)
        self.assertIsInstance(cliente.eventos, queue.Queue)
        self.assertGreater(cliente.eventos.maxsize, 0)

    def test_la_clave_puede_venir_de_la_credencial_de_systemd(self):
        with tempfile.TemporaryDirectory() as d:
            with open(os.path.join(d, "mqtt_pantalla"), "w") as f:
                f.write("de-la-credencial\n")
            parar, p = self.parada()
            with mock.patch.object(main.threading, "Event", return_value=parar):
                _, err, cliente = self.correr(
                    pantalla=p, entorno={"CREDENTIALS_DIRECTORY": d})
        self.assertEqual(cliente.args[2], "de-la-credencial")
        self.assertNotIn("de-la-credencial", err)

    def test_avisa_si_la_clave_esta_en_el_entorno_bajo_systemd(self):
        with tempfile.TemporaryDirectory() as d:
            with open(os.path.join(d, "mqtt_pantalla"), "w") as f:
                f.write("de-la-credencial\n")
            parar, p = self.parada()
            with mock.patch.object(main.threading, "Event", return_value=parar):
                _, err, _ = self.correr(pantalla=p, entorno=dict(
                    ENTORNO, CREDENTIALS_DIRECTORY=d))
        self.assertIn("MQTT_PANTALLA_PASSWORD", err)
        self.assertIn("entorno", err)
        self.assertNotIn(CLAVE, err)
        self.assertNotIn("de-la-credencial", err)

    def test_no_avisa_con_solo_una_de_las_dos_fuentes(self):
        with tempfile.TemporaryDirectory() as d:
            with open(os.path.join(d, "mqtt_pantalla"), "w") as f:
                f.write("de-la-credencial")
            for entorno in ({"CREDENTIALS_DIRECTORY": d}, ENTORNO):
                with self.subTest(entorno=sorted(entorno)):
                    parar, p = self.parada()
                    with mock.patch.object(main.threading, "Event",
                                           return_value=parar):
                        _, err, _ = self.correr(pantalla=p, entorno=entorno)
                    self.assertNotIn("entorno", err)

    def test_fallo_de_escritura_libera_y_sale_con_error(self):
        p = PantallaFalsa()
        p.image = mock.Mock(side_effect=OSError("spi"))
        t, c = TactilFalso(), ClienteFalso()
        codigo, err, _ = self.correr(pantalla=p, tactil=t, cliente=c)
        self.assertEqual(codigo, main.EX_IOERR)
        self.assertEqual((p.cerrada, t.cerrado, c.detenido), (1, 1, 1))
        self.assertIn("spi", err)


if __name__ == "__main__":
    unittest.main()
