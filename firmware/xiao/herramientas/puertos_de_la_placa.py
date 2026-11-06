#!/usr/bin/env python3
"""Imprime los puertos USB que pueden ser la placa XIAO.

La lista llega por la entrada estándar en JSON (la salida de
`arduino-cli board list --format json`). Se imprimen las direcciones de los
puertos cuya identidad USB (VID:PID) está en la lista de identidades que se
pasa por argumentos, una por línea.

Uso:
    arduino-cli board list --format json | puertos_de_la_placa.py 303a:1001 2886:0056

Dos cosas que hace:

- El VID:PID va en "properties" del JSON, no en el nivel superior, y depende
  del modo USB con el que se flashee (el USB Serial/JTAG del ROM es
  303a:1001; los modos TinyUSB de la variante son 2886:0056 y 2886:8056).
- Una sola placa puede exponer más de un puerto (consola y JTAG). Si todos los
  que coinciden tienen el mismo número de serie, es una placa, y se prefiere el
  puerto de la consola.
"""

import json
import sys


def normaliza(valor: str) -> str:
    texto = str(valor or "").lower()
    return texto[2:] if texto.startswith("0x") else texto


def main() -> int:
    if len(sys.argv) < 2:
        print("uso: puertos_de_la_placa.py IDENTIDAD...", file=sys.stderr)
        return 2
    identidades = {i.lower() for i in sys.argv[1:]}
    try:
        puertos = json.load(sys.stdin).get("detected_ports", [])
    except ValueError:
        return 5  # el JSON cambio de formato: el que llama lo avisa
    coincidencias = []
    for puerto in puertos:
        propiedades = puerto.get("properties", {}) or {}
        identidad = (
            normaliza(propiedades.get("vid")) + ":" + normaliza(propiedades.get("pid"))
        )
        nombre = str(puerto.get("board_name", "") or "")
        direccion = puerto.get("address", "")
        if direccion and (identidad in identidades or "XIAO_ESP32S3" in nombre.upper()):
            coincidencias.append(
                (
                    direccion,
                    identidad,
                    str(propiedades.get("serial_number", "") or ""),
                )
            )
    # Una sola placa puede exponer varios puertos: si comparten numero de
    # serie, se elige el de la consola.
    seriales = {c[2] for c in coincidencias if c[2]}
    if len(seriales) == 1:
        preferidos = [c for c in coincidencias if c[1] in ("303a:1001", "2886:0056")]
        if preferidos:
            coincidencias = preferidos
    for direccion in sorted({c[0] for c in coincidencias}):
        print(direccion)
    return 0


if __name__ == "__main__":
    sys.exit(main())
