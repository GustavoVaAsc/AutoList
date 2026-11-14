#!/usr/bin/env bash
# monitorear_serie.sh: vuelca el monitor serie de la XIAO en tiempo real, con
# timestamp por linea, para las pruebas con la placa conectada por USB (las
# etapas E1-E5 de docs/specs/camara-xiao-zerotier.md). Log plano a stdout; los
# avisos de este script van a stderr.
#
#   uso: ./monitorear_serie.sh [puerto|-] [baudios]
#     puerto   /dev/ttyACM0 por omision. "-" lee de stdin (para pipes y pruebas)
#     baudios  115200 por omision. Solo hace efecto en un UART de verdad: el
#              USB Serial/JTAG de la XIAO la ignora
#
#   guardar el log:  ./monitorear_serie.sh | tee /tmp/xiao.log
#   parar:           Ctrl+C (solo en primer plano; en segundo plano bash ignora
#                    SIGINT en los trabajos async) o kill -TERM
#
# Si el puerto no abre: comprobar que la placa esta conectada (el USB
# Serial/JTAG es 303a:1001; ver "Monitor serie" en firmware/xiao/README.md) y
# que el usuario puede leerlo. Si OTRO programa tiene el puerto abierto a la
# vez (p. ej. arduino-cli monitor), este script no falla: las lineas se
# reparten entre los dos lectores y el log sale incompleto.
#
# Codigo de salida: 0 normal (Ctrl+C, TERM, HUP o fin del flujo), 1 el puerto
# no se pudo abrir o configurar, 2 argumentos invalidos o bash antiguo, 3 el
# puerto desaparecio durante la lectura (tipico: la placa se desconecto).

set -euo pipefail

if (( BASH_VERSINFO[0] < 4 || (BASH_VERSINFO[0] == 4 && BASH_VERSINFO[1] < 2) )); then
  echo "monitorear_serie: se necesita bash >= 4.2 (para los timestamps)" >&2
  exit 2
fi

puerto="${1:-/dev/ttyACM0}"
baudios="${2:-115200}"

if [[ ! "$baudios" =~ ^[0-9]+$ ]]; then
  echo "monitorear_serie: baudios no numericos: $baudios" >&2
  exit 2
fi

if [[ "$puerto" == "-" ]]; then
  exec 3<&0
else
  if [[ ! -e "$puerto" ]]; then
    echo "monitorear_serie: no existe $puerto (¿conectaste la XIAO? 'arduino-cli board list' la muestra)" >&2
    exit 1
  fi
  # raw: sin traduccion de lineas ni eco (el \r de los \r\n de la placa llega
  # tal cual y se quita al emitir). El baudaje del USB CDC es virtual, pero se
  # fija igual por si el puerto es un UART de verdad. -hupcl evita que el cierre
  # del fd mande un hangup que puede resetear la placa.
  if ! stty -F "$puerto" raw "$baudios" -hupcl; then
    echo "monitorear_serie: no se pudo configurar $puerto (¿permisos de lectura? ¿la ruta no es un puerto serie?)" >&2
    exit 1
  fi
  exec 3<"$puerto"
fi

echo "monitorear_serie: leyendo $puerto (velocidad nominal: $baudios; el USB Serial/JTAG la ignora). Ctrl+C para parar." >&2

# La trampa no sale directamente: solo marca la senal. read -t devuelve el
# control a mas tardar un ciclo despues, el bucle ve la marca y termina.
salir=0
trap 'salir=1' INT TERM HUP

emitir() {
  local ahora
  # Se quitan TODOS los \r: uno intermedio haria que el log se lea pisandose
  # a si mismo (y encima pisando el timestamp).
  local texto="${1//$'\r'/}"
  printf -v ahora '%(%Y-%m-%dT%H:%M:%S)T' -1
  printf '%s %s\n' "$ahora" "$texto"
}

# read con tiempo limite y parciales acumulados. Sin el limite, una trampa que
# solo marca bandera no corta la lectura (bash la reanuda y se cuelga); con el
# limite, cada ciclo devuelve el control y se mira la marca. Una linea que
# llega a trozos (mas de un ciclo entre trozos) se reconstruye en $acumulado.
acumulado=""
while (( ! salir )); do
  linea=""
  if IFS= read -r -t 0.5 -u 3 linea; then
    emitir "$acumulado$linea"
    acumulado=""
  else
    rc=$?
    if (( rc > 128 )); then
      # tiempo agotado o senal: el trozo leido sin \n se guarda y se une al
      # siguiente; no se emite solo para no partir lineas por la mitad.
      acumulado+="$linea"
      continue
    fi
    # EOF o error de lectura (tipico al desconectar la placa): se emite lo
    # pendiente una sola vez y se termina; un error persistente no se repite.
    if [[ -n "$acumulado$linea" ]]; then
      emitir "$acumulado$linea"
    fi
    acumulado=""
    break
  fi
done

if [[ -n "$acumulado" ]]; then
  emitir "$acumulado"
fi

if [[ "$puerto" != "-" && ! -e "$puerto" ]]; then
  echo "monitorear_serie: el puerto $puerto ya no existe (¿se desconecto la placa?)" >&2
  echo "monitorear_serie: fin." >&2
  exit 3
fi

echo "monitorear_serie: fin." >&2
