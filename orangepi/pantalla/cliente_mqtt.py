"""
Cliente MQTT de la pantalla (paho-mqtt), con el usuario `pantalla` del contrato.

Los callbacks corren en el hilo de red de paho: aqui solo se interpretan los
mensajes y se dejan en una cola (queue.SimpleQueue); el bucle principal los
aplica. Los eventos son (tipo, datos) como los de estado.interpretar, mas
("conexion", True o False).

La clave no se registra nunca. El broker usa el usuario como client id
(use_username_as_clientid): dos programas con el usuario `pantalla` se expulsan
entre si, asi que solo puede haber una instancia.
"""

import json
import queue
import sys
import time

import paho.mqtt.client as mqtt

from estado import SUSCRIPCIONES, interpretar

USUARIO = "pantalla"
KEEPALIVE_S = 30
RECONEXION_MIN_S = 1
RECONEXION_MAX_S = 30
# Lo que paho llama a un CONNACK que rechaza el usuario o la clave (MQTT 3.1.1)
CREDENCIALES_RECHAZADAS = ("Bad user name or password", "Not authorized")
# Con la cola llena se expulsa primero lo que menos duele perder (el progreso del
# registro se corrige con el siguiente mensaje, un estado de actuadores o de
# asistencia igual) y al final la sesion y el reinicio.
ORDEN_DE_EXPULSION = ("enrolamiento", "actuadores", "asistencia", "conexion",
                      "credenciales", "sesion", "reinicio")
RESUSCRIBIR_MIN_S = 5.0     # entre dos suscripciones de recuperacion


def _crear_paho():
    # MQTT 3.1.1 como los demas clientes del proyecto (el contrato, sección 1,
    # describe su limitacion con el retain)
    return mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=USUARIO,
                       protocol=mqtt.MQTTv311)


class ClienteMQTT:
    def __init__(self, host, puerto, clave, eventos, crear=None):
        self._host, self._puerto = host, puerto
        self._eventos = eventos
        self._conectado = False
        self._cerrando = False
        self._cola_llena = False
        self._ultima_resuscripcion = None
        self._ultimo_aviso = None
        self._ultima_publicacion = None
        self._client = (crear or _crear_paho)()
        self._client.username_pw_set(USUARIO, clave)
        self._client.on_connect = self._al_conectar
        self._client.on_connect_fail = self._al_fallar
        self._client.on_disconnect = self._al_desconectar
        self._client.on_subscribe = self._al_suscribir
        self._client.on_message = self._al_mensaje

    @property
    def conectado(self):
        return self._conectado

    def iniciar(self):
        """Empieza a conectar en segundo plano; reintenta solo, sin limite."""
        self._client.reconnect_delay_set(RECONEXION_MIN_S, RECONEXION_MAX_S)
        self._client.connect_async(self._host, self._puerto, KEEPALIVE_S)
        self._client.loop_start()

    def publicar(self, topico, datos):
        """Publica un dict como JSON (QoS 1, sin retain). False si no se pudo.

        Sin conexion no publica: con QoS 1 paho encolaria el mensaje y lo
        enviaria al reconectar, y una orden de hace minutos no debe ejecutarse.

        Ventana de carrera aceptada: entre mirar `_conectado` y llamar a
        `publish` la conexion puede caerse (milisegundos); en ese caso paho
        encola el mensaje y lo envia al reconectar. No se cierra: paho no ofrece
        una forma publica de quitarlo de su cola, y la alternativa (QoS 0)
        perderia ordenes del contrato (QoS 1) sin acuse.
        """
        if not self._conectado:
            return False
        info = self._client.publish(topico, json.dumps(datos), qos=1, retain=False)
        if info.rc != mqtt.MQTT_ERR_SUCCESS:
            return False
        self._ultima_publicacion = info
        return True

    def detener(self, espera=1.0):
        """Cierre limpio: espera lo ultimo publicado, se desconecta y para el hilo.

        Devuelve False si lo ultimo que se publico no llego a confirmarse.
        """
        self._cerrando = True
        confirmada = True
        try:
            if self._ultima_publicacion is not None:
                self._ultima_publicacion.wait_for_publish(timeout=espera)
                confirmada = bool(self._ultima_publicacion.is_published())
        except (RuntimeError, ValueError):
            confirmada = False
        self._client.disconnect()
        self._client.loop_stop()
        return confirmada

    # ----- Callbacks de paho (hilo de red) -----

    def _poner(self, evento, imprescindible=False):
        """Deja un evento en la cola sin bloquear nunca el hilo de red.

        Con la cola llena se descarta el evento y se avisa una vez por racha; los
        de conexion (`imprescindible`) hacen sitio expulsando el menos valioso.
        Al terminar una racha de descartes se vuelve a suscribir: el broker
        reenvia los estados retenidos que pudieron perderse.
        """
        try:
            self._eventos.put_nowait(evento)
        except queue.Full:
            if imprescindible:
                self._hacer_sitio()
                try:
                    self._eventos.put_nowait(evento)
                except queue.Full:
                    pass
            elif not self._cola_llena:
                self._cola_llena = True
                print("pantalla: la cola de mensajes está llena; se descartan los "
                      "que lleguen hasta que se atienda", file=sys.stderr)
            return
        if self._cola_llena:
            self._cola_llena = False
            self._resuscribir()

    def _hacer_sitio(self):
        """Quita de la cola el evento mas viejo del tipo menos valioso."""
        cola = self._eventos
        mutex = getattr(cola, "mutex", None)
        if mutex is None:       # una cola que no es queue.Queue: lo mas viejo
            try:
                cola.get_nowait()
            except queue.Empty:
                pass
            return
        with mutex:
            for tipo in ORDEN_DE_EXPULSION:
                for i, (t, _) in enumerate(cola.queue):
                    if t == tipo:
                        del cola.queue[i]
                        return
            if cola.queue:
                cola.queue.popleft()

    def _resuscribir(self):
        ahora = time.monotonic()
        if self._conectado and (self._ultima_resuscripcion is None or
                                ahora - self._ultima_resuscripcion
                                >= RESUSCRIBIR_MIN_S):
            self._ultima_resuscripcion = ahora
            self._client.subscribe(list(SUSCRIPCIONES))

    def _avisar(self, texto):
        # Cada reintento fallido repite el mismo motivo: se registra una vez.
        if texto != self._ultimo_aviso:
            self._ultimo_aviso = texto
            print("pantalla: " + texto, file=sys.stderr)

    def _al_conectar(self, client, userdata, flags, reason_code, properties):
        if reason_code.is_failure:
            self._conectado = False
            if str(reason_code) in CREDENCIALES_RECHAZADAS:
                self._avisar("MQTT: el broker rechazó las credenciales del "
                             "usuario %s (%s); revisa la clave; se reintenta"
                             % (USUARIO, reason_code))
                self._poner(("credenciales", True), imprescindible=True)
            else:
                self._avisar("MQTT: el broker rechazó la conexión (%s); se "
                             "reintenta" % reason_code)
                self._poner(("credenciales", False), imprescindible=True)
            self._poner(("conexion", False), imprescindible=True)
            return
        self._conectado = True
        self._avisar("MQTT conectado a %s:%d" % (self._host, self._puerto))
        client.subscribe(list(SUSCRIPCIONES))
        self._poner(("conexion", True), imprescindible=True)

    def _al_fallar(self, client, userdata):
        self._conectado = False
        self._avisar("MQTT: no se pudo conectar a %s:%d; se reintenta"
                     % (self._host, self._puerto))
        self._poner(("credenciales", False), imprescindible=True)
        self._poner(("conexion", False), imprescindible=True)

    def _al_desconectar(self, client, userdata, disconnect_flags, reason_code,
                        properties):
        # Tras un CONNACK rechazado paho tambien llama aqui: ya se aviso y ya
        # se mando el evento.
        if not self._conectado:
            return
        self._conectado = False
        if self._cerrando:
            return
        self._avisar("MQTT: sin conexión (%s); se reintenta" % reason_code)
        self._poner(("credenciales", False), imprescindible=True)
        self._poner(("conexion", False), imprescindible=True)

    def _al_suscribir(self, client, userdata, mid, reason_codes, properties):
        if any(r.is_failure for r in reason_codes):
            self._avisar("MQTT: el broker denegó una suscripción (%s)"
                         % ", ".join(str(r) for r in reason_codes))

    def _al_mensaje(self, client, userdata, mensaje):
        # Una excepcion aqui mataria el hilo de red de paho: se registra el
        # tipo y el topico, nunca el contenido (trae nombres de alumnos).
        try:
            evento = interpretar(mensaje.topic, mensaje.payload, mensaje.retain)
        except Exception as e:
            print("pantalla: error al leer un mensaje de %s (%s)"
                  % (mensaje.topic, type(e).__name__), file=sys.stderr)
            return
        if evento is not None:
            self._poner(evento)
