#!/usr/bin/env python3
"""Valida payloads MQTT contra el contrato de docs/mqtt-topics.json.

Solo usa la biblioteca estandar de Python 3: no necesita instalar nada.

Uso:
    python3 herramientas/validar_protocolo.py <fichero.json> [<fichero.json> ...]
    cat payloads.json | python3 herramientas/validar_protocolo.py
    python3 herramientas/validar_protocolo.py --lista

Cada fichero de entrada es un objeto JSON con la forma

    {"topic": "aula/vision/resultado", "payload": {"cuenta": "320068234"}}

o una lista de esos objetos. Se comprueba que:

- el topico exista en el contrato;
- no falte ningun campo marcado como requerido;
- los tipos de los campos coincidan (un entero es valido donde se pide un
  flotante, pero un booleano no vale como entero);
- no aparezcan campos que no esten en el contrato.

Los errores se escriben por stderr y el proceso sale con codigo 1 si hay
alguno.
"""

import argparse
import json
import os
import sys

RUTA_CONTRATO_POR_DEFECTO = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "docs",
    "mqtt-topics.json",
)

TIPOS_SIMPLE = ("int", "float", "string", "bool")


def cargar_contrato(ruta):
    """Carga el contrato y devuelve {topico: especificacion}."""
    with open(ruta, encoding="utf-8") as fichero:
        datos = json.load(fichero)

    topicos = datos.get("topics") if isinstance(datos, dict) else datos
    if not isinstance(topicos, list):
        raise ValueError("el contrato no contiene una lista 'topics'")

    contrato = {}
    for entrada in topicos:
        if "topic" not in entrada:
            raise ValueError("una entrada del contrato no tiene 'topic'")
        contrato[entrada["topic"]] = entrada
    return contrato


def cargar_mensajes(ruta):
    """Lee un fichero de mensajes y devuelve una lista de (origen, objeto)."""
    with open(ruta, encoding="utf-8") as fichero:
        contenido = fichero.read()

    try:
        datos = json.loads(contenido)
    except json.JSONDecodeError as error:
        raise ValueError("JSON invalido: %s" % error)

    if isinstance(datos, dict):
        datos = [datos]
    if not isinstance(datos, list):
        raise ValueError("se esperaba un objeto o una lista de objetos")

    return [(ruta, mensaje) for mensaje in datos]


def valor_compatible(valor, tipo_esperado):
    """Indica si un valor de JSON encaja con el tipo pedido por el contrato."""
    if tipo_esperado == "bool":
        return isinstance(valor, bool)

    # bool es subclase de int en Python: hay que descartarlo antes.
    if tipo_esperado == "int":
        return isinstance(valor, int) and not isinstance(valor, bool)

    if tipo_esperado == "float":
        if isinstance(valor, bool):
            return False
        return isinstance(valor, (int, float))

    if tipo_esperado == "string":
        return isinstance(valor, str)

    if tipo_esperado == "object":
        return isinstance(valor, dict)

    if tipo_esperado == "array":
        return isinstance(valor, list)

    return True


def tipo_real(valor):
    """Nombre del tipo de un valor de JSON, para los mensajes de error."""
    if isinstance(valor, bool):
        return "bool"
    if isinstance(valor, int):
        return "int"
    if isinstance(valor, float):
        return "float"
    if isinstance(valor, str):
        return "string"
    if isinstance(valor, dict):
        return "object"
    if isinstance(valor, list):
        return "array"
    if valor is None:
        return "null"
    return type(valor).__name__


def validar_campos(campos, valores, ruta_campo, errores):
    """Valida un diccionario de valores contra una lista de campos del contrato."""
    por_nombre = {campo["name"]: campo for campo in campos}

    for nombre, campo in por_nombre.items():
        if nombre not in valores:
            if campo.get("required", False):
                errores.append(
                    "falta el campo requerido '%s'%s" % (nombre, ruta_campo)
                )
            continue

        valor = valores[nombre]
        tipo_esperado = campo.get("type")

        if tipo_esperado in TIPOS_SIMPLE:
            if not valor_compatible(valor, tipo_esperado):
                errores.append(
                    "el campo '%s'%s es de tipo %s y se esperaba %s"
                    % (nombre, ruta_campo, tipo_real(valor), tipo_esperado)
                )
                continue

        elif tipo_esperado == "object":
            if not isinstance(valor, dict):
                errores.append(
                    "el campo '%s'%s es de tipo %s y se esperaba object"
                    % (nombre, ruta_campo, tipo_real(valor))
                )
                continue
            validar_campos(
                campo.get("fields", []),
                valor,
                "%s.%s" % (ruta_campo, nombre),
                errores,
            )

        elif tipo_esperado == "array":
            if not isinstance(valor, list):
                errores.append(
                    "el campo '%s'%s es de tipo %s y se esperaba array"
                    % (nombre, ruta_campo, tipo_real(valor))
                )
                continue
            item = campo.get("items", {})
            tipo_item = item.get("type")
            if tipo_item in TIPOS_SIMPLE:
                for indice, elemento in enumerate(valor):
                    if not valor_compatible(elemento, tipo_item):
                        errores.append(
                            "el elemento %d de '%s'%s es de tipo %s y se esperaba %s"
                            % (
                                indice,
                                nombre,
                                ruta_campo,
                                tipo_real(elemento),
                                tipo_item,
                            )
                        )

    conocidos = set(por_nombre)
    for nombre in valores:
        if nombre not in conocidos:
            errores.append(
                "campo desconocido '%s'%s" % (nombre, ruta_campo)
            )


def validar_mensaje(mensaje, contrato, origen, indice):
    """Valida un mensaje. Devuelve la lista de errores encontrados."""
    errores = []

    if not isinstance(mensaje, dict):
        return ["el mensaje %d de %s no es un objeto" % (indice, origen)]

    topico = mensaje.get("topic")
    if topico is None:
        return ["falta la clave 'topic' en el mensaje %d de %s" % (indice, origen)]

    if not isinstance(topico, str):
        return [
            "el campo 'topic' del mensaje %d de %s no es una cadena"
            % (indice, origen)
        ]

    if topico not in contrato:
        return [
            "topico desconocido '%s' en el mensaje %d de %s" % (topico, indice, origen)
        ]

    if "payload" not in mensaje:
        return ["falta la clave 'payload' en el mensaje %d de %s" % (indice, origen)]

    payload = mensaje["payload"]
    if not isinstance(payload, dict):
        return [
            "el 'payload' del mensaje %d de %s no es un objeto" % (indice, origen)
        ]

    validar_campos(
        contrato[topico].get("payload", []),
        payload,
        "",
        errores,
    )
    return errores


def main(argv=None):
    analizador = argparse.ArgumentParser(
        prog="validar_protocolo.py",
        description=(
            "Valida payloads MQTT contra el contrato docs/mqtt-topics.json. "
            "Usa solo la biblioteca estandar de Python 3."
        ),
    )
    analizador.add_argument(
        "ficheros",
        nargs="*",
        metavar="fichero.json",
        help="ficheros JSON con los mensajes a validar; si no se indica ninguno, se lee de la entrada estandar",
    )
    analizador.add_argument(
        "--contrato",
        default=RUTA_CONTRATO_POR_DEFECTO,
        metavar="ruta.json",
        help="ruta del contrato MQTT (por defecto: docs/mqtt-topics.json)",
    )
    analizador.add_argument(
        "--lista",
        action="store_true",
        help="imprime los topicos del contrato y termina",
    )
    argumentos = analizador.parse_args(argv)

    try:
        contrato = cargar_contrato(argumentos.contrato)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print("error: no se pudo leer el contrato: %s" % error, file=sys.stderr)
        return 2

    if argumentos.lista:
        for topico in contrato:
            print(topico)
        return 0

    if argumentos.ficheros:
        mensajes = []
        for ruta in argumentos.ficheros:
            try:
                mensajes.extend(cargar_mensajes(ruta))
            except (OSError, ValueError) as error:
                print("error: %s: %s" % (ruta, error), file=sys.stderr)
                return 2
    else:
        contenido = sys.stdin.read()
        if not contenido.strip():
            print(
                "error: no hay ficheros ni datos en la entrada estandar",
                file=sys.stderr,
            )
            return 2
        try:
            datos = json.loads(contenido)
        except json.JSONDecodeError as error:
            print("error: JSON invalido en la entrada estandar: %s" % error, file=sys.stderr)
            return 2
        if isinstance(datos, dict):
            datos = [datos]
        if not isinstance(datos, list):
            print(
                "error: se esperaba un objeto o una lista de objetos",
                file=sys.stderr,
            )
            return 2
        mensajes = [("<stdin>", mensaje) for mensaje in datos]

    total_errores = 0
    for indice, (origen, mensaje) in enumerate(mensajes, start=1):
        errores = validar_mensaje(mensaje, contrato, origen, indice)
        for error in errores:
            print("%s: mensaje %d: %s" % (origen, indice, error), file=sys.stderr)
        total_errores += len(errores)

    if total_errores:
        print(
            "%d error(es) de validacion en %d mensaje(s)" % (total_errores, len(mensajes)),
            file=sys.stderr,
        )
        return 1

    print("OK: %d mensaje(s) valido(s)" % len(mensajes))
    return 0


if __name__ == "__main__":
    sys.exit(main())
