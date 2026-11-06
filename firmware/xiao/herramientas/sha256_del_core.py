#!/usr/bin/env python3
"""Imprime el sha256 del paquete de un core en el índice de placas.

El índice llega por la entrada estándar en JSON (el `package_esp32_index.json`
de Espressif, que se descarga por HTTPS). Se imprime el sha256 en hexadecimal
sin el prefijo "SHA-256:" que trae el índice.

Uso:
    curl -fsSL "$URL_ESPRESSIF" | sha256_del_core.py esp32 3.3.12

Sirve para anclar el paquete del core en el repo: el script de compilación
compara lo que devuelve esto con el sha256 fijado, así que un índice sustituido
no puede colar otro paquete. El índice no se firma (limitación conocida del
toolchain, anotada en el README del firmware).
"""

import json
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print("uso: sha256_del_core.py PAQUETE VERSION", file=sys.stderr)
        return 2
    paquete, version = sys.argv[1], sys.argv[2]
    try:
        datos = json.load(sys.stdin)
    except ValueError:
        return 1
    for entrada in datos.get("packages", []):
        if entrada.get("name") != paquete:
            continue
        for plataforma in entrada.get("platforms", []):
            if plataforma.get("version") == version:
                print(plataforma.get("checksum", "").split(":")[-1].strip())
                return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
