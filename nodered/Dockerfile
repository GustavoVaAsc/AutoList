# Imagen propia de Node-RED: los nodos extra se instalan al construir la imagen
# (reproducible), nunca desde la paleta en tiempo de ejecución.
# Base fijada por digest; para actualizarla, cambiar versión y digest a la vez.
FROM nodered/node-red:5.0.9@sha256:2ccd38b4066648bd5e17430c50a2f8102c566a82fd4f2fc5c342e61664d6896f

# package.json y package-lock.json fijan Node-RED y node-red-contrib-postgresql
# (consultas parametrizadas con $1, $2...). Ninguna dependencia necesita scripts
# de instalación, por eso --ignore-scripts. Para subir una versión, regenerar el
# lock con "npm install --package-lock-only --omit=dev --ignore-scripts".
COPY package.json package-lock.json /usr/src/node-red/
RUN npm ci --omit=dev --ignore-scripts --no-update-notifier --no-fund

# Los ajustes van en /etc/nodered, un directorio de root (755) con el archivo
# en 644. En /usr/src/node-red el usuario node-red puede borrar y reemplazar
# archivos aunque sean de root, porque el directorio es suyo; aquí no. Así el
# contenedor no puede quitar adminAuth, el HTTPS ni el bloqueo de módulos.
# settings.js encuentra bcryptjs por el NODE_PATH de la imagen base.
COPY settings.js /etc/nodered/settings.js

# El entrypoint de la imagen base ya pasa --userDir /data y $FLOWS.
CMD ["--settings", "/etc/nodered/settings.js"]
