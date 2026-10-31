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
- no aparezcan campos que no esten en el contrato;
- el valor de los campos con 'enum' este entre los valores admitidos;
- los campos numericos esten dentro de los limites 'min' y 'max';
- los arrays con 'length' tengan la longitud que declara el contrato;
- el payload tenga al menos 'min_fields' campos, si el topico lo declara;
- esten los campos que el topico exige con 'required_if'.

El validador no inventa reglas: si el contrato no declara un limite para un
campo, no lo comprueba.

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

# Tope de lo que se lee de un fichero o de la entrada estandar. Por encima de
# esto ya no es un payload, y leerlo entero no aporta nada.
LIMITE_ENTRADA = 16 * 1024 * 1024

# Longitud maxima de los textos que se imprimen en los mensajes de error.
LIMITE_TEXTO = 200


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


def texto_seguro(valor, limite=LIMITE_TEXTO):
    """Convierte un valor en texto apto para imprimir.

    Usa repr() para que los caracteres de control salgan escapados (un escape
    ANSI en un topico no puede redibujar la terminal) y trunca a 200
    caracteres para que un valor enorme no llene la salida.
    """
    limpio = "".join(
        caracter if caracter.isprintable() else "?" for caracter in repr(valor)
    )
    if len(limpio) > limite:
        limpio = limpio[:limite] + "..."
    return limpio


def leer_texto(ruta):
    """Lee un fichero entero como texto, con un tope de tamano."""
    with open(ruta, "rb") as fichero:
        datos = fichero.read(LIMITE_ENTRADA + 1)

    if len(datos) > LIMITE_ENTRADA:
        raise ValueError(
            "el fichero supera el tope de %d bytes" % LIMITE_ENTRADA
        )

    try:
        return datos.decode("utf-8")
    except UnicodeDecodeError as error:
        raise ValueError("el fichero no es UTF-8 valido: %s" % error)


def leer_texto_entrada_estandar():
    """Lee la entrada estandar entera, con un tope de tamano."""
    datos = sys.stdin.buffer.read(LIMITE_ENTRADA + 1)

    if len(datos) > LIMITE_ENTRADA:
        raise ValueError(
            "la entrada estandar supera el tope de %d bytes" % LIMITE_ENTRADA
        )

    try:
        return datos.decode("utf-8")
    except UnicodeDecodeError as error:
        raise ValueError("la entrada estandar no es UTF-8 valido: %s" % error)


def cargar_mensajes_desde_texto(contenido, origen):
    """Convierte un texto JSON en una lista de (origen, mensaje)."""
    try:
        datos = json.loads(contenido)
    except json.JSONDecodeError as error:
        raise ValueError("JSON invalido: %s" % error)

    if isinstance(datos, dict):
        datos = [datos]
    if not isinstance(datos, list):
        raise ValueError("se esperaba un objeto o una lista de objetos")

    return [(origen, mensaje) for mensaje in datos]


def cargar_mensajes(ruta):
    """Lee un fichero de mensajes y devuelve una lista de (origen, objeto)."""
    return cargar_mensajes_desde_texto(leer_texto(ruta), ruta)


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


def longitud_esperada(campo):
    """Longitud que el contrato exige a un array, o None si no la declara.

    La regla 'length' puede ser un numero o un objeto con la variable de
    entorno de la que sale (EMBEDDING_DIM) y su valor por defecto.
    """
    regla = campo.get("length")
    if regla is None:
        return None

    if isinstance(regla, bool):
        return None

    if isinstance(regla, int):
        return regla

    if isinstance(regla, dict):
        variable = regla.get("env")
        if variable:
            valor = os.environ.get(variable)
            if valor is not None:
                try:
                    return int(valor)
                except ValueError:
                    print(
                        "aviso: la variable de entorno %s no es un entero; "
                        "se usa el valor por defecto" % variable,
                        file=sys.stderr,
                    )
        return regla.get("default")

    return None


def comprobar_enum(nombre, campo, valor, ruta_campo, errores):
    """Comprueba que el valor este entre los admitidos, si el contrato dice."""
    if "enum" not in campo:
        return

    admitidos = campo["enum"]
    if valor in admitidos:
        return

    errores.append(
        "el campo %s%s vale %s y se esperaba uno de: %s"
        % (
            texto_seguro(nombre),
            ruta_campo,
            texto_seguro(valor),
            ", ".join(texto_seguro(opcion) for opcion in admitidos),
        )
    )


def comprobar_limites(nombre, campo, valor, ruta_campo, errores):
    """Comprueba los limites 'min' y 'max' declarados por el contrato."""
    if isinstance(valor, bool) or not isinstance(valor, (int, float)):
        return

    if "min" in campo and valor < campo["min"]:
        errores.append(
            "el campo %s%s vale %s y el minimo del contrato es %s"
            % (
                texto_seguro(nombre),
                ruta_campo,
                texto_seguro(valor),
                texto_seguro(campo["min"]),
            )
        )

    if "max" in campo and valor > campo["max"]:
        errores.append(
            "el campo %s%s vale %s y el maximo del contrato es %s"
            % (
                texto_seguro(nombre),
                ruta_campo,
                texto_seguro(valor),
                texto_seguro(campo["max"]),
            )
        )


def comprobar_longitud(nombre, campo, valor, ruta_campo, errores):
    """Comprueba que el array tenga la longitud que declara el contrato."""
    esperada = longitud_esperada(campo)
    if esperada is None:
        return

    if len(valor) != esperada:
        errores.append(
            "el array %s%s tiene %d elemento(s) y el contrato exige %d"
            % (texto_seguro(nombre), ruta_campo, len(valor), esperada)
        )


def validar_campos(campos, valores, ruta_campo, errores):
    """Valida un diccionario de valores contra una lista de campos del contrato."""
    por_nombre = {campo["name"]: campo for campo in campos}

    for nombre, campo in por_nombre.items():
        if nombre not in valores:
            if campo.get("required", False):
                errores.append(
                    "falta el campo requerido %s%s" % (texto_seguro(nombre), ruta_campo)
                )
            continue

        valor = valores[nombre]
        tipo_esperado = campo.get("type")

        if tipo_esperado in TIPOS_SIMPLE:
            if not valor_compatible(valor, tipo_esperado):
                errores.append(
                    "el campo %s%s es de tipo %s y se esperaba %s"
                    % (texto_seguro(nombre), ruta_campo, tipo_real(valor), tipo_esperado)
                )
                continue

        elif tipo_esperado == "object":
            if not isinstance(valor, dict):
                errores.append(
                    "el campo %s%s es de tipo %s y se esperaba object"
                    % (texto_seguro(nombre), ruta_campo, tipo_real(valor))
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
                    "el campo %s%s es de tipo %s y se esperaba array"
                    % (texto_seguro(nombre), ruta_campo, tipo_real(valor))
                )
                continue
            item = campo.get("items", {})
            tipo_item = item.get("type")
            if tipo_item in TIPOS_SIMPLE:
                for indice, elemento in enumerate(valor):
                    if not valor_compatible(elemento, tipo_item):
                        errores.append(
                            "el elemento %d de %s%s es de tipo %s y se esperaba %s"
                            % (
                                indice,
                                texto_seguro(nombre),
                                ruta_campo,
                                tipo_real(elemento),
                                tipo_item,
                            )
                        )
            comprobar_longitud(nombre, campo, valor, ruta_campo, errores)

        comprobar_enum(nombre, campo, valor, ruta_campo, errores)
        comprobar_limites(nombre, campo, valor, ruta_campo, errores)

    conocidos = set(por_nombre)
    for nombre in valores:
        if nombre not in conocidos:
            errores.append(
                "campo desconocido %s%s" % (texto_seguro(nombre), ruta_campo)
            )


def comprobar_min_fields(especificacion, payload, errores):
    """Comprueba el numero minimo de campos del payload, si el topico lo dice."""
    minimo = especificacion.get("min_fields")
    if minimo is None:
        return

    if len(payload) < minimo:
        errores.append(
            "el payload lleva %d campo(s) y el contrato exige al menos %d"
            % (len(payload), minimo)
        )


def comprobar_required_if(especificacion, payload, errores):
    """Comprueba las reglas condicionales declaradas por el topico."""
    for regla in especificacion.get("required_if", []):
        condicion = regla.get("when", {})
        se_cumple = True

        for campo, valor in condicion.items():
            if campo not in payload or payload[campo] != valor:
                se_cumple = False
                break

        if not se_cumple:
            continue

        for nombre in regla.get("require", []):
            if nombre not in payload:
                errores.append(
                    "falta el campo requerido %s cuando %s"
                    % (
                        texto_seguro(nombre),
                        ", ".join(
                            "%s vale %s"
                            % (texto_seguro(campo), texto_seguro(valor))
                            for campo, valor in condicion.items()
                        ),
                    )
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
            "topico desconocido %s en el mensaje %d de %s"
            % (texto_seguro(topico), indice, origen)
        ]

    if "payload" not in mensaje:
        return ["falta la clave 'payload' en el mensaje %d de %s" % (indice, origen)]

    payload = mensaje["payload"]
    if not isinstance(payload, dict):
        return [
            "el 'payload' del mensaje %d de %s no es un objeto" % (indice, origen)
        ]

    especificacion = contrato[topico]

    validar_campos(
        especificacion.get("payload", []),
        payload,
        "",
        errores,
    )
    comprobar_min_fields(especificacion, payload, errores)
    comprobar_required_if(especificacion, payload, errores)
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
        try:
            mensajes = cargar_mensajes_desde_texto(
                leer_texto_entrada_estandar(), "<stdin>"
            )
        except (OSError, ValueError) as error:
            print("error: <stdin>: %s" % error, file=sys.stderr)
            return 2
        if not mensajes:
            print(
                "error: no hay ficheros ni datos en la entrada estandar",
                file=sys.stderr,
            )
            return 2

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
