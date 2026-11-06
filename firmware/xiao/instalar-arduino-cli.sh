#!/usr/bin/env bash
# Instala arduino-cli en el usuario, sin sudo, en la versión fijada y con el
# sha256 comprobado.
#
#   ./instalar-arduino-cli.sh
#
# El toolchain va en el equipo que compila, nunca en la Orange Pi: en la placa
# no se instalan arduino-cli ni PlatformIO (AGENTS.md, Hardware compartido).
#
# Variable de entorno:
#   DESTINO  carpeta donde se deja el binario (por defecto ~/.local/bin).
set -euo pipefail

VERSION="1.5.1"
DESTINO="${DESTINO:-$HOME/.local/bin}"

# Los sha256 son los que publica la release 1.5.1. Para volver a derivarlos:
#   curl -fsSL https://github.com/arduino/arduino-cli/releases/download/v1.5.1/1.5.1-checksums.txt
# Comprobados el 4 nov de 2026.
case "$(uname -s)-$(uname -m)" in
  Linux-x86_64)
    ARCHIVO="arduino-cli_${VERSION}_Linux_64bit.tar.gz"
    SHA256="28a8e119c498a25607821c36cb2dc49e8463941b261a0d99091baa7bc692dd2b"
    ;;
  Linux-aarch64 | Linux-arm64)
    ARCHIVO="arduino-cli_${VERSION}_Linux_ARM64.tar.gz"
    SHA256="1e69e077479f300614d4551334e0a33f08ee40b04315d83b8e7e0e94f0d0ee62"
    ;;
  Darwin-x86_64)
    ARCHIVO="arduino-cli_${VERSION}_macOS_64bit.tar.gz"
    SHA256="c982e940027996bea9901050e95fae99c59c1dcfee54beedecaf28141e7bf2e7"
    ;;
  Darwin-arm64)
    ARCHIVO="arduino-cli_${VERSION}_macOS_ARM64.tar.gz"
    SHA256="cb952e8c1621c95ef5f1d17831c945e3d0ec5973f89c557a7ec8feb9c4f7d4c9"
    ;;
  *)
    echo "Sistema no soportado por este script: $(uname -s) $(uname -m)." >&2
    echo "Descarga a mano la release $VERSION y pon el binario en $DESTINO." >&2
    exit 1
    ;;
esac

URL="https://github.com/arduino/arduino-cli/releases/download/v${VERSION}/${ARCHIVO}"

# La carpeta de destino tiene que ser del usuario y no escribible por el
# grupo ni por otros: si lo fuera, otro usuario local podría cambiar el
# binario entre la descarga y el flasheo.
mkdir -p "$DESTINO"
if [ ! -O "$DESTINO" ]; then
  echo "La carpeta $DESTINO no es de este usuario. Elige otra con DESTINO=..." >&2
  exit 1
fi
case "$DESTINO" in
  "$HOME" | "$HOME"/*) ;;
  *)
    echo "Aviso: $DESTINO está fuera de tu HOME. Comprueba que nadie más pueda escribir ahí." >&2
    ;;
esac
PERMISOS_DESTINO="$(stat -c '%a' "$DESTINO" 2>/dev/null || stat -f '%Lp' "$DESTINO" 2>/dev/null || echo '?')"
# Se miran los tres dígitos: el del grupo (el central) y el de otros (el
# último). Basta con que cualquiera de los dos tenga el bit de escritura.
MODO="${PERMISOS_DESTINO: -3}"
if [ ${#MODO} -eq 3 ]; then
  GRUPO="${MODO:1:1}"
  OTROS="${MODO:2:1}"
  if [ "$GRUPO" -ge 2 ] && [ "$GRUPO" -le 7 ] || [ "$OTROS" -ge 2 ] && [ "$OTROS" -le 7 ]; then
    case "$DESTINO" in
      "$HOME"/*)
        # En tu propia carpeta se corrige sola: se quita el permiso de
        # escritura al grupo y a los demás.
        chmod go-w "$DESTINO"
        echo "Se quitó el permiso de escritura al grupo y a los demás de $DESTINO (era $PERMISOS_DESTINO)."
        ;;
      *)
        echo "La carpeta $DESTINO es escribible por el grupo o por otros (modo $PERMISOS_DESTINO)." >&2
        echo "Elige otra con DESTINO=..., o corrígela: chmod go-w \"$DESTINO\"" >&2
        exit 1
        ;;
    esac
  fi
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "Se descarga $ARCHIVO..."
curl -fsSL --proto '=https' --proto-redir '=https' --retry 3 -o "$TMP/$ARCHIVO" "$URL"

echo "Se comprueba el sha256..."
if command -v sha256sum >/dev/null 2>&1; then
  echo "$SHA256  $TMP/$ARCHIVO" | sha256sum -c - >/dev/null
elif command -v shasum >/dev/null 2>&1; then
  echo "$SHA256  $TMP/$ARCHIVO" | shasum -a 256 -c - >/dev/null
else
  echo "No hay sha256sum ni shasum: no se puede comprobar la descarga." >&2
  exit 1
fi

# Se extrae en un temporal y se instala de golpe, para no dejar el binario a
# medias en la carpeta de destino.
tar -xzf "$TMP/$ARCHIVO" -C "$TMP" arduino-cli
install -m 755 "$TMP/arduino-cli" "$DESTINO/arduino-cli.nuevo"
mv -f "$DESTINO/arduino-cli.nuevo" "$DESTINO/arduino-cli"

VERSION_INSTALADA="$("$DESTINO/arduino-cli" version 2>/dev/null | awk '{for (i = 1; i <= NF; i++) if ($i == "Version:") {print $(i + 1); exit}}')"
if [ "$VERSION_INSTALADA" != "$VERSION" ]; then
  echo "El binario instalado no responde como arduino-cli $VERSION (dice '${VERSION_INSTALADA:-nada}')." >&2
  exit 1
fi

echo "arduino-cli $VERSION instalado en $DESTINO."
case ":$PATH:" in
  *":$DESTINO:"*) ;;
  *) echo "Añade $DESTINO al PATH, o llama a los scripts con ARDUINO_CLI=$DESTINO/arduino-cli" ;;
esac
