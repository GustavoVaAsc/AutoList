"""
Tactil XPT2046: de coordenadas crudas a pixeles, calibracion y deteccion de
un toque. No toca hardware: la lectura del chip esta en xpt2046_opi.py.

El XPT2046 entrega dos valores de 12 bits (0 a 4095). La calibracion dice a
que pixeles de la pantalla corresponden; depende de la rotacion y del montaje
del panel, asi que se mide con calibrar_tactil.py.
"""

from collections import namedtuple
from statistics import median

# x_min, x_max, y_min, y_max: crudo que corresponde a los bordes de la pantalla
# DESPUES de aplicar cambiar_ejes. invertir_x e invertir_y: el crudo crece hacia
# la izquierda o hacia arriba.
Calibracion = namedtuple(
    "Calibracion", "x_min x_max y_min y_max cambiar_ejes invertir_x invertir_y")

CRUDO_MAX = 4095
MIN_MUESTRAS = 2        # lecturas validas para aceptar un toque (descarta ruido)
SUELTA_S = 0.06         # sin dedo este tiempo = se solto (absorbe rebotes)
MIN_RANGO = 500         # recorrido crudo minimo de un eje al calibrar


def calibracion_de_config(cfg):
    return Calibracion(
        cfg["TACTIL_X_MIN"], cfg["TACTIL_X_MAX"],
        cfg["TACTIL_Y_MIN"], cfg["TACTIL_Y_MAX"],
        bool(cfg["TACTIL_CAMBIAR_EJES"]),
        bool(cfg["TACTIL_INVERTIR_X"]), bool(cfg["TACTIL_INVERTIR_Y"]))


def _fraccion(valor, minimo, maximo, invertir):
    f = (valor - minimo) / (maximo - minimo)
    return 1 - f if invertir else f


def a_pixeles(crudo_x, crudo_y, cal, ancho, alto):
    """Pixel (x, y) de la pantalla para una lectura cruda; recorta a los bordes."""
    if cal.cambiar_ejes:
        crudo_x, crudo_y = crudo_y, crudo_x
    fx = _fraccion(crudo_x, cal.x_min, cal.x_max, cal.invertir_x)
    fy = _fraccion(crudo_y, cal.y_min, cal.y_max, cal.invertir_y)
    return (min(max(int(fx * ancho), 0), ancho - 1),
            min(max(int(fy * alto), 0), alto - 1))


def _media(a, b):
    return ((a[0] + b[0]) / 2, (a[1] + b[1]) / 2)


def _eje(a, b, largo, margen):
    """(minimo, maximo, invertido) del crudo de un eje, extrapolado a los bordes.

    a y b son el crudo medido en el pixel `margen` y en `largo - margen`.
    """
    por_pixel = (b - a) / (largo - 2 * margen)
    en_cero = a - margen * por_pixel
    en_largo = b + margen * por_pixel
    return (round(min(en_cero, en_largo)), round(max(en_cero, en_largo)),
            int(en_cero > en_largo))


def calcular_calibracion(esquinas, ancho, alto, margen):
    """Calibracion a partir del crudo de 4 toques en las esquinas.

    `esquinas`: [(x, y)] crudo de arriba-izquierda, arriba-derecha,
    abajo-derecha y abajo-izquierda, tocadas a `margen` pixeles de los bordes.
    Devuelve un dict con las claves de Calibracion (los booleanos como 0 o 1,
    como se pasan por PANTALLA_TACTIL_*). ValueError si no son coherentes.
    """
    if len(esquinas) != 4:
        raise ValueError("se necesitan 4 esquinas, llegaron %d" % len(esquinas))
    tl, tr, br, bl = esquinas
    izq, der = _media(tl, bl), _media(tr, br)
    arr, aba = _media(tl, tr), _media(bl, br)
    cambiar = abs(der[1] - izq[1]) > abs(der[0] - izq[0])
    cx, cy = (1, 0) if cambiar else (0, 1)
    if (abs(der[cx] - izq[cx]) < MIN_RANGO or abs(aba[cy] - arr[cy]) < MIN_RANGO
            or abs(aba[cx] - arr[cx]) > abs(aba[cy] - arr[cy])):
        raise ValueError("las esquinas no son coherentes: cada eje de la "
                         "pantalla debe recorrer un canal distinto del tactil")
    x_min, x_max, inv_x = _eje(izq[cx], der[cx], ancho, margen)
    y_min, y_max, inv_y = _eje(arr[cy], aba[cy], alto, margen)
    return {"x_min": x_min, "x_max": x_max, "y_min": y_min, "y_max": y_max,
            "cambiar_ejes": int(cambiar), "invertir_x": inv_x,
            "invertir_y": inv_y}


class DetectorToque:
    """Convierte lecturas periodicas en toques: uno por cada vez que se pone y
    se suelta el dedo, con la mediana de lo medido mientras estuvo puesto.

    Se entrega al soltar porque las primeras lecturas, al apoyar, son ruidosas.
    """

    def __init__(self):
        self._muestras = []
        self._ultimo = None         # ultima vez que se vio el dedo puesto

    def actualizar(self, presionado, crudo, ahora):
        """Una lectura. Devuelve el crudo (x, y) del toque al soltar, o None."""
        if presionado:
            self._ultimo = ahora
            if crudo and all(0 < c < CRUDO_MAX for c in crudo):
                self._muestras.append(crudo)
            return None
        if self._ultimo is None or ahora - self._ultimo < SUELTA_S:
            return None
        muestras, self._muestras, self._ultimo = self._muestras, [], None
        if len(muestras) < MIN_MUESTRAS:
            return None
        return (int(median(m[0] for m in muestras)),
                int(median(m[1] for m in muestras)))
