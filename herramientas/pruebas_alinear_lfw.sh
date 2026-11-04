#!/bin/sh
# Pruebas de la herramienta de alineacion (herramientas/alinear_lfw.sh).
#
# Comprueba los tres formatos de manifiesto, las rutas con espacios, el
# rechazo de las coordenadas no finitas, el de las lineas mal formadas, el
# limite de tamano de imagen y la barrera que impide escribir datos
# biometricos dentro del repositorio.
#
# No necesita red ni el dataset real: genera un PNG sintetico con python3 y
# comprueba el resultado con el propio binario.
#
# Uso:
#   herramientas/pruebas_alinear_lfw.sh
#
# Sale con 0 si todas las comprobaciones pasan y con 1 si alguna falla.
set -eu

RAIZ="$(cd "$(dirname "$0")/.." && pwd)"
DIR_TMP="$(mktemp -d)"
# El binario va dentro de DIR_TMP, que el trap borra: mktemp -u ademas es
# predictable (no es atomico).
BINARIO="$DIR_TMP/alinear_lfw"
FALLOS=0

limpiar() {
    rm -rf "$DIR_TMP"
}
trap limpiar EXIT INT TERM

if ! command -v python3 >/dev/null 2>&1; then
    echo "Error: python3 no esta instalado" >&2
    exit 1
fi

# Genera un PNG RGB solido, sin dependencias externas de python.
generar_png() {
    python3 - "$1" "$2" "$3" <<'PY'
import struct, sys, zlib
ruta, ancho, alto = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
crudo = b"".join(b"\x00" + bytes((120, 130, 140)) * ancho for _ in range(alto))
def trozo(tipo, datos):
    return (struct.pack(">I", len(datos)) + tipo + datos +
            struct.pack(">I", zlib.crc32(tipo + datos) & 0xffffffff))
with open(ruta, "wb") as f:
    f.write(b"\x89PNG\r\n\x1a\n")
    f.write(trozo(b"IHDR", struct.pack(">IIBBBBB", ancho, alto, 8, 2, 0, 0, 0)))
    f.write(trozo(b"IDAT", zlib.compress(crudo, 6)))
    f.write(trozo(b"IEND", b""))
PY
}

# Devuelve "ancho alto" de un PNG.
tam_png() {
    python3 - "$1" <<'PY'
import struct, sys
datos = open(sys.argv[1], "rb").read(24)
print("%d %d" % struct.unpack(">II", datos[16:24]))
PY
}

ok() { echo "PASA: $1"; }
fallo() {
    echo "FALLA: $1" >&2
    FALLOS=$((FALLOS + 1))
}

# Compila el binario una sola vez: la placa tiene 4 GB.
if ! command -v pkg-config >/dev/null 2>&1 || ! pkg-config --exists opencv4; then
    echo "Error: no se encuentra opencv4 con pkg-config" >&2
    exit 1
fi
CFLAGS="$(pkg-config --cflags opencv4 | sed 's/-I/-isystem /g')"
LIBS="$(pkg-config --libs opencv4)"
mkdir -p "$(dirname "$BINARIO")"
g++ -std=c++20 -O2 -Wall -Wextra \
    -I "$RAIZ/orangepi/vision/include" \
    "$RAIZ/herramientas/alinear_lfw.cpp" \
    "$RAIZ/orangepi/vision/src/alignment.cpp" \
    $CFLAGS -o "$BINARIO" $LIBS

# Landmarks de la imagen sintetica de 250x250.
LANDMARKS="95 110 155 110 125 140 108 165 142 165"
IMAGEN="$DIR_TMP/rostro.png"
IMAGEN_ESPACIOS="$DIR_TMP/rostro grande.png"
generar_png "$IMAGEN" 250 250
generar_png "$IMAGEN_ESPACIOS" 250 250
generar_png "$DIR_TMP/enorme.png" 4100 100

# Comprueba que la herramienta escribe un PNG de 112x112 y sale con 0.
comprobar_alineado() {
    nombre="$1"; salida="$2"; png="$3"
    if [ ! -f "$png" ]; then
        fallo "$nombre: no se escribio $png"
        return
    fi
    if [ "$(tam_png "$png")" != "112 112" ]; then
        fallo "$nombre: la salida no es de 112x112 ($(tam_png "$png"))"
        return
    fi
    ok "$nombre"
}

echo "== Formato (a): todo separado por espacios"
rm -rf "$DIR_TMP/sal_a"
printf '%s %s\n' "$IMAGEN" "$LANDMARKS" > "$DIR_TMP/a.txt"
"$BINARIO" --manifest "$DIR_TMP/a.txt" --out "$DIR_TMP/sal_a" --repo-root "$RAIZ" > "$DIR_TMP/a.log" 2>&1 || {
    fallo "formato (a): la herramienta salio con error"; cat "$DIR_TMP/a.log" >&2; }
comprobar_alineado "formato (a)" "$DIR_TMP/sal_a" "$DIR_TMP/sal_a/rostro.png"

echo "== Formato (b): TAB entre la ruta y las coordenadas"
rm -rf "$DIR_TMP/sal_b"
printf '%s\t%s\n' "$IMAGEN" "$LANDMARKS" > "$DIR_TMP/b.txt"
"$BINARIO" --manifest "$DIR_TMP/b.txt" --out "$DIR_TMP/sal_b" --repo-root "$RAIZ" > "$DIR_TMP/b.log" 2>&1 || {
    fallo "formato (b): la herramienta salio con error"; cat "$DIR_TMP/b.log" >&2; }
comprobar_alineado "formato (b)" "$DIR_TMP/sal_b" "$DIR_TMP/sal_b/rostro.png"

echo "== Formato (c): todo separado por TAB"
rm -rf "$DIR_TMP/sal_c"
printf '%s\t%s\n' "$IMAGEN" "$(printf '%s' "$LANDMARKS" | tr ' ' '\t')" > "$DIR_TMP/c.txt"
"$BINARIO" --manifest "$DIR_TMP/c.txt" --out "$DIR_TMP/sal_c" --repo-root "$RAIZ" > "$DIR_TMP/c.log" 2>&1 || {
    fallo "formato (c): la herramienta salio con error"; cat "$DIR_TMP/c.log" >&2; }
comprobar_alineado "formato (c)" "$DIR_TMP/sal_c" "$DIR_TMP/sal_c/rostro.png"

echo "== Ruta con espacios"
rm -rf "$DIR_TMP/sal_esp" "$DIR_TMP/sal_espc"
printf '%s\t%s\n' "$IMAGEN_ESPACIOS" "$LANDMARKS" > "$DIR_TMP/esp.txt"
"$BINARIO" --manifest "$DIR_TMP/esp.txt" --out "$DIR_TMP/sal_esp" --repo-root "$RAIZ" > "$DIR_TMP/esp.log" 2>&1 || {
    fallo "ruta con espacios: la herramienta salio con error"; cat "$DIR_TMP/esp.log" >&2; }
comprobar_alineado "ruta con espacios (b)" "$DIR_TMP/sal_esp" "$DIR_TMP/sal_esp/rostro grande.png"

printf '%s\t%s\n' "$IMAGEN_ESPACIOS" "$(printf '%s' "$LANDMARKS" | tr ' ' '\t')" > "$DIR_TMP/espc.txt"
"$BINARIO" --manifest "$DIR_TMP/espc.txt" --out "$DIR_TMP/sal_espc" --repo-root "$RAIZ" > "$DIR_TMP/espc.log" 2>&1 || {
    fallo "ruta con espacios (c): la herramienta salio con error"; cat "$DIR_TMP/espc.log" >&2; }
comprobar_alineado "ruta con espacios (c)" "$DIR_TMP/sal_espc" "$DIR_TMP/sal_espc/rostro grande.png"

echo "== Linea mal formada"
# Coordenadas con valores que no pueden aparecer en una ruta (ni en un nombre de
# directorio temporal): la comprobacion de que el log NO las imprime es exacta.
printf '%s 987654 876543\n' "$IMAGEN" > "$DIR_TMP/mal.txt"
if "$BINARIO" --manifest "$DIR_TMP/mal.txt" --out "$DIR_TMP/sal_mal" --repo-root "$RAIZ" > "$DIR_TMP/mal.log" 2>&1; then
    fallo "linea mal formada: la herramienta deberia salir con error"
else
    if grep -q "mal.txt:1" "$DIR_TMP/mal.log" && grep -q "se esperaban" "$DIR_TMP/mal.log"; then
        ok "linea mal formada: mensaje con linea y motivo"
    else
        fallo "linea mal formada: el mensaje no es claro"; cat "$DIR_TMP/mal.log" >&2
    fi
    if grep -q "987654" "$DIR_TMP/mal.log" || grep -q "876543" "$DIR_TMP/mal.log"; then
        fallo "linea mal formada: el mensaje imprime el valor de la coordenada"
    else
        ok "linea mal formada: el mensaje no imprime el valor de la coordenada"
    fi
fi

echo "== Coordenada no finita"
printf '%s nan 40 50 60 70 80 90 100 110 120\n' "$IMAGEN" > "$DIR_TMP/nan.txt"
if "$BINARIO" --manifest "$DIR_TMP/nan.txt" --out "$DIR_TMP/sal_nan" --repo-root "$RAIZ" > "$DIR_TMP/nan.log" 2>&1; then
    fallo "coordenada no finita: la herramienta deberia salir con error"
else
    if grep -q "no finita" "$DIR_TMP/nan.log"; then
        ok "coordenada no finita: rechazada con un motivo claro"
    else
        fallo "coordenada no finita: el mensaje no es claro"; cat "$DIR_TMP/nan.log" >&2
    fi
fi

echo "== Imagen inexistente"
rm -rf "$DIR_TMP/sal_inex"
printf '%s %s\n' "$DIR_TMP/no_existe.png" "$LANDMARKS" > "$DIR_TMP/inex.txt"
printf '%s %s\n' "$IMAGEN" "$LANDMARKS" >> "$DIR_TMP/inex.txt"
if "$BINARIO" --manifest "$DIR_TMP/inex.txt" --out "$DIR_TMP/sal_inex" --repo-root "$RAIZ" > "$DIR_TMP/inex.log" 2>&1; then
    ok "imagen inexistente: se omite y el resto se escribe"
else
    fallo "imagen inexistente: la herramienta deberia salir con 0"; cat "$DIR_TMP/inex.log" >&2
fi
if [ -f "$DIR_TMP/sal_inex/rostro.png" ]; then
    ok "imagen inexistente: la imagen buena esta escrita"
else
    fallo "imagen inexistente: la imagen buena no se escribio"
fi

echo "== Imagen por encima del limite de tamano"
rm -rf "$DIR_TMP/sal_grande"
printf '%s %s\n' "$DIR_TMP/enorme.png" "$LANDMARKS" > "$DIR_TMP/grande.txt"
printf '%s %s\n' "$IMAGEN" "$LANDMARKS" >> "$DIR_TMP/grande.txt"
if "$BINARIO" --manifest "$DIR_TMP/grande.txt" --out "$DIR_TMP/sal_grande" --repo-root "$RAIZ" > "$DIR_TMP/grande.log" 2>&1; then
    ok "imagen grande: se omite y el resto se escribe"
else
    fallo "imagen grande: la herramienta deberia salir con 0"; cat "$DIR_TMP/grande.log" >&2
fi
if grep -q "limite" "$DIR_TMP/grande.log"; then
    ok "imagen grande: el aviso menciona el limite"
else
    fallo "imagen grande: no hay aviso con el limite"; cat "$DIR_TMP/grande.log" >&2
fi

echo "== Salida dentro del repositorio"
# La ruta de salida va en un directorio que el .gitignore ya cubre: si la barrera
# se rompiera, los recortes no quedarian sin ignorar a un "git add ." de distancia.
SALIDA_DENTRO="$RAIZ/herramientas/salida-prueba"
rm -rf "$SALIDA_DENTRO"
if "$BINARIO" --manifest "$DIR_TMP/a.txt" --out "$SALIDA_DENTRO" --repo-root "$RAIZ" > "$DIR_TMP/dentro.log" 2>&1; then
    fallo "salida dentro del repo: la herramienta deberia salir con error"
else
    ok "salida dentro del repo: rechazada"
fi
if grep -q "biometricos" "$DIR_TMP/dentro.log"; then
    ok "salida dentro del repo: el mensaje explica el motivo"
else
    fallo "salida dentro del repo: el mensaje no explica el motivo"; cat "$DIR_TMP/dentro.log" >&2
fi
if [ -e "$SALIDA_DENTRO" ]; then
    fallo "salida dentro del repo: se escribio algo"
    rm -rf "$SALIDA_DENTRO"
else
    ok "salida dentro del repo: no se escribio nada"
fi

echo "== Salida fuera del repositorio"
rm -rf "$DIR_TMP/sal_fuera"
if "$BINARIO" --manifest "$DIR_TMP/a.txt" --out "$DIR_TMP/sal_fuera" --repo-root "$RAIZ" > "$DIR_TMP/fuera.log" 2>&1; then
    ok "salida fuera del repo: funciona"
else
    fallo "salida fuera del repo: la herramienta salio con error"; cat "$DIR_TMP/fuera.log" >&2
fi

echo "== El script de la herramienta de punta a punta"
rm -rf "$DIR_TMP/sal_script"
if "$RAIZ/herramientas/alinear_lfw.sh" --manifest "$DIR_TMP/a.txt" --out "$DIR_TMP/sal_script" > "$DIR_TMP/script.log" 2>&1; then
    ok "script: ejecuta y escribe la salida"
else
    fallo "script: la herramienta salio con error"; cat "$DIR_TMP/script.log" >&2
fi

if [ "$FALLOS" -eq 0 ]; then
    echo "Todas las comprobaciones pasaron."
    exit 0
fi
echo "Fallaron $FALLOS comprobaciones." >&2
exit 1
