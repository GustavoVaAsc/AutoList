#!/usr/bin/env python3
"""Genera el manifiesto de landmarks para calibrar la cuantización de w600k_mbf.

Corre el ONNX de RetinaFace R50 a 320 del model zoo de Allwinner en flotante,
con onnxruntime, y aplica el mismo preproceso y la misma decodificación que la
referencia de la A733. Por imagen conserva la cara de mayor score y escribe una
línea del manifiesto en el formato TAB de herramientas/alinear_lfw.sh:

    ruta<TAB>x1 y1 x2 y2 x3 y3 x4 y4 x5 y5

en píxeles de la imagen original, en el orden ojo izquierdo, ojo derecho,
nariz, boca izquierda y boca derecha (el de FaceDetection::landmarks).

Uso:
    python3 landmarks_retinaface.py --onnx RUTA.onnx --lista imagenes.txt \
        --salida manifiesto.tsv [--umbral 0.4] [--max N] [--rgb]

--rgb alimenta el modelo en RGB (invierte los canales y la media, que pasa a
[123,117,104]); por omisión es BGR. Las rutas se escriben absolutas.

Requisitos (solo en la laptop): onnxruntime, numpy y opencv-python. Se importan
dentro de las funciones que los usan; la lógica pura (anclas, decodificación,
NMS, letterbox, formato de línea y barrera de salida) usa solo la biblioteca
estándar y se prueba en la placa con test_landmarks_retinaface.py.

AVISO: los landmarks son datos biométricos. La salida no puede quedar dentro
del repositorio (misma barrera que herramientas/alinear_lfw.sh) y el script
nunca imprime rutas ni coordenadas, solo conteos.

Referencia: model zoo de Allwinner, awnpu_model_zoo v0.9.0-20260116-83a67d4b,
examples/retinaface/ (en adelante "zoo:"):
    retinaface_pre.cpp, retinaface_post.cpp, model_config.h y
    convert_model/config_yml.py.

Lo que se dedujo del ONNX (sin poder ejecutarlo en la placa):
  - Es el ONNX original de biubug6/Pytorch_Retinaface (convert_to_onnx.py,
    fase "test") con entrada fija de 320: float32 NCHW [1,3,320,320].
  - Espera BGR y la resta de la media [104,117,123] hecha fuera del grafo
    (biubug6 detect.py:94). En el .nb esa resta la hace el nodo de preproceso
    de Acuity (zoo: convert_model/config_yml.py:13-14); aquí se hace en numpy,
    después del relleno de 114 del letterbox, igual que en el .nb.
  - Salidas: loc [1,N,4], conf [1,N,2] (softmax dentro del grafo, cara en el
    índice 1) y landms [1,N,10]. Se identifican por el último eje.
Queda sin confirmar: el orden de canales
exacto (BGR frente a RGB: se asume BGR, como biubug6) y la forma real de las
entradas y salidas del ONNX publicado. El script lee los metadatos y falla con
un mensaje claro si no encajan.
"""

import argparse
import collections
import math
import os
import sys

# zoo: model_config.h:12-13 (LETTERBOX_ROWS/COLS) y retinaface_pre.cpp:76.
TAMANO_ENTRADA = 320
RELLENO_BGR = 114
# zoo: convert_model/config_yml.py:13 (MEAN) y :14 (SCALE = 1).
MEDIA_BGR = (104.0, 117.0, 123.0)


def media_efectiva(rgb):
    """Media por canal en el orden del tensor: BGR por omisión, RGB con --rgb."""
    return MEDIA_BGR[::-1] if rgb else MEDIA_BGR


# zoo: model_config.h:19-20 (SCORE_THRESHOLD, NMS_THRESHOLD).
UMBRAL_POR_OMISION = 0.4
UMBRAL_NMS = 0.45
# zoo: retinaface_post.cpp:132-133 (min_sizes y steps) y :188 (variances).
MIN_SIZES = ((16, 32), (64, 128), (256, 512))
STEPS = (8, 16, 32)
VARIANZAS = (0.1, 0.2)
NUM_PUNTOS = 5

Letterbox = collections.namedtuple(
    "Letterbox",
    "ancho_redim alto_redim arriba abajo izquierda derecha pad_ancho pad_alto ratio_x ratio_y",
)


def _redondear(x):
    """round() de C++ (mitad lejos de cero), no el redondeo bancario de Python."""
    return int(math.floor(x + 0.5)) if x >= 0 else -int(math.floor(-x + 0.5))


# ---------------------------------------------------------------- anclas


def generar_anclas(alto, ancho):
    """Anclas (cx, cy, ancho, alto) normalizadas, en el orden de la referencia.

    zoo: retinaface_post.cpp:130-164 (create_priorBox; ceil en :138-139): por stride, fila,
    columna y min_size; centro (k+0.5)*step; tamaño min_size, ambos
    normalizados por ancho y alto. Sin clip. A 320 salen 4200 y a 640, 16 800.
    """
    anclas = []
    for step, min_sizes in zip(STEPS, MIN_SIZES):
        filas = math.ceil(alto / step)
        columnas = math.ceil(ancho / step)
        for j in range(filas):
            for k in range(columnas):
                for m in min_sizes:
                    anclas.append(
                        ((k + 0.5) * step / ancho, (j + 0.5) * step / alto, m / ancho, m / alto)
                    )
    return anclas


# ---------------------------------------------------------- decodificación


def decodificar(delta_caja, delta_lm, ancla):
    """Decodifica una caja y sus 5 landmarks, normalizados al letterbox.

    Devuelve ((x, y, ancho, alto), [(x, y)] * 5). zoo: retinaface_post.cpp:
    - :202-205 el centro suma delta * 0.1 * tamaño del ancla; el tamaño es
      exp(delta * 0.2) * tamaño del ancla;
    - :218-219 cada landmark es delta * 0.1 * tamaño del ancla + centro del
      ancla (relativo al ancla, no a la caja decodificada).
    """
    cx_a, cy_a, w_a, h_a = ancla
    cx = delta_caja[0] * VARIANZAS[0] * w_a + cx_a
    cy = delta_caja[1] * VARIANZAS[0] * h_a + cy_a
    w = math.exp(delta_caja[2] * VARIANZAS[1]) * w_a
    h = math.exp(delta_caja[3] * VARIANZAS[1]) * h_a
    lms = [
        (
            delta_lm[2 * j] * VARIANZAS[0] * w_a + cx_a,
            delta_lm[2 * j + 1] * VARIANZAS[0] * h_a + cy_a,
        )
        for j in range(NUM_PUNTOS)
    ]
    return (cx - w * 0.5, cy - h * 0.5, w, h), lms


def _iou(a, b):
    ax, ay, aw, ah = a
    bx, by, bw, bh = b
    ancho = min(ax + aw, bx + bw) - max(ax, bx)
    alto = min(ay + ah, by + bh) - max(ay, by)
    if ancho <= 0 or alto <= 0:
        return 0.0
    inter = ancho * alto
    union = aw * ah + bw * bh - inter
    return inter / union if union > 0 else 0.0


def nms(cajas, scores, umbral):
    """Índices que sobreviven al NMS, de mayor a menor score.

    Cajas (x, y, ancho, alto). zoo: retinaface_post.cpp:95-126
    (nms_sorted_bboxes): ordenadas por score descendente, una caja se descarta
    si su IoU con una ya elegida supera el umbral (estrictamente mayor).
    """
    orden = sorted(range(len(cajas)), key=lambda i: scores[i], reverse=True)
    elegidas = []
    for i in orden:
        if all(_iou(cajas[i], cajas[j]) <= umbral for j in elegidas):
            elegidas.append(i)
    return elegidas


# --------------------------------------------------------------- letterbox


def calcular_letterbox(alto_img, ancho_img, alto_in, ancho_in):
    """Cuentas del letterbox centrado.

    zoo: retinaface_pre.cpp:49-58 (escala = min de las dos razones, tamaño
    redondeado) y :49-73 (mitad del relleno por lado; arriba = round(dh - 0.1)
    y abajo = round(dh + 0.1), igual en horizontal) y retinaface_post.cpp:
    :245-252 (hpad, wpad, ratio_x, ratio_y).
    """
    if alto_img <= 0 or ancho_img <= 0 or alto_in <= 0 or ancho_in <= 0:
        raise ValueError("las dimensiones del letterbox deben ser positivas")
    escala = min(alto_in / alto_img, ancho_in / ancho_img)
    ancho_redim = _redondear(escala * ancho_img)
    alto_redim = _redondear(escala * alto_img)
    dh = (alto_in - alto_redim) / 2.0
    dw = (ancho_in - ancho_redim) / 2.0
    return Letterbox(
        ancho_redim,
        alto_redim,
        _redondear(dh - 0.1),
        _redondear(dh + 0.1),
        _redondear(dw - 0.1),
        _redondear(dw + 0.1),
        ancho_in - ancho_redim,
        alto_in - alto_redim,
        ancho_img / ancho_redim,
        alto_img / alto_redim,
    )


def deshacer_letterbox(x, y, lb, alto_in, ancho_in):
    """Pasa un punto normalizado al letterbox a píxeles de la imagen original.

    zoo: retinaface_post.cpp:245-252,272-273: (x * ancho_in - pad // 2) *
    ratio, con la división entera de C++ (pad / 2 con enteros). Sin recorte,
    igual que la referencia con los landmarks.
    """
    return (
        (x * ancho_in - lb.pad_ancho // 2) * lb.ratio_x,
        (y * alto_in - lb.pad_alto // 2) * lb.ratio_y,
    )


def hacia_letterbox(x, y, lb, alto_in, ancho_in):
    """Inversa de deshacer_letterbox: píxeles originales a coordenadas normalizadas."""
    return (
        (x / lb.ratio_x + lb.pad_ancho // 2) / ancho_in,
        (y / lb.ratio_y + lb.pad_alto // 2) / alto_in,
    )


def mejor_cara(loc, conf, landms, alto_img, ancho_img, alto_in, ancho_in, umbral, umbral_nms):
    """Landmarks (en píxeles originales) de la cara de mayor score, o None.

    loc, conf y landms son listas con una fila por ancla ([4], [2] y [10]).
    zoo: retinaface_post.cpp:200 (solo se decodifican las anclas cuyo
    score de cara, índice 1, supera estrictamente el umbral) y :95-126 (NMS).
    """
    anclas = generar_anclas(alto_in, ancho_in)
    if not (len(loc) == len(conf) == len(landms) == len(anclas)):
        raise ValueError(
            "el número de filas de las salidas (%d, %d, %d) no coincide con las %d anclas "
            "de una entrada de %dx%d"
            % (len(loc), len(conf), len(landms), len(anclas), ancho_in, alto_in)
        )
    cajas, scores, puntos = [], [], []
    for i, ancla in enumerate(anclas):
        score = conf[i][1]
        if score > umbral:
            caja, lms = decodificar(loc[i], landms[i], ancla)
            cajas.append(caja)
            scores.append(score)
            puntos.append(lms)
    elegidas = nms(cajas, scores, umbral_nms)
    if not elegidas:
        return None
    lb = calcular_letterbox(alto_img, ancho_img, alto_in, ancho_in)
    return [deshacer_letterbox(x, y, lb, alto_in, ancho_in) for x, y in puntos[elegidas[0]]]


# ------------------------------------------------- manifiesto, lista, barrera


def linea_manifiesto(ruta, puntos):
    """Línea del manifiesto en formato TAB: ruta<TAB>x1 y1 ... x5 y5."""
    if len(puntos) != NUM_PUNTOS:
        raise ValueError("se necesitan %d puntos" % NUM_PUNTOS)
    # Ruta absoluta: alinear_lfw.sh resuelve las relativas contra su propio
    # directorio de trabajo, que puede no ser el de este script.
    ruta = os.path.abspath(ruta)
    if "\t" in ruta or "\n" in ruta or "\r" in ruta:
        raise ValueError("la ruta no puede contener TAB ni saltos de línea")
    coords = []
    for x, y in puntos:
        if not (math.isfinite(x) and math.isfinite(y)):
            raise ValueError("coordenada no finita")
        coords.append("%.2f %.2f" % (x, y))
    return ruta + "\t" + " ".join(coords)


def leer_lista(lineas):
    """Rutas de una lista: se ignoran las líneas vacías y las que empiezan con #."""
    rutas = []
    for linea in lineas:
        ruta = linea.rstrip("\r\n")
        if ruta.strip() == "" or ruta.lstrip().startswith("#"):
            continue
        rutas.append(ruta)
    return rutas


def raiz_repo():
    """Raíz del repositorio: PASELISTA_RAIZ_REPO, o tres niveles sobre este archivo."""
    entorno = os.environ.get("PASELISTA_RAIZ_REPO")
    if entorno:
        return os.path.realpath(entorno)
    aqui = os.path.dirname(os.path.realpath(__file__))
    return os.path.realpath(os.path.join(aqui, "..", "..", ".."))


def salida_dentro_del_repo(salida, raiz):
    """True si la ruta de salida cae dentro del repo (o es el repo).

    Misma barrera que herramientas/alinear_lfw.sh/.cpp: se comparan rutas
    absolutas con los enlaces simbólicos y los '..' resueltos.
    """
    salida_abs = os.path.realpath(salida)
    raiz_abs = os.path.realpath(raiz)
    return os.path.commonpath([salida_abs, raiz_abs]) == raiz_abs


# ------------------------------------------------------- onnxruntime (laptop)


def _cargar_sesion(ruta_onnx):
    """Abre el ONNX y valida entrada y salidas. Devuelve (sesion, nombre_entrada, alto, ancho)."""
    try:
        import onnxruntime
    except ImportError:
        raise RuntimeError("falta onnxruntime: pip install onnxruntime numpy opencv-python")
    sesion = onnxruntime.InferenceSession(ruta_onnx, providers=["CPUExecutionProvider"])
    entradas = sesion.get_inputs()
    if len(entradas) != 1:
        raise RuntimeError("el ONNX debe tener 1 entrada y tiene %d" % len(entradas))
    entrada = entradas[0]
    forma = list(entrada.shape)
    if entrada.type != "tensor(float)":
        raise RuntimeError(
            "la entrada debe ser tensor(float) y es %s: ¿es el ONNX con el nodo de "
            "preproceso de Acuity en vez del original modificado?" % entrada.type
        )
    if len(forma) != 4 or forma[1] != 3 or not all(isinstance(d, int) and d > 0 for d in forma[1:]):
        raise RuntimeError(
            "la entrada debe ser NCHW [1,3,alto,ancho] fijo y es %s" % (forma,)
        )
    if isinstance(forma[0], int) and forma[0] != 1:
        raise RuntimeError("el lote de la entrada debe ser 1 o dinámico y es %s" % (forma[0],))
    return sesion, entrada.name, forma[2], forma[3]


def _inferir(sesion, nombre_entrada, bgr, alto_in, ancho_in, rgb=False):
    """Preproceso de la referencia, inferencia y separación de las 3 salidas por forma."""
    import cv2
    import numpy as np

    filas, cols = bgr.shape[:2]
    lb = calcular_letterbox(filas, cols, alto_in, ancho_in)
    redim = cv2.resize(bgr, (lb.ancho_redim, lb.alto_redim))
    # zoo: retinaface_pre.cpp:76 (copyMakeBorder con relleno 114, en BGR)
    marco = cv2.copyMakeBorder(
        redim, lb.arriba, lb.abajo, lb.izquierda, lb.derecha, cv2.BORDER_CONSTANT,
        value=(RELLENO_BGR,) * 3,
    )
    if marco.shape[:2] != (alto_in, ancho_in):
        raise RuntimeError("el letterbox dio %s y la entrada es %dx%d" % (marco.shape, ancho_in, alto_in))
    if rgb:
        marco = marco[:, :, ::-1]  # el relleno es igual en los 3 canales
    x = marco.astype(np.float32) - np.array(media_efectiva(rgb), dtype=np.float32)
    x = np.ascontiguousarray(x.transpose(2, 0, 1)[None])
    salidas = sesion.run(None, {nombre_entrada: x})

    por_ancho = {}
    for s in salidas:
        if s.ndim != 3 or s.shape[0] != 1 or s.shape[2] not in (2, 4, 10) or s.shape[2] in por_ancho:
            raise RuntimeError(
                "salidas inesperadas: se esperan loc [1,N,4], conf [1,N,2] y landms [1,N,10], "
                "formas recibidas: %s" % [tuple(o.shape) for o in salidas]
            )
        por_ancho[s.shape[2]] = s[0]
    if set(por_ancho) != {2, 4, 10}:
        raise RuntimeError("faltan salidas: se esperan los anchos 4, 2 y 10")
    return por_ancho[4].tolist(), por_ancho[2].tolist(), por_ancho[10].tolist()


# ---------------------------------------------------------------------- CLI


def procesar_lista(rutas, out, leer_imagen, inferir, alto_in, ancho_in, umbral, maximo):
    """Escribe en `out` la línea de cada imagen con cara; devuelve (escritas, omitidas).

    leer_imagen(ruta) devuelve la imagen (con .shape) o None si es ilegible;
    inferir(imagen) devuelve (loc, conf, landms) como listas. Una imagen
    ilegible, sin cara o cuya decodificación desborda (ArithmeticError, p. ej.
    exp de un delta enorme) cuenta como omitida y el lote sigue.
    """
    escritas = omitidas = 0
    for ruta in rutas:
        if maximo is not None and escritas >= maximo:
            break
        img = leer_imagen(ruta)
        if img is None:
            omitidas += 1
            continue
        loc, conf, lms = inferir(img)
        try:
            puntos = mejor_cara(loc, conf, lms, img.shape[0], img.shape[1],
                                alto_in, ancho_in, umbral, UMBRAL_NMS)
        except ArithmeticError:
            puntos = None
        if puntos is None:
            omitidas += 1
            continue
        out.write(linea_manifiesto(ruta, puntos) + "\n")
        escritas += 1
    return escritas, omitidas


def _umbral(texto):
    try:
        v = float(texto)
    except ValueError:
        raise argparse.ArgumentTypeError("no es un número: %r" % texto)
    if not (0.0 < v < 1.0):
        raise argparse.ArgumentTypeError("debe estar entre 0 y 1 (sin incluirlos)")
    return v


def _entero_positivo(texto):
    try:
        v = int(texto)
    except ValueError:
        raise argparse.ArgumentTypeError("no es un entero: %r" % texto)
    if v < 1:
        raise argparse.ArgumentTypeError("debe ser 1 o mayor")
    return v


def _parser():
    p = argparse.ArgumentParser(
        description="Genera el manifiesto de landmarks (formato TAB de alinear_lfw.sh) "
        "con RetinaFace R50 a 320 en flotante."
    )
    p.add_argument("--onnx", required=True, help="ONNX original modificado de RetinaFace (320)")
    p.add_argument("--lista", required=True, help="una ruta de imagen por línea")
    p.add_argument("--salida", required=True, help="manifiesto TAB; fuera del repositorio")
    p.add_argument("--umbral", type=_umbral, default=UMBRAL_POR_OMISION,
                   help="score mínimo de cara (por omisión %(default)s)")
    p.add_argument("--rgb", action="store_true",
                   help="alimenta RGB en vez de BGR (invierte los canales y la media)")
    p.add_argument("--max", type=_entero_positivo, default=None, dest="maximo",
                   help="corta después de N imágenes escritas")
    return p


def main(argv=None):
    args = _parser().parse_args(argv)

    if salida_dentro_del_repo(args.salida, raiz_repo()):
        print(
            "Error: la salida está dentro del repositorio. Los landmarks y los recortes son "
            "datos biométricos: un 'git add .' los metería en el historial. Usa una ruta "
            "fuera del repositorio (por ejemplo /tmp/manifiesto.tsv).",
            file=sys.stderr,
        )
        return 1
    if not os.path.isfile(args.onnx):
        print("Error: --onnx no es un archivo", file=sys.stderr)
        return 1
    if not os.path.isfile(args.lista):
        print("Error: --lista no es un archivo", file=sys.stderr)
        return 1

    try:
        import cv2  # noqa: F401
        import numpy  # noqa: F401
        sesion, nombre, alto_in, ancho_in = _cargar_sesion(args.onnx)
    except ImportError as e:
        print("Error: falta una dependencia (%s): pip install onnxruntime numpy opencv-python"
              % e.name, file=sys.stderr)
        return 1
    except RuntimeError as e:
        print("Error: %s" % e, file=sys.stderr)
        return 1

    with open(args.lista, encoding="utf-8") as f:
        rutas = leer_lista(f)

    import cv2

    try:
        carpeta = os.path.dirname(os.path.abspath(args.salida))
        os.makedirs(carpeta, exist_ok=True)
        with open(args.salida, "w", encoding="utf-8") as out:
            escritas, omitidas = procesar_lista(
                rutas, out, lambda ruta: cv2.imread(ruta, 1),
                lambda bgr: _inferir(sesion, nombre, bgr, alto_in, ancho_in, args.rgb),
                alto_in, ancho_in, args.umbral, args.maximo,
            )
    except (RuntimeError, ValueError, OSError) as e:
        print("Error: %s" % e, file=sys.stderr)
        return 1

    print("Imágenes escritas: %d; omitidas (sin cara o ilegibles): %d" % (escritas, omitidas),
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
