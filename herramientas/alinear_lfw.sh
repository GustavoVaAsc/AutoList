#!/bin/sh
# Alinea las imagenes de un manifiesto a 112x112 con el mismo warp que el
# servicio en produccion (vision::align_face) y escribe un PNG por imagen.
#
# Compila alinear_lfw.cpp junto con orangepi/vision/src/alignment.cpp y lo
# ejecuta. El binario se compila en un directorio temporal: el script no
# modifica el repositorio, solo escribe en el directorio de salida.
#
# Uso:
#   herramientas/alinear_lfw.sh --manifest RUTA --out DIR
#
# Las rutas del manifiesto y del directorio de salida se pueden dar relativas
# al directorio de trabajo actual.
set -eu

RAIZ="$(cd "$(dirname "$0")/.." && pwd)"

MANIFIESTO=""
SALIDA=""
while [ $# -gt 0 ]; do
    case "$1" in
        --manifest)
            [ $# -ge 2 ] || { echo "Error: --manifest necesita una ruta" >&2; exit 1; }
            MANIFIESTO="$2"; shift 2 ;;
        --out)
            [ $# -ge 2 ] || { echo "Error: --out necesita una ruta" >&2; exit 1; }
            SALIDA="$2"; shift 2 ;;
        -h|--help)
            echo "Uso: $(basename "$0") --manifest RUTA --out DIR"; exit 0 ;;
        *)
            echo "Error: argumento desconocido: $1" >&2
            echo "Uso: $(basename "$0") --manifest RUTA --out DIR" >&2
            exit 1 ;;
    esac
done

if [ -z "$MANIFIESTO" ] || [ -z "$SALIDA" ]; then
    echo "Error: --manifest y --out son obligatorios" >&2
    echo "Uso: $(basename "$0") --manifest RUTA --out DIR" >&2
    exit 1
fi

if ! command -v pkg-config >/dev/null 2>&1; then
    echo "Error: pkg-config no esta instalado" >&2
    exit 1
fi
if ! pkg-config --exists opencv4; then
    echo "Error: no se encuentra opencv4 con pkg-config" >&2
    exit 1
fi

CFLAGS="$(pkg-config --cflags opencv4)"
LIBS="$(pkg-config --libs opencv4)"

# Los headers de OpenCV avisan con -Wextra de enums deprecated; como sistema
# (no como includes del usuario) esos avisos no ensucian la compilacion.
CFLAGS="$(printf '%s' "$CFLAGS" | sed 's/-I/-isystem /g')"

# El binario va a un directorio temporal: el repositorio no se toca.
DIR_TMP="$(mktemp -d)"
limpiar() {
    rm -f "$DIR_TMP/alinear_lfw"
    rmdir "$DIR_TMP" 2>/dev/null || true
}
trap limpiar EXIT INT TERM

# -j1 y una sola compilacion: la placa es de 4 GB y se ha reiniciado por falta
# de memoria al compilar en paralelo.
g++ -std=c++20 -O2 -Wall -Wextra \
    -I "$RAIZ/orangepi/vision/include" \
    "$RAIZ/herramientas/alinear_lfw.cpp" \
    "$RAIZ/orangepi/vision/src/alignment.cpp" \
    $CFLAGS -o "$DIR_TMP/alinear_lfw" $LIBS

"$DIR_TMP/alinear_lfw" --manifest "$MANIFIESTO" --out "$SALIDA"
