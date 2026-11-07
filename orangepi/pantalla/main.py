"""
Programa de la pantalla ILI9341 de AutoList (pase de lista)

Menu base del plan (sección 6.5): las vistas Sesión, Lista y Guía de registro,
con navegación por el táctil XPT2046 y un cliente MQTT con el usuario
`pantalla` (docs/contrato-mqtt.md). Sesión y Lista muestran "sin datos" sin
sesión activa o antes del primer mensaje retenido (ver estado.Estado).

Con SIGTERM o SIGINT apaga la luz de prueba si estaba encendida, cierra MQTT,
limpia la pantalla, libera el SPI y los GPIO y sale con código 0. Con un
mensaje de aula/pantalla/reinicio hace lo mismo y sale con 0 para que systemd
(Restart=always) la relance. Códigos de salida distintos de 0:
  74  no se pudo abrir o escribir un dispositivo (/dev/spidev*, /dev/gpiochip*)
      ni cargar la fuente DejaVu
  78  configuración inválida: una variable PANTALLA_* fuera de rango o que no es
      un entero, o falta la clave MQTT (credencial mqtt_pantalla de systemd o
      MQTT_PANTALLA_PASSWORD) o es el placeholder cambiar_esta_clave

Si el broker no responde, la pantalla sigue viva: muestra "Sin conexión" y
reintenta sin límite.

Configuración: la tabla CONFIG de abajo es el único lugar. Cada valor se puede
cambiar con la variable de entorno PANTALLA_<NOMBRE>. La clave del usuario MQTT
`pantalla` llega aparte y no está en la tabla, para que no se imprima por error:
primero se lee la credencial `mqtt_pantalla` que systemd deja en
$CREDENTIALS_DIRECTORY (LoadCredential=: no queda en el entorno del proceso ni
se hereda, pero la puede leer el mismo uid), y como reserva, para correrlo a
mano, la variable MQTT_PANTALLA_PASSWORD (el mismo nombre que en
orangepi/.env.example).
"""

import os
import queue
import signal
import sys
import threading
import time
from datetime import datetime

from cliente_mqtt import ClienteMQTT
from estado import Estado
from ili9341_opi import ILI9341, color565
from tactil import DetectorToque, a_pixeles, calibracion_de_config
import vistas
from xpt2046_opi import Touch

EX_IOERR = 74
EX_CONFIG = 78

VARIABLE_CLAVE = "MQTT_PANTALLA_PASSWORD"
CREDENCIAL_CLAVE = "mqtt_pantalla"      # archivo de $CREDENTIALS_DIRECTORY
PLACEHOLDER_CLAVE = "cambiar_esta_clave"
HOSTS_LOCALES = ("127.0.0.1", "::1", "localhost")

# nombre: (valor por defecto, ¿puede ir vacio?). Lineas GPIO de gpiochip0.
# PANTALLA_CS vacio o sin definir deja el chip select al kernel (CS0 del bus).
# Los booleanos son 0 o 1.
CONFIG = {
    "SPI_BUS": (3, False),              # /dev/spidev3.x
    "SPI_DISPOSITIVO": (0, False),      # /dev/spidev3.0 es la pantalla
    "GPIOCHIP": ("/dev/gpiochip0", False),
    "DC": (139, False),                 # pin fisico 15
    "RST": (33, False),                 # pin fisico 13
    "CS": (None, True),                 # pin fisico 24, lo maneja el kernel
    "ROTACION": (0, False),             # 0 o 2 (vertical); las vistas son de 240x320
    "BAUDRATE": (40000000, False),
    # Broker: Mosquitto corre en Docker en la misma placa con el 1883 publicado
    "MQTT_HOST": ("127.0.0.1", False),
    "MQTT_PUERTO": (1883, False),
    # Lo que publica "Iniciar sesion" (aula/sesion/cmd): la pantalla no los elige
    "SESION_MATERIA": ("FSE", False),
    "SESION_GRUPO": ("1", False),
    "SESION_TOL_MIN": (10, False),
    "LUZ_PRUEBA_S": (3, False),         # s que dura "Probar luz LED"
    "GUIA_VENCE_S": (330, False),       # s sin progreso para dejar la Guia
    "GUIA_MAX_S": (900, False),         # s maximos de una Guia desde su 1er mensaje
    # Tactil XPT2046, en el mismo bus SPI que la pantalla
    "TACTIL": (1, False),               # 0 = sin tactil (solo mostrar)
    "TACTIL_BUS": (3, False),
    "TACTIL_DISPOSITIVO": (1, False),   # /dev/spidev3.1
    "TACTIL_IRQ": (32, False),          # pin fisico 11, baja al tocar
    "TACTIL_BAUDRATE": (2000000, False),
    # Calibracion medida con calibrar_tactil.py en esta placa el 5 nov (ver
    # README): valida con ROTACION = 0; hay que recalibrar si cambia de rotacion
    # o se remonta el panel. Cada panel y montaje dan otros, pero solo hay una.
    "TACTIL_X_MIN": (228, False),
    "TACTIL_X_MAX": (3830, False),
    "TACTIL_Y_MIN": (153, False),
    "TACTIL_Y_MAX": (3742, False),
    "TACTIL_CAMBIAR_EJES": (0, False),
    "TACTIL_INVERTIR_X": (0, False),
    "TACTIL_INVERTIR_Y": (0, False),
}

# nombre: (minimo, maximo), ambos incluidos
RANGOS = {
    "MQTT_PUERTO": (1, 65535),
    "SESION_TOL_MIN": (1, 600),
    "LUZ_PRUEBA_S": (1, 60),
    "GUIA_VENCE_S": (10, 3600),
    "GUIA_MAX_S": (60, 7200),
    "TACTIL": (0, 1),
    "TACTIL_CAMBIAR_EJES": (0, 1),
    "TACTIL_INVERTIR_X": (0, 1),
    "TACTIL_INVERTIR_Y": (0, 1),
}

TROZO_ESPERA = 0.02     # s entre pasadas: tope de latencia del toque y de SIGTERM
MAX_EVENTOS_POR_PASO = 64
EVENTOS_MAX = 256       # tope de la cola de mensajes MQTT pendientes de atender


def leer_config(entorno=None):
    """Devuelve la configuracion como dict; ValueError si un valor no sirve."""
    entorno = os.environ if entorno is None else entorno
    cfg = {}
    for nombre, (defecto, vacio_ok) in CONFIG.items():
        crudo = entorno.get("PANTALLA_" + nombre)
        if crudo is None or (crudo.strip() == "" and vacio_ok):
            cfg[nombre] = defecto
        elif isinstance(defecto, str):
            if crudo.strip() == "":
                raise ValueError("PANTALLA_%s no puede ir vacía" % nombre)
            cfg[nombre] = crudo
        else:
            try:
                cfg[nombre] = int(crudo)
            except ValueError:
                raise ValueError("PANTALLA_%s debe ser un entero, no %r"
                                 % (nombre, crudo)) from None
    _validar(cfg)
    return cfg


def _validar(cfg):
    for nombre, (minimo, maximo) in RANGOS.items():
        if not minimo <= cfg[nombre] <= maximo:
            raise ValueError("PANTALLA_%s debe estar entre %d y %d, no %d"
                             % (nombre, minimo, maximo, cfg[nombre]))
    if cfg["ROTACION"] not in (0, 2):
        raise ValueError("PANTALLA_ROTACION debe ser 0 o 2: las vistas son para "
                         "la pantalla vertical, no %d" % cfg["ROTACION"])
    if cfg["GUIA_MAX_S"] < cfg["GUIA_VENCE_S"]:
        raise ValueError("PANTALLA_GUIA_MAX_S (%d) no puede ser menor que "
                         "PANTALLA_GUIA_VENCE_S (%d)"
                         % (cfg["GUIA_MAX_S"], cfg["GUIA_VENCE_S"]))
    for eje in ("X", "Y"):
        if cfg["TACTIL_%s_MIN" % eje] >= cfg["TACTIL_%s_MAX" % eje]:
            raise ValueError("PANTALLA_TACTIL_%s_MIN debe ser menor que "
                             "PANTALLA_TACTIL_%s_MAX" % (eje, eje))


def leer_clave(entorno=None):
    """Clave MQTT del usuario `pantalla`; ValueError si falta o no sirve.

    Orden: la credencial `mqtt_pantalla` de $CREDENTIALS_DIRECTORY (la deja
    systemd con LoadCredential=: no queda en el entorno ni se hereda, pero la
    puede leer el mismo uid) y, como reserva para la corrida manual, la
    variable MQTT_PANTALLA_PASSWORD. Una credencial que
    existe pero esta vacia es un error: no se cae a la variable. No imprime la
    clave, ni en los errores.
    """
    entorno = os.environ if entorno is None else entorno
    clave = None
    directorio = entorno.get("CREDENTIALS_DIRECTORY")
    if directorio:
        ruta = os.path.join(directorio, CREDENCIAL_CLAVE)
        try:
            with open(ruta, encoding="utf-8") as f:
                clave = f.read().rstrip("\r\n")    # el editor deja un salto al final
        except FileNotFoundError:
            pass
        except (OSError, UnicodeDecodeError) as e:
            # el texto de UnicodeDecodeError trae un byte del contenido: solo el tipo
            raise ValueError("no se pudo leer la credencial %s (%s)"
                             % (ruta, type(e).__name__)) from None
    if clave is None:
        clave = entorno.get(VARIABLE_CLAVE)
    if not clave:
        raise ValueError("falta la clave del usuario MQTT `pantalla`: ni la "
                         "credencial %s ni la variable %s tienen valor"
                         % (CREDENCIAL_CLAVE, VARIABLE_CLAVE))
    if clave == PLACEHOLDER_CLAVE:
        raise ValueError("la clave del usuario MQTT `pantalla` es el "
                         "placeholder %s: pon la real" % PLACEHOLDER_CLAVE)
    return clave


def abrir_pantalla(cfg):
    return ILI9341(bus=cfg["SPI_BUS"], dispositivo=cfg["SPI_DISPOSITIVO"],
                   dc=cfg["DC"], rst=cfg["RST"], cs=cfg["CS"],
                   rotacion=cfg["ROTACION"], baudrate=cfg["BAUDRATE"],
                   chip=cfg["GPIOCHIP"])


def abrir_tactil(cfg):
    return Touch(bus=cfg["TACTIL_BUS"], dispositivo=cfg["TACTIL_DISPOSITIVO"],
                 irq=cfg["TACTIL_IRQ"], baudrate=cfg["TACTIL_BAUDRATE"],
                 chip=cfg["GPIOCHIP"])


class Aplicacion:
    """Une el estado, la red, el tactil y la pantalla en una pasada del bucle."""

    def __init__(self, pantalla, fuentes, estado, cliente, eventos, tactil=None,
                 calibracion=None, reloj=time.monotonic, ahora=None):
        self.pantalla, self.fuentes = pantalla, fuentes
        self.estado, self.cliente, self.tactil = estado, cliente, tactil
        self._eventos = eventos
        self._calibracion = calibracion
        self._detector = DetectorToque()
        self._reloj = reloj
        self._ahora = ahora or (lambda: datetime.now())
        self._segundo = None        # ultimo segundo dibujado
        self._error_dibujo = None   # tipo de la ultima excepcion al dibujar

    def paso(self):
        """Una pasada: eventos de red, toque, vencimientos y dibujo si hace falta."""
        t = self._reloj()
        for _ in range(MAX_EVENTOS_POR_PASO):
            try:
                evento = self._eventos.get_nowait()
            except queue.Empty:
                break
            self.estado.aplicar(evento, t)
        if self.tactil is not None:
            self._leer_tactil(t)
        self._publicar(self.estado.tick(t), t)
        ahora = self._ahora()
        segundo = ahora.replace(microsecond=0)
        # Se redibuja cuando algo cambia y cada segundo, por el reloj.
        if self.estado.sucio or segundo != self._segundo:
            self.pantalla.image(self._cuadro(ahora))
            self.estado.sucio = False
            self._segundo = segundo

    def _cuadro(self, ahora):
        """Imagen de la vista; si dibujarla falla por un dato raro (el estado
        viene de la red), un cuadro fijo de error en lugar de caerse."""
        try:
            imagen = vistas.dibujar(self.estado, ahora, self.fuentes)
            self._error_dibujo = None
            return imagen
        except Exception as e:      # defensa en profundidad: nunca el contenido
            tipo = type(e).__name__
            if tipo != self._error_dibujo:
                self._error_dibujo = tipo
                print("pantalla: error al dibujar el estado (%s); se muestra el "
                      "cuadro de error de datos" % tipo, file=sys.stderr)
            return vistas.dibujar_error(self.fuentes)

    def _leer_tactil(self, t):
        presionado = self.tactil.is_pressed()
        crudo = self.tactil.get_coordinates() if presionado else None
        toque = self._detector.actualizar(presionado, crudo, t)
        if toque is None or self._error_dibujo is not None:
            return          # con el cuadro de error no se ven los botones
        x, y = a_pixeles(toque[0], toque[1], self._calibracion,
                         vistas.ANCHO, vistas.ALTO)
        boton = vistas.boton_en(vistas.botones(self.estado), x, y)
        self._publicar(self.estado.tocar(boton.id if boton else None, t), t)

    def _publicar(self, acciones, t=None):
        for topico, datos in acciones:
            if not self.cliente.publicar(topico, datos):
                self.estado.fallo_envio(topico, datos,
                                        self._reloj() if t is None else t)

    def cerrar(self):
        """Apaga la luz de prueba, cierra MQTT y el tactil, y deja la pantalla
        en negro. Cada paso se intenta aunque falle el anterior. Si no se pudo
        apagar la luz de prueba, lo dice por stderr."""
        try:
            pendientes = self.estado.cierre()
            for topico, datos in pendientes:
                if not self.cliente.publicar(topico, datos):
                    self._avisar_luz("no se pudo enviar el apagado")
            if not self.cliente.detener() and pendientes:
                self._avisar_luz("el apagado no llegó a confirmarse")
        finally:
            try:
                if self.tactil is not None:
                    self.tactil.cerrar()
            finally:
                apagar(self.pantalla)

    @staticmethod
    def _avisar_luz(motivo):
        print("pantalla: luz de prueba posiblemente encendida (%s)" % motivo,
              file=sys.stderr)


def ejecutar(app, parar):
    """Pasadas del bucle hasta que parar se active o llegue un reinicio."""
    while not parar.is_set() and not app.estado.reiniciar:
        app.paso()
        # time.sleep y no parar.wait(): el manejador de señales corre en este
        # mismo hilo y llama a parar.set(), que toma el candado interno del
        # Event; si la señal llegara con wait() reteniendolo, se bloquearia.
        time.sleep(TROZO_ESPERA)


def apagar(pantalla):
    """Deja la pantalla en negro y libera el SPI y los GPIO."""
    try:
        pantalla.fill(color565(0, 0, 0))
    finally:
        pantalla.cerrar()


def main():
    try:
        cfg = leer_config()
        clave = leer_clave()
    except ValueError as e:
        print("pantalla: configuración inválida: %s" % e, file=sys.stderr)
        return EX_CONFIG

    if os.environ.get("CREDENTIALS_DIRECTORY") and os.environ.get(VARIABLE_CLAVE):
        print("pantalla: aviso: %s está en el entorno además de la credencial de "
              "systemd; bajo systemd la clave no debe ir en el entorno (queda en "
              "/proc/<pid>/environ). Si la credencial existe, se usa esa." % VARIABLE_CLAVE,
              file=sys.stderr)
    if cfg["MQTT_HOST"] not in HOSTS_LOCALES:
        print("pantalla: aviso: PANTALLA_MQTT_HOST=%s no es la máquina local; el "
              "broker no usa TLS y la clave MQTT viajaría en claro por la red"
              % cfg["MQTT_HOST"], file=sys.stderr)

    parar = threading.Event()
    for senal in (signal.SIGTERM, signal.SIGINT):
        signal.signal(senal, lambda *_: parar.set())

    try:
        fuentes = vistas.cargar_fuentes()
    except OSError as e:
        print("pantalla: no se pudo cargar la fuente DejaVu de %s: %s"
              % (vistas.DIR_FUENTES, e), file=sys.stderr)
        return EX_IOERR

    try:
        pantalla = abrir_pantalla(cfg)
    except OSError as e:
        print("pantalla: no se pudo abrir %s (SPI) o %s (GPIO): %s\n"
              "Revisa que existan los dispositivos y que el usuario esté en "
              "los grupos spi y gpio."
              % ("/dev/spidev%d.%d" % (cfg["SPI_BUS"], cfg["SPI_DISPOSITIVO"]),
                 cfg["GPIOCHIP"], e), file=sys.stderr)
        return EX_IOERR

    try:
        tactil = abrir_tactil(cfg) if cfg["TACTIL"] else None
    except OSError as e:
        pantalla.cerrar()
        print("pantalla: no se pudo abrir el táctil %s (SPI) o la línea %d de "
              "%s (TP_IRQ): %s\nPuedes arrancar sin táctil con PANTALLA_TACTIL=0."
              % ("/dev/spidev%d.%d" % (cfg["TACTIL_BUS"],
                                       cfg["TACTIL_DISPOSITIVO"]),
                 cfg["TACTIL_IRQ"], cfg["GPIOCHIP"], e), file=sys.stderr)
        return EX_IOERR

    estado = Estado(materia=cfg["SESION_MATERIA"], grupo=cfg["SESION_GRUPO"],
                    tol_min=cfg["SESION_TOL_MIN"], luz_prueba_s=cfg["LUZ_PRUEBA_S"],
                    guia_vence_s=cfg["GUIA_VENCE_S"], guia_max_s=cfg["GUIA_MAX_S"])
    eventos = queue.Queue(maxsize=EVENTOS_MAX)
    cliente = ClienteMQTT(cfg["MQTT_HOST"], cfg["MQTT_PUERTO"], clave, eventos)
    app = Aplicacion(pantalla, fuentes, estado, cliente, eventos, tactil=tactil,
                     calibracion=calibracion_de_config(cfg))

    fallo = None
    try:
        cliente.iniciar()
        ejecutar(app, parar)
    except OSError as e:
        fallo = e
    finally:
        try:
            app.cerrar()
        except OSError as e:
            # Si la parada fue pedida (señal o reinicio) un apagado que falla no
            # es motivo para que systemd lo cuente como fallo: sale con 0.
            print("pantalla: falló el apagado (%s); la pantalla puede haber "
                  "quedado encendida" % e, file=sys.stderr)
    if fallo is not None:
        print("pantalla: falló la escritura en la pantalla o el táctil: %s"
              % fallo, file=sys.stderr)
        return EX_IOERR
    if estado.reiniciar:
        print("pantalla: reinicio pedido por MQTT; se sale para que systemd la "
              "relance", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
