# Imagen propia de Node-RED: los nodos extra se instalan al construir la imagen
# (reproducible), nunca desde la paleta en tiempo de ejecución.
# Incluye el frontend (web/), compilado en una etapa aparte de Node 22.
# Las bases van fijadas por digest; para actualizarlas, cambiar versión y digest
# a la vez.

# Etapa 1: compila el frontend. "web" es un contexto adicional del build
# (additional_contexts en orangepi/docker-compose.yml) y trae solo web/, así el
# contexto principal sigue siendo nodered/ y orangepi/.env nunca entra. Las
# dependencias de web/ no necesitan scripts de instalación, por eso
# --ignore-scripts. Para subir una versión, regenerar el lock en web/ con
# "npm install --package-lock-only --ignore-scripts".
FROM node:22-bookworm-slim@sha256:4b150b48b4116e2a22f2c39cc27025fbf4f153e2078fd3b14d159fdb5c7279da AS compilar-web
WORKDIR /web
COPY --from=web package.json package-lock.json ./
RUN npm ci --ignore-scripts --no-update-notifier --no-fund
# Solo lo que el build necesita; web/.dockerignore queda como segunda capa.
COPY --from=web index.html tsconfig.json vite.config.ts ./
COPY --from=web public ./public
COPY --from=web src ./src
RUN npm run build

# Etapa 2: la imagen final no lleva Node 22, npm ni código fuente de web/; solo
# el dist/ compilado.
FROM nodered/node-red:5.0.9@sha256:2ccd38b4066648bd5e17430c50a2f8102c566a82fd4f2fc5c342e61664d6896f

# package.json y package-lock.json fijan Node-RED y node-red-contrib-postgresql
# (consultas parametrizadas con $1, $2...). Ninguna dependencia necesita scripts
# de instalación, por eso --ignore-scripts. Para subir una versión, regenerar el
# lock con "npm install --package-lock-only --omit=dev --ignore-scripts".
COPY package.json package-lock.json /usr/src/node-red/
RUN npm ci --omit=dev --ignore-scripts --no-update-notifier --no-fund

# Producción: con NODE_ENV sin definir, Express responde a un cuerpo JSON mal formado (el bodyParser de la app del
# editor, que no pasa por la autenticación de la API) con la pila del error, y se la entrega a un cliente sin
# credenciales. Node-RED solo mira NODE_ENV para "development" (editor-api, theme.js). Va después de npm ci (que ya
# pasa --omit=dev) y solo en esta etapa: la del frontend instala devDependencies (Vite) y no debe tenerlo.
ENV NODE_ENV=production

# Los ajustes van en /etc/nodered, un directorio de root (755) con el archivo
# en 644. En /usr/src/node-red el usuario node-red puede borrar y reemplazar
# archivos aunque sean de root, porque el directorio es suyo; aquí no. Así el
# contenedor no puede quitar adminAuth, la autenticación de la API
# (autenticacion-api.js, que settings.js carga desde el mismo directorio), el enmascarado de cuentas del
# registro (registro-log.js, igual), el HTTPS ni el bloqueo de módulos.
# settings.js encuentra bcryptjs por el NODE_PATH de la imagen base.
# reporte_csv.py es el programa de Python que ejecuta el nodo exec de la pestaña Sesiones (CSV de
# asistencia, plan sección 6.4). Va aquí por el mismo motivo: lo que ejecuta exec no debe poder
# reescribirlo el contenedor. Usa solo la biblioteca estándar y el /usr/bin/python3 de la imagen base:
# se verificó en la placa el 5 de noviembre de 2026 (es Python 3.14.10, enlace a python3.14), y el
# paso 27 de orangepi/AGENTS.md lo vuelve a comprobar. No hace falta instalar nada (sin apk add).
COPY settings.js autenticacion-api.js registro-log.js reporte_csv.py /etc/nodered/

# El frontend compilado va en /usr/share/nodered-web, también de root y sin
# escritura para el usuario node-red (mismo criterio que settings.js): si el
# contenedor se compromete, no puede alterar el JavaScript que se sirve al
# navegador del profesor. settings.js lo publica en /app/ con httpStatic.
COPY --from=compilar-web --chown=root:root /web/dist/ /usr/share/nodered-web/

# El entrypoint de la imagen base ya pasa --userDir /data y $FLOWS.
CMD ["--settings", "/etc/nodered/settings.js"]
