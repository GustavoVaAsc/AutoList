#!/bin/sh
# Genera el archivo de contraseñas de Mosquitto desde las variables de entorno
# y arranca el broker. Se ejecuta en cada arranque del contenedor.
set -eu

: "${MQTT_USER:?falta MQTT_USER en orangepi/.env}"
: "${MQTT_PASSWORD:?falta MQTT_PASSWORD en orangepi/.env}"

mkdir -p /run/mosquitto
# Un reinicio conserva el sistema de archivos del contenedor, y el archivo ya
# pertenece a "mosquitto": mosquitto_passwd -c, ejecutado como root, se niega a
# sobrescribirlo ("File exists"), lo que causó un bucle de reinicios. Se borra antes.
rm -f /run/mosquitto/passwd
umask 077
mosquitto_passwd -b -c /run/mosquitto/passwd "$MQTT_USER" "$MQTT_PASSWORD"
chown mosquitto:mosquitto /run/mosquitto/passwd

# El volumen nuevo es de root y el broker escribe la persistencia como "mosquitto".
chown mosquitto:mosquitto /mosquitto/data

exec "$@"
