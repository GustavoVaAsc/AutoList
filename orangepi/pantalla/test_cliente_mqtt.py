"""
Pruebas de cliente_mqtt.py sin red: el cliente de paho es un objeto falso que
registra lo que se le pide y deja llamar a mano los callbacks.

Ejecutar desde orangepi/pantalla:  .venv/bin/python -m unittest -v
"""

import io
import json
import queue
import socket
import time
import unittest
from contextlib import redirect_stderr
from unittest import mock

import paho.mqtt.client as paho

import cliente_mqtt
import estado
from cliente_mqtt import ClienteMQTT

CLAVE = "clave-ficticia-de-prueba-123"


class Razon:
    """Parecido a paho.ReasonCode: solo lo que usa el cliente."""

    def __init__(self, texto, fallo):
        self._texto, self.is_failure = texto, fallo

    def __str__(self):
        return self._texto


EXITO = Razon("Success", False)
RECHAZO = Razon("Not authorized", True)
CLAVE_MALA = Razon("Bad user name or password", True)
SIN_SERVICIO = Razon("Server unavailable", True)


class Info:
    def __init__(self, rc=0):
        self.rc = rc
        self.esperas = []
        self.publicada = True

    def wait_for_publish(self, timeout=None):
        self.esperas.append(timeout)

    def is_published(self):
        return self.publicada


class PahoFalso:
    def __init__(self):
        self.llamadas = []
        self.conectado = False
        self.rc_publicar = 0
        self.info = None

    def _reg(self, nombre, *args, **kw):
        self.llamadas.append((nombre, args, kw))

    def username_pw_set(self, *a, **k):
        self._reg("username_pw_set", *a, **k)

    def reconnect_delay_set(self, *a, **k):
        self._reg("reconnect_delay_set", *a, **k)

    def connect_async(self, *a, **k):
        self._reg("connect_async", *a, **k)

    def loop_start(self):
        self._reg("loop_start")

    def loop_stop(self):
        self._reg("loop_stop")

    def disconnect(self):
        self._reg("disconnect")

    def subscribe(self, *a, **k):
        self._reg("subscribe", *a, **k)

    def publish(self, topico, payload=None, qos=0, retain=False):
        self._reg("publish", topico, payload, qos=qos, retain=retain)
        self.info = Info(self.rc_publicar)
        return self.info

    def nombres(self):
        return [n for n, _, _ in self.llamadas]

    def de(self, nombre):
        return [(a, k) for n, a, k in self.llamadas if n == nombre]


def mensaje(topico, datos, retain=False):
    payload = datos if isinstance(datos, bytes) else json.dumps(datos).encode()
    return mock.Mock(topic=topico, payload=payload, retain=retain)


class Base(unittest.TestCase):
    def setUp(self):
        self.paho = PahoFalso()
        self.cola = queue.SimpleQueue()
        self.err = io.StringIO()
        self.cliente = ClienteMQTT("127.0.0.1", 1883, CLAVE, self.cola,
                                   crear=lambda: self.paho)
        # los callbacks que registro el cliente, como los llamaria paho
        self.cb = self.cliente._client

    def eventos(self):
        salida = []
        while not self.cola.empty():
            salida.append(self.cola.get())
        return salida

    def conectar(self):
        with redirect_stderr(self.err):
            self.paho.on_connect(self.paho, None, {}, EXITO, None)
        self.eventos()


class Arranque(Base):
    def test_credenciales_y_reconexion(self):
        self.cliente.iniciar()
        self.assertEqual(self.paho.de("username_pw_set"),
                         [(("pantalla", CLAVE), {})])
        self.assertEqual(self.paho.nombres(),
                         ["username_pw_set", "reconnect_delay_set",
                          "connect_async", "loop_start"])
        args, kw = self.paho.de("connect_async")[0]
        self.assertEqual(args[:2], ("127.0.0.1", 1883))

    def test_cliente_real_usa_mqtt_311_y_el_usuario_como_id(self):
        real = ClienteMQTT("127.0.0.1", 1883, CLAVE, self.cola)._client
        self.assertEqual(real._client_id, b"pantalla")
        self.assertEqual(real._protocol, paho.MQTTv311)
        self.assertFalse(real.is_connected())

    def test_nunca_registra_la_clave(self):
        with redirect_stderr(self.err):
            self.cliente.iniciar()
            self.paho.on_connect(self.paho, None, {}, RECHAZO, None)
            self.paho.on_connect_fail(self.paho, None)
            self.paho.on_disconnect(self.paho, None, {}, RECHAZO, None)
        self.assertNotIn(CLAVE, self.err.getvalue())


class Conexion(Base):
    def test_al_conectar_se_suscribe_y_avisa(self):
        with redirect_stderr(self.err):
            self.paho.on_connect(self.paho, None, {}, EXITO, None)
        self.assertEqual(self.paho.de("subscribe"),
                         [((list(estado.SUSCRIPCIONES),), {})])
        self.assertEqual(self.eventos(), [("conexion", True)])
        self.assertTrue(self.cliente.conectado)

    def test_conexion_rechazada(self):
        with redirect_stderr(self.err):
            self.paho.on_connect(self.paho, None, {}, RECHAZO, None)
        self.assertEqual(self.paho.de("subscribe"), [])
        self.assertEqual(self.eventos(),
                         [("credenciales", True), ("conexion", False)])
        self.assertFalse(self.cliente.conectado)
        self.assertIn("credenciales", self.err.getvalue())

    def test_clave_o_usuario_incorrectos_tambien_son_credenciales(self):
        with redirect_stderr(self.err):
            self.paho.on_connect(self.paho, None, {}, CLAVE_MALA, None)
        self.assertEqual(self.eventos(),
                         [("credenciales", True), ("conexion", False)])

    def test_otro_rechazo_no_es_de_credenciales(self):
        with redirect_stderr(self.err):
            self.paho.on_connect(self.paho, None, {}, SIN_SERVICIO, None)
        self.assertEqual(self.eventos(),
                         [("credenciales", False), ("conexion", False)])
        self.assertIn("Server unavailable", self.err.getvalue())

    def test_con_los_codigos_reales_de_paho(self):
        # los nombres que paho pone a un CONNACK de MQTT 3.1.1
        for nombre, esperado in (("Not authorized", True),
                                 ("Bad user name or password", True),
                                 ("Server unavailable", False)):
            with self.subTest(nombre=nombre):
                codigo = paho.convert_connack_rc_to_reason_code(
                    {"Not authorized": paho.CONNACK_REFUSED_NOT_AUTHORIZED,
                     "Bad user name or password":
                         paho.CONNACK_REFUSED_BAD_USERNAME_PASSWORD,
                     "Server unavailable":
                         paho.CONNACK_REFUSED_SERVER_UNAVAILABLE}[nombre])
                with redirect_stderr(self.err):
                    self.paho.on_connect(self.paho, None, {}, codigo, None)
                self.assertEqual(("credenciales", True) in self.eventos(), esperado)

    def test_un_fallo_de_red_limpia_la_marca_de_credenciales(self):
        # tras "credenciales rechazadas", si ahora el broker ni responde, la
        # pantalla debe decir "Sin conexion" y no seguir culpando a la clave
        with redirect_stderr(self.err):
            self.paho.on_connect(self.paho, None, {}, RECHAZO, None)
            self.paho.on_connect_fail(self.paho, None)
        self.assertEqual(self.eventos()[-2:],
                         [("credenciales", False), ("conexion", False)])

    def test_un_rechazo_de_credenciales_sigue_marcandolas(self):
        with redirect_stderr(self.err):
            self.paho.on_connect(self.paho, None, {}, RECHAZO, None)
            self.paho.on_disconnect(self.paho, None, {}, Razon("x", True), None)
        self.assertNotIn(("credenciales", False), self.eventos())

    def test_al_conectar_de_nuevo_no_manda_el_evento_de_credenciales(self):
        with redirect_stderr(self.err):
            self.paho.on_connect(self.paho, None, {}, RECHAZO, None)
        self.eventos()
        self.conectar()     # vacia la cola
        with redirect_stderr(self.err):
            self.paho.on_connect(self.paho, None, {}, EXITO, None)
        self.assertEqual(self.eventos(), [("conexion", True)])

    def test_se_cae_la_conexion(self):
        self.conectar()
        with redirect_stderr(self.err):
            self.paho.on_disconnect(self.paho, None, {}, Razon("Lost", True), None)
        self.assertEqual(self.eventos(),
                         [("credenciales", False), ("conexion", False)])
        self.assertFalse(self.cliente.conectado)

    def test_la_desconexion_tras_un_rechazo_no_repite_el_aviso(self):
        # paho llama a on_disconnect despues de un CONNACK rechazado
        with redirect_stderr(self.err):
            self.paho.on_connect(self.paho, None, {}, RECHAZO, None)
            avisos = self.err.getvalue()
            self.paho.on_disconnect(self.paho, None, {}, Razon("Unspecified", True), None)
        self.assertEqual(self.err.getvalue(), avisos)
        self.assertEqual(self.eventos(),
                         [("credenciales", True), ("conexion", False)])

    def test_no_se_pudo_conectar(self):
        with redirect_stderr(self.err):
            self.paho.on_connect_fail(self.paho, None)
        self.assertEqual(self.eventos(),
                         [("credenciales", False), ("conexion", False)])

    def test_reconectar_vuelve_a_suscribirse(self):
        self.conectar()
        self.conectar()
        self.assertEqual(len(self.paho.de("subscribe")), 2)

    def test_el_mismo_fallo_se_registra_una_sola_vez(self):
        with redirect_stderr(self.err):
            for _ in range(5):
                self.paho.on_connect_fail(self.paho, None)
        self.assertEqual(len(self.err.getvalue().splitlines()), 1)

    def test_suscripcion_denegada_se_registra(self):
        with redirect_stderr(self.err):
            self.paho.on_subscribe(self.paho, None, 1, [Razon("Not authorized", True)], None)
        self.assertIn("suscripción", self.err.getvalue())


class Mensajes(Base):
    def test_mensaje_valido_llega_a_la_cola(self):
        self.paho.on_message(self.paho, None,
                             mensaje("aula/sesion/estado", {"activa": False}))
        self.assertEqual(self.eventos()[0][0], "sesion")

    def test_retenido_en_topico_sin_retain_no_llega(self):
        self.paho.on_message(self.paho, None, mensaje(
            "aula/pantalla/reinicio", {"reiniciar": 1}, retain=True))
        self.assertEqual(self.eventos(), [])

    def test_reinicio_en_vivo_llega(self):
        self.paho.on_message(self.paho, None,
                             mensaje("aula/pantalla/reinicio", {"reiniciar": 1}))
        self.assertEqual(self.eventos(), [("reinicio", None)])

    def test_basura_no_llega_ni_lanza(self):
        for p in (b"", b"\xff\xfe", b"{", b"[]"):
            self.paho.on_message(self.paho, None,
                                 mensaje("aula/vision/enrolamiento", p))
        self.assertEqual(self.eventos(), [])

    def test_un_error_inesperado_no_mata_el_hilo_de_red(self):
        with mock.patch.object(cliente_mqtt, "interpretar",
                               side_effect=RuntimeError("bug")), \
                redirect_stderr(self.err):
            self.paho.on_message(self.paho, None,
                                 mensaje("aula/sesion/estado", {"activa": True}))
        self.assertEqual(self.eventos(), [])
        self.assertIn("RuntimeError", self.err.getvalue())
        self.assertNotIn("bug", self.err.getvalue())    # solo el tipo, no el texto

    def test_no_registra_el_contenido(self):
        with redirect_stderr(self.err):
            self.paho.on_message(self.paho, None, mensaje(
                "aula/vision/enrolamiento", b"Ana Lopez 999000001 {"))
        self.assertNotIn("Ana", self.err.getvalue())
        self.assertNotIn("999000001", self.err.getvalue())


class ColaAcotada(unittest.TestCase):
    def armar(self, maximo=3):
        paho_falso = PahoFalso()
        cola = queue.Queue(maxsize=maximo)
        err = io.StringIO()
        ClienteMQTT("127.0.0.1", 1883, CLAVE, cola, crear=lambda: paho_falso)
        return paho_falso, cola, err

    def inundar(self, paho_falso, n):
        for i in range(n):
            paho_falso.on_message(paho_falso, None, mensaje(
                "aula/actuador/estado", {"servo": "cerrado", "luz": i % 2}))

    def test_si_se_llena_descarta_y_avisa_una_sola_vez(self):
        paho_falso, cola, err = self.armar(3)
        with redirect_stderr(err):
            self.inundar(paho_falso, 50)
        self.assertEqual(cola.qsize(), 3)
        avisos = [l for l in err.getvalue().splitlines() if "descart" in l]
        self.assertEqual(len(avisos), 1)

    def test_tras_vaciarla_vuelve_a_recibir_y_puede_avisar_de_nuevo(self):
        paho_falso, cola, err = self.armar(2)
        with redirect_stderr(err):
            self.inundar(paho_falso, 10)
            while not cola.empty():
                cola.get_nowait()
            self.inundar(paho_falso, 1)
            self.assertEqual(cola.qsize(), 1)
            self.inundar(paho_falso, 10)
        avisos = [l for l in err.getvalue().splitlines() if "descart" in l]
        self.assertEqual(len(avisos), 2)

    def test_un_evento_de_conexion_no_se_pierde_con_la_cola_llena(self):
        paho_falso, cola, err = self.armar(2)
        with redirect_stderr(err):
            self.inundar(paho_falso, 10)
            paho_falso.on_connect(paho_falso, None, {}, EXITO, None)
        eventos = []
        while not cola.empty():
            eventos.append(cola.get_nowait())
        self.assertIn(("conexion", True), eventos)

    def llena_con(self, eventos):
        paho_falso = PahoFalso()
        cola = queue.Queue(maxsize=len(eventos))
        cliente = ClienteMQTT("127.0.0.1", 1883, CLAVE, cola,
                              crear=lambda: paho_falso)
        for ev in eventos:
            cola.put_nowait(ev)
        return paho_falso, cola, cliente

    def contenido(self, cola):
        return [e[0] for e in list(cola.queue)]

    def test_al_hacer_sitio_se_expulsa_primero_el_enrolamiento_mas_viejo(self):
        paho_falso, cola, _ = self.llena_con([
            ("sesion", {}), ("enrolamiento", {"paso": 1}), ("reinicio", None),
            ("enrolamiento", {"paso": 2})])
        with redirect_stderr(io.StringIO()):
            paho_falso.on_connect(paho_falso, None, {}, EXITO, None)
        self.assertEqual(self.contenido(cola),
                         ["sesion", "reinicio", "enrolamiento", "conexion"])
        self.assertEqual(list(cola.queue)[2][1]["paso"], 2)     # quedo el mas nuevo

    def test_sin_enrolamientos_se_expulsa_actuadores_y_luego_asistencia(self):
        paho_falso, cola, _ = self.llena_con([
            ("sesion", {}), ("asistencia", {"activa": False}),
            ("actuadores", {"luz": 1}), ("reinicio", None)])
        with redirect_stderr(io.StringIO()):
            paho_falso.on_connect(paho_falso, None, {}, EXITO, None)
        self.assertEqual(self.contenido(cola),
                         ["sesion", "asistencia", "reinicio", "conexion"])
        with redirect_stderr(io.StringIO()):
            paho_falso.on_connect(paho_falso, None, {}, EXITO, None)
        self.assertEqual(self.contenido(cola),
                         ["sesion", "reinicio", "conexion", "conexion"])

    def test_reinicio_y_sesion_son_lo_ultimo_que_se_expulsa(self):
        paho_falso, cola, _ = self.llena_con([
            ("reinicio", None), ("sesion", {"n": 1}), ("sesion", {"n": 2})])
        with redirect_stderr(io.StringIO()):
            paho_falso.on_connect(paho_falso, None, {}, EXITO, None)
        self.assertIn("reinicio", self.contenido(cola))

    def test_tras_una_racha_de_descartes_se_vuelve_a_suscribir(self):
        paho_falso, cola, err = self.armar(2)
        with redirect_stderr(err):
            paho_falso.on_connect(paho_falso, None, {}, EXITO, None)
            while not cola.empty():
                cola.get_nowait()
            self.inundar(paho_falso, 10)            # racha de descartes
            self.assertEqual(len(paho_falso.de("subscribe")), 1)
            while not cola.empty():
                cola.get_nowait()
            self.inundar(paho_falso, 1)             # la racha termina
        subs = paho_falso.de("subscribe")
        self.assertEqual(len(subs), 2)
        self.assertEqual(subs[1], subs[0])          # los mismos tópicos y QoS

    def test_no_se_suscribe_de_nuevo_sin_racha_ni_sin_conexion(self):
        paho_falso, cola, err = self.armar(2)
        with redirect_stderr(err):
            self.inundar(paho_falso, 10)            # racha, pero sin conexion
            while not cola.empty():
                cola.get_nowait()
            self.inundar(paho_falso, 1)
            paho_falso.on_connect(paho_falso, None, {}, EXITO, None)
            while not cola.empty():
                cola.get_nowait()
            self.inundar(paho_falso, 1)             # sin racha
        self.assertEqual(len(paho_falso.de("subscribe")), 1)    # solo el de on_connect

    def test_las_resuscripciones_tienen_un_intervalo_minimo(self):
        paho_falso, cola, err = self.armar(2)
        with redirect_stderr(err):
            paho_falso.on_connect(paho_falso, None, {}, EXITO, None)
            for _ in range(3):
                while not cola.empty():
                    cola.get_nowait()
                self.inundar(paho_falso, 10)
                while not cola.empty():
                    cola.get_nowait()
                self.inundar(paho_falso, 1)
        self.assertEqual(len(paho_falso.de("subscribe")), 2)    # 1 + una sola

    def test_el_hilo_de_red_no_se_bloquea(self):
        paho_falso, cola, err = self.armar(1)
        with redirect_stderr(err):
            self.inundar(paho_falso, 5)     # con put() bloqueante esto se colgaria


class Publicar(Base):
    def test_sin_conexion_no_publica(self):
        # con QoS 1 paho encola y lo enviaria al reconectar: una orden vieja
        self.assertFalse(self.cliente.publicar("aula/actuador/cmd", {"servo": "abrir"}))
        self.assertEqual(self.paho.de("publish"), [])

    def test_publica_json_qos1_sin_retain(self):
        self.conectar()
        self.assertTrue(self.cliente.publicar("aula/actuador/cmd", {"servo": "abrir"}))
        (args, kw), = self.paho.de("publish")
        self.assertEqual(args[0], "aula/actuador/cmd")
        self.assertEqual(json.loads(args[1]), {"servo": "abrir"})
        self.assertEqual((kw["qos"], kw["retain"]), (1, False))

    def test_error_de_paho(self):
        self.conectar()
        self.paho.rc_publicar = 4
        self.assertFalse(self.cliente.publicar("aula/actuador/cmd", {"luz": 0}))

    def test_acentos_viajan_bien(self):
        self.conectar()
        self.cliente.publicar("aula/sesion/cmd", {"materia": "Física"})
        args, _ = self.paho.de("publish")[0]
        self.assertEqual(json.loads(args[1])["materia"], "Física")


class Detener(Base):
    def test_cierre_limpio(self):
        self.conectar()
        self.cliente.detener()
        self.assertEqual(self.paho.nombres()[-2:], ["disconnect", "loop_stop"])

    def test_espera_la_ultima_publicacion(self):
        self.conectar()
        self.cliente.publicar("aula/actuador/cmd", {"luz": 0})
        info = self.paho.info
        self.cliente.detener(espera=1.0)
        self.assertEqual(info.esperas, [1.0])
        self.assertLess(self.paho.nombres().index("publish"),
                        self.paho.nombres().index("disconnect"))

    def test_la_desconexion_al_cerrar_no_dice_que_reintenta(self):
        self.conectar()
        antes = self.err.getvalue()
        self.cliente.detener()
        with redirect_stderr(self.err):
            self.paho.on_disconnect(self.paho, None, {}, Razon("Normal", False), None)
        self.assertEqual(self.err.getvalue(), antes)

    def test_sin_publicaciones_no_espera(self):
        self.cliente.detener()
        self.assertIn("disconnect", self.paho.nombres())

    def test_devuelve_si_lo_publicado_se_confirmo(self):
        self.conectar()
        self.assertTrue(self.cliente.detener())          # nada pendiente

    def test_devuelve_false_si_la_ultima_publicacion_no_se_confirmo(self):
        self.conectar()
        self.cliente.publicar("aula/actuador/cmd", {"luz": 0})
        self.paho.info.publicada = False
        self.assertFalse(self.cliente.detener())

    def test_espera_que_falla_no_impide_cerrar(self):
        self.conectar()
        self.cliente.publicar("aula/actuador/cmd", {"luz": 0})
        self.paho.info.wait_for_publish = mock.Mock(side_effect=RuntimeError("x"))
        self.cliente.detener()
        self.assertEqual(self.paho.nombres()[-1], "loop_stop")


class ContraUnPuertoCerrado(unittest.TestCase):
    """Cliente de paho de verdad, sin broker: debe seguir reintentando, no
    publicar y poder cerrarse sin colgarse."""

    def puerto_cerrado(self):
        s = socket.socket()
        s.bind(("127.0.0.1", 0))
        puerto = s.getsockname()[1]
        s.close()
        return puerto

    def test_no_publica_y_se_cierra_a_tiempo(self):
        cola = queue.SimpleQueue()
        cliente = ClienteMQTT("127.0.0.1", self.puerto_cerrado(), CLAVE, cola)
        err = io.StringIO()
        with redirect_stderr(err):
            cliente.iniciar()
            self.assertEqual(cola.get(timeout=5), ("credenciales", False))
            self.assertEqual(cola.get(timeout=5), ("conexion", False))
            self.assertFalse(cliente.conectado)
            self.assertFalse(cliente.publicar("aula/actuador/cmd", {"servo": "abrir"}))
            inicio = time.monotonic()
            self.assertTrue(cliente.detener())
            self.assertLess(time.monotonic() - inicio, 3)
        self.assertNotIn(CLAVE, err.getvalue())


if __name__ == "__main__":
    unittest.main()
