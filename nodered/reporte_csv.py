"""Escribe el CSV de asistencia de una sesión (plan, secciones 6.4 y 7.3).

Lo ejecuta el nodo `exec` "Generar CSV (Python)" de la pestaña Sesiones de Node-RED:

    python3 -I -B /etc/nodered/reporte_csv.py <id de la sesión>

El programa no se conecta a Postgres (la imagen no trae un controlador). Node-RED ejecuta la
consulta del plan, sección 7.3, y deja las filas en /data/reportes/.entrada-<id>.json
({"sesion": id, "filas": [{"no_cuenta", "nombre", "estado", "hora"}]}); este programa las valida
y escribe /data/reportes/sesion-<id>.csv. Al comando solo llega el id entero de la sesión, que la
base devolvió y que aquí se vuelve a validar: ningún dato de MQTT ni de HTTP pasa por la línea
de comandos.

La entrada lleva nombres: el programa la borra siempre al terminar, salga bien o mal, y nada la
reintenta (un reporte que falla no se regenera solo; ver orangepi/README.md, Reportes CSV).

El CSV es UTF-8 con BOM (así Excel lo abre con los acentos bien), separado por comas (la
configuración regional es-MX de Excel usa la coma) y con líneas CRLF (RFC 4180). Un valor que
empieza con =, +, -, @, tabulador o retorno de carro se antecede con una comilla simple: sin
eso, un nombre como =HYPERLINK(...) se ejecutaría como fórmula al abrir el archivo. Lo mismo vale
para cada segmento que, tras un ; o una , (y espacios), empiece con uno de esos caracteres: con
otro separador de lista (es-ES usa ;) el valor se parte en celdas y ese segmento sería una fórmula.
Se incluyen las formas de ancho completo (＝ ＋ － ＠) y el signo menos U+2212.

Los mensajes de error nunca incluyen datos de los alumnos (nombres, cuentas ni estados): solo
la posición de la fila y el nombre del campo, porque Node-RED los copia a su log.

Códigos de salida: 0 bien, 1 entrada inválida o error al escribir, 2 argumentos incorrectos.
Solo biblioteca estándar. Pruebas: cd nodered/pruebas && python3 -m unittest
"""
import csv
import json
import os
import re
import stat
import sys

DIRECTORIO = "/data/reportes"
ENCABEZADO = ("no_cuenta", "nombre", "estado", "hora")
ESTADOS = ("presente", "retardo", "falta")
# Prefijos con los que una hoja de cálculo interpreta una celda como fórmula (OWASP, CSV injection),
# más las formas de ancho completo y el signo menos U+2212 (CWE-1236).
PREFIJOS_DE_FORMULA = ("=", "+", "-", "@", "\t", "\r", "\uff1d", "\uff0b", "\uff0d", "\uff20", "\u2212")
# Un segmento tras ; o , (y espacios) que empieza con un prefijo: se le antepone la comilla.
_SEGMENTO_DE_FORMULA = re.compile("([;,][ ]*)(?=[" + re.escape("".join(PREFIJOS_DE_FORMULA)) + "])")
MAX_FILAS = 1000        # la importación de alumnos admite 500 por archivo
MAX_BYTES_ENTRADA = 1024 * 1024
MAX_NOMBRE = 200        # la importación admite 120 caracteres
MAX_HORA = 40
MAX_ID = 2147483647     # INT de Postgres (sesiones.id es SERIAL)


class ErrorReporte(Exception):
    """Error esperado. Su mensaje es seguro de registrar: no lleva datos de alumnos."""


def neutralizar(valor):
    """Antecede una comilla simple al valor, y a cada segmento tras ; o , , que una hoja de
    cálculo leería como fórmula."""
    valor = _SEGMENTO_DE_FORMULA.sub(lambda m: m.group(1) + "'", valor)
    if valor.startswith(PREFIJOS_DE_FORMULA):
        return "'" + valor
    return valor


def leer_id(texto):
    """El id de la sesión: un entero positivo en ASCII, sin ceros a la izquierda ni signo."""
    if not isinstance(texto, str) or not re.fullmatch(r"[1-9][0-9]{0,9}", texto, re.ASCII):
        raise ErrorReporte("el id de la sesión debe ser un entero positivo")
    valor = int(texto)
    if valor > MAX_ID:
        raise ErrorReporte("el id de la sesión es demasiado grande")
    return valor


def _validar_fila(posicion, f):
    if not isinstance(f, dict):
        raise ErrorReporte("fila %d: no es un objeto" % posicion)
    for campo in ENCABEZADO:
        if campo not in f:
            raise ErrorReporte("fila %d: falta el campo %s" % (posicion, campo))
    cuenta, nombre, estado, hora = (f[c] for c in ENCABEZADO)
    if not isinstance(cuenta, str) or not re.fullmatch(r"[0-9]{9}", cuenta, re.ASCII):
        raise ErrorReporte("fila %d: no_cuenta inválido" % posicion)
    if not isinstance(nombre, str) or nombre.strip() == "" or len(nombre) > MAX_NOMBRE:
        raise ErrorReporte("fila %d: nombre inválido" % posicion)
    if estado not in ESTADOS:
        raise ErrorReporte("fila %d: estado inválido" % posicion)
    if hora is not None and (not isinstance(hora, str) or len(hora) > MAX_HORA):
        raise ErrorReporte("fila %d: hora inválida" % posicion)
    return (cuenta, nombre, estado, "" if hora is None else hora)


def leer_entrada(directorio, sesion_id):
    """Lee y valida .entrada-<id>.json; devuelve la lista de filas (tuplas de cuatro textos).

    Se abre con O_NOFOLLOW (un enlace simbólico no se sigue) y se mide con fstat sobre el
    descriptor ya abierto, para que lo que se mide sea lo que se lee.
    """
    ruta = os.path.join(directorio, ".entrada-%d.json" % sesion_id)
    try:
        fd = os.open(ruta, os.O_RDONLY | os.O_NOFOLLOW)
    except FileNotFoundError:
        raise ErrorReporte("no existe el archivo de entrada de la sesión %d" % sesion_id) from None
    except OSError:
        raise ErrorReporte("no se pudo abrir el archivo de entrada") from None
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode):
            raise ErrorReporte("el archivo de entrada no es un archivo regular")
        if info.st_size > MAX_BYTES_ENTRADA:
            raise ErrorReporte("el archivo de entrada es demasiado grande")
        with os.fdopen(fd, encoding="utf-8", closefd=False) as f:
            datos = json.load(f)
    except (OSError, ValueError):
        raise ErrorReporte("no se pudo leer el archivo de entrada como JSON") from None
    finally:
        os.close(fd)
    if not isinstance(datos, dict) or datos.get("sesion") != sesion_id \
            or isinstance(datos.get("sesion"), bool):
        raise ErrorReporte("el archivo de entrada no corresponde a la sesión %d" % sesion_id)
    filas = datos.get("filas")
    if not isinstance(filas, list):
        raise ErrorReporte("el archivo de entrada no trae una lista de filas")
    if len(filas) > MAX_FILAS:
        raise ErrorReporte("el archivo de entrada trae más de %d filas" % MAX_FILAS)
    return [_validar_fila(i + 1, f) for i, f in enumerate(filas)]


def escribir_csv(directorio, sesion_id, filas):
    """Escribe sesion-<id>.csv de forma atómica (archivo temporal y rename); devuelve su ruta.

    El temporal previo se borra (sin seguir un enlace) y se crea de nuevo con O_EXCL y
    O_NOFOLLOW: si alguien lo recreó entre medio, falla en vez de escribir donde no debe.
    """
    destino = os.path.join(directorio, "sesion-%d.csv" % sesion_id)
    temporal = destino + ".tmp"
    try:
        os.remove(temporal)
    except FileNotFoundError:
        pass
    # 0600: el reporte lleva nombres y cuentas; solo el usuario de Node-RED lo lee. fchmod lo
    # fija aunque el umask sea laxo.
    fd = os.open(temporal, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    try:
        os.fchmod(fd, 0o600)
        with os.fdopen(fd, "w", encoding="utf-8-sig", newline="") as f:
            escritor = csv.writer(f, lineterminator="\r\n")
            escritor.writerow(ENCABEZADO)
            for fila in filas:
                escritor.writerow([neutralizar(valor) for valor in fila])
        os.replace(temporal, destino)
    except BaseException:
        try:
            os.remove(temporal)
        except OSError:
            pass
        raise
    return destino


def generar(sesion_id, directorio=DIRECTORIO):
    """Lee la entrada, escribe el CSV y borra la entrada, salga bien o mal: la entrada lleva
    nombres y ningún camino la reintenta. Devuelve (ruta del CSV, filas)."""
    try:
        filas = leer_entrada(directorio, sesion_id)
        destino = escribir_csv(directorio, sesion_id, filas)
    finally:
        # Si no se pudo borrar, no oculta el resultado ni el error original.
        try:
            os.remove(os.path.join(directorio, ".entrada-%d.json" % sesion_id))
        except OSError:
            pass
    return destino, len(filas)


def main(argv, directorio=DIRECTORIO):
    if len(argv) != 1:
        print("reporte_csv: uso: reporte_csv.py <id de la sesión>", file=sys.stderr)
        return 2
    try:
        sesion_id = leer_id(argv[0])
    except ErrorReporte as e:
        print("reporte_csv: %s" % e, file=sys.stderr)
        return 2
    try:
        destino, n = generar(sesion_id, directorio)
    except ErrorReporte as e:
        print("reporte_csv: %s" % e, file=sys.stderr)
        return 1
    except Exception as e:  # noqa: BLE001 - solo se nombra la clase: el mensaje podría llevar datos
        print("reporte_csv: error inesperado (%s)" % type(e).__name__, file=sys.stderr)
        return 1
    print("%s: %d filas" % (os.path.basename(destino), n))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
