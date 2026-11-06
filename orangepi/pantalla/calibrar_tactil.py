"""
Calibracion del tactil XPT2046 con la pantalla delante.

Con la unidad de la pantalla detenida (el programa y esta herramienta usan los
mismos GPIO y el mismo bus SPI), desde orangepi/pantalla:

  .venv/bin/python calibrar_tactil.py --crudo   imprime el crudo del tactil
                                                mientras lo tocas (Ctrl+C sale)
  .venv/bin/python calibrar_tactil.py           guiada: pide tocar 4 cruces,
                                                imprime los PANTALLA_TACTIL_* y
                                                luego dibuja un punto donde
                                                detecta cada toque, para
                                                comprobarlos (Ctrl+C sale)

La calibracion depende de PANTALLA_ROTACION y del montaje del panel: hay que
repetirla si cambian. Usa la misma configuracion (PANTALLA_*) que main.py.
Salida: 0, 74 (no se pudo abrir un dispositivo) o 78 (configuracion invalida).
"""

import argparse
import signal
import sys
import threading
import time

from PIL import Image, ImageDraw

import main as programa
import vistas
from ili9341_opi import color565
from tactil import (Calibracion, DetectorToque, a_pixeles,
                    calcular_calibracion)

MARGEN_PX = 20          # las cruces se dibujan a 20 px de los bordes
BRAZO_PX = 10
PERIODO_S = 0.02
PERIODO_CRUDO_S = 0.1   # una linea cada 100 ms en el modo --crudo
CLAVES = ("x_min", "x_max", "y_min", "y_max", "cambiar_ejes", "invertir_x",
          "invertir_y")

ESQUINAS_PX = ((MARGEN_PX, MARGEN_PX), (vistas.ANCHO - MARGEN_PX, MARGEN_PX),
               (vistas.ANCHO - MARGEN_PX, vistas.ALTO - MARGEN_PX),
               (MARGEN_PX, vistas.ALTO - MARGEN_PX))


def formato_salida(cal):
    """Lineas PANTALLA_TACTIL_*=valor, para `Environment=` o para CONFIG."""
    return ["PANTALLA_TACTIL_%s=%d" % (c.upper(), cal[c]) for c in CLAVES]


def dibujar_objetivo(indice, fuentes):
    """Imagen con las 4 cruces; la que hay que tocar ahora va en verde."""
    img = Image.new("RGB", (vistas.ANCHO, vistas.ALTO), vistas.FONDO)
    d = ImageDraw.Draw(img)
    for i, (x, y) in enumerate(ESQUINAS_PX):
        activa = i == indice
        color = vistas.OK if activa else vistas.APAGADO
        brazo = BRAZO_PX if activa else BRAZO_PX // 2
        d.line((x - brazo, y, x + brazo, y), fill=color, width=3 if activa else 1)
        d.line((x, y - brazo, x, y + brazo), fill=color, width=3 if activa else 1)
    centro = vistas.ANCHO // 2
    d.text((centro, 130), "Calibración", font=fuentes["titulo"],
           fill=vistas.TEXTO, anchor="mm")
    d.text((centro, 165), "Toca la cruz verde", font=fuentes["negrita"],
           fill=vistas.OK, anchor="mm")
    d.text((centro, 190), "%d de %d" % (indice + 1, len(ESQUINAS_PX)),
           font=fuentes["negrita"], fill=vistas.APAGADO, anchor="mm")
    return img


def tomar_esquina(tactil, parar, reloj, dormir):
    """Espera un toque y devuelve su crudo (x, y); None si hay que parar."""
    detector = DetectorToque()
    while not parar.is_set():
        presionado = tactil.is_pressed()
        crudo = tactil.get_coordinates() if presionado else None
        toque = detector.actualizar(presionado, crudo, reloj())
        if toque is not None:
            return toque
        dormir(PERIODO_S)
    return None


def modo_crudo(tactil, parar, salida, reloj, dormir):
    """Imprime el crudo mientras haya un dedo puesto."""
    while not parar.is_set():
        if tactil.is_pressed():
            x, y = tactil.get_coordinates()
            print("x=%4d y=%4d" % (x, y), file=salida, flush=True)
            dormir(PERIODO_CRUDO_S)
        else:
            dormir(PERIODO_S)


def calibrar(pantalla, tactil, fuentes, parar, salida, reloj, dormir):
    """Pide tocar las 4 esquinas. Devuelve el dict de calibracion, o None si se
    cancela o las esquinas no son coherentes."""
    puntos = []
    for i in range(len(ESQUINAS_PX)):
        pantalla.image(dibujar_objetivo(i, fuentes))
        crudo = tomar_esquina(tactil, parar, reloj, dormir)
        if crudo is None:
            return None
        print("esquina %d de %d: x=%d y=%d" % (i + 1, len(ESQUINAS_PX), *crudo),
              file=salida, flush=True)
        puntos.append(crudo)
    try:
        resultado = calcular_calibracion(puntos, vistas.ANCHO, vistas.ALTO,
                                         MARGEN_PX)
    except ValueError as e:
        print("No se pudo calibrar: %s. Vuelve a correr la herramienta." % e,
              file=salida, flush=True)
        return None
    print("\nCalibracion (en la unidad, como lineas Environment=; o como "
          "valores por defecto de CONFIG en main.py):", file=salida)
    print("\n".join(formato_salida(resultado)), file=salida, flush=True)
    return resultado


def probar(pantalla, tactil, cal, parar, reloj, dormir):
    """Dibuja un punto verde donde se detecta cada toque, con `cal`."""
    while not parar.is_set():
        crudo = tomar_esquina(tactil, parar, reloj, dormir)
        if crudo is None:
            return
        x, y = a_pixeles(crudo[0], crudo[1], cal, vistas.ANCHO, vistas.ALTO)
        pantalla.fill_rectangle(x - 3, y - 3, 7, 7, color565(0, 255, 0))


def main(argv=None):
    ap = argparse.ArgumentParser(description="Calibracion del tactil XPT2046.")
    ap.add_argument("--crudo", action="store_true",
                    help="solo imprime las coordenadas crudas mientras tocas")
    args = ap.parse_args(argv)

    try:
        cfg = programa.leer_config()
    except ValueError as e:
        print("calibrar_tactil: configuración inválida: %s" % e, file=sys.stderr)
        return programa.EX_CONFIG
    parar = threading.Event()
    for senal in (signal.SIGTERM, signal.SIGINT):
        signal.signal(senal, lambda *_: parar.set())

    try:
        tactil = programa.abrir_tactil(cfg)
    except OSError as e:
        print("calibrar_tactil: no se pudo abrir el táctil: %s" % e,
              file=sys.stderr)
        return programa.EX_IOERR
    try:
        if args.crudo:
            print("Toca la pantalla; Ctrl+C para salir.", flush=True)
            modo_crudo(tactil, parar, sys.stdout, time.monotonic, time.sleep)
            return 0
        pantalla = programa.abrir_pantalla(cfg)
        try:
            resultado = calibrar(pantalla, tactil, vistas.cargar_fuentes(), parar,
                                 sys.stdout, time.monotonic, time.sleep)
            if resultado:
                print("\nComprueba: toca la pantalla y debe aparecer un punto "
                      "verde justo bajo el dedo. Ctrl+C para salir.", flush=True)
                probar(pantalla, tactil,
                       Calibracion(*(resultado[c] for c in CLAVES[:4]),
                                   *(bool(resultado[c]) for c in CLAVES[4:])),
                       parar, time.monotonic, time.sleep)
        finally:
            programa.apagar(pantalla)
    except OSError as e:
        print("calibrar_tactil: falló un dispositivo: %s" % e, file=sys.stderr)
        return programa.EX_IOERR
    finally:
        tactil.cerrar()
    return 0


if __name__ == "__main__":
    sys.exit(main())
