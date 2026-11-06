#!/usr/bin/env bash
# Compila el firmware de la XIAO con arduino-cli y las versiones fijadas.
#
# El toolchain vive en el equipo que compila, no en la Orange Pi: en la placa
# no se instala arduino-cli ni PlatformIO (AGENTS.md, Hardware compartido).
# El IDE de Arduino sigue siendo válido: usa el mismo core y las mismas
# bibliotecas; este script solo hace la build reproducible.
#
#   ./compilar.sh              compila
#   ./compilar.sh --subir      compila y flashea (el puerto se detecta)
#   ./compilar.sh --subir /dev/ttyACM0
#
# Variables de entorno:
#   ARDUINO_CLI   ruta al ejecutable de arduino-cli (por defecto, en el PATH).
#   BUILD_DIR     directorio del build. Si se define, no se borra al salir:
#   sirve para inspeccionar el .bin, que lleva las claves en claro.
set -euo pipefail

RAIZ="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Los scripts de Python que hacen de puente con el JSON de la CLI viven en
# herramientas/, cada uno en su archivo (no en linea dentro de este script).
HERRAMIENTAS="$RAIZ/herramientas"

ARDUINO_CLI="${ARDUINO_CLI:-arduino-cli}"
VERSION_CLI="1.5.1"
CORE_ID="esp32:esp32"
CORE_VERSION="3.3.12"
# Biblioteca: nombre|versión que se instala|versión que reporta arduino-cli.
# Ojo: PubSubClient 2.8.0 declara version=2.8 en su library.properties, y eso
# es lo que muestra la CLI.
BIBLIOTECAS=(
  "PubSubClient|2.8.0|2.8"
  "ArduinoJson|7.4.3|7.4.3"
)
# PSRAM=opi: la XIAO tiene 8 MB de flash y 8 MB de PSRAM OPI, y sin PSRAM no
# hay JPEG de la cámara a UXGA. USBMode=hwcdc: la consola y la programación
# salen por el USB Serial/JTAG de la placa.
FQBN="esp32:esp32:XIAO_ESP32S3:PSRAM=opi,USBMode=hwcdc,PartitionScheme=default_8MB"
URL_ESPRESSIF="https://espressif.github.io/arduino-esp32/package_esp32_index.json"
# Ancla del core: sha256 del paquete esp32:esp32@3.3.12, según el índice de
# Espressif. La firma GPG del índice no se verifica (limitación asumida, ver el
# README), así que al menos se comprueba que el índice sirve este paquete y no
# otro. Para re-derivar el valor:
#   curl -fsSL "$URL_ESPRESSIF" | herramientas/sha256_del_core.py esp32 3.3.12
SHA256_CORE="87401004f279a206781e368b66098a8383b7cf87cce7811a6afd0163503fa78f"
# Identidad USB de la placa, según el modo USB con el que se flashea:
#   303a:1001  USB Serial/JTAG del ROM (USBMode=hwcdc, el que fijan los scripts)
#   2886:0056  TinyUSB, el VID:PID de la variante (modo USB-OTG)
#   2886:8056  TinyUSB, el segundo modo de la variante
IDENTIDADES=("303a:1001" "2886:0056" "2886:8056")
# Escape para dejar flashear a un puerto que no se pudo identificar (solo para
# depurar; con la placa en la mano, lo normal es que la detección funcione).
PERMITIR_CUALQUIER_PUERTO="${PERMITIR_CUALQUIER_PUERTO:-0}"
# Escape aparte para flashear con secrets.h legible por el grupo o por otros.
PERMITIR_SECRETOS_LEGIBLES="${PERMITIR_SECRETOS_LEGIBLES:-0}"

SUBIR=0
PUERTO=""
if [ "$#" -gt 2 ]; then
  echo "Uso: $0 [--subir [puerto]]" >&2
  exit 2
fi
case "${1:-}" in
  "") ;;
  --subir)
    SUBIR=1
    PUERTO="${2:-}"
    ;;
  *)
    echo "Uso: $0 [--subir [puerto]]" >&2
    exit 2
    ;;
esac

if ! command -v "$ARDUINO_CLI" >/dev/null 2>&1; then
  echo "No se encuentra arduino-cli. Se instala en el usuario, sin sudo:" >&2
  echo "  $RAIZ/instalar-arduino-cli.sh" >&2
  exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
  echo "Falta python3: se usa para leer el JSON de la CLI (versiones y puertos)." >&2
  exit 1
fi

VERSION_DETECTADA="$("$ARDUINO_CLI" version 2>/dev/null | awk '{for (i = 1; i <= NF; i++) if ($i == "Version:") {print $(i + 1); exit}}')"
if [ "$VERSION_DETECTADA" != "$VERSION_CLI" ]; then
  echo "Se espera arduino-cli $VERSION_CLI y esta es la '${VERSION_DETECTADA:-desconocida}'." >&2
  echo "Los parseos del script dependen del JSON de esa versión. Instálala con:" >&2
  echo "  $RAIZ/instalar-arduino-cli.sh" >&2
  exit 1
fi

# El sketch incluye secrets.h, que no se versiona: sin él no compila. Se
# comprueba antes de instalar nada, que tarda cientos de MB.
if [ ! -f "$RAIZ/secrets.h" ]; then
  echo "Falta secrets.h. Se crea a partir de la plantilla, con permisos solo del propietario:" >&2
  echo "  install -m 600 $RAIZ/secrets.h.example $RAIZ/secrets.h" >&2
  exit 1
fi

# Modo del archivo, con el stat de GNU y el de BSD/macOS.
modo_archivo() {
  stat -c '%a' "$1" 2>/dev/null || stat -f '%Lp' "$1" 2>/dev/null || echo '?'
}

PERMISOS="$(modo_archivo "$RAIZ/secrets.h")"
# Se exige que el archivo no sea legible por el grupo ni por otros (600 o 400).
case "$PERMISOS" in
  6[0][0] | 4[0][0]) ;;
  *)
    echo "Aviso: firmware/xiao/secrets.h está en $PERMISOS y contiene el Wi-Fi y la" >&2
    echo "      clave MQTT del usuario xiao. Déjalo solo para el propietario:" >&2
    echo "      chmod 600 firmware/xiao/secrets.h" >&2
    if [ "$SUBIR" -eq 1 ] && [ "$PERMITIR_SECRETOS_LEGIBLES" != "1" ]; then
      echo "      No se flashea con el archivo legible para otros. O se corrigen los" >&2
      echo "      permisos, o se flashea con PERMITIR_SECRETOS_LEGIBLES=1 (solo para depurar)." >&2
      exit 1
    fi
    ;;
esac

if grep -q "cambiar_" "$RAIZ/secrets.h"; then
  if [ "$SUBIR" -eq 1 ]; then
    echo "secrets.h todavía tiene los valores de ejemplo de la plantilla." >&2
    echo "Rellena el SSID, la clave del Wi-Fi, la IP del broker y la clave MQTT." >&2
    exit 1
  fi
  echo "Aviso: secrets.h tiene los valores de ejemplo de la plantilla; el" >&2
  echo "      firmware compila pero no se conectará a nada." >&2
fi

# El índice de placas de Espressif no viene por defecto en arduino-cli. Se
# descarga por HTTPS y arduino-cli verifica el sha256 de cada paquete contra
# el índice; la firma GPG del índice no se comprueba (limitación conocida del
# toolchain, anotada en el README). La lista de URLs se lee en JSON, porque
# `config get` la devuelve en YAML y la salida se colaría como si fuera una URL.
URLS="$("$ARDUINO_CLI" --json config get board_manager.additional_urls 2>/dev/null || echo '[]')"
if ! printf '%s' "$URLS" | "$HERRAMIENTAS/config_tiene_url.py" "$URL_ESPRESSIF"; then
  "$ARDUINO_CLI" config add board_manager.additional_urls "$URL_ESPRESSIF" >/dev/null
fi

version_core() {
  "$ARDUINO_CLI" --json core list 2>/dev/null | "$HERRAMIENTAS/version_core.py" "$CORE_ID"
}

version_biblioteca() {
  "$ARDUINO_CLI" --json lib list 2>/dev/null | "$HERRAMIENTAS/version_bibliotecas.py" "$1"
}

tiene_version() {
  local versiones
  versiones="$(version_biblioteca "$1" || true)"
  local esperada="$2"
  while IFS= read -r version; do
    [ "$version" = "$esperada" ] && return 0
  done <<<"$versiones"
  return 1
}

# Core: se comprueba la versión instalada, no solo que exista. Si es otra, se
# instala la fijada (arduino-cli reemplaza la anterior).
if [ "$(version_core || true)" != "$CORE_VERSION" ]; then
  # curl solo hace falta aquí: para el instalador (que es otro script) y para
  # comprobar el sha256 del paquete del core contra el índice.
  if ! command -v curl >/dev/null 2>&1; then
    echo "Falta curl: hace falta para instalar el core y comprobar su sha256." >&2
    exit 1
  fi
  # El índice se refresca solo cuando hace falta instalar: en cada build
  # habría una ventana extra en la que el índice local podría estar
  # sustituido.
  "$ARDUINO_CLI" core update-index >/dev/null

  # Ancla del paquete: el índice debe servir este sha256 para el core
  # fijado. Si no, el índice fue sustituido (o cambió el paquete).
  CHECKSUM_INDICE="$(curl -fsSL --proto '=https' --proto-redir '=https' "$URL_ESPRESSIF" \
    | "$HERRAMIENTAS/sha256_del_core.py" esp32 "$CORE_VERSION" || true)"
  if [ "$CHECKSUM_INDICE" != "$SHA256_CORE" ]; then
    echo "El índice de Espressif no sirve el core $CORE_VERSION esperado." >&2
    echo "  Sha256 del índice: ${CHECKSUM_INDICE:-desconocido}" >&2
    echo "  Sha256 anclado:    $SHA256_CORE" >&2
    echo "No se instala nada: el índice pudo ser sustituido." >&2
    exit 1
  fi

  echo "Se instala el core $CORE_ID@$CORE_VERSION..."
  "$ARDUINO_CLI" core install "$CORE_ID@$CORE_VERSION"
fi
if [ "$(version_core || true)" != "$CORE_VERSION" ]; then
  echo "El core $CORE_ID instalado no es la versión fijada ($CORE_VERSION)." >&2
  exit 1
fi

# Bibliotecas: `lib install` con versión es idempotente (no hace nada si ya
# está esa versión y reemplaza la que haya, diciendo cuál cambió en pantalla).
for entrada in "${BIBLIOTECAS[@]}"; do
  IFS='|' read -r nombre version reportada <<<"$entrada"
  if ! tiene_version "$nombre" "$reportada"; then
    "$ARDUINO_CLI" lib install "$nombre@$version"
  fi
  if ! tiene_version "$nombre" "$reportada"; then
    echo "La biblioteca $nombre instalada no es la versión fijada ($reportada)." >&2
    exit 1
  fi
done

# El binario y el .elf llevan dentro el SSID, la clave del Wi-Fi y la clave
# MQTT como cadenas en claro: el build va a un directorio temporal que se
# borra al terminar, y nunca dentro del repo. Con --build-path, arduino-cli
# no deja copia en su caché de sketches (~/.cache/arduino); los builds hechos
# a mano o con el IDE sí la dejan, y hay que borrarla (ver el README).
SALIDA="$(mktemp)"
if [ -n "${BUILD_DIR:-}" ]; then
  # El directorio lo eligió quien llama: NO se borra (la cabecera y el README
  # lo prometen). Con eso, el `rm -rf` nunca corre sobre una ruta ajena.
  DIR_BUILD="$BUILD_DIR"
  trap 'rm -f "$SALIDA"' EXIT INT TERM
  echo "Aviso: el build queda en $DIR_BUILD y no se borra; lleva el SSID y las claves en claro." >&2
else
  DIR_BUILD="$(mktemp -d)"
  trap 'rm -rf "$DIR_BUILD"; rm -f "$SALIDA"' EXIT INT TERM
fi
set +e
"$ARDUINO_CLI" compile --warnings all --fqbn "$FQBN" --build-path "$DIR_BUILD" "$RAIZ" >"$SALIDA" 2>&1
CODIGO=$?
set -e
sed -E 's#(secrets\.h:[0-9]+):[0-9]+:.*#\1: [contenido omitido: puede contener una clave]#' "$SALIDA"
if [ "$CODIGO" -ne 0 ]; then
  exit "$CODIGO"
fi

# El flasheo usa esos binarios: si falta alguno, no se sube (la receta de
# subida del core espera el bootloader, las particiones y la app).
for NECESARIO in xiao.ino.bootloader.bin xiao.ino.partitions.bin xiao.ino.bin; do
  if [ ! -f "$DIR_BUILD/$NECESARIO" ]; then
    echo "Falta $NECESARIO en el build; no se flashea." >&2
    exit 1
  fi
done

if [ "$SUBIR" -eq 0 ]; then
  exit 0
fi

# Puertos de la placa. El VID:PID va en "properties" del JSON de la CLI, no en
# el nivel superior, y depende del modo USB (ver IDENTIDADES). Imprime un
# puerto por línea; el shell decide qué hacer con la lista.
puertos_de_la_placa() {
  "$ARDUINO_CLI" board list --format json 2>/dev/null \
    | "$HERRAMIENTAS/puertos_de_la_placa.py" "${IDENTIDADES[@]}"
}

PUERTOS_PLACA=""
if ! PUERTOS_PLACA="$(puertos_de_la_placa)"; then
  echo "No se pudo leer la lista de puertos de la placa (el JSON de la CLI cambió)." >&2
  exit 1
fi

if [ -z "$PUERTO" ]; then
  case "$PUERTOS_PLACA" in
    "") ;;
    *$'\n'*)
      echo "Hay más de una placa conectada: indica el puerto: $0 --subir <puerto>" >&2
      exit 1
      ;;
    *)
      PUERTO="$PUERTOS_PLACA"
      ;;
  esac
  if [ -z "$PUERTO" ]; then
    echo "No se encontró una XIAO ESP32S3 conectada (VID:PID ${IDENTIDADES[*]})." >&2
    echo "Conéctala por USB-C y revisa los puertos con: arduino-cli board list" >&2
    exit 1
  fi
else
  # Un puerto a mano también se valida: no se quiere escribir la imagen de la
  # XIAO en otro dispositivo.
  if [ "$PERMITIR_CUALQUIER_PUERTO" != "1" ]; then
    ENCONTRADO=0
    while IFS= read -r candidato; do
      [ "$candidato" = "$PUERTO" ] && ENCONTRADO=1
    done <<<"$PUERTOS_PLACA"
    if [ "$ENCONTRADO" -eq 0 ]; then
      echo "El puerto $PUERTO no parece una XIAO ESP32S3 (VID:PID ${IDENTIDADES[*]})." >&2
      echo "Si es la placa y no la ve la CLI, flashea con PERMITIR_CUALQUIER_PUERTO=1." >&2
      exit 1
    fi
  fi
fi

"$ARDUINO_CLI" upload --fqbn "$FQBN" --port "$PUERTO" --input-dir "$DIR_BUILD" "$RAIZ"

echo
echo "Flasheado en $PUERTO. Monitor serie a 115200 (el USB Serial/JTAG de la placa):"
echo "  arduino-cli monitor --port $PUERTO --config baudrate=115200"
