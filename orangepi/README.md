# Stack de la Orange Pi

Este directorio levanta con Docker Compose el stack del edge de la Orange Pi Zero 3W: broker MQTT, Node-RED, Postgres con pgvector y Portainer. El diseño general está en [docs/HIGH-LEVEL-PLAN.md](../docs/HIGH-LEVEL-PLAN.md).

Todos los comandos de esta guía se ejecutan desde `orangepi/`, salvo que se indique otra carpeta. El proyecto se llama `pase-lista` (`name:` en `docker-compose.yml`), así que contenedores y volúmenes llevan ese prefijo.

## Contenido

- [Qué levanta](#qué-levanta)
- [Requisitos](#requisitos)
- [Primer arranque](#primer-arranque)
- [Acceso](#acceso)
- [Usuarios MQTT y ACL](#usuarios-mqtt-y-acl)
- [Energía](#energía)
- [Operación](#operación)
- [Seguridad y riesgos aceptados](#seguridad-y-riesgos-aceptados)

## Qué levanta

El archivo `docker-compose.yml` define cuatro servicios. Node-RED se conecta a Mosquitto y a Postgres por el nombre del servicio dentro de la red del compose (`mosquitto`, `postgres`).

| Servicio | Puerto | Protocolo | Quién puede conectarse | Autenticación |
|---|---|---|---|---|
| Mosquitto | 1883 | MQTT | Cualquier equipo que llegue a la placa | Usuario y contraseña por rol (`MQTT_<USUARIO>_PASSWORD`) y ACL. Ver [Usuarios MQTT y ACL](#usuarios-mqtt-y-acl) |
| Node-RED | 1880 | HTTPS, certificado autofirmado | Cualquier equipo que llegue a la placa | Editor (`/`): `adminAuth`. API `/api/...` y frontend (`/app/`): sin autenticación hoy |
| Postgres | 5432 | SQL | Solo la placa (`127.0.0.1`) | `POSTGRES_USER` es superusuario; Node-RED usa `NODERED_DB_USER` |
| Portainer CE | 9443 | HTTPS, certificado autofirmado | Solo la placa; desde otro equipo, por túnel SSH | Contraseña de administrador (`PORTAINER_ADMIN_PASSWORD`) |

Los puertos 1883 y 1880 se publican sin `127.0.0.1:` (`docker-compose.yml`, servicios `mosquitto` y `nodered`), así que cualquier equipo de la red que llegue a la placa los alcanza. Postgres y Portainer solo escuchan en el loopback de la placa.

Mosquitto no acepta conexiones anónimas (`mosquitto/mosquitto.conf`, `allow_anonymous false`) y solo tiene el listener 1883: no hay WebSocket, porque el frontend no habla MQTT.

## Requisitos

| Requisito | Detalle |
|---|---|
| Docker con Compose | Docker Compose 2.23.1 o posterior. Los secretos con origen `environment` lo exigen (`docker-compose.yml`, comentario del servicio `portainer`). Ese mínimo cubre también `additional_contexts` del servicio `nodered`, que `docker-compose.yml` usa para compilar `web/`. La placa tiene 5.6.0, que lo soporta. |
| Arquitectura | arm64, la de la Orange Pi Zero 3W |
| Escritura en `nodered/flows` | El uid 1000 debe poder escribir en `nodered/flows`, porque Node-RED guarda ahí `flows.json` (`docker-compose.yml`, servicio `nodered`). En la placa, ese usuario es `orangepi`. Comprobar con `stat -c '%u %A' nodered/flows`. |
| `openssl` | En el host, para generar contraseñas (`.env.example`). |

## Primer arranque

### Crear el archivo de credenciales

Copiar la plantilla y restringir el archivo:

```bash
cp .env.example .env
chmod 600 .env
```

`orangepi/.env` contiene las credenciales reales y git lo ignora. Compose lo lee solo porque está junto a `docker-compose.yml`. Nunca lo subas al repositorio.

Generar cada contraseña con `openssl rand -hex 16`. Las cinco de MQTT se generan con el comando de [Generar las contraseñas MQTT](#generar-las-contraseñas-mqtt), que no las imprime. El comando imprime 32 caracteres hexadecimales. La plantilla pide usar solo letras y números, y Portainer exige al menos 12 caracteres.

Después, sustituir cada valor `cambiar_esta_clave` en `.env`:

| Variable | Uso | Cuándo se aplica |
|---|---|---|
| `MQTT_XIAO_PASSWORD`, `MQTT_VISION_PASSWORD`, `MQTT_NODERED_PASSWORD`, `MQTT_PANTALLA_PASSWORD`, `MQTT_SIMULADOR_PASSWORD` | Contraseña de cada usuario de Mosquitto (los nombres de usuario son fijos). Node-RED usa la de `nodered`. Ver [Usuarios MQTT y ACL](#usuarios-mqtt-y-acl). | En cada arranque de Mosquitto. La de `nodered` también en el de Node-RED |
| `POSTGRES_USER`, `POSTGRES_PASSWORD`, `POSTGRES_DB` | Superusuario y base de datos de Postgres. Node-RED no usa el superusuario. | Solo al crear el volumen `pgdata` |
| `NODERED_DB_USER`, `NODERED_DB_PASSWORD` | Rol de Node-RED en Postgres, con permisos mínimos (`sql/roles.sh`) | Al crear `pgdata`. Node-RED lee su copia en cada arranque, así que debe coincidir con la contraseña real del rol. |
| `NODERED_ADMIN_USER`, `NODERED_ADMIN_PASSWORD` | Login del editor de Node-RED | En cada arranque de Node-RED |
| `NODERED_TLS_SAN` | Nombres e IP del certificado HTTPS. Opcional; por omisión `DNS:localhost,IP:127.0.0.1`. | En cada arranque de Node-RED. Si cambia, el certificado se regenera. |
| `PORTAINER_ADMIN_PASSWORD` | Contraseña inicial del administrador de Portainer | Solo al crear `portainer_data` |
| `GALERIA_TOKEN` | Token de `GET /api/galeria`. Se usará desde el 11 nov; ningún contenedor lo recibe todavía. | Aún no se aplica |

Si Compose no encuentra una de las variables obligatorias, falla con un mensaje que indica cuál falta.

### NODERED_TLS_SAN

El certificado de Node-RED solo es válido para los nombres e IP que aparecen en `NODERED_TLS_SAN`. Para entrar por la IP de la placa, incluirla. Ejemplo con la IP `192.168.1.50`, que se debe sustituir por la de la placa:

```bash
NODERED_TLS_SAN=DNS:localhost,IP:127.0.0.1,IP:192.168.1.50,DNS:orangepi.local
```

Si la IP de la placa cambia, actualizar esta variable y reiniciar Node-RED.

### Levantar el stack

```bash
docker compose up -d --build
```

El `--build` construye la imagen de Node-RED, que instala sus nodos extra (`nodered/Dockerfile`). Para comprobar el estado:

```bash
docker compose ps
```

Mosquitto, Postgres y Node-RED tienen healthcheck y deben mostrar `healthy`. Portainer no tiene healthcheck, porque su imagen no trae shell ni curl.

### Cambiar valores después del primer arranque

Las variables de Postgres y de Portainer no se releen al arrancar. Para cambiarlas hay que aplicar el cambio dentro del servicio:

| Qué cambiar | Procedimiento |
|---|---|
| `MQTT_*_PASSWORD`, `NODERED_ADMIN_*`, `NODERED_TLS_SAN` | Editar `.env` y ejecutar `docker compose up -d`. Compose recrea los contenedores con los valores nuevos. |
| `NODERED_DB_PASSWORD` | Editar `.env`, ejecutar `docker compose up -d` y luego `docker compose exec postgres sh /docker-entrypoint-initdb.d/02-roles.sh`. El script es repetible: crea el rol si falta y actualiza su contraseña. |
| `POSTGRES_PASSWORD` | Abrir psql con `docker compose exec postgres sh -c 'psql -U "$POSTGRES_USER" -d "$POSTGRES_DB"'`, ejecutar `\password` (pide la clave nueva sin mostrarla) y después igualar `.env`. |
| `PORTAINER_ADMIN_PASSWORD` | Cambiar la contraseña desde Portainer. La variable de `.env` ya no tiene efecto. |

La alternativa para cualquier valor de Postgres o Portainer es `docker compose down -v`, que borra todos los datos. Solo sirve en un volumen que se puede perder.

## Acceso

### Node-RED

El editor responde en `https://<ip>:1880/`, la API en `https://<ip>:1880/api/...` y el frontend web en `https://<ip>:1880/app/`. El navegador avisa del certificado autofirmado: aceptar la excepción, o instalar el certificado como de confianza.

Para instalarlo, copiar el certificado desde el volumen a tu home de la placa. La llave privada se queda en el volumen:

```bash
docker compose cp nodered:/data/certs/nodered.crt ~/nodered.crt
```

Después, copiar `nodered.crt` a tu equipo (por ejemplo con `scp`) e instalarlo como certificado de confianza. El certificado no incluye la IP si no está en `NODERED_TLS_SAN`, y el navegador lo rechaza en ese caso.

Iniciar sesión con `NODERED_ADMIN_USER` y `NODERED_ADMIN_PASSWORD`.

### Portainer

Portainer solo escucha en `127.0.0.1:9443` de la placa. Desde tu equipo, abrir un túnel SSH y dejarlo abierto:

```bash
ssh -L 9443:localhost:9443 orangepi@<ip>
```

Después, abrir `https://localhost:9443` en el navegador de tu equipo. Entrar con el usuario `admin` y la contraseña de `PORTAINER_ADMIN_PASSWORD`.

### Postgres

Postgres publica el puerto 5432 solo en `127.0.0.1`. Para administrarlo, entrar al contenedor. Las variables se expanden dentro del contenedor, así que la contraseña no aparece en el historial de la shell:

```bash
docker compose exec postgres sh -c 'psql -U "$POSTGRES_USER" -d "$POSTGRES_DB"'
```

Para ejecutar una sola consulta, añadir `-c` con la sentencia entre comillas dobles, dentro de las comillas simples:

```bash
docker compose exec postgres sh -c 'psql -U "$POSTGRES_USER" -d "$POSTGRES_DB" -c "SELECT count(*) FROM metricas_energia"'
```

Zona horaria. La base del proyecto tiene `timezone = America/Mexico_City` (`sql/schema.sql`), así que `::date`, `date_trunc('day', ...)` y `CURRENT_DATE` calculan el día de CDMX. Los `timestamptz` se guardan en UTC igual que antes, y `log_timezone` sigue en `Etc/UTC`: los logs de Postgres y Mosquitto no cambian. Regla para las consultas de reporte que agrupen o comparen por día: escribir la zona explícita, aunque la base ya la tenga, para que el resultado no dependa de la configuración de la sesión:

```sql
SELECT (hora AT TIME ZONE 'America/Mexico_City')::date AS dia, count(*)
FROM asistencias GROUP BY dia;
```

Un volumen creado antes de este ajuste necesita un paso manual: ver [Volúmenes existentes](#volúmenes-existentes).

## Usuarios MQTT y ACL

Mosquitto tiene cinco usuarios, con nombre fijo (el de `docs/contrato-mqtt.md`, sección 6) y contraseña propia en `orangepi/.env`. La ACL (`mosquitto/acl`, sin secretos) limita lo que cada uno lee y escribe. Lo que no está permitido se deniega.

| Usuario | Quién lo usa | Contraseña en `.env` | Puede |
|---|---|---|---|
| `xiao` | Firmware de la XIAO | `MQTT_XIAO_PASSWORD` | Leer órdenes (`aula/actuador/cmd`) y el estado de la sesión; escribir `aula/camara/pir`, `aula/camara/energia`, `aula/camara/estado` y `aula/actuador/estado` |
| `vision` | Servicio de visión | `MQTT_VISION_PASSWORD` | Leer el PIR, `aula/vision/cmd` y `aula/vision/galeria/#`; escribir sus resultados, el progreso, el embedding y el estado |
| `nodered` | Node-RED | `MQTT_NODERED_PASSWORD` | Leer y escribir `aula/#` |
| `pantalla` | Programa de la pantalla ILI9341 | `MQTT_PANTALLA_PASSWORD` | Leer sesión, progreso del registro, estado del actuador y reinicio; escribir `aula/sesion/cmd` y `aula/actuador/cmd` |
| `simulador` | `herramientas/simular_energia.sh` | `MQTT_SIMULADOR_PASSWORD` | Solo escribir `aula/camara/energia`; no lee nada |

No existe usuario `frontend`: el navegador no habla MQTT y todo pasa por la API de Node-RED, que publica con el usuario `nodered`. La tabla completa de permisos está en la sección 6 del contrato.

Cada vez que Mosquitto arranca, `mosquitto/entrypoint.sh` crea el archivo de contraseñas con los cinco usuarios a partir de las variables, sin pasarlas por la línea de comandos de ningún proceso, y copia la ACL. Un cambio en `.env` se aplica con `docker compose up -d`. Un cambio en `mosquitto/acl` o en `mosquitto/mosquitto.conf` no lo detecta Compose (son archivos montados): aplicarlo con `docker compose restart mosquitto`.

### Generar las contraseñas MQTT

Este comando lo ejecuta la persona que administra el stack, desde `orangepi/`, con `.env` ya creado (si no existe, falla sin tocar nada). Escribe una contraseña nueva y aleatoria para cada usuario directamente en `.env`, sin imprimirla, y quita las variables viejas `MQTT_USER` y `MQTT_PASSWORD`. `printf` es una orden interna del shell, así que las contraseñas no aparecen en la lista de procesos. Las contraseñas MQTT solo pueden llevar letras y números: Mosquitto no arranca con otra cosa (`:` o un salto de línea romperían su archivo de contraseñas sin avisar), y `openssl rand -hex 16` siempre cumple. Si algo falla, el subshell aborta; `.env` nunca queda vacío ni truncado, porque cada paso lo reemplaza completo.

```bash
(
  umask 077
  set -e
  trap 'rm -f .env.nuevo' EXIT
  [ -f .env ] || { echo "no existe .env: copiar .env.example" >&2; exit 1; }
  for u in XIAO VISION NODERED PANTALLA SIMULADOR; do
    v="MQTT_${u}_PASSWORD"
    grep -v "^${v}=" .env > .env.nuevo || [ $? -eq 1 ]
    nueva=$(openssl rand -hex 16)
    [ -n "$nueva" ] || { echo "openssl no generó la contraseña" >&2; exit 1; }
    printf '%s=%s\n' "$v" "$nueva" >> .env.nuevo
    [ -s .env.nuevo ] || { echo "el .env nuevo quedó vacío" >&2; exit 1; }
    mv .env.nuevo .env
  done
  grep -v -e '^MQTT_USER=' -e '^MQTT_PASSWORD=' .env > .env.nuevo || [ $? -eq 1 ]
  [ -s .env.nuevo ] || { echo "el .env nuevo quedó vacío" >&2; exit 1; }
  mv .env.nuevo .env
)
```

Volver a ejecutarlo cambia las cinco contraseñas y deja sin acceso a los clientes que tengan las anteriores. Para cambiar solo una, dejar esa en la lista del `for` y omitir las tres últimas líneas antes del `)`. Después, `docker compose up -d --build`.

### Copiar las contraseñas a los clientes

Cada cliente guarda su contraseña fuera de git:

| Cliente | Dónde va su contraseña |
|---|---|
| Servicio de visión (Gustavo) | La de `vision` se copia también a `MQTT_PASSWORD` en `/etc/pase-lista/vision.env` (`orangepi/vision/vision.env.example`). Su `vision.conf` ya usa `usuario: "vision"` y `client_id: "vision"`. Reiniciar el servicio después. |
| Firmware de la XIAO (Gustavo) | La de `xiao`, en la configuración del firmware que no se versiona. |
| Pantalla (`orangepi/pantalla`, aún no existe) | La de `pantalla`, en el archivo de entorno de su unidad systemd. |

### Un usuario, un cliente

El broker usa el nombre de usuario como client id (`use_username_as_clientid`) e ignora el que traiga el cliente: por eso el `clientid` del nodo MQTT en `flows.json` y el `client_id` de `vision.conf` no cuentan. Consecuencia: **dos clientes conectados con el mismo usuario se expulsan entre sí**. El segundo desconecta al primero, y si los dos reconectan solos, se desconectan en bucle. Por eso:

- No depurar con el usuario de un servicio que está corriendo. Un `mosquitto_sub` con el usuario `vision` expulsa al servicio de visión.
- No lanzar dos simuladores a la vez.
- Una sola instancia por rol. Un segundo firmware de XIAO o una segunda pantalla con el mismo usuario expulsaría al primero.

El healthcheck del compose no usa usuario por esa razón: se conecta sin credenciales y espera ser rechazado (código 5), lo que prueba que el broker contesta y que no admite anónimos. Cada minuto deja en el log un `Client healthcheck ... disconnected: not authorised`.

### Depurar el tráfico MQTT

Mosquitto descarta en silencio las publicaciones que la ACL deniega: el publicador no recibe error. Para comprobar un permiso, usar un suscriptor autorizado con tiempo límite (`-W`), no el código de salida del publicador. Para ver tráfico:

1. El editor de Node-RED, con un nodo `mqtt in` en `aula/#` y un nodo debug: no necesita otro usuario ni detener nada. Es la opción preferida.
2. Con `mosquitto_sub` dentro del contenedor y el usuario `nodered`, **solo con Node-RED detenido**, porque usaría el mismo client id. Se excluyen los tópicos con embeddings (datos biométricos) y miniaturas. Escucha 30 s; después se vuelve a levantar Node-RED:

   ```bash
   docker compose stop nodered
   docker compose exec -T mosquitto sh -c 'd=$(mktemp -d /run/mosquitto/xdg.XXXXXX); printf -- "-u nodered\n-P %s\n" "$MQTT_NODERED_PASSWORD" > "$d/mosquitto_sub"; XDG_CONFIG_HOME="$d" mosquitto_sub -h localhost -t "aula/#" -T aula/vision/embedding -T aula/vision/enrolamiento -v -W 30; rm -rf "$d"'
   docker compose start nodered
   ```

   La contraseña se expande dentro del contenedor y va en el archivo de configuración del cliente (en el tmpfs `/run/mosquitto`), no en `-P`: así no queda en el historial, ni en el chat, ni en `/proc/<pid>/cmdline` del host, donde cualquier usuario de la placa vería la línea de comandos de `mosquitto_sub`. El archivo se borra al terminar. Mientras Node-RED está detenido no hay ingesta ni pase de lista.
3. `docker compose logs --tail=100 mosquitto` muestra conexiones, expulsiones (`session taken over`) y rechazos de login.

No se creó un usuario de depuración: sería otro actor con lectura de `aula/#`, incluidos los embeddings. Si el equipo lo necesita, se decide en el contrato.

## Energía

### Mensajes de energía

El flujo `Energía` de `nodered/flows/flows.json` se suscribe al tópico `aula/camara/energia` con QoS 1.

| Campo | Valor |
|---|---|
| Tópico | `aula/camara/energia` |
| Payload | JSON con `v`, `i` y `p` numéricos: `{"v":12.1,"i":0.85,"p":10.3}` |
| Rangos | `v` de 0 a 30 (V), `i` de 0 a 10 (A), `p` de 0 a 300 (W) |
| Tamaño máximo | 1024 caracteres |

Un mensaje que no cumple el formato o los rangos se descarta y su motivo aparece en el log de Node-RED. Cada lectura válida se inserta en `metricas_energia` con `origen = 'ina219'`.

El broker usa `cleansession=true`. Las lecturas publicadas mientras Node-RED se redespliega se pierden. El flujo lo documenta como aceptable para lecturas de energía.

### Simulador

`herramientas/simular_energia.sh` publica lecturas falsas por el mismo tópico y por el mismo flujo de ingesta. Necesita el stack levantado. Publica como el usuario `simulador` (solo puede escribir en `aula/camara/energia`). Se ejecuta dentro del contenedor de Mosquitto, así que la contraseña no pasa por la línea de comandos del host.

Con `docker compose exec -T`, un Ctrl-C en el host puede no llegar al script, que termina solo. Si se mata a la fuerza, el directorio temporal `/run/mosquitto/xdg.*` (con la contraseña) queda en el tmpfs hasta que se reinicie el contenedor de Mosquitto, que lo borra.

```bash
herramientas/simular_energia.sh 5 1
```

Cada línea de salida tiene esta forma (los valores son ilustrativos):

```
publicado: {"v":11.94,"i":0.713,"p":8.51}
```

| Argumento | Por omisión | Significado |
|---|---|---|
| `N` | 10 | Número de mensajes a publicar. Debe ser un entero mayor o igual a 1. |
| `segundos_entre_mensajes` | 0.2 | Pausa entre mensajes |
| `--limpiar` | No aplica | Pide confirmación (escribir `s`) y borra de `metricas_energia` todas las filas con `origen = 'ina219'` |

> **Antes de que la XIAO publique datos reales, borrar las lecturas simuladas con `herramientas/simular_energia.sh --limpiar`.** Las simuladas y las reales se guardan igual, con `origen = 'ina219'`, y no hay forma de distinguirlas después. Si se ejecuta `--limpiar` cuando ya hay lecturas reales, también se borran.

### API de lectura

`GET /api/metricas/energia` devuelve las lecturas `ina219` de la más nueva a la más vieja (`nodered/flows/flows.json`, flujo `Energía`). Acepta estos parámetros:

| Parámetro | Por omisión | Reglas | Si no se cumplen |
|---|---|---|---|
| `limite` | 100 | Entero mayor o igual a 1. Valores mayores a 1000 se recortan a 1000. | 400 |
| `desde` | Sin filtro | Fecha ISO 8601 entre los años 1970 y 9999. Formatos válidos: `2026-11-04`, `2026-11-04T10:00`, `2026-11-04T10:00:00Z`, `2026-11-04T10:00:00-06:00`. | 400 |

En la URL, un `+` del desfase horario se lee como espacio. Usar `Z` o codificarlo como `%2B`.

Cada elemento de la respuesta tiene los campos `hora`, `v`, `i` y `p`. El formato de `hora` lo serializa el driver de Postgres, y el flujo no lo fija.

```bash
curl -k "https://<ip>:1880/api/metricas/energia?limite=5&desde=2026-11-04T10:00:00Z"
```

La respuesta correcta es un arreglo JSON (valores ilustrativos):

```json
[
  {"hora": "2026-11-04T10:02:11.000Z", "v": 11.94, "i": 0.713, "p": 8.51}
]
```

Un error de validación responde 400 con `{"error": "..."}`. Un error de la base de datos responde 500 con `{"error": "No se pudo consultar la base de datos"}`.

`-k` acepta el certificado autofirmado sin verificarlo. Con el certificado instalado, se puede quitar.

## Operación

### Comandos

| Acción | Comando |
|---|---|
| Levantar o aplicar cambios de compose y `.env` | `docker compose up -d` |
| Reconstruir la imagen de Node-RED (incluye el frontend) | `docker compose up -d --build` |
| Reconstruir solo Node-RED, sin tocar los demás servicios | `docker compose up -d --no-deps --build nodered` |
| Detener y quitar los contenedores (conserva los volúmenes) | `docker compose down` |
| Ver el estado de los servicios | `docker compose ps` |
| Seguir el log de un servicio | `docker compose logs -f nodered` |

Los nombres de servicio son `mosquitto`, `postgres`, `nodered` y `portainer`.

### Reconstruir Node-RED

Usar `--build` después de cambiar `nodered/settings.js`, `nodered/Dockerfile`, `nodered/package.json`, `nodered/package-lock.json` o cualquier archivo de `web/`. Estos archivos se copian a la imagen (los de `web/` se compilan en ella), así que sin reconstruirla el cambio no tiene efecto.

La etiqueta de la imagen está fijada en `docker-compose.yml` (`image: pase-lista/nodered:5.0.9-11`). Al reconstruir por un cambio, subir el sufijo (`-12`, `-13`...), como indica el comentario del archivo.

Para aplicar solo un cambio de Node-RED o del frontend, sin tocar los demás servicios, desde `orangepi/`:

```bash
docker compose up -d --no-deps --build nodered
```

`--no-deps` evita recrear Mosquitto y Postgres. Node-RED tarda hasta 60 s en estar `healthy`.

### Frontend web

El frontend (`web/`, Vite + React + TypeScript) se compila al construir la imagen de Node-RED y se sirve en `https://<ip>:1880/app/`. El editor sigue en `/`. Los archivos compilados quedan en `/usr/share/nodered-web`, un directorio de root que el contenedor no puede modificar. No habla MQTT: no hay listener WebSocket ni usuario MQTT para el frontend, y todo pasa por la API HTTP de Node-RED. El mecanismo del progreso en vivo del registro se decide el 11 de noviembre (`docs/contrato-mqtt.md`, sección 6). Para desarrollarlo desde una laptop, ver [web/README.md](../web/README.md).

El contexto de build de Node-RED sigue siendo `nodered/`. `web/` entra como contexto adicional (`additional_contexts` en `docker-compose.yml`), de modo que `orangepi/.env` nunca forma parte del contexto.

### Desplegar flujos

Cada Deploy del editor de Node-RED escribe `nodered/flows/flows.json`. Antes de hacer commit, revisar el cambio:

```bash
git diff -- nodered/flows/flows.json
```

Revisar el diff también busca cambios que no se hicieron en el editor. Los respaldos y temporales que Node-RED crea junto a `flows.json` están en `.gitignore`. Las credenciales no van en el archivo: Node-RED las guarda en el volumen `/data`.

### Logs

Los logs de cada contenedor rotan: se guardan como máximo 3 archivos de 1 MB.

Mosquitto registra las conexiones y los rechazos de login (`not authorised`, con el cliente y su IP). Para revisar intentos fallidos: `docker compose logs mosquitto | grep "not authorised" | grep -v -E 'Client healthcheck \[(::1|127\.0\.0\.1):'`. El healthcheck es un cliente anónimo que se rechaza a propósito cada minuto, y se reconoce por venir de la propia placa (`::1` o `127.0.0.1`); filtrar por el nombre "healthcheck" a secas ocultaría a un atacante que use `-i healthcheck` desde la red. Con la rotación, el historial alcanza unos días; si se necesita conservar evidencia, copiarlo antes de que rote.

### Validar el compose sin mostrar secretos

No pegar la salida de `docker compose config` en un documento, un chat o un issue: imprime las contraseñas de `.env`. Usar una de estas dos opciones:

| Opción | Efecto |
|---|---|
| `docker compose config -q` | Valida el archivo y no imprime nada |
| `docker compose config --no-interpolate` | Imprime el archivo sin sustituir las variables, así que aparecen como `${VAR}` |

### Volúmenes existentes

Los scripts de initdb de Postgres (`sql/schema.sql` como `01-schema.sql` y `sql/roles.sh` como `02-roles.sh`) solo corren al crear `pgdata`. Si el volumen ya existía, aplicar sus cambios con el stack levantado:

```bash
docker compose exec postgres sh /docker-entrypoint-initdb.d/02-roles.sh
docker compose exec postgres sh -c 'psql -U "$POSTGRES_USER" -d "$POSTGRES_DB" -c "CREATE INDEX IF NOT EXISTS metricas_energia_origen_hora_idx ON metricas_energia (origen, hora DESC, id DESC)"'
```

La alternativa es `docker compose down -v`, que borra todos los datos.

Zona horaria de la base. `sql/schema.sql` fija `timezone = America/Mexico_City` en la base del proyecto (`ALTER DATABASE ... SET`), pero un volumen creado antes de ese cambio sigue en `Etc/UTC`. Para aplicarlo sin borrar datos, con el stack levantado (es un ajuste de configuración, no una tabla nueva; se puede repetir):

```bash
docker compose exec -T postgres sh -c 'psql -U "$POSTGRES_USER" -d "$POSTGRES_DB" -v ON_ERROR_STOP=1' <<'SQL'
DO $$
BEGIN
  EXECUTE format('ALTER DATABASE %I SET timezone = %L',
                 current_database(), 'America/Mexico_City');
END
$$;
SQL
docker compose restart nodered
```

Postgres no se reinicia: el ajuste vale para las conexiones nuevas. Node-RED sí se reinicia, porque su pool de conexiones conserva la zona anterior hasta que reconecta. No se toca Mosquitto. Para comprobarlo (`log_timezone` sigue en `Etc/UTC` a propósito, para que los logs no cambien):

```bash
docker compose exec -T postgres sh -c 'psql -U "$POSTGRES_USER" -d "$POSTGRES_DB" -tA -c "SHOW timezone" -c "SHOW log_timezone"'
```

Debe imprimir `America/Mexico_City` y `Etc/UTC`. La zona está en la base y no en el rol de Node-RED: así la heredan todos los roles (incluido el superusuario, con el que se hacen las consultas a mano) y hay un solo lugar que cambiar. Un `ALTER ROLE ... SET timezone` pisaría el de la base y quedaría duplicado.

Las tablas `alumnos`, `sesiones`, `asistencias` e `intentos` también vienen de `schema.sql`, así que un volumen creado antes de agregarlas no las tiene. En ese caso `02-roles.sh` falla a mitad: el rol y su contraseña ya se aplicaron, pero los permisos sobre las tablas que no existen no. Recrear el volumen con `down -v` (si solo hay datos de prueba) o consultar al equipo antes de crear las tablas a mano.

## Seguridad y riesgos aceptados

### Riesgos aceptados hoy

| Riesgo | Detalle |
|---|---|
| La API no tiene autenticación | `/api/metricas/energia` responde a cualquiera que llegue a la placa. Solo sirve lecturas de energía. |
| MQTT sin TLS | El listener 1883 va en texto plano: usuario y contraseña viajan sin cifrar. |
| Una contraseña MQTT filtrada | La de `xiao` es la más expuesta: viaja en claro por la LAN. Con la contraseña de un rol, alguien conectado a la LAN puede conectarse con ese usuario y, por `use_username_as_clientid`, expulsar al cliente legítimo; al caer su conexión se dispara su Will (por ejemplo `{"en_linea": false}` en `aula/camara/estado`, retenido), y el atacante puede dejar retenido un estado falso en los tópicos que ese rol puede escribir. La ACL limita qué tópicos toca cada rol, pero no evita esto. Mitigaciones pendientes: restringir el puerto 1883 a la IP de la XIAO (y a la placa) en la cadena `DOCKER-USER` del host (cambio del host: lo coordina Yordi con el equipo, y aún no se hizo); descartar en Node-RED los estados vencidos por `ts` (flujos del 11 nov, Yordi); TLS en MQTT (sin decidir, ver Pendiente). |
| Mensajes retenidos falsos | La ACL de Mosquitto 2 (`acl_file`) no puede impedir el retain por tópico. La defensa es que los consumidores de todo tópico con Retain = no ignoren lo retenido y lo vacío, en cualquier tópico (contrato MQTT, sección 1: la XIAO, el servicio de visión, incluidos `aula/camara/pir` y `aula/vision/cmd`, y Node-RED). Node-RED borra los retenidos de `*/cmd`, `aula/camara/pir` y `aula/camara/energia` en cada conexión al broker (flujo "Limpieza de retenidos"), lo que provoca un mensaje vacío en vivo en cada uno de esos tópicos, y su ingesta descarta `aula/camara/energia` con retain (con aviso) y vacío (en silencio). Para los demás tópicos con Retain = no (`aula/vision/resultado`, `aula/vision/embedding`, `aula/vision/enrolamiento`, `aula/vision/galeria/alta`, `aula/vision/galeria/baja`, `aula/pantalla/reinicio` y `aula/sistema/metricas`), los flujos del 11 nov deben descartar retain, o se amplía la lista de la limpieza. Un plugin de autorización de broker que sí lo impidiera queda fuera: es una decisión humana, y no se instala sin ella. |
| Node-RED puede ejecutar código | Un nodo `exec`, previsto en el plan para el reporte CSV, ejecutaría comandos en el contenedor. Por eso el editor exige `adminAuth` (`nodered/settings.js`, línea 13), y `settings.js` no es escribible desde el contenedor (`nodered/Dockerfile`). |
| Portainer controla el host | Portainer monta el socket de Docker (`/var/run/docker.sock`). Por eso su puerto solo escucha en la placa. |

### Pendiente

No se sirve `/api/galeria` sin `httpNodeAuth` y `GALERIA_TOKEN` (11 nov). No se registra a ninguna persona real sin `httpNodeAuth` y sin las ACL de Mosquitto (4 nov); antes de eso, solo identidades de prueba (LFW y miembros del equipo). Las ACL de Mosquitto ya están activas (contrato MQTT v0.5); falta `httpNodeAuth`.

#### Para el 11 de noviembre

| Pendiente | Estado en el repo |
|---|---|
| `httpNodeAuth` en la API | Pendiente. El comentario de `nodered/settings.js` indica dónde se añade. |
| `GALERIA_TOKEN` | La variable está en `.env.example` y ningún contenedor la recibe todavía. Desde el 11 nov, `GET /api/galeria` exigirá el encabezado `X-Galeria-Token`; solo Node-RED y el servicio de visión tendrán el token. |

#### Sin fecha (sin decidir)

| Pendiente | Estado en el repo |
|---|---|
| MQTT cifrado | No hay listeners TLS en `mosquitto/mosquitto.conf`. No se implementa sin decisión del equipo. |

### Datos biométricos

Retirar el consentimiento de un alumno borra su embedding de la base al instante (`embedding = NULL`) y de los archivos de datos cuando autovacuum limpia las tuplas muertas de `alumnos` y de su tabla TOAST. Los bytes pueden sobrevivir en el WAL o en espacio libre hasta que se sobrescriban, y cualquier `pg_dump` o copia del volumen `pgdata` los conserva. Borrar por completo la identidad de una persona (`nombre`, `no_cuenta`) es un procedimiento de superusuario, porque `asistencias` referencia su fila.

### Respuesta si se sospecha que Node-RED está comprometido

1. Detener y quitar el contenedor: `docker compose stop nodered` y luego `docker compose rm -f nodered`.
2. Borrar el volumen de Node-RED: `docker volume rm pase-lista_nodered_data`. Si se conserva el volumen, borrar al menos `/data/node_modules`, `/data/lib` y `/data/.config*`. Este README no documenta cómo hacerlo; el borrado completo es el paso más seguro.
3. Restaurar `nodered/flows/flows.json` desde un commit revisado: `git checkout <commit-revisado> -- nodered/flows/flows.json`.
4. Cambiar `NODERED_ADMIN_PASSWORD` y `NODERED_DB_PASSWORD` en `.env`, con `openssl rand -hex 16`, y las cinco contraseñas MQTT con el comando de [Generar las contraseñas MQTT](#generar-las-contraseñas-mqtt).
5. Ejecutar `docker compose up -d` para recrear los contenedores con los valores nuevos.
6. Ejecutar `docker compose exec postgres sh /docker-entrypoint-initdb.d/02-roles.sh` para aplicar la contraseña nueva del rol de Node-RED.
7. Copiar la contraseña nueva a cada cliente MQTT: la XIAO, el servicio de visión y la pantalla (ver [Copiar las contraseñas a los clientes](#copiar-las-contraseñas-a-los-clientes)).

Si el acceso pudo llegar al host, revisar también `POSTGRES_PASSWORD` y la cuenta de Portainer.

## Next steps

- Para el diseño del sistema y los tópicos MQTT, ver [docs/HIGH-LEVEL-PLAN.md](../docs/HIGH-LEVEL-PLAN.md), sección 7.
- Para el esquema de la base de datos, ver `orangepi/sql/schema.sql`.
- Para el flujo de energía y la API, ver `nodered/flows/flows.json`.
