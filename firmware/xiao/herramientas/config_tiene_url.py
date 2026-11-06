#!/usr/bin/env python3
"""Comprueba si una URL está en la lista de índices de placas de arduino-cli.

La lista llega por la entrada estándar en JSON (la salida de
`arduino-cli --json config get board_manager.additional_urls`, que es una lista
JSON de verdad; la versión de texto de `config get` devuelve YAML y su salida se
colaría como si fuera una URL).

Uso:
    arduino-cli --json config get board_manager.additional_urls | config_tiene_url.py URL

Sale con 0 si la URL está en la lista y con 1 si no (o si el JSON no se puede
leer).
"""

import json
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print("uso: config_tiene_url.py URL", file=sys.stderr)
        return 2
    url = sys.argv[1]
    try:
        urls = json.load(sys.stdin)
    except ValueError:
        return 1
    if not isinstance(urls, list):
        return 1
    return 0 if url in urls else 1


if __name__ == "__main__":
    sys.exit(main())
