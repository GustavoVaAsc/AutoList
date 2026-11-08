#!/usr/bin/env python3
"""Servidor de la webcam de la laptop, para probar el panel de vista previa sin la XIAO.

Habla los dos caminos del sistema real:

  GET /capture           -> una JPEG nueva de la webcam (lo que sirve la XIAO)
  GET /api/camara/previa -> esa misma JPEG, como la serviría el borde en la
                            Orange Pi, con la cabecera X-Edad-Cuadro

La webcam se lee con ffmpeg (un cuadro por petición), así que no hace falta
OpenCV. Sin autenticar y escuchando solo en loopback: es una ayuda de
desarrollo, no algo que vaya a la placa.

Uso:
  python3 herramientas/previa_desarrollo.py --listar
  python3 herramientas/previa_desarrollo.py --dispositivo /dev/video2 --puerto 18081
"""
import argparse
import json
import subprocess
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

RUTA_FUENTE = "/capture"
RUTA_PREVIA = "/api/camara/previa"
RUTA_ESTADO = "/estado"


def tomar_foto(opciones):
    """Una JPEG de la webcam. Devuelve (bytes, None) o (None, motivo del fallo)."""
    comando = [
        "ffmpeg", "-hide_banner", "-loglevel", "error",
        "-f", "v4l2", "-i", opciones.dispositivo,
        "-frames:v", "1",
    ]
    if opciones.ancho > 0:
        # El sensor puede dar más resolución de la que interesa: la vista previa
        # no tiene que pagar el tamaño de un cuadro grande.
        comando += ["-vf", f"scale={opciones.ancho}:-2"]
    comando += ["-q:v", str(opciones.calidad), "-f", "image2pipe", "-vcodec", "mjpeg", "-"]
    try:
        salida = subprocess.run(comando, capture_output=True, timeout=opciones.plazo, check=False)
    except FileNotFoundError:
        return None, "ffmpeg no esta instalado"
    except subprocess.TimeoutExpired:
        return None, f"la camara no respondio en {opciones.plazo} s"
    if salida.returncode != 0:
        motivo = salida.stderr.decode("utf-8", "replace").strip()
        return None, motivo or f"ffmpeg salio con codigo {salida.returncode}"
    if not salida.stdout:
        return None, "no llego ninguna imagen"
    return salida.stdout, None


class Manejador(BaseHTTPRequestHandler):
    opciones = None

    def responder(self, codigo, tipo, cuerpo, cabeceras_extra=None):
        self.send_response(codigo)
        self.send_header("Content-Type", tipo)
        self.send_header("Content-Length", str(len(cuerpo)))
        self.send_header("Cache-Control", "no-store")
        for nombre, valor in (cabeceras_extra or {}).items():
            self.send_header(nombre, valor)
        self.end_headers()
        self.wfile.write(cuerpo)

    def do_GET(self):
        ruta = self.path.split("?")[0]
        if ruta == RUTA_ESTADO:
            cuerpo = json.dumps({"ok": True, "dispositivo": self.opciones.dispositivo}).encode("utf-8")
            self.responder(200, "application/json", cuerpo)
            return
        if ruta not in (RUTA_FUENTE, RUTA_PREVIA):
            self.responder(404, "text/plain; charset=utf-8", b"No encontrado\n")
            return

        foto, motivo = tomar_foto(self.opciones)
        if foto is None:
            # La XIAO responde 503 cuando la cámara no está lista; el panel lo
            # entiende como "todavía no hay cuadro".
            cuerpo = json.dumps({"error": motivo}).encode("utf-8")
            self.responder(503, "application/json", cuerpo)
            return

        if ruta == RUTA_FUENTE:
            self.responder(200, "image/jpeg", foto)
        else:
            self.responder(200, "image/jpeg", foto, {"X-Edad-Cuadro": "0"})

    def log_message(self, formato, *args):
        # El log por consola sirve para ver el sondeo; el formato por defecto de
        # la biblioteca escribe en la salida de error, que aqui no aporta nada.
        sys.stderr.write("[previa] " + (formato % args) + "\n")


def listar_dispositivos():
    """Qué dispositivos de /dev/video* dan un cuadro ahora mismo."""
    import glob

    for ruta in sorted(glob.glob("/dev/video*")):
        comando = [
            "ffmpeg", "-hide_banner", "-loglevel", "error",
            "-f", "v4l2", "-i", ruta, "-frames:v", "1", "-f", "null", "-",
        ]
        try:
            salida = subprocess.run(comando, capture_output=True, timeout=5, check=False)
        except (FileNotFoundError, subprocess.TimeoutExpired):
            continue
        if salida.returncode == 0:
            print(f"{ruta}: sirve cuadros")
        else:
            motivo = salida.stderr.decode("utf-8", "replace").strip().splitlines()
            print(f"{ruta}: no sirve ({motivo[0] if motivo else 'sin detalle'})")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dispositivo", default="/dev/video0", help="dispositivo de la webcam")
    parser.add_argument("--puerto", type=int, default=18081, help="puerto de escucha")
    parser.add_argument("--ancho", type=int, default=640, help="ancho del cuadro (0 = el del sensor)")
    parser.add_argument("--calidad", type=int, default=8, help="calidad JPEG de ffmpeg (2-31, menor es mejor)")
    parser.add_argument("--plazo", type=float, default=5.0, help="segundos de espera a la cámara")
    parser.add_argument("--listar", action="store_true", help="lista los dispositivos de cámara y sale")
    opciones = parser.parse_args()

    if opciones.listar:
        listar_dispositivos()
        return 0

    Manejador.opciones = opciones
    servidor = ThreadingHTTPServer(("127.0.0.1", opciones.puerto), Manejador)
    print(f"previa de desarrollo en http://127.0.0.1:{opciones.puerto}{RUTA_PREVIA}")
    print(f"  fuente de la webcam: {opciones.dispositivo}")
    try:
        servidor.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        servidor.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
