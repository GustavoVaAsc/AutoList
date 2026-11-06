#!/bin/sh
# Simula lecturas del INA219 de la XIAO: publica N mensajes en
# aula/camara/energia con el formato del plan (sección 7.1):
#   {"v":12.1,"i":0.85,"p":10.3}
# Publica como el usuario "simulador" (solo puede escribir en aula/camara/energia;
# docs/contrato-mqtt.md, sección 6). Se ejecuta dentro del contenedor de Mosquitto,
# que ya tiene MQTT_SIMULADOR_PASSWORD, así que la contraseña no pasa por la línea
# de comandos del host. Tampoco por la de mosquitto_pub (se vería en
# /proc/<pid>/cmdline): va en el archivo de configuración del cliente, en un
# directorio temporal de /run/mosquitto (tmpfs, en RAM) que se borra al salir.
# Con use_username_as_clientid, dos simuladores a la vez se expulsan entre sí:
# ejecutar uno solo. Requiere el stack levantado (docker compose up -d).
#
# Uso:
#   herramientas/simular_energia.sh [N] [segundos_entre_mensajes]
#       N por defecto: 10. Pausa por defecto: 0.2
#   herramientas/simular_energia.sh --limpiar
#       Borra de metricas_energia TODAS las filas con origen 'ina219' (previa
#       confirmación).
#
# Las lecturas simuladas se guardan con origen='ina219', igual que las reales,
# y no se pueden distinguir después. Ejecutar --limpiar ANTES de que la XIAO
# empiece a publicar datos reales; después borraría también las reales.
# Termina con error (código distinto de cero) si no se puede publicar.
set -eu

COMPOSE="$(dirname "$0")/../orangepi/docker-compose.yml"

if [ "${1:-}" = "--limpiar" ]; then
    SQL_CUENTA="SELECT count(*) FROM metricas_energia WHERE origen = 'ina219'"
    # Usa el superusuario del contenedor de Postgres (el rol de Node-RED no borra).
    cuenta=$(docker compose -f "$COMPOSE" exec -T postgres sh -c "psql -U \"\$POSTGRES_USER\" -d \"\$POSTGRES_DB\" -tA -c \"$SQL_CUENTA\"" </dev/null)
    printf "Se borrarán %s filas con origen 'ina219' (simuladas Y reales). Escribe s para continuar: " "$cuenta"
    read -r respuesta
    [ "$respuesta" = "s" ] || { echo "Cancelado." >&2; exit 1; }
    docker compose -f "$COMPOSE" exec -T postgres sh -c 'psql -U "$POSTGRES_USER" -d "$POSTGRES_DB" -v ON_ERROR_STOP=1 -c "DELETE FROM metricas_energia WHERE origen = '"'"'ina219'"'"'"'
    exit 0
fi

N="${1:-10}"
PAUSA="${2:-0.2}"
case "$N" in
    ''|*[!0-9]*) echo "N debe ser un entero positivo" >&2; exit 1 ;;
esac
[ "$N" -ge 1 ] || { echo "N debe ser un entero positivo" >&2; exit 1; }

docker compose -f "$COMPOSE" exec -T mosquitto sh -s -- "$N" "$PAUSA" <<'FIN'
set -eu
n=$1
pausa=$2
d=$(mktemp -d /run/mosquitto/xdg.XXXXXX)
trap 'rm -rf "$d"' EXIT
trap 'exit 1' INT TERM
printf -- '-u simulador\n-P %s\n' "$MQTT_SIMULADOR_PASSWORD" > "$d/mosquitto_pub"
# v ~ 12 V (±0.3), i entre 0.6 y 1.1 A, p = v * i
awk -v n="$n" -v semilla="$(date +%s)" 'BEGIN {
    srand(semilla)
    for (k = 0; k < n; k++) {
        v = 11.7 + rand() * 0.6
        i = 0.6 + rand() * 0.5
        printf "{\"v\":%.2f,\"i\":%.3f,\"p\":%.2f}\n", v, i, v * i
    }
}' | while read -r payload; do
    if ! XDG_CONFIG_HOME="$d" mosquitto_pub -h localhost \
            -t aula/camara/energia -q 1 -m "$payload"; then
        echo "error: no se pudo publicar en aula/camara/energia (¿broker caído o credenciales incorrectas?)" >&2
        exit 1
    fi
    echo "publicado: $payload"
    sleep "$pausa"
done
FIN
