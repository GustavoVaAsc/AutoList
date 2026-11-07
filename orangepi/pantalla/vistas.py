"""
Vistas de la pantalla ILI9341 de 240x320 (plan, sección 6.5): Sesión, Lista y
Guía de registro. Genera imagenes PIL y la geometria de los botones; no toca
hardware ni red.

El trazado de los botones sale de una sola funcion (`botones`), la misma que
usan el dibujo y la deteccion del toque, para que lo que se ve sea lo que se
toca. Esta pensada para la pantalla vertical (ROTACION 0 o 2).
"""

import os
from collections import namedtuple
from datetime import datetime

from PIL import Image, ImageDraw, ImageFont

ANCHO, ALTO = 240, 320

DIR_FUENTES = "/usr/share/fonts/truetype/dejavu"
FUENTE_NEGRITA = "DejaVuSans-Bold.ttf"
FUENTE_REGULAR = "DejaVuSans.ttf"
FUENTE_MONO = "DejaVuSansMono-Bold.ttf"

FONDO = (16, 24, 40)
PANEL = (28, 40, 66)
TEXTO = (240, 244, 250)
ACENTO = (80, 170, 255)
APAGADO = (140, 150, 165)
OK = (90, 205, 130)
ALERTA = (255, 190, 60)
ERROR = (245, 100, 100)
BOTON = (44, 62, 100)
BOTON_APAGADO = (30, 38, 56)

TONOS = {"normal": TEXTO, "apagado": APAGADO, "ok": OK, "alerta": ALERTA,
         "error": ERROR, "acento": ACENTO}

DIAS = ("lun", "mar", "mié", "jue", "vie", "sáb", "dom")
MESES = ("ene", "feb", "mar", "abr", "may", "jun",
         "jul", "ago", "sep", "oct", "nov", "dic")

MARGEN = 8
INTERIOR = ANCHO - 2 * MARGEN       # ancho util
SIN_DATOS = "sin datos"
MAX_CARACTERES = 120    # tope de una linea antes de medirla: el costo no depende del texto

Boton = namedtuple("Boton", "id x y ancho alto etiqueta estilo")

# Geometria (x, y, ancho, alto) de cada boton. Pestañas abajo; la Guía no
# lleva botones: el registro se controla desde el frontend.
GEOMETRIA = {
    "sesion_toggle": (MARGEN, 154, INTERIOR, 42),
    "puerta": (MARGEN, 202, 108, 42),
    "luz": (ANCHO - MARGEN - 108, 202, 108, 42),
    "nav_sesion": (0, 282, ANCHO // 2, 38),
    "nav_lista": (ANCHO // 2, 282, ANCHO // 2, 38),
    "guia_salir": (0, 282, ANCHO, 38),
}
IDS_SESION = ("sesion_toggle", "puerta", "luz", "nav_sesion", "nav_lista")
IDS_LISTA = ("nav_sesion", "nav_lista")

INSTRUCCIONES = {
    "frente": "Mira de frente a la cámara",
    "izquierda": "Gira un poco la cabeza a la izquierda",
    "derecha": "Gira un poco la cabeza a la derecha",
    "arriba": "Levanta un poco la cara",
    "frente_2": "Mira de frente otra vez",
}
# Motivos del contrato (docs/contrato-mqtt.md, aula/vision/enrolamiento)
MOTIVOS_RECHAZO = {
    "sin_rostro": "no se ve tu rostro",
    "varios_rostros": "se ven varios rostros",
    "rostro_chico": "acércate a la cámara",
    "borrosa": "la imagen salió borrosa",
    "giro_excesivo": "gira menos la cabeza",
    "fallo_captura": "falló la captura",
}
MOTIVOS_ERROR = {
    "ocupado": "ya hay un registro",
    "inconsistente": "las fotos no coinciden",
    "npu": "falló el reconocimiento",
    "cuenta_invalida": "cuenta no válida",
}


def cargar_fuentes():
    """Fuentes DejaVu del sistema. OSError si faltan."""
    def f(archivo, tamano):
        return ImageFont.truetype(os.path.join(DIR_FUENTES, archivo), tamano)
    return {
        "titulo": f(FUENTE_NEGRITA, 20),
        "subtitulo": f(FUENTE_NEGRITA, 16),
        "negrita": f(FUENTE_NEGRITA, 14),
        "texto": f(FUENTE_REGULAR, 14),
        "pequeno": f(FUENTE_REGULAR, 12),
        "hora": f(FUENTE_MONO, 16),
        "numero": f(FUENTE_MONO, 28),
        "contador": f(FUENTE_MONO, 56),
    }


# ----- Botones -----

def etiqueta_sesion(estado):
    if estado.armado:
        return "¿Seguro?\nToca otra vez"
    return "Terminar sesión" if estado.accion_sesion() == "terminar" \
        else "Iniciar sesión"


def etiqueta_luz(estado):
    return "Apagar\nluz" if estado.luz_hasta is not None else "Probar\nluz LED"


def botones(estado):
    """Botones de la vista visible: [Boton]."""
    if estado.vista_visible == "guia":
        # Una salida visible: si el registro se atora, el profesor no se queda
        # con la Guia puesta hasta que venza.
        return [Boton("guia_salir", *GEOMETRIA["guia_salir"], "Salir de la guía",
                      "normal")]
    ids = IDS_LISTA if estado.vista_visible == "lista" else IDS_SESION
    etiquetas = {"sesion_toggle": etiqueta_sesion(estado),
                 "puerta": "Abrir\npuerta", "luz": etiqueta_luz(estado),
                 "nav_sesion": "Sesión", "nav_lista": "Lista"}
    sin_envio = None if estado.conectado else "deshabilitado"
    sin_sesion = sin_envio or ("deshabilitado" if estado.sesion is None
                               or estado.sesion_bloqueo is not None else None)
    estilos = {
        "sesion_toggle": "alerta" if estado.armado else sin_sesion or "normal",
        "puerta": sin_envio or "normal",
        "luz": "alerta" if estado.luz_hasta is not None else sin_envio or "normal",
        "nav_sesion": "activo" if estado.vista == "sesion" else "normal",
        "nav_lista": "activo" if estado.vista == "lista" else "normal",
    }
    return [Boton(i, *GEOMETRIA[i], etiquetas[i], estilos[i]) for i in ids]


def boton_en(lista, x, y):
    for b in lista:
        if b.x <= x < b.x + b.ancho and b.y <= y < b.y + b.alto:
            return b
    return None


# ----- Textos -----

def hora_local(iso):
    """HH:MM en hora local de una fecha ISO 8601, o None si no se entiende."""
    try:
        dt = datetime.fromisoformat(iso)
        if dt.tzinfo:
            dt = dt.astimezone()    # en los extremos del calendario desborda
    except (TypeError, ValueError, OverflowError, OSError):
        return None
    return dt.strftime("%H:%M")


def _unir(*partes):
    return " · ".join(p for p in partes if p)


def lineas_sesion(estado):
    """Las 5 lineas de la tarjeta de Sesión: [(texto, tono)]."""
    s, p = estado.sesion, estado.parametros_inicio
    if s is None:
        cabecera = [("Sesión: sin datos", "apagado"), ("", "normal"),
                    ("", "normal")]
    elif s["activa"]:
        tol_min, grupo = s.get("tol_min"), s.get("grupo")
        inicio = hora_local(s.get("inicio"))
        cabecera = [
            ("Sesión activa", "ok"),
            (_unir(s.get("materia"), "grupo " + grupo if grupo else ""), "normal"),
            (_unir("Tol. %d min" % tol_min if tol_min is not None else "",
                   "desde " + inicio if inicio else ""), "normal"),
        ]
    else:
        cabecera = [
            ("Sin sesión activa", "apagado"),
            ("Al iniciar usará:", "apagado"),
            (_unir(p["materia"], "grupo %s" % p["grupo"],
                   "%d min" % p["tol_min"]), "normal"),
        ]
    a, u = estado.asistencia, estado.ultimo_registro
    presentes = a["presentes"] if a else SIN_DATOS
    ultimo = "%s (%s)" % (u["nombre"], u["estado"]) if u else SIN_DATOS
    return cabecera + [("Presentes: %s" % presentes, "normal" if a else "apagado"),
                       ("Último: %s" % ultimo, "normal" if u else "apagado")]


def resumen_actuadores(estado):
    a = estado.actuadores or {}
    luz = {0: "apagada", 1: "encendida"}.get(a.get("luz"), "–")
    return [("Puerta", a.get("servo", "–")), ("LED", a.get("led", "–")),
            ("Luz", luz)]


def texto_guia(estado):
    """Lo que dice la Guía de registro: nombre, contador, pose y mensaje."""
    g = estado.guia
    est, motivo = g["estado"], g["motivo"]
    final = est in ("completado", "cancelado", "error")
    if est == "rechazada":
        mensaje = ("Otra vez: " + MOTIVOS_RECHAZO.get(motivo, "no se pudo tomar"),
                   "alerta")
    elif est == "error":
        mensaje = ("No se pudo registrar: " + MOTIVOS_ERROR.get(motivo, "error"),
                   "error")
    else:
        mensaje = {
            "esperando_pose": ("Prepárate", "normal"),
            "capturando": ("No te muevas", "acento"),
            "aceptada": ("Pose lista", "ok"),
            "advertencia": ("Registro con aviso", "alerta"),
            "completado": ("Captura completa", "ok"),
            "cancelado": ("Registro cancelado", "alerta"),
        }.get(est, ("", "normal"))
    return {
        "nombre": g["nombre"] or "Registro de rostro",
        "contador": "%d/%d" % (g["paso"], g["total"]),
        "instruccion": "" if final else INSTRUCCIONES.get(g["pose"], ""),
        "mensaje": mensaje[0],
        "tono": mensaje[1],
    }


def texto_conexion(estado):
    """(texto, tono) del estado de la conexion con el broker."""
    if estado.conectado:
        return "En línea", "ok"
    if estado.credenciales_rechazadas:
        return "Credenciales rechazadas", "error"
    return "Sin conexión", "error"


def ajustar_texto(texto, fuente, ancho_max):
    """Recorta con puntos suspensivos lo que no cabe en ancho_max px.

    Mide a lo mas MAX_CARACTERES caracteres y con busqueda binaria: un texto
    enorme cuesta lo mismo que uno de MAX_CARACTERES.
    """
    recortado = len(texto) > MAX_CARACTERES
    texto = texto[:MAX_CARACTERES]
    if not recortado and fuente.getlength(texto) <= ancho_max:
        return texto
    bajo, alto = 0, len(texto)      # el mayor n con texto[:n] + "…" dentro del ancho
    while bajo < alto:
        medio = (bajo + alto + 1) // 2
        if fuente.getlength(texto[:medio] + "…") <= ancho_max:
            bajo = medio
        else:
            alto = medio - 1
    return texto[:bajo] + "…"


def envolver(texto, fuente, ancho_max, max_lineas):
    """Parte el texto en lineas de ancho_max px; recorta la ultima si sobra."""
    palabras = texto[:MAX_CARACTERES * (max_lineas + 1)].split()
    lineas, actual = [], ""
    for palabra in palabras:
        prueba = (actual + " " + palabra).strip()
        if fuente.getlength(prueba) <= ancho_max:
            actual = prueba
            continue
        if actual:
            lineas.append(actual)
        actual = palabra
    if actual:
        lineas.append(actual)
    if len(lineas) > max_lineas:
        lineas = lineas[:max_lineas]
        lineas[-1] = lineas[-1] + "…"
    return [ajustar_texto(linea, fuente, ancho_max) for linea in lineas]


# ----- Dibujo -----

def _encabezado(d, ahora_dt, fuentes, titulo):
    d.rectangle((0, 0, ANCHO, 30), fill=PANEL)
    d.text((MARGEN, 15), titulo, font=fuentes["titulo"], fill=TEXTO, anchor="lm")
    d.text((ANCHO - MARGEN, 15), ahora_dt.strftime("%H:%M:%S"),
           font=fuentes["hora"], fill=ACENTO, anchor="rm")


def _conexion(d, estado, ahora_dt, fuentes):
    texto, tono = texto_conexion(estado)
    color = TONOS[tono]
    d.ellipse((MARGEN + 2, 36, MARGEN + 10, 44), fill=color)
    d.text((MARGEN + 18, 40), texto, font=fuentes["pequeno"], fill=color,
           anchor="lm")
    fecha = "%s %d %s" % (DIAS[ahora_dt.weekday()], ahora_dt.day,
                          MESES[ahora_dt.month - 1])
    # con un texto de conexion largo no cabe la fecha
    if MARGEN + 18 + fuentes["pequeno"].getlength(texto) + 8 \
            < ANCHO - MARGEN - fuentes["pequeno"].getlength(fecha):
        d.text((ANCHO - MARGEN, 40), fecha, font=fuentes["pequeno"],
               fill=APAGADO, anchor="rm")


def _boton(d, b, fuentes):
    fondo, texto = {
        "normal": (BOTON, TEXTO),
        "activo": (ACENTO, FONDO),
        "alerta": (ALERTA, FONDO),
        "deshabilitado": (BOTON_APAGADO, APAGADO),
    }[b.estilo]
    caja = (b.x, b.y, b.x + b.ancho - 1, b.y + b.alto - 1)
    if b.id.startswith("nav_"):
        d.rectangle(caja, fill=fondo)
    else:
        d.rounded_rectangle(caja, radius=6, fill=fondo)
    d.multiline_text((b.x + b.ancho / 2, b.y + b.alto / 2), b.etiqueta,
                     font=fuentes["negrita"], fill=texto, anchor="mm",
                     align="center", spacing=2)


def _vista_sesion(d, estado, fuentes):
    d.rounded_rectangle((MARGEN, 52, ANCHO - MARGEN, 148), radius=6, fill=PANEL)
    for i, (texto, tono) in enumerate(lineas_sesion(estado)):
        d.text((MARGEN + 8, 61 + i * 18),
               ajustar_texto(texto, fuentes["texto"], INTERIOR - 16),
               font=fuentes["texto"], fill=TONOS[tono], anchor="lm")
    # Franja de avisos y actuadores: un aviso la ocupa un momento.
    if estado.aviso:
        d.text((ANCHO // 2, 264), ajustar_texto(estado.aviso[0], fuentes["negrita"],
                                                 INTERIOR),
               font=fuentes["negrita"], fill=ALERTA, anchor="mm")
        return
    color = TEXTO if estado.conectado else APAGADO
    for i, (rotulo, valor) in enumerate(resumen_actuadores(estado)):
        x = MARGEN + 37 + i * 75
        d.text((x, 254), rotulo, font=fuentes["pequeno"], fill=APAGADO, anchor="mm")
        d.text((x, 270), ajustar_texto(valor, fuentes["texto"], 72),
               font=fuentes["texto"], fill=color, anchor="mm")


def _vista_lista(d, estado, fuentes):
    d.text((MARGEN, 66), "Asistencia", font=fuentes["subtitulo"], fill=TEXTO,
           anchor="lm")
    a = estado.asistencia
    for i, (campo, rotulo, tono) in enumerate((
            ("presentes", "Presentes", "ok"), ("retardos", "Retardos", "alerta"),
            ("faltantes", "Faltantes", "error"))):
        y = 80 + i * 56
        d.rounded_rectangle((MARGEN, y, ANCHO - MARGEN, y + 50), radius=6,
                            fill=PANEL)
        d.text((MARGEN + 12, y + 25), rotulo, font=fuentes["negrita"],
               fill=TONOS[tono], anchor="lm")
        if a:
            d.text((ANCHO - MARGEN - 12, y + 25), str(a[campo]),
                   font=fuentes["numero"], fill=TEXTO, anchor="rm")
        else:
            d.text((ANCHO - MARGEN - 12, y + 25), SIN_DATOS,
                   font=fuentes["texto"], fill=APAGADO, anchor="rm")
    if not a:
        leyenda = ("Sin sesión activa."
                   if estado.sesion and not estado.sesion["activa"]
                   else "Esperando los conteos de la sesión.")
        for i, linea in enumerate(envolver(
                leyenda, fuentes["pequeno"], INTERIOR, 2)):
            d.text((ANCHO // 2, 258 + i * 15), linea, font=fuentes["pequeno"],
                   fill=APAGADO, anchor="mm")


def _vista_guia(d, estado, fuentes):
    t = texto_guia(estado)
    g = estado.guia
    for i, linea in enumerate(envolver(t["nombre"], fuentes["titulo"], INTERIOR, 2)):
        d.text((ANCHO // 2, 64 + i * 24), linea, font=fuentes["titulo"],
               fill=TEXTO, anchor="mm")
    d.text((ANCHO // 2, 130), t["contador"], font=fuentes["contador"],
           fill=ACENTO, anchor="mm")
    # Barra de avance: una casilla por pose
    final = g["estado"] in ("completado", "cancelado", "error")
    hechas = g["paso"] if g["estado"] in ("aceptada", "completado") \
        else max(g["paso"] - 1, 0)
    ancho_c = (INTERIOR - 4 * (g["total"] - 1)) / g["total"]
    for i in range(g["total"]):
        x0 = MARGEN + i * (ancho_c + 4)
        color = OK if i < hechas else (ACENTO if i == hechas and not final
                                        else PANEL)
        d.rounded_rectangle((x0, 164, x0 + ancho_c, 174), radius=3, fill=color)
    for i, linea in enumerate(envolver(t["instruccion"], fuentes["subtitulo"],
                                       INTERIOR, 3)):
        d.text((ANCHO // 2, 194 + i * 19), linea, font=fuentes["subtitulo"],
               fill=TEXTO, anchor="mm")
    # El mensaje va sobre el boton "Salir de la guia" (y=282): se deja el
    # mismo aire que la franja de avisos de la vista Sesion (unos 10 px).
    for i, linea in enumerate(envolver(t["mensaje"], fuentes["subtitulo"],
                                       INTERIOR, 2)):
        d.text((ANCHO // 2, 244 + i * 19), linea, font=fuentes["subtitulo"],
               fill=TONOS[t["tono"]], anchor="mm")


def dibujar_error(fuentes):
    """Cuadro fijo, sin ningun dato variable, para cuando dibujar() falla."""
    img = Image.new("RGB", (ANCHO, ALTO), FONDO)
    d = ImageDraw.Draw(img)
    d.text((ANCHO // 2, 130), "Error de datos", font=fuentes["titulo"],
           fill=ERROR, anchor="mm")
    d.text((ANCHO // 2, 165), "No se puede mostrar", font=fuentes["texto"],
           fill=TEXTO, anchor="mm")
    d.text((ANCHO // 2, 185), "el estado recibido", font=fuentes["texto"],
           fill=TEXTO, anchor="mm")
    return img


def dibujar(estado, ahora_dt, fuentes):
    """Imagen RGB de 240x320 de la vista visible; no toca el hardware."""
    img = Image.new("RGB", (ANCHO, ALTO), FONDO)
    d = ImageDraw.Draw(img)
    vista = estado.vista_visible
    _encabezado(d, ahora_dt, fuentes, "Registro" if vista == "guia" else "AutoList")
    _conexion(d, estado, ahora_dt, fuentes)
    {"sesion": _vista_sesion, "lista": _vista_lista,
     "guia": _vista_guia}[vista](d, estado, fuentes)
    for b in botones(estado):
        _boton(d, b, fuentes)
    return img
