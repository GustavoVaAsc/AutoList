#!/bin/sh
# Genera el archivo de contraseñas de Mosquitto desde las variables de entorno
# y arranca el broker. Se ejecuta en cada arranque del contenedor.
set -eu

# El archivo de contraseñas separa usuario y clave con ":" y un usuario por línea:
# una clave con ":" o con salto de línea crearía un usuario que nunca autentica, sin
# ningún aviso. Se exige solo letras y números (openssl rand -hex 16). La función
# recibe el valor como parámetro del shell y nunca lo imprime.
requerir_clave() {
    nombre=$1
    valor=$2
    if [ -z "$valor" ]; then
        echo "error: falta $nombre en orangepi/.env" >&2
        exit 1
    fi
    case "$valor" in
        *[!A-Za-z0-9]*)
            echo "error: $nombre solo puede llevar letras y números (sin ':', espacios ni saltos de línea); genera una con openssl rand -hex 16" >&2
            exit 1
            ;;
    esac
}
requerir_clave MQTT_XIAO_PASSWORD "${MQTT_XIAO_PASSWORD:-}"
requerir_clave MQTT_VISION_PASSWORD "${MQTT_VISION_PASSWORD:-}"
requerir_clave MQTT_NODERED_PASSWORD "${MQTT_NODERED_PASSWORD:-}"
requerir_clave MQTT_PANTALLA_PASSWORD "${MQTT_PANTALLA_PASSWORD:-}"
requerir_clave MQTT_SIMULADOR_PASSWORD "${MQTT_SIMULADOR_PASSWORD:-}"

mkdir -p /run/mosquitto
# /run/mosquitto es un tmpfs del compose (nada de esto llega a la microSD), pero se
# limpia igual por si el entrypoint corre sobre un directorio que ya tenía archivos:
# mosquitto_passwd -c, ejecutado como root, se niega a sobrescribir ("File exists"),
# lo que causó un bucle de reinicios.
rm -f /run/mosquitto/passwd /run/mosquitto/passwd.tmp /run/mosquitto/acl
umask 077

# El archivo temporal tiene las claves en texto plano: se borra siempre al salir,
# también si algo falla.
trap 'rm -f /run/mosquitto/passwd.tmp' EXIT

# Las claves no pasan por la línea de comandos de ningún proceso (mosquitto_passwd -b
# las dejaría visibles en la lista de procesos). printf es una orden interna del
# shell: escribe el archivo en texto plano, y mosquitto_passwd -U lo convierte a
# hashes en el mismo archivo.
{
    printf '%s:%s\n' xiao "$MQTT_XIAO_PASSWORD"
    printf '%s:%s\n' vision "$MQTT_VISION_PASSWORD"
    printf '%s:%s\n' nodered "$MQTT_NODERED_PASSWORD"
    printf '%s:%s\n' pantalla "$MQTT_PANTALLA_PASSWORD"
    printf '%s:%s\n' simulador "$MQTT_SIMULADOR_PASSWORD"
} > /run/mosquitto/passwd.tmp
mosquitto_passwd -U /run/mosquitto/passwd.tmp >/dev/null
mv /run/mosquitto/passwd.tmp /run/mosquitto/passwd
chown mosquitto:mosquitto /run/mosquitto/passwd

# El broker exige que el archivo de ACL sea suyo y no sea legible por otros. El
# del repo se monta de solo lectura con el dueño del host, así que se copia.
cp /mosquitto/acl /run/mosquitto/acl
chown mosquitto:mosquitto /run/mosquitto/acl

# El volumen nuevo es de root y el broker escribe la persistencia como "mosquitto".
chown mosquitto:mosquitto /mosquitto/data

exec "$@"
