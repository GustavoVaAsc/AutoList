#!/usr/bin/env python3
"""Imprime la versión instalada de un core de arduino-cli.

La lista llega por la entrada estándar en JSON (la salida de
`arduino-cli --json core list`). Se imprime la versión instalada, que es la
que hay que comparar con la fijada en el script de compilación; si el core no
está instalado, no se imprime nada.

Uso:
    arduino-cli --json core list | version_core.py esp32:esp32
"""

import json
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print("uso: version_core.py ID", file=sys.stderr)
        return 2
    core_id = sys.argv[1]
    try:
        datos = json.load(sys.stdin)
    except ValueError:
        return 1
    for plataforma in datos.get("platforms", []):
        if plataforma.get("id") == core_id:
            print(plataforma.get("installed_version", ""))
            break
    return 0


if __name__ == "__main__":
    sys.exit(main())
