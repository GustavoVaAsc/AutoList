"""
Pruebas de estado.py sin hardware ni broker: lectura y descarte de los
mensajes MQTT, cambio de vista, y botones que producen la publicacion.

Ejecutar desde orangepi/pantalla:  .venv/bin/python -m unittest -v
"""

import json
import unittest

import estado
from estado import Estado, interpretar

SESION = "aula/sesion/estado"
ASISTENCIA = "aula/sesion/asistencia"
ACTUADOR = "aula/actuador/estado"
ENROL = "aula/vision/enrolamiento"
REINICIO = "aula/pantalla/reinicio"
CMD_SESION = "aula/sesion/cmd"
CMD_ACTUADOR = "aula/actuador/cmd"


def j(obj):
    return json.dumps(obj).encode()


def enrol(**campos):
    base = {"ts": "2026-11-04T09:12:33-06:00", "cuenta": "999000001",
            "nombre": "Ana López", "paso": 1, "total": 5, "pose": "frente",
            "estado": "esperando_pose"}
    base.update(campos)
    return ("enrolamiento", interpretar(ENROL, j(base), False)[1])


class InterpretarSesion(unittest.TestCase):
    def test_sesion_activa(self):
        ev = interpretar(SESION, j({"activa": True, "id": 12, "materia": "FSE",
                                    "grupo": "1", "inicio": "2026-11-04T09:00:00-06:00",
                                    "tol_min": 10}), False)
        self.assertEqual(ev[0], "sesion")
        self.assertTrue(ev[1]["activa"])
        self.assertEqual((ev[1]["materia"], ev[1]["grupo"], ev[1]["tol_min"]),
                         ("FSE", "1", 10))

    def test_sesion_inactiva(self):
        ev = interpretar(SESION, j({"activa": False, "id": 12}), False)
        self.assertEqual(ev[0], "sesion")
        self.assertFalse(ev[1]["activa"])

    def test_el_estado_retenido_se_acepta(self):
        # Retain = si: es el estado que se recupera al conectar
        self.assertIsNotNone(interpretar(SESION, j({"activa": False}), True))

    def test_grupo_numerico_se_vuelve_texto(self):
        ev = interpretar(SESION, j({"activa": True, "grupo": 2}), False)
        self.assertEqual(ev[1]["grupo"], "2")

    def test_descartes(self):
        for payload in (b"", b"no es json", b"\xff\xfe", j([1, 2]), j("x"),
                        j({"activa": "si"}), j({"id": 3}), j({"activa": 1})):
            with self.subTest(payload=payload):
                self.assertIsNone(interpretar(SESION, payload, False))

    def test_campos_desconocidos_se_ignoran(self):
        ev = interpretar(SESION, j({"activa": False, "nuevo": [1]}), False)
        self.assertEqual(ev[0], "sesion")


class InterpretarActuador(unittest.TestCase):
    def test_estado_valido(self):
        ev = interpretar(ACTUADOR, j({"servo": "cerrado", "led": "azul", "luz": 0}),
                         True)
        self.assertEqual(ev, ("actuadores", {"servo": "cerrado", "led": "azul",
                                             "luz": 0}))

    def test_luz_fuera_de_rango_se_ignora(self):
        ev = interpretar(ACTUADOR, j({"servo": "cerrado", "luz": 7}), False)
        self.assertEqual(ev, ("actuadores", {"servo": "cerrado"}))

    def test_sin_campos_utiles(self):
        self.assertIsNone(interpretar(ACTUADOR, j({}), False))
        self.assertIsNone(interpretar(ACTUADOR, j({"luz": True}), False))
        self.assertIsNone(interpretar(ACTUADOR, b"", True))


class InterpretarEnrolamiento(unittest.TestCase):
    def test_progreso_valido(self):
        ev = interpretar(ENROL, j({"ts": "x", "cuenta": "999000001",
                                   "nombre": "Ana López", "paso": 3, "total": 5,
                                   "pose": "izquierda", "estado": "capturando",
                                   "miniatura": "AAAA"}), False)
        self.assertEqual(ev[0], "enrolamiento")
        d = ev[1]
        self.assertEqual((d["nombre"], d["paso"], d["total"], d["pose"],
                          d["estado"]), ("Ana López", 3, 5, "izquierda",
                                         "capturando"))
        self.assertNotIn("miniatura", d)    # la pantalla la ignora

    def test_con_retain_se_descarta(self):
        self.assertIsNone(interpretar(ENROL, j({"paso": 1, "total": 5,
                                                "estado": "capturando"}), True))

    def test_payload_vacio_se_descarta(self):
        self.assertIsNone(interpretar(ENROL, b"", False))

    def test_descartes_por_forma(self):
        base = {"paso": 1, "total": 5, "estado": "capturando"}
        casos = [
            dict(base, paso=6), dict(base, paso=-1), dict(base, total=0),
            dict(base, total=99), dict(base, paso=True), dict(base, total="5"),
            dict(base, estado=3), {"paso": 1, "total": 5},
            dict(base, nombre=7), dict(base, pose=[]),
        ]
        for caso in casos:
            with self.subTest(caso=caso):
                self.assertIsNone(interpretar(ENROL, j(caso), False))

    def test_payload_enorme(self):
        grande = j({"paso": 1, "total": 5, "estado": "aceptada",
                    "miniatura": "A" * 100000})
        self.assertIsNone(interpretar(ENROL, grande, False))

    def test_textos_largos_se_recortan(self):
        d = interpretar(ENROL, j({"paso": 1, "total": 5, "estado": "aceptada",
                                  "cuenta": "999000001", "nombre": "N" * 500}),
                        False)[1]
        self.assertLessEqual(len(d["nombre"]), 80)

    def test_rechazo_de_una_orden_paso_cero(self):
        d = interpretar(ENROL, j({"paso": 0, "total": 5, "estado": "error",
                                  "motivo": "ocupado", "pose": ""}), False)[1]
        self.assertEqual((d["paso"], d["motivo"]), (0, "ocupado"))


class EnrolamientoSinCuenta(unittest.TestCase):
    BASE = {"paso": 2, "total": 5, "estado": "capturando", "pose": "izquierda"}

    def test_sin_cuenta_valida_se_descarta(self):
        for cuenta in (None, 5, [], {}, "", "  ", "\n\u202e", True):
            with self.subTest(cuenta=cuenta):
                d = dict(self.BASE)
                if cuenta is not None:
                    d["cuenta"] = cuenta
                self.assertIsNone(interpretar(ENROL, j(d), False))

    def test_con_cuenta_se_acepta(self):
        d = dict(self.BASE, cuenta="999000001")
        self.assertEqual(interpretar(ENROL, j(d), False)[1]["cuenta"], "999000001")

    def test_el_rechazo_de_una_orden_puede_ir_sin_cuenta(self):
        # el contrato: la cuenta va vacia si la del comando no era valida
        for cuenta in ("", None):
            with self.subTest(cuenta=cuenta):
                d = {"paso": 0, "total": 5, "estado": "error",
                     "motivo": "cuenta_invalida"}
                if cuenta is not None:
                    d["cuenta"] = cuenta
                self.assertEqual(interpretar(ENROL, j(d), False)[1]["cuenta"], "")

    def test_error_con_paso_distinto_de_cero_sin_cuenta_se_descarta(self):
        d = {"paso": 3, "total": 5, "estado": "error", "motivo": "npu"}
        self.assertIsNone(interpretar(ENROL, j(d), False))


class InterpretarReinicio(unittest.TestCase):
    def test_reinicio(self):
        self.assertEqual(interpretar(REINICIO, j({"reiniciar": 1}), False),
                         ("reinicio", None))

    def test_retenido_se_descarta(self):
        # sin esto, un retenido reiniciaria la pantalla en cada reconexion
        self.assertIsNone(interpretar(REINICIO, j({"reiniciar": 1}), True))

    def test_otros_valores(self):
        for payload in (b"", j({"reiniciar": 0}), j({"reiniciar": True}),
                        j({}), b"1", j({"reiniciar": "1"})):
            with self.subTest(payload=payload):
                self.assertIsNone(interpretar(REINICIO, payload, False))

    def test_topico_desconocido(self):
        self.assertIsNone(interpretar("aula/camara/pir", j({"mov": 1}), False))
        self.assertIsNone(interpretar("aula/sesion/cmd",
                                      j({"accion": "terminar"}), False))


class Suscripciones(unittest.TestCase):
    def test_son_los_cinco_topicos_de_la_acl(self):
        self.assertEqual(dict(estado.SUSCRIPCIONES),
                         {SESION: 1, ASISTENCIA: 1, ACTUADOR: 1,
                          ENROL: 0, REINICIO: 1})


class InterpretarAsistencia(unittest.TestCase):
    def test_activa_con_ultimo(self):
        ev = interpretar(ASISTENCIA, j({
            "activa": True, "id": 23, "presentes": 5, "retardos": 1,
            "faltantes": 4,
            "ultimo": {"nombre": "Valenzuela Ascencio, Gustavo",
                       "estado": "presente"}
        }), True)
        self.assertEqual(ev, ("asistencia", {
            "activa": True, "id": 23,
            "asistencia": {"presentes": 5, "retardos": 1, "faltantes": 4},
            "ultimo": {"nombre": "Valenzuela Ascencio, Gustavo",
                       "estado": "presente"},
        }))

    def test_activa_sin_ultimo(self):
        ev = interpretar(ASISTENCIA, j({
            "activa": True, "id": 23, "presentes": 0, "retardos": 0,
            "faltantes": 10, "ultimo": None
        }), False)
        self.assertIsNone(ev[1]["ultimo"])
        self.assertEqual(ev[1]["asistencia"],
                         {"presentes": 0, "retardos": 0, "faltantes": 10})

    def test_inactiva(self):
        ev = interpretar(ASISTENCIA, j({"activa": False}), True)
        self.assertEqual(ev, ("asistencia", {"activa": False, "id": None,
                                             "asistencia": None, "ultimo": None}))

    def test_limpia_y_recorta_el_nombre_del_ultimo(self):
        ev = interpretar(ASISTENCIA, j({
            "activa": True, "id": 1, "presentes": 0, "retardos": 1,
            "faltantes": 9,
            "ultimo": {"nombre": "Ana\n\u202eLópez" + "x" * 200,
                       "estado": "retardo"}
        }), False)
        self.assertEqual(ev[1]["ultimo"]["estado"], "retardo")
        self.assertTrue(ev[1]["ultimo"]["nombre"].startswith("Ana  López"))
        self.assertLessEqual(len(ev[1]["ultimo"]["nombre"]), 80)

    def test_descartes(self):
        base = {"activa": True, "id": 1, "presentes": 1, "retardos": 0,
                "faltantes": 9, "ultimo": None}
        malos = [
            b"", b"no", j([]), j({"activa": "si"}), j({"activa": 1}),
            j(dict(base, presentes=-1)), j(dict(base, retardos=True)),
            j(dict(base, faltantes=10001)), j(dict(base, presentes="1")),
            j({"activa": True}),
            j(dict(base, ultimo="Ana")), j(dict(base, ultimo=[])),
            j(dict(base, ultimo={"nombre": "", "estado": "presente"})),
            j(dict(base, ultimo={"nombre": "   ", "estado": "presente"})),
            j(dict(base, ultimo={"nombre": "Ana", "estado": "falta"})),
        ]
        for payload in malos:
            with self.subTest(payload=payload):
                self.assertIsNone(interpretar(ASISTENCIA, payload, False))


class EstadoAsistencia(unittest.TestCase):
    def test_actualiza_conteos_y_ultimo_y_limpia_al_cerrar(self):
        e = Estado()
        e.aplicar(interpretar(SESION, j({"activa": True, "id": 23,
                                         "materia": "FSE", "grupo": "1"}), True), 0)
        e.sucio = False
        e.aplicar(interpretar(ASISTENCIA, j({
            "activa": True, "id": 23, "presentes": 1, "retardos": 0,
            "faltantes": 9,
            "ultimo": {"nombre": "Ana López", "estado": "presente"}
        }), False), 1)
        self.assertTrue(e.sucio)
        self.assertEqual(e.asistencia,
                         {"presentes": 1, "retardos": 0, "faltantes": 9})
        self.assertEqual(e.ultimo_registro,
                         {"nombre": "Ana López", "estado": "presente"})
        e.sucio = False
        e.aplicar(interpretar(SESION, j({"activa": False, "id": 23}), False), 2)
        self.assertTrue(e.sucio)
        self.assertIsNone(e.asistencia)
        self.assertIsNone(e.ultimo_registro)

    def test_funciona_aunque_asistencia_llegue_antes_que_el_estado_de_sesion(self):
        e = Estado()
        e.aplicar(interpretar(SESION, j({"activa": False, "id": 22}), True), 0)
        # Al iniciar la 23, asistencia llega un instante antes que estado
        e.aplicar(interpretar(ASISTENCIA, j({
            "activa": True, "id": 23, "presentes": 0, "retardos": 0,
            "faltantes": 10, "ultimo": None
        }), False), 1)
        self.assertEqual(e.asistencia["faltantes"], 10)
        e.aplicar(interpretar(SESION, j({"activa": True, "id": 23,
                                         "materia": "FSE", "grupo": "1"}), False), 2)
        self.assertEqual(e.asistencia["faltantes"], 10)

    def test_descarta_asistencia_rezagada_de_una_sesion_cerrada_o_anterior(self):
        e = Estado()
        e.aplicar(interpretar(SESION, j({"activa": False, "id": 23}), True), 0)
        e.sucio = False
        e.aplicar(interpretar(ASISTENCIA, j({
            "activa": True, "id": 23, "presentes": 3, "retardos": 0,
            "faltantes": 7, "ultimo": None
        }), False), 1)
        self.assertIsNone(e.asistencia)
        self.assertFalse(e.sucio)
        e.aplicar(interpretar(SESION, j({"activa": True, "id": 24}), False), 2)
        e.aplicar(interpretar(ASISTENCIA, j({
            "activa": True, "id": 23, "presentes": 3, "retardos": 0,
            "faltantes": 7, "ultimo": None
        }), False), 3)
        self.assertIsNone(e.asistencia)


class Conexion(unittest.TestCase):
    def test_estado_inicial(self):
        e = Estado()
        self.assertFalse(e.conectado)
        self.assertIsNone(e.sesion)
        self.assertEqual(e.vista_visible, "sesion")
        self.assertFalse(e.reiniciar)

    def test_conexion_y_datos(self):
        e = Estado()
        e.sucio = False
        e.aplicar(("conexion", True), 0)
        self.assertTrue(e.conectado)
        self.assertTrue(e.sucio)
        e.aplicar(interpretar(SESION, j({"activa": True, "materia": "FSE"}), True), 1)
        self.assertEqual(e.sesion["materia"], "FSE")
        e.aplicar(("actuadores", {"servo": "cerrado", "luz": 0}), 2)
        e.aplicar(("actuadores", {"luz": 1}), 3)
        self.assertEqual(e.actuadores, {"servo": "cerrado", "luz": 1})

    def test_reinicio(self):
        e = Estado()
        e.aplicar(("reinicio", None), 0)
        self.assertTrue(e.reiniciar)

    def test_evento_desconocido_no_rompe(self):
        e = Estado()
        e.aplicar(("otra_cosa", 1), 0)
        self.assertFalse(e.reiniciar)


class Guia(unittest.TestCase):
    def test_se_activa_sola_con_el_registro(self):
        e = Estado()
        e.aplicar(enrol(paso=3, pose="izquierda", estado="capturando"), 10)
        self.assertEqual(e.vista_visible, "guia")
        self.assertEqual((e.guia["paso"], e.guia["total"]), (3, 5))
        self.assertEqual(e.guia["nombre"], "Ana López")

    def test_conserva_el_nombre_si_un_mensaje_no_lo_trae(self):
        e = Estado()
        e.aplicar(enrol(), 0)
        e.aplicar(("enrolamiento", interpretar(ENROL, j({
            "cuenta": "999000001", "paso": 2, "total": 5, "pose": "derecha",
            "estado": "capturando"}), False)[1]), 1)
        self.assertEqual(e.guia["nombre"], "Ana López")
        self.assertEqual(e.guia["paso"], 2)

    def test_terminado_vuelve_a_sesion(self):
        for final in ("completado", "cancelado", "error"):
            with self.subTest(final=final):
                e = Estado()
                e.vista = "lista"
                e.aplicar(enrol(paso=5, estado="aceptada"), 0)
                e.aplicar(enrol(paso=5, estado=final,
                                motivo="npu" if final == "error" else ""), 10)
                self.assertEqual(e.vista_visible, "guia")   # muestra el desenlace
                e.tick(10 + Estado.GUIA_FIN_S - 0.1)
                self.assertEqual(e.vista_visible, "guia")
                e.tick(10 + Estado.GUIA_FIN_S)
                self.assertIsNone(e.guia)
                self.assertEqual(e.vista_visible, "sesion")

    def test_sin_mensajes_vence(self):
        e = Estado(guia_vence_s=330)
        e.aplicar(enrol(), 100)
        e.tick(100 + 329)
        self.assertEqual(e.vista_visible, "guia")
        e.tick(100 + 330)
        self.assertEqual(e.vista_visible, "sesion")

    def test_cada_mensaje_renueva_el_plazo(self):
        e = Estado(guia_vence_s=330)
        e.aplicar(enrol(), 0)
        e.aplicar(enrol(paso=2), 300)
        e.tick(400)
        self.assertEqual(e.vista_visible, "guia")

    def test_rechazo_de_orden_no_pisa_un_registro_en_curso(self):
        e = Estado()
        e.aplicar(enrol(paso=2, estado="capturando"), 0)
        e.aplicar(enrol(paso=0, estado="error", motivo="ocupado", pose="",
                        cuenta="999000002", nombre=""), 1)
        self.assertEqual(e.guia["paso"], 2)
        self.assertEqual(e.guia["estado"], "capturando")
        e.tick(1 + Estado.GUIA_FIN_S + 1)
        self.assertEqual(e.vista_visible, "guia")

    def test_rechazo_de_orden_sin_registro_solo_avisa(self):
        e = Estado()
        e.aplicar(enrol(paso=0, estado="error", motivo="ocupado", pose=""), 0)
        self.assertIsNone(e.guia)
        self.assertEqual(e.vista_visible, "sesion")
        self.assertIsNotNone(e.aviso)
        # El aviso es de una linea: el texto tiene que caber sin recortarse.
        self.assertEqual(e.aviso[0], "Registro rechazado")

    def test_en_la_guia_solo_actua_salir(self):
        e = Estado()
        e.aplicar(("conexion", True), 0)
        e.aplicar(enrol(), 0)
        self.assertEqual(e.botones_activos(), {"guia_salir"})
        self.assertEqual(e.tocar("puerta", 1), [])
        self.assertEqual(e.tocar("nav_lista", 1), [])
        self.assertEqual(e.vista, "sesion")


class Navegacion(unittest.TestCase):
    def test_pestanas(self):
        e = Estado()
        e.tocar("nav_lista", 0)
        self.assertEqual(e.vista_visible, "lista")
        e.tocar("nav_sesion", 1)
        self.assertEqual(e.vista_visible, "sesion")

    def test_en_lista_solo_hay_pestanas(self):
        e = Estado()
        e.aplicar(("conexion", True), 0)
        e.tocar("nav_lista", 0)
        self.assertEqual(e.botones_activos(), {"nav_sesion", "nav_lista"})
        self.assertEqual(e.tocar("puerta", 1), [])

    def test_boton_desconocido_o_ninguno(self):
        e = Estado()
        self.assertEqual(e.tocar(None, 0), [])
        self.assertEqual(e.tocar("xyz", 0), [])


class BotonSesion(unittest.TestCase):
    def conectado(self, **kw):
        e = Estado(**kw)
        e.aplicar(("conexion", True), 0)
        e.aplicar(("sesion", {"activa": False}), 0)
        return e

    def test_sin_conexion_no_publica_y_avisa(self):
        e = Estado()
        self.assertEqual(e.tocar("sesion_toggle", 0), [])
        self.assertIn("conexión", e.aviso[0])

    def test_iniciar_pide_confirmacion_y_usa_los_valores_de_config(self):
        e = self.conectado(materia="FSE", grupo="1", tol_min=10)
        self.assertEqual(e.tocar("sesion_toggle", 1), [])
        self.assertIsNotNone(e.armado)
        self.assertEqual(e.tocar("sesion_toggle", 2),
                         [(CMD_SESION, {"accion": "iniciar", "materia": "FSE",
                                        "grupo": "1", "tol_min": 10})])
        self.assertIsNone(e.armado)

    def test_terminar_cuando_hay_sesion_activa(self):
        e = self.conectado()
        e.aplicar(("sesion", {"activa": True}), 0)
        e.tocar("sesion_toggle", 1)
        self.assertEqual(e.tocar("sesion_toggle", 2),
                         [(CMD_SESION, {"accion": "terminar"})])

    def test_la_confirmacion_vence(self):
        e = self.conectado()
        e.tocar("sesion_toggle", 1)
        e.tick(1 + Estado.CONFIRMAR_S)
        self.assertIsNone(e.armado)
        self.assertEqual(e.tocar("sesion_toggle", 10), [])      # vuelve a armar
        self.assertIsNotNone(e.armado)

    def test_si_cambia_la_sesion_se_vuelve_a_armar(self):
        e = self.conectado()
        e.tocar("sesion_toggle", 1)                 # arma "iniciar"
        e.aplicar(("sesion", {"activa": True}), 2)  # alguien la inicia desde el frontend
        self.assertEqual(e.tocar("sesion_toggle", 3), [])   # arma "terminar", no publica

    def test_cambiar_de_pestana_desarma(self):
        e = self.conectado()
        e.tocar("sesion_toggle", 1)
        e.tocar("nav_lista", 2)
        self.assertIsNone(e.armado)

    def test_si_se_cae_la_conexion_no_publica(self):
        e = self.conectado()
        e.tocar("sesion_toggle", 1)
        e.aplicar(("conexion", False), 2)
        self.assertEqual(e.tocar("sesion_toggle", 3), [])


class BotonPuerta(unittest.TestCase):
    def test_abrir_puerta(self):
        e = Estado()
        e.aplicar(("conexion", True), 0)
        self.assertEqual(e.tocar("puerta", 1),
                         [(CMD_ACTUADOR, {"servo": "abrir"})])

    def test_sin_conexion(self):
        e = Estado()
        self.assertEqual(e.tocar("puerta", 1), [])
        self.assertIn("conexión", e.aviso[0])


class BotonLuz(unittest.TestCase):
    def conectado(self):
        e = Estado(luz_prueba_s=3)
        e.aplicar(("conexion", True), 0)
        return e

    def test_enciende_y_se_apaga_sola(self):
        e = self.conectado()
        self.assertEqual(e.tocar("luz", 10), [(CMD_ACTUADOR, {"luz": 1})])
        self.assertEqual(e.tick(12.9), [])
        self.assertEqual(e.tick(13), [(CMD_ACTUADOR, {"luz": 0})])
        self.assertIsNone(e.luz_hasta)
        self.assertEqual(e.tick(14), [])            # una sola orden de apagado

    def test_un_segundo_toque_la_apaga_de_inmediato(self):
        e = self.conectado()
        e.tocar("luz", 10)
        self.assertEqual(e.tocar("luz", 12.5), [(CMD_ACTUADOR, {"luz": 0})])
        self.assertIsNone(e.luz_hasta)
        self.assertEqual(e.tick(13), [])

    def test_sin_conexion_al_vencer_espera_a_conectar(self):
        e = self.conectado()
        e.tocar("luz", 10)
        e.aplicar(("conexion", False), 11)
        self.assertEqual(e.tick(20), [])
        self.assertIsNotNone(e.luz_hasta)
        e.aplicar(("conexion", True), 30)
        self.assertEqual(e.tick(30), [(CMD_ACTUADOR, {"luz": 0})])

    def test_sin_conexion_no_enciende(self):
        e = Estado()
        self.assertEqual(e.tocar("luz", 0), [])
        self.assertIsNone(e.luz_hasta)

    def test_cierre_apaga_la_luz_de_prueba(self):
        e = self.conectado()
        e.tocar("luz", 10)
        self.assertEqual(e.cierre(), [(CMD_ACTUADOR, {"luz": 0})])
        self.assertEqual(Estado().cierre(), [])


class FalloDeEnvio(unittest.TestCase):
    def test_avisa(self):
        e = Estado()
        e.fallo_envio(CMD_ACTUADOR, {"servo": "abrir"}, 5)
        self.assertIn("enviar", e.aviso[0])
        self.assertIsNone(e.luz_hasta)

    def test_si_no_salio_encender_la_luz_no_queda_como_encendida(self):
        e = Estado()
        e.aplicar(("conexion", True), 0)
        e.tocar("luz", 10)
        self.assertIsNotNone(e.luz_hasta)
        e.fallo_envio(CMD_ACTUADOR, {"luz": 1}, 10)
        self.assertIsNone(e.luz_hasta)          # la interfaz no dice "Apagar luz"
        self.assertEqual(e.tick(14), [])        # y no manda un apagado que no hace falta

    def test_si_era_apagar_la_luz_la_reintenta_en_un_segundo(self):
        e = Estado()
        e.aplicar(("conexion", True), 0)
        e.fallo_envio(CMD_ACTUADOR, {"luz": 0}, 5)
        self.assertEqual(e.tick(5.9), [])
        self.assertEqual(e.tick(6), [(CMD_ACTUADOR, {"luz": 0})])


class Avisos(unittest.TestCase):
    def test_el_aviso_vence(self):
        e = Estado()
        e.tocar("puerta", 0)
        self.assertIsNotNone(e.aviso)
        e.tick(Estado.AVISO_S)
        self.assertIsNone(e.aviso)

    def test_tick_sin_cambios_no_ensucia(self):
        e = Estado()
        e.sucio = False
        e.tick(1)
        self.assertFalse(e.sucio)


class SesionDesconocida(unittest.TestCase):
    def test_sin_estado_de_la_sesion_el_boton_no_actua(self):
        e = Estado()
        e.aplicar(("conexion", True), 0)
        self.assertEqual(e.tocar("sesion_toggle", 1), [])
        self.assertIsNone(e.armado)
        self.assertIsNotNone(e.aviso)

    def test_con_el_estado_ya_actua(self):
        e = Estado()
        e.aplicar(("conexion", True), 0)
        e.aplicar(("sesion", {"activa": False}), 0)
        e.tocar("sesion_toggle", 1)
        self.assertIsNotNone(e.armado)


class BloqueoTrasConfirmar(unittest.TestCase):
    def listo(self):
        e = Estado()
        e.aplicar(("conexion", True), 0)
        e.aplicar(("sesion", {"activa": False}), 0)
        e.tocar("sesion_toggle", 1)
        self.assertEqual(len(e.tocar("sesion_toggle", 2)), 1)
        return e

    def test_un_tercer_toque_no_manda_un_iniciar_doble(self):
        e = self.listo()
        self.assertEqual(e.tocar("sesion_toggle", 3), [])
        self.assertEqual(e.tocar("sesion_toggle", 4), [])
        self.assertIsNone(e.armado)

    def test_se_desbloquea_cuando_llega_el_estado(self):
        e = self.listo()
        e.aplicar(("sesion", {"activa": True}), 3)
        e.tocar("sesion_toggle", 4)
        self.assertIsNotNone(e.armado)

    def test_se_desbloquea_por_tiempo(self):
        e = self.listo()
        e.tick(2 + Estado.BLOQUEO_SESION_S)
        e.tocar("sesion_toggle", 2 + Estado.BLOQUEO_SESION_S)
        self.assertIsNotNone(e.armado)

    def test_el_bloqueo_se_ve_en_los_botones_activos_y_el_estilo(self):
        e = self.listo()
        self.assertTrue(e.sesion_bloqueada(3))
        self.assertFalse(e.sesion_bloqueada(2 + Estado.BLOQUEO_SESION_S))


class GuiaRobusta(unittest.TestCase):
    def test_otra_cuenta_no_pisa_una_guia_en_curso(self):
        e = Estado()
        e.aplicar(enrol(cuenta="999000001", nombre="Ana", paso=2), 0)
        e.aplicar(enrol(cuenta="999000002", nombre="Intruso", paso=1,
                        estado="capturando"), 1)
        self.assertEqual(e.guia["nombre"], "Ana")
        self.assertEqual(e.guia["paso"], 2)

    def test_otra_cuenta_se_acepta_si_la_guia_ya_termino(self):
        e = Estado()
        e.aplicar(enrol(cuenta="999000001", estado="cancelado"), 0)
        e.aplicar(enrol(cuenta="999000002", nombre="Beto", paso=1), 1)
        self.assertEqual(e.guia["nombre"], "Beto")

    def test_un_mensaje_sin_cuenta_no_toca_una_guia_en_curso(self):
        e = Estado()
        e.aplicar(enrol(cuenta="999000001", paso=2), 0)
        # llegando directo al estado (interpretar ya lo descarta)
        e.aplicar(("enrolamiento", {"cuenta": "", "nombre": "Intruso", "paso": 4,
                                    "total": 5, "pose": "arriba",
                                    "estado": "capturando", "motivo": ""}), 1)
        self.assertEqual((e.guia["paso"], e.guia["nombre"]), (2, "Ana López"))

    def test_un_mensaje_sin_cuenta_no_reabre_una_guia_cerrada_con_salir(self):
        e = Estado()
        e.aplicar(enrol(cuenta="999000001", paso=2), 0)
        e.tocar("guia_salir", 1)
        e.aplicar(("enrolamiento", {"cuenta": "", "nombre": "", "paso": 3,
                                    "total": 5, "pose": "arriba",
                                    "estado": "capturando", "motivo": ""}), 2)
        self.assertIsNone(e.guia)
        self.assertEqual(e.vista_visible, "sesion")

    def test_techo_absoluto_desde_el_primer_mensaje(self):
        e = Estado(guia_vence_s=330, guia_max_s=900)
        e.aplicar(enrol(paso=1), 0)
        for t in range(100, 1000, 100):     # mensajes que renuevan el plazo suave
            e.aplicar(enrol(paso=2), t)
            e.tick(t)
        self.assertIsNone(e.guia)           # a los 900 s se cierra
        self.assertEqual(e.vista_visible, "sesion")

    def test_tras_el_techo_el_mismo_registro_no_reabre_la_guia(self):
        e = Estado(guia_max_s=900)
        e.aplicar(enrol(paso=1), 0)
        e.tick(900)
        e.aplicar(enrol(paso=2), 901)
        self.assertIsNone(e.guia)

    def test_el_techo_no_se_reinicia_con_mensajes_de_la_misma_cuenta(self):
        e = Estado(guia_vence_s=330, guia_max_s=900)
        e.aplicar(enrol(paso=1), 0)
        e.aplicar(enrol(paso=2), 800)
        e.tick(900)
        self.assertIsNone(e.guia)

    def test_salir_vuelve_a_sesion(self):
        e = Estado()
        e.vista = "lista"
        e.aplicar(enrol(paso=2), 0)
        e.tocar("guia_salir", 1)
        self.assertIsNone(e.guia)
        self.assertEqual(e.vista_visible, "sesion")

    def test_salir_no_se_deshace_con_mensajes_del_mismo_registro(self):
        e = Estado()
        e.aplicar(enrol(paso=2), 0)
        e.tocar("guia_salir", 1)
        e.aplicar(enrol(paso=3), 2)
        self.assertIsNone(e.guia)

    def test_un_registro_distinto_si_abre_la_guia_aunque_se_haya_salido(self):
        e = Estado()
        e.aplicar(enrol(cuenta="999000001", paso=2), 0)
        e.tocar("guia_salir", 1)
        e.aplicar(enrol(cuenta="999000002", nombre="Beto", paso=1), 2)
        self.assertEqual(e.guia["nombre"], "Beto")

    def test_el_final_del_registro_descartado_permite_uno_nuevo_de_la_misma_cuenta(self):
        e = Estado()
        e.aplicar(enrol(paso=2), 0)
        e.tocar("guia_salir", 1)
        e.aplicar(enrol(paso=2, estado="cancelado"), 2)
        e.aplicar(enrol(paso=1), 3)
        self.assertIsNotNone(e.guia)

    def test_tras_un_silencio_largo_el_mismo_registro_ya_es_otro(self):
        e = Estado(guia_vence_s=330)
        e.aplicar(enrol(paso=2), 0)
        e.tocar("guia_salir", 1)
        e.aplicar(enrol(paso=1), 1 + 331)
        self.assertIsNotNone(e.guia)


class RegistroNuevo(unittest.TestCase):
    """Un final perdido (QoS 0) no debe ocultar la Guia del registro siguiente."""

    def inicio(self, **kw):
        return enrol(paso=1, estado="esperando_pose", **kw)

    def test_tras_salir_el_inicio_de_otro_registro_de_la_misma_cuenta_la_abre(self):
        e = Estado()
        e.aplicar(enrol(paso=3), 0)
        e.tocar("guia_salir", 1)
        e.aplicar(self.inicio(), 2)        # el final del primero se perdio
        self.assertIsNotNone(e.guia)

    def test_paso_cero_tambien_es_inicio(self):
        e = Estado()
        e.aplicar(enrol(paso=3), 0)
        e.tocar("guia_salir", 1)
        e.aplicar(enrol(paso=0, estado="esperando_pose"), 2)
        self.assertIsNotNone(e.guia)

    def test_un_progreso_cualquiera_no_cuenta_como_inicio(self):
        e = Estado()
        e.aplicar(enrol(paso=3), 0)
        e.tocar("guia_salir", 1)
        e.aplicar(enrol(paso=2, estado="esperando_pose"), 2)
        e.aplicar(enrol(paso=1, estado="capturando"), 3)
        self.assertIsNone(e.guia)

    def test_el_inicio_de_otra_cuenta_reemplaza_una_guia_atorada(self):
        e = Estado()
        e.aplicar(enrol(cuenta="999000001", nombre="Ana", paso=3), 0)
        e.aplicar(self.inicio(cuenta="999000002", nombre="Beto"), 1)
        self.assertEqual(e.guia["nombre"], "Beto")

    def test_el_progreso_de_otra_cuenta_sigue_ignorado(self):
        e = Estado()
        e.aplicar(enrol(cuenta="999000001", nombre="Ana", paso=3), 0)
        e.aplicar(enrol(cuenta="999000002", nombre="Beto", paso=2), 1)
        self.assertEqual(e.guia["nombre"], "Ana")

    def test_repetir_el_inicio_de_la_misma_cuenta_no_alarga_el_techo(self):
        e = Estado(guia_vence_s=330, guia_max_s=900)
        e.aplicar(self.inicio(), 0)
        for t in range(100, 900, 100):
            e.aplicar(self.inicio(), t)
        e.tick(900)
        self.assertIsNone(e.guia)

    def test_tras_el_techo_un_inicio_nuevo_si_abre(self):
        e = Estado(guia_max_s=900)
        e.aplicar(enrol(paso=2), 0)
        e.tick(900)
        e.aplicar(self.inicio(), 901)
        self.assertIsNotNone(e.guia)


class CredencialesRechazadas(unittest.TestCase):
    def test_se_marcan_y_se_limpian_al_conectar(self):
        e = Estado()
        self.assertFalse(e.credenciales_rechazadas)
        e.aplicar(("credenciales", True), 0)
        self.assertTrue(e.credenciales_rechazadas)
        e.aplicar(("conexion", True), 1)
        self.assertFalse(e.credenciales_rechazadas)

    def test_un_evento_de_credenciales_falso_la_limpia_sin_conexion(self):
        e = Estado()
        e.aplicar(("credenciales", True), 0)
        e.aplicar(("credenciales", False), 1)
        e.aplicar(("conexion", False), 1)
        self.assertFalse(e.credenciales_rechazadas)
        self.assertFalse(e.conectado)


class Enfriamiento(unittest.TestCase):
    def conectado(self):
        e = Estado()
        e.aplicar(("conexion", True), 0)
        return e

    def test_puerta_ignora_toques_seguidos(self):
        e = self.conectado()
        self.assertEqual(len(e.tocar("puerta", 10)), 1)
        self.assertEqual(e.tocar("puerta", 11.9), [])
        self.assertEqual(len(e.tocar("puerta", 12)), 1)

    def test_un_toque_bloqueado_lo_dice(self):
        e = self.conectado()
        e.tocar("puerta", 10)
        e.aviso = None
        e.tocar("puerta", 11)
        self.assertEqual(e.aviso[0], "Espera un momento")
        e.aviso = None
        e.tocar("luz", 20)
        e.tocar("luz", 20.5)                      # apagar: no se bloquea ni avisa
        e.tocar("luz", 21)                        # encender: bloqueado
        self.assertEqual(e.aviso[0], "Espera un momento")

    def test_si_el_envio_falla_el_reintento_inmediato_funciona(self):
        e = self.conectado()
        e.tocar("puerta", 10)
        e.fallo_envio(CMD_ACTUADOR, {"servo": "abrir"}, 10)
        self.assertEqual(len(e.tocar("puerta", 10.5)), 1)

    def test_si_falla_encender_la_luz_el_reintento_inmediato_funciona(self):
        e = self.conectado()
        e.tocar("luz", 10)
        e.fallo_envio(CMD_ACTUADOR, {"luz": 1}, 10)
        self.assertEqual(e.tocar("luz", 10.5), [(CMD_ACTUADOR, {"luz": 1})])

    def test_un_fallo_de_otra_orden_no_borra_el_enfriamiento_de_la_puerta(self):
        e = self.conectado()
        e.tocar("puerta", 10)
        e.fallo_envio("aula/sesion/cmd", {"accion": "terminar"}, 10)
        self.assertEqual(e.tocar("puerta", 10.5), [])

    def test_encender_la_luz_ignora_toques_seguidos(self):
        e = self.conectado()
        e.tocar("luz", 10)
        e.tocar("luz", 10.5)                    # apagar: nunca se bloquea
        self.assertIsNone(e.luz_hasta)
        self.assertEqual(e.tocar("luz", 11), [])    # encender otra vez, muy pronto
        self.assertIsNone(e.luz_hasta)
        self.assertEqual(e.tocar("luz", 12.5), [(CMD_ACTUADOR, {"luz": 1})])

    def test_apagar_la_luz_no_se_frena_por_el_enfriamiento(self):
        e = self.conectado()
        e.tocar("luz", 10)
        self.assertEqual(e.tocar("luz", 10.1), [(CMD_ACTUADOR, {"luz": 0})])
        self.assertIsNone(e.luz_hasta)

    def test_son_independientes_entre_si(self):
        e = self.conectado()
        e.tocar("puerta", 10)
        self.assertEqual(len(e.tocar("luz", 10.1)), 1)

    def test_un_toque_sin_conexion_no_gasta_el_enfriamiento(self):
        e = Estado()
        e.tocar("puerta", 10)
        e.aplicar(("conexion", True), 10.5)
        self.assertEqual(len(e.tocar("puerta", 11)), 1)

    def test_el_apagado_automatico_no_se_frena(self):
        e = self.conectado()
        e.tocar("luz", 10)
        self.assertEqual(e.tick(13), [(CMD_ACTUADOR, {"luz": 0})])


class Sucio(unittest.TestCase):
    def test_un_evento_repetido_no_ensucia(self):
        e = Estado()
        e.aplicar(("conexion", True), 0)
        e.aplicar(("sesion", {"activa": False, "id": 1}), 0)
        e.aplicar(("actuadores", {"luz": 0}), 0)
        e.aplicar(enrol(paso=2), 0)
        e.sucio = False
        e.aplicar(("conexion", True), 1)
        e.aplicar(("sesion", {"activa": False, "id": 1}), 1)
        e.aplicar(("actuadores", {"luz": 0}), 1)
        e.aplicar(enrol(paso=2), 1)
        self.assertFalse(e.sucio)

    def test_un_cambio_si_ensucia(self):
        casos = [("conexion", False), ("sesion", {"activa": True, "id": 1}),
                 ("actuadores", {"luz": 1}), enrol(paso=3)]
        for ev in casos:
            with self.subTest(ev=ev[0]):
                e = Estado()
                e.aplicar(("conexion", True), 0)
                e.aplicar(("sesion", {"activa": False, "id": 1}), 0)
                e.aplicar(("actuadores", {"luz": 0}), 0)
                e.aplicar(enrol(paso=2), 0)
                e.sucio = False
                e.aplicar(ev, 1)
                self.assertTrue(e.sucio)

    def test_un_mensaje_repetido_si_renueva_el_plazo(self):
        e = Estado(guia_vence_s=330)
        e.aplicar(enrol(paso=2), 0)
        e.aplicar(enrol(paso=2), 300)
        e.tick(400)
        self.assertEqual(e.vista_visible, "guia")


class ValidacionDeSesion(unittest.TestCase):
    def sesion(self, **campos):
        d = {"activa": True}
        d.update(campos)
        return interpretar(SESION, j(d), False)[1]

    def test_inicio_fuera_de_2000_a_2100_se_descarta(self):
        for iso in ("0001-01-01T00:00:00+14:00", "9999-12-31T23:59:59-14:00",
                    "1999-12-31T23:59:59Z", "2101-01-01T00:00:00Z", "ayer", 5):
            with self.subTest(iso=iso):
                self.assertIsNone(self.sesion(inicio=iso)["inicio"])

    def test_inicio_valido_se_conserva(self):
        iso = "2026-11-04T09:00:00-06:00"
        self.assertEqual(self.sesion(inicio=iso)["inicio"], iso)

    def test_tol_min_en_rango(self):
        self.assertEqual(self.sesion(tol_min=0)["tol_min"], 0)
        self.assertEqual(self.sesion(tol_min=600)["tol_min"], 600)
        for v in (-1, 601, 10**12, "10", True, 1.5):
            with self.subTest(v=v):
                self.assertIsNone(self.sesion(tol_min=v)["tol_min"])

    def test_id_en_rango(self):
        self.assertEqual(self.sesion(id=1)["id"], 1)
        self.assertEqual(self.sesion(id=2**31 - 1)["id"], 2**31 - 1)
        for v in (0, -3, 2**31, 10**30, "1", True):
            with self.subTest(v=v):
                self.assertIsNone(self.sesion(id=v)["id"])


class LimpiarTexto(unittest.TestCase):
    def test_deja_el_texto_normal(self):
        self.assertEqual(estado.limpiar_texto("Ana María López Ñ"), "Ana María López Ñ")
        self.assertEqual(estado.limpiar_texto("e\u0301"), "e\u0301")      # 1 marca

    def test_sustituye_controles_y_separadores_por_espacio(self):
        for ch in ("\n", "\r", "\t", "\x00", "\x1b", "\u202e", "\u200b",
                   "\u2028", "\u2029", "\ufeff", "\ue000", "\x85"):
            with self.subTest(ch=repr(ch)):
                self.assertEqual(estado.limpiar_texto("a%sb" % ch), "a b")

    def test_limita_las_marcas_combinantes_seguidas(self):
        r = estado.limpiar_texto("a" + "\u0301" * 50)
        self.assertEqual(r, "a" + "\u0301" * estado.MAX_MARCAS)

    def test_el_contador_de_marcas_se_reinicia(self):
        r = estado.limpiar_texto(("a" + "\u0301" * 5) * 2)
        self.assertEqual(r, ("a" + "\u0301" * estado.MAX_MARCAS) * 2)

    def test_se_aplica_a_todos_los_textos_que_llegan(self):
        d = interpretar(ENROL, j({"paso": 1, "total": 5, "estado": "aceptada",
                                  "nombre": "A\nB\u202eC", "pose": "fre\nnte",
                                  "motivo": "x\u2028y", "cuenta": "9\n9"}),
                        False)[1]
        for campo in ("nombre", "pose", "motivo", "cuenta"):
            self.assertNotRegex(d[campo], r"[\n\u202e\u2028]")
        s = interpretar(SESION, j({"activa": True, "materia": "F\nSE",
                                   "grupo": "1\u202e"}), False)[1]
        self.assertEqual((s["materia"], s["grupo"]), ("F SE", "1 "))
        a = interpretar(ACTUADOR, j({"servo": "ce\nrrado"}), False)[1]
        self.assertEqual(a["servo"], "ce rrado")


if __name__ == "__main__":
    unittest.main()
