"""
Estado de la pantalla y lectura de los mensajes MQTT.

No toca hardware ni red: main.py le pasa los eventos que llegan del broker y
los toques ya convertidos en botones, y publica lo que este modulo devuelve.
Tampoco usa hilos: el cliente MQTT deja los eventos en una cola y el bucle
principal los aplica aqui, asi que Estado solo se usa desde un hilo.

Tópicos, payloads y ACL: docs/contrato-mqtt.md (manda sobre el plan).
"""

import json
import unicodedata
from datetime import datetime

TOPICO_SESION_ESTADO = "aula/sesion/estado"
TOPICO_ACTUADOR_ESTADO = "aula/actuador/estado"
TOPICO_ENROLAMIENTO = "aula/vision/enrolamiento"
TOPICO_REINICIO = "aula/pantalla/reinicio"
TOPICO_SESION_CMD = "aula/sesion/cmd"
TOPICO_ACTUADOR_CMD = "aula/actuador/cmd"

# (tópico, QoS) que lee el usuario `pantalla` segun la ACL; el QoS es el del
# contrato (sección 2).
SUSCRIPCIONES = (
    (TOPICO_SESION_ESTADO, 1),
    (TOPICO_ACTUADOR_ESTADO, 1),
    (TOPICO_ENROLAMIENTO, 0),
    (TOPICO_REINICIO, 1),
)

# Tópicos con Retain = no en el contrato: se ignora todo mensaje con la bandera
# retain (contrato, sección 1). Los dos de estado van retenidos a proposito:
# asi se recupera lo ultimo que se sabe al conectar.
SIN_RETAIN = (TOPICO_ENROLAMIENTO, TOPICO_REINICIO)

MAX_PAYLOAD = 65536     # el progreso del registro puede traer una miniatura
MAX_TEXTO = 80
MAX_PASOS = 20
MAX_MARCAS = 2          # marcas combinantes seguidas que se conservan
ESTADOS_FINALES = ("completado", "cancelado", "error")


def _entero(v):
    return isinstance(v, int) and not isinstance(v, bool)


def limpiar_texto(texto):
    """Texto seguro para dibujar: los caracteres de control, de formato (como
    U+202E), privados, sin asignar y los separadores de linea y de parrafo se
    sustituyen por un espacio, y solo se conservan MAX_MARCAS marcas combinantes
    seguidas (una pila de marcas sobre una letra se sale de su renglon)."""
    salida, marcas = [], 0
    for c in texto:
        categoria = unicodedata.category(c)
        if categoria[0] == "C" or categoria in ("Zl", "Zp"):
            salida.append(" ")
            marcas = 0
            continue
        if categoria[0] == "M":
            marcas += 1
            if marcas > MAX_MARCAS:
                continue
        else:
            marcas = 0
        salida.append(c)
    return "".join(salida)


def _texto(v):
    """Texto limpio y recortado, o None si no es un string."""
    if not isinstance(v, str):
        return None
    return limpiar_texto(v[:MAX_TEXTO * 4])[:MAX_TEXTO]


def _inicio(v):
    """La fecha de inicio si es ISO 8601 y cae entre 2000 y 2100; si no, None."""
    if not isinstance(v, str):
        return None
    try:
        fecha = datetime.fromisoformat(v)
    except ValueError:
        return None
    return v[:MAX_TEXTO] if 2000 <= fecha.year <= 2100 else None


def _sesion(d):
    if not isinstance(d.get("activa"), bool):
        return None
    grupo = d.get("grupo")
    if _entero(grupo):
        grupo = str(grupo)
    return ("sesion", {
        "activa": d["activa"],
        "id": d["id"] if _entero(d.get("id")) and 0 < d["id"] < 2**31 else None,
        "materia": _texto(d.get("materia")),
        "grupo": _texto(grupo),
        "inicio": _inicio(d.get("inicio")),
        "tol_min": d["tol_min"] if _entero(d.get("tol_min"))
        and 0 <= d["tol_min"] <= 600 else None,
    })


def _actuadores(d):
    datos = {}
    for campo in ("servo", "led"):
        if _texto(d.get(campo)) is not None:
            datos[campo] = _texto(d[campo])
    if _entero(d.get("luz")) and d["luz"] in (0, 1):
        datos["luz"] = d["luz"]
    return ("actuadores", datos) if datos else None


def _enrolamiento(d):
    paso, total = d.get("paso"), d.get("total")
    if not (_entero(paso) and _entero(total) and 1 <= total <= MAX_PASOS
            and 0 <= paso <= total):
        return None
    if not isinstance(d.get("estado"), str):
        return None
    nombre, pose, motivo = (_texto(d.get(c, "")) for c in
                            ("nombre", "pose", "motivo"))
    if None in (nombre, pose, motivo):
        return None
    # Sin cuenta no se sabe de que registro es: se descarta (si no, se saltaria
    # la regla de "otra cuenta no pisa"). Excepcion: el rechazo de una orden
    # (`error` con paso 0) va con la cuenta vacia si la del comando no era valida.
    cuenta = _texto(d.get("cuenta"))
    if not cuenta or not cuenta.strip():
        if not (d["estado"] == "error" and paso == 0):
            return None
        cuenta = ""
    return ("enrolamiento", {
        "cuenta": cuenta,
        "nombre": nombre,
        "paso": paso,
        "total": total,
        "pose": pose,
        "estado": _texto(d["estado"]),
        "motivo": motivo,
    })


def _reinicio(d):
    return ("reinicio", None) if _entero(d.get("reiniciar")) \
        and d["reiniciar"] == 1 else None


_LECTORES = {
    TOPICO_SESION_ESTADO: _sesion,
    TOPICO_ACTUADOR_ESTADO: _actuadores,
    TOPICO_ENROLAMIENTO: _enrolamiento,
    TOPICO_REINICIO: _reinicio,
}


def interpretar(topico, payload, retain):
    """Convierte un mensaje MQTT en un evento (tipo, datos), o None si se descarta.

    Se descarta lo vacio, lo que no es un objeto JSON valido o no cumple el
    contrato, y lo retenido en los tópicos con Retain = no. Nunca lanza: un
    mensaje malo no debe tumbar el hilo de red.
    """
    lector = _LECTORES.get(topico)
    if lector is None or not payload or len(payload) > MAX_PAYLOAD:
        return None
    if retain and topico in SIN_RETAIN:
        return None
    try:
        d = json.loads(payload.decode("utf-8"))
    except (UnicodeDecodeError, ValueError):
        return None
    return lector(d) if isinstance(d, dict) else None


class Estado:
    """Lo que la pantalla sabe y la vista que muestra.

    El tiempo (`ahora`) lo da quien llama, en segundos de un reloj monotono,
    para que las pruebas no esperen.

    Hueco de datos: `asistencia` (presentes, retardos y faltantes) y
    `ultimo_registro` (nombre y estado) no tienen fuente todavia: el contrato
    no define un tópico para ellos y Node-RED no tiene flujos de sesiones. Las
    vistas muestran "sin datos" mientras valgan None. Para conectarlos, agrega
    el tópico a SUSCRIPCIONES, un lector a _LECTORES y un caso en `aplicar`
    que los asigne (y cambia el contrato y la ACL si hace falta).
    """

    GUIA_FIN_S = 4.0        # cuanto se muestra el desenlace de un registro
    CONFIRMAR_S = 3.0       # ventana para confirmar iniciar o terminar la sesion
    BLOQUEO_SESION_S = 5.0  # tras confirmar, espera la respuesta antes de otra orden
    ENFRIAMIENTO_S = 2.0    # entre dos toques de "Abrir puerta" o de "Probar luz"
    AVISO_S = 2.5

    def __init__(self, materia="FSE", grupo="1", tol_min=10, luz_prueba_s=3,
                 guia_vence_s=330, guia_max_s=900):
        # Lo que publica "Iniciar sesion": la pantalla no elige materia, grupo
        # ni tolerancia (ver CONFIG en main.py).
        self.parametros_inicio = {"materia": materia, "grupo": grupo,
                                  "tol_min": tol_min}
        self._luz_prueba_s = luz_prueba_s
        self._guia_vence_s = guia_vence_s
        self._guia_max_s = guia_max_s
        self._ultimo_toque = {}     # id del boton -> cuando se acepto el ultimo
        self.conectado = False
        self.credenciales_rechazadas = False    # el broker rechazo usuario o clave
        self.sesion = None          # dict de _sesion; None = aun sin dato
        self.actuadores = None
        self.guia = None
        self.descartada = None      # (cuenta, ultimo mensaje) de una Guia cerrada
        self.vista = "sesion"       # la que eligio el usuario: "sesion" o "lista"
        self.asistencia = None      # hueco de datos
        self.ultimo_registro = None  # hueco de datos
        self.armado = None          # (accion, vence) de iniciar o terminar
        self.sesion_bloqueo = None  # hasta cuando se espera la respuesta a una orden
        self.aviso = None           # (texto, vence)
        self.luz_hasta = None       # cuando apagar la luz de prueba
        self.reiniciar = False
        self.sucio = True           # hay algo nuevo que dibujar

    # ----- Lectura -----

    @property
    def vista_visible(self):
        return "guia" if self.guia else self.vista

    def accion_sesion(self):
        """Lo que haria el boton de sesion ahora."""
        return "terminar" if self.sesion and self.sesion["activa"] else "iniciar"

    def sesion_bloqueada(self, ahora):
        return self.sesion_bloqueo is not None and ahora < self.sesion_bloqueo

    def botones_activos(self):
        if self.guia:
            return {"guia_salir"}
        if self.vista == "lista":
            return {"nav_sesion", "nav_lista"}
        return {"nav_sesion", "nav_lista", "sesion_toggle", "puerta", "luz"}

    # ----- Eventos del broker -----

    def aplicar(self, evento, ahora):
        """Aplica un evento; marca `sucio` solo si cambio algo que se dibuja."""
        tipo, datos = evento
        if tipo == "conexion":
            cambio = self.conectado != bool(datos) or \
                (bool(datos) and self.credenciales_rechazadas)
            self.conectado = bool(datos)
            if datos:
                self.credenciales_rechazadas = False
        elif tipo == "credenciales":
            cambio = self.credenciales_rechazadas != bool(datos)
            self.credenciales_rechazadas = bool(datos)
        elif tipo == "sesion":
            # llego la respuesta a la ultima orden (o un cambio de otro lado)
            cambio = self.sesion != datos or self.sesion_bloqueo is not None
            self.sesion = datos
            self.sesion_bloqueo = None
        elif tipo == "actuadores":
            nuevo = {**(self.actuadores or {}), **datos}
            cambio = nuevo != self.actuadores
            self.actuadores = nuevo
        elif tipo == "enrolamiento":
            cambio = self._aplicar_enrolamiento(datos, ahora)
        elif tipo == "reinicio":
            self.reiniciar = True
            return
        else:
            return
        if cambio:
            self.sucio = True

    def _aplicar_enrolamiento(self, d, ahora):
        """Devuelve True si cambia lo que se dibuja."""
        g, cuenta = self.guia, d["cuenta"]
        final = d["estado"] in ESTADOS_FINALES
        # `error` con paso 0 es el rechazo de una orden (p. ej. "ocupado"): no
        # dice nada del registro en curso, ni abre la Guia por si solo.
        if d["estado"] == "error" and d["paso"] == 0:
            if g is None:
                self.avisar("Registro rechazado por la visión", ahora)
                return True
            return False
        if not cuenta:
            return False        # interpretar ya lo descarta; defensa en profundidad
        # `esperando_pose` en el paso 0 o 1 es el inicio de un registro: si se
        # perdio el final del anterior (QoS 0), no debe quedar oculto.
        nuevo = d["estado"] == "esperando_pose" and d["paso"] <= 1
        # Una Guia cerrada a mano o por el techo no se reabre con los mensajes
        # del mismo registro: se ignoran, y su mensaje final solo quita la marca
        # (no reabre la Guia). La reabren el inicio de un registro, uno de otra
        # cuenta o un silencio como el del vencimiento.
        if self.descartada:
            cuenta_d, visto = self.descartada
            if cuenta == cuenta_d and ahora - visto < self._guia_vence_s \
                    and not nuevo:
                self.descartada = None if final else (cuenta, ahora)
                return False
            self.descartada = None
        # Con una Guia en curso, otra cuenta no la pisa (un solo registro a la
        # vez), salvo que empiece uno: asi una Guia atorada no tapa al siguiente.
        if g and g["fin_hasta"] is None and g["cuenta"] and cuenta != g["cuenta"] \
                and not nuevo:
            return False
        nombre = d["nombre"]
        if not nombre and g and g["cuenta"] == cuenta:
            nombre = g["nombre"]
        # El techo cuenta desde el primer mensaje de ese registro.
        inicio = g["inicio"] if g and g["fin_hasta"] is None \
            and g["cuenta"] == cuenta else ahora
        nueva = {**d, "nombre": nombre, "inicio": inicio, "ultimo": ahora,
                 "fin_hasta": ahora + self.GUIA_FIN_S if final else None}
        cambio = g is None or any(nueva[k] != g[k] for k in nueva
                                  if k != "ultimo")
        self.guia = nueva
        return cambio

    # ----- Botones -----

    def tocar(self, id_boton, ahora):
        """Atiende un boton; devuelve las publicaciones [(tópico, dict)]."""
        if id_boton not in self.botones_activos():
            return []
        self.sucio = True
        if id_boton in ("nav_sesion", "nav_lista"):
            self.vista = id_boton[4:]
            self.armado = None
            return []
        if id_boton == "guia_salir":
            self._salir_de_la_guia(ahora)
            return []
        if not self.conectado:
            self.avisar("Sin conexión: no se envió", ahora)
            return []
        if id_boton == "sesion_toggle":
            return self._tocar_sesion(ahora)
        # Puerta y luz: un toque seguido de otro (rebote del dedo, o alguien
        # insistiendo) no cuenta. Apagar la luz de prueba nunca se bloquea.
        ultimo = self._ultimo_toque.get(id_boton)
        apagando = id_boton == "luz" and self.luz_hasta is not None
        if not apagando and ultimo is not None \
                and ahora - ultimo < self.ENFRIAMIENTO_S:
            self.avisar("Espera un momento", ahora)
            return []
        self._ultimo_toque[id_boton] = ahora
        if id_boton == "puerta":
            self.avisar("Abriendo la puerta…", ahora)
            return [(TOPICO_ACTUADOR_CMD, {"servo": "abrir"})]
        return self._alternar_luz(ahora)

    def _salir_de_la_guia(self, ahora):
        if self.guia["cuenta"] and self.guia["fin_hasta"] is None:
            self.descartada = (self.guia["cuenta"], ahora)
        self.guia = None
        self.vista = "sesion"

    def _tocar_sesion(self, ahora):
        if self.sesion is None:
            self.avisar("Aún no hay estado de la sesión", ahora)
            return []
        if self.sesion_bloqueada(ahora):
            self.avisar("Esperando la respuesta…", ahora)
            return []
        accion = self.accion_sesion()
        if not (self.armado and self.armado[0] == accion):
            self.armado = (accion, ahora + self.CONFIRMAR_S)
            return []
        self.armado = None
        # Hasta que llegue un aviso de la sesion (o pase un rato), otra orden
        # seria un "iniciar" doble.
        self.sesion_bloqueo = ahora + self.BLOQUEO_SESION_S
        self.avisar("Orden enviada", ahora)
        if accion == "iniciar":
            return [(TOPICO_SESION_CMD,
                     {"accion": "iniciar", **self.parametros_inicio})]
        return [(TOPICO_SESION_CMD, {"accion": "terminar"})]

    def _alternar_luz(self, ahora):
        if self.luz_hasta is not None:
            self.luz_hasta = None
            return [(TOPICO_ACTUADOR_CMD, {"luz": 0})]
        self.luz_hasta = ahora + self._luz_prueba_s
        return [(TOPICO_ACTUADOR_CMD, {"luz": 1})]

    def avisar(self, texto, ahora):
        self.aviso = (texto, ahora + self.AVISO_S)
        self.sucio = True

    def fallo_envio(self, topico, datos, ahora):
        """Una publicacion no salio. Avisa; si era apagar la luz, la reintenta; si
        era encenderla, deja de contarla como encendida."""
        self.avisar("No se pudo enviar la orden", ahora)
        if topico == TOPICO_ACTUADOR_CMD:
            # no salio: el reintento inmediato no debe toparse con el enfriamiento
            if "servo" in datos:
                self._ultimo_toque.pop("puerta", None)
            if "luz" in datos:
                self._ultimo_toque.pop("luz", None)
        if topico == TOPICO_ACTUADOR_CMD and datos == {"luz": 0}:
            self.luz_hasta = ahora + 1.0
        elif topico == TOPICO_ACTUADOR_CMD and datos == {"luz": 1}:
            self.luz_hasta = None

    # ----- Paso del tiempo -----

    def tick(self, ahora):
        """Vence avisos, confirmaciones y la guia; devuelve publicaciones."""
        if self.aviso and ahora >= self.aviso[1]:
            self.aviso = None
            self.sucio = True
        if self.armado and ahora >= self.armado[1]:
            self.armado = None
            self.sucio = True
        if self.sesion_bloqueo is not None and ahora >= self.sesion_bloqueo:
            self.sesion_bloqueo = None
            self.sucio = True
        g = self.guia
        if g:
            techo = ahora - g["inicio"] >= self._guia_max_s
            if (g["fin_hasta"] is not None and ahora >= g["fin_hasta"]) \
                    or ahora - g["ultimo"] >= self._guia_vence_s or techo:
                if techo and g["cuenta"] and g["fin_hasta"] is None:
                    self.descartada = (g["cuenta"], ahora)
                self.guia = None
                self.vista = "sesion"
                self.sucio = True
        # La luz de prueba se apaga solo con conexion: sin ella la orden se
        # quedaria encolada y se enviaria tarde. Se apaga al reconectar.
        if self.luz_hasta is not None and ahora >= self.luz_hasta \
                and self.conectado:
            self.luz_hasta = None
            self.sucio = True
            return [(TOPICO_ACTUADOR_CMD, {"luz": 0})]
        return []

    def cierre(self):
        """Publicaciones pendientes al apagar: no dejar la luz de prueba encendida."""
        if self.luz_hasta is None:
            return []
        self.luz_hasta = None
        return [(TOPICO_ACTUADOR_CMD, {"luz": 0})]
