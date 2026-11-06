#!/usr/bin/env python3
"""Imprime las versiones instaladas de una biblioteca de arduino-cli.

La lista llega por la entrada estándar en JSON (la salida de
`arduino-cli --json lib list`). Se imprime una versión por línea: puede haber
más de una instalada de la misma biblioteca (por ejemplo, la antigua y la
nueva en carpetas distintas), y la que sirve es la que se acaba de instalar.

Uso:
    arduino-cli --json lib list | version_bibliotecas.py PubSubClient
"""

import json
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print("uso: version_bibliotecas.py NOMBRE", file=sys.stderr)
        return 2
    nombre = sys.argv[1]
    try:
        datos = json.load(sys.stdin)
    except ValueError:
        return 1
    for entrada in datos.get("installed_libraries", []):
        biblioteca = entrada.get("library", {})
        if biblioteca.get("name") == nombre:
            print(biblioteca.get("version", ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
