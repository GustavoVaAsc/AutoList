#!/usr/bin/env bash
# Compila y flashea el firmware de la XIAO por USB.
#
#   ./flashear.sh [puerto]
#
# Sin puerto, se busca la XIAO entre los puertos USB. Para verlos:
# arduino-cli board list
#
# Antes: la placa conectada por USB-C al equipo que compila, y con la
# secrets.h del equipo (SSID, IP del broker y clave del usuario xiao).
set -euo pipefail

RAIZ="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ "$#" -gt 1 ]; then
  echo "Uso: $0 [puerto]" >&2
  exit 2
fi

"$RAIZ/compilar.sh" --subir "${1:-}"
