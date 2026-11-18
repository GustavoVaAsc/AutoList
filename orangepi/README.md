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
- [API de alumnos y galería](#api-de-alumnos-y-galería)
- [Sesiones y reportes CSV](#sesiones-y-reportes-csv)
- [Pase de lista](#pase-de-lista)
- [Operación](#operación)
- [Seguridad y riesgos aceptados](#seguridad-y-riesgos-aceptados)

## Qué levanta

El archivo `docker-compose.yml` define cuatro servicios. Node-RED se conecta a Mosquitto y a Postgres por el nombre del servicio dentro de la red del compose (`mosquitto`, `postgres`).

| Servicio | Puerto | Protocolo | Quién puede conectarse | Autenticación |
|---|---|---|---|---|
| Mosquitto | 1883 | MQTT | Cualquier equipo que llegue a la placa | Usuario y contraseña por rol (`MQTT_<USUARIO>_PASSWORD`) y ACL. Ver [Usuarios MQTT y ACL](#usuarios-mqtt-y-acl) |
| Node-RED | 1880 | HTTPS, certificado autofirmado | Cualquier equipo que llegue a la placa | Editor (`/`): `adminAuth`. API `/api/...`: HTTP Basic del profesor (`NODERED_PROFESOR_USER`), salvo `GET /api/galeria`, que solo acepta `X-Galeria-Token`. Frontend (`/app/`): público, sin secretos. Ver [API de alumnos y galería](#api-de-alumnos-y-galería) |
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

Generar cada contraseña (y `GALERIA_TOKEN`) con `openssl rand -hex 16`, que da 32 caracteres hexadecimales. Node-RED **no arranca** si `NODERED_PROFESOR_PASSWORD`, `GALERIA_TOKEN` o `NODERED_ADMIN_PASSWORD` contienen `cambiar_esta_clave` (cualquier valor que lo incluya, como `cambiar_esta_clave_larga`, el placeholder de Portainer), si dos de esos tres valores son iguales, si `GALERIA_TOKEN` tiene menos de 32 caracteres o `NODERED_PROFESOR_PASSWORD` menos de 16, o si cualquiera de estos dos tiene menos de 8 caracteres distintos (`"p"` repetido 16 veces no pasa). `openssl rand -hex 16` cumple todo. `NODERED_ADMIN_PASSWORD` solo está sujeta a lo del placeholder y a no repetirse: no tiene mínimos de longitud ni de variedad. El mensaje de error nombra la variable, nunca su valor. Las cinco de MQTT se generan con el comando de [Generar las contraseñas MQTT](#generar-las-contraseñas-mqtt), que no las imprime. El comando imprime 32 caracteres hexadecimales. La plantilla pide usar solo letras y números, y Portainer exige al menos 12 caracteres.

Después, sustituir cada valor `cambiar_esta_clave` en `.env`:

| Variable | Uso | Cuándo se aplica |
|---|---|---|
| `MQTT_XIAO_PASSWORD`, `MQTT_VISION_PASSWORD`, `MQTT_NODERED_PASSWORD`, `MQTT_PANTALLA_PASSWORD`, `MQTT_SIMULADOR_PASSWORD` | Contraseña de cada usuario de Mosquitto (los nombres de usuario son fijos). Node-RED usa la de `nodered`. Ver [Usuarios MQTT y ACL](#usuarios-mqtt-y-acl). | En cada arranque de Mosquitto. La de `nodered` también en el de Node-RED |
| `POSTGRES_USER`, `POSTGRES_PASSWORD`, `POSTGRES_DB` | Superusuario y base de datos de Postgres. Node-RED no usa el superusuario. | Solo al crear el volumen `pgdata` |
| `NODERED_DB_USER`, `NODERED_DB_PASSWORD` | Rol de Node-RED en Postgres, con permisos mínimos (`sql/roles.sh`) | Al crear `pgdata`. Node-RED lee su copia en cada arranque, así que debe coincidir con la contraseña real del rol. |
| `NODERED_ADMIN_USER`, `NODERED_ADMIN_PASSWORD` | Login del editor de Node-RED | En cada arranque de Node-RED |
| `NODERED_PROFESOR_USER`, `NODERED_PROFESOR_PASSWORD` | Único usuario de la API (HTTP Basic): es el que el navegador pide al abrir `/app/`. El usuario no puede llevar `:`. Sin ellas, Node-RED no arranca. | En cada arranque de Node-RED |
| `NODERED_TLS_SAN` | Nombres e IP del certificado HTTPS. Opcional; por omisión `DNS:localhost,IP:127.0.0.1`. | En cada arranque de Node-RED. Si cambia, el certificado se regenera. |
| `PORTAINER_ADMIN_PASSWORD` | Contraseña inicial del administrador de Portainer | Solo al crear `portainer_data` |
| `GALERIA_TOKEN` | Token de `GET /api/galeria` (encabezado `X-Galeria-Token`), lo único que acepta esa ruta. Node-RED lo recibe por el compose; el servicio de visión lo lee de su propio `/etc/pase-lista/vision.env`, y los dos valores deben coincidir. Sin él, Node-RED no arranca. | En cada arranque de Node-RED |

Si Compose no encuentra una de las variables obligatorias, falla con un mensaje que indica cuál falta.

> **Antes de ejecutar cualquier comando `docker compose` con este compose (incluidos `ps`, `logs` y `config -q`), añadir a `orangepi/.env` `NODERED_PROFESOR_USER`, `NODERED_PROFESOR_PASSWORD` y `GALERIA_TOKEN`** (esta última ya estaba en `.env.example`, pero ningún servicio la recibía). Compose las exige con `:?` y se niega a procesar el proyecto si falta alguna; el stack ya levantado sigue corriendo mientras tanto.

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
| `MQTT_*_PASSWORD`, `NODERED_ADMIN_*`, `NODERED_PROFESOR_*`, `GALERIA_TOKEN`, `NODERED_TLS_SAN` | Editar `.env` y ejecutar `docker compose up -d`. Compose recrea los contenedores con los valores nuevos. Al cambiar `GALERIA_TOKEN`, cambiarlo también en `/etc/pase-lista/vision.env` y reiniciar el servicio de visión. |
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

Iniciar sesión con `NODERED_ADMIN_USER` y `NODERED_ADMIN_PASSWORD`. Para `/api/...` (y, por tanto, para los datos del frontend en `/app/`) el navegador pide, con su diálogo nativo, `NODERED_PROFESOR_USER` y `NODERED_PROFESOR_PASSWORD`: son otras credenciales, que no abren el editor.

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
| Pantalla (`orangepi/pantalla`, ya habla MQTT) | La de `pantalla`, en `/etc/pase-lista/pantalla.clave` (root, modo 600), que la unidad entrega con `LoadCredential=mqtt_pantalla:...` (propuesta pendiente de Arnau, `orangepi/pantalla/README.md`); para correrla a mano, la variable `MQTT_PANTALLA_PASSWORD` en un subshell. No en `EnvironmentFile=`: quedaría en `/proc/<pid>/environ`. `LoadCredential` no queda en el entorno ni se hereda, pero la puede leer el mismo uid (`orangepi`); un aislamiento real exigiría un usuario de sistema propio para la unidad (opción propuesta a Arnau). El programa rechaza el placeholder `cambiar_esta_clave`. |

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

`GET /api/metricas/energia` (con HTTP Basic del profesor, ver [API de alumnos y galería](#api-de-alumnos-y-galería); los `curl` de abajo llevan `-u profesor` y piden la contraseña) devuelve las lecturas `ina219` de la más nueva a la más vieja (`nodered/flows/flows.json`, flujo `Energía`). Acepta estos parámetros:

| Parámetro | Por omisión | Reglas | Si no se cumplen |
|---|---|---|---|
| `limite` | 100 | Entero mayor o igual a 1. Valores mayores a 1000 se recortan a 1000. | 400 |
| `desde` | Sin filtro | Fecha ISO 8601 entre los años 1970 y 9999. Formatos válidos: `2026-11-04`, `2026-11-04T10:00`, `2026-11-04T10:00:00Z`, `2026-11-04T10:00:00-06:00`. | 400 |

En la URL, un `+` del desfase horario se lee como espacio. Usar `Z` o codificarlo como `%2B`.

Cada elemento de la respuesta tiene los campos `hora`, `v`, `i` y `p`. El formato de `hora` lo serializa el driver de Postgres, y el flujo no lo fija.

```bash
curl -k -u profesor "https://<ip>:1880/api/metricas/energia?limite=5&desde=2026-11-04T10:00:00Z"
```

La respuesta correcta es un arreglo JSON (valores ilustrativos):

```json
[
  {"hora": "2026-11-04T10:02:11.000Z", "v": 11.94, "i": 0.713, "p": 8.51}
]
```

Sin credenciales o con credenciales erróneas responde 401. Un error de validación responde 400 con `{"error": "..."}`. Un error de la base de datos responde 500 con `{"error": "No se pudo consultar la base de datos"}`.

`-k` acepta el certificado autofirmado sin verificarlo. Con el certificado instalado, se puede quitar.

### Otros orígenes en `metricas_energia`

Además de `ina219` (la XIAO o el simulador), la misma tabla guarda dos orígenes que escribe Node-RED:

| `origen` | Qué guarda | Frecuencia |
|---|---|---|
| `npu` | `cap`, `det`, `emb` y `total` de cada resultado válido de la visión (en ms, `null` si una etapa no corrió) | Hasta 2 por segundo |
| `sistema` | `temp_soc`, la temperatura de la placa | Cada 10 s |

Las filas `npu` salen de la pestaña `Pase de lista`, y las `sistema` de la pestaña `Sistema`. Sin purga: ver [Riesgos abiertos](#riesgos-abiertos).

## API de alumnos y galería

Los tres endpoints están en el flujo `Alumnos` de `nodered/flows/flows.json`. El formato de cada uno está en el [plan, sección 6.7](../docs/HIGH-LEVEL-PLAN.md#67-frontend-web-de-registro-web-vite--react--typescript); aquí, cómo operarlos y probarlos.

### Autenticación

`nodered/settings.js` instala un `httpNodeMiddleware` propio (`nodered/autenticacion-api.js`), no `httpNodeAuth`: `httpNodeAuth` es global y bloquearía al servicio de visión.

| Ruta | Qué exige | Si falta o falla |
|---|---|---|
| `/api/*` (todo menos `GET /api/galeria`) | HTTP Basic con `NODERED_PROFESOR_USER` y `NODERED_PROFESOR_PASSWORD` | 401 con `WWW-Authenticate: Basic` (el navegador abre su diálogo) |
| `GET /api/galeria` | Solo el encabezado `X-Galeria-Token` igual a `GALERIA_TOKEN`. Las credenciales del profesor **no** bastan | 401 con `WWW-Authenticate: X-Galeria-Token` (no abre el diálogo) |
| `/app/` (estáticos) | Nada: no llevan secretos | |
| Editor (`/`) | `adminAuth` (`NODERED_ADMIN_USER`) | |

Las comparaciones son en tiempo constante.

**Contra CSRF** (el navegador reenvía el Basic guardado aunque la petición la dispare otro sitio), en cualquier método que no sea `GET` ni `HEAD`, ya autenticado, el middleware responde **403** si llega `Sec-Fetch-Site` y no es `same-origin`, si llega `Origin` y no es `https://` + `Host`, o si falta la cabecera propia **`X-Pase-Lista: 1`**. Un sitio ajeno no puede añadir esa cabecera sin un preflight CORS, que esta API no concede. Por eso **todo `POST`, `PUT`, `PATCH` o `DELETE` debe mandar `X-Pase-Lista: 1`**, también los futuros: el frontend actual solo hace `GET` y no la necesita todavía, pero cada `fetch` que cambie datos debe incluirla. La visión solo hace `GET`. Esto descansa en una regla: **ningún `http in` con método `GET` o `HEAD` cambia datos ni publica en MQTT**, porque esos métodos no exigen `X-Pase-Lista`. `nodered/pruebas/get-solo-lectura.test.js` la comprueba sobre `flows.json` (también para los flujos futuros): desde cada `http in` GET sigue los cables y falla si llega a un nodo `postgresql` con `INSERT`, `UPDATE`, `DELETE`, `TRUNCATE` o `ALTER`, o sin consulta fija, o a un `mqtt out`. Además, la importación del CSV exige `Content-Type: text/csv` (otro tipo, 415).

Todas las peticiones de la API pasan por un tope de cuerpo de 256 KiB: 413 si `Content-Length` lo supera y 411 si la petición no trae `Content-Length` (por ejemplo, `Transfer-Encoding: chunked`). Desde la imagen `5.0.9-23`, esa comprobación corre antes de que los parsers del editor lean el cuerpo, y las peticiones a `/api/*` sin credenciales se rechazan antes de parsearlo (ver [nodered/AGENTS.md](../nodered/AGENTS.md#settings-e-imagen)). Las respuestas llevan `Cache-Control: no-store`.

**Falla cerrado.** Si falta `NODERED_PROFESOR_USER`, `NODERED_PROFESOR_PASSWORD` o `GALERIA_TOKEN` (o el usuario lleva `:`), o si los secretos son de ejemplo, repetidos o cortos (ver [Crear el archivo de credenciales](#crear-el-archivo-de-credenciales)), `settings.js` lanza una excepción y Node-RED no arranca: el contenedor se reinicia en bucle y `docker compose logs --tail=50 nodered` muestra "Faltan NODERED_PROFESOR_USER, ..." o "autenticacion-api: ... tiene el valor de ejemplo". No hay modo de servir la API sin autenticar. Compose tampoco procesa el proyecto si falta alguna de las tres en `.env`: añadirlas antes de usarlo.

### Probar con curl

Desde la placa, con `-k` por el certificado autofirmado. `-u profesor` pide la contraseña sin dejarla en el historial ni en la línea de comandos. Para el token, `-H @-` lo lee de la entrada estándar:

```bash
read -rs GALERIA_TOKEN   # pegar el token y Enter (no se muestra)

# Sin credenciales: 401
curl -sk -o /dev/null -w '%{http_code}\n' https://127.0.0.1:1880/api/alumnos
# Con las del profesor: 200 y un arreglo
curl -sk -u profesor https://127.0.0.1:1880/api/alumnos
curl -sk -u profesor "https://127.0.0.1:1880/api/alumnos?grupo=3CM1"

# Galería: sin token 401; con las credenciales del profesor, también 401
curl -sk -o /dev/null -w '%{http_code}\n' https://127.0.0.1:1880/api/galeria
curl -sk -o /dev/null -w '%{http_code}\n' -u profesor https://127.0.0.1:1880/api/galeria
# Con el token: 200 y {"embeddings":[...]}; no imprimir la salida, son embeddings
printf 'X-Galeria-Token: %s\n' "$GALERIA_TOKEN" | curl -sk -o /dev/null -w '%{http_code}\n' -H @- https://127.0.0.1:1880/api/galeria
# El token no sirve en las demás rutas: 401
printf 'X-Galeria-Token: %s\n' "$GALERIA_TOKEN" | curl -sk -o /dev/null -w '%{http_code}\n' -H @- https://127.0.0.1:1880/api/alumnos
```

### GET /api/alumnos

`GET /api/alumnos?grupo=` devuelve `[{"cuenta","nombre","grupo","rostro"}]`, con `rostro` igual a `pendiente` o `registrado`, ordenado por grupo y nombre. `grupo` es opcional: de 1 a 40 caracteres, sin caracteres de control; si no, 400 con `{"error": "..."}`. Nunca devuelve embeddings.

### GET /api/galeria

Solo para el servicio de visión. Devuelve `{"embeddings":[{"cuenta":"320068234","embedding":[...512 números...]}]}` con los alumnos que tienen consentimiento y embedding, o `{"embeddings":[]}`. Es el formato que lee `orangepi/vision/src/gallery_store.cpp`. Sus nodos no registran el mensaje ni el payload ni cuentas: ante un fallo de la base solo el código SQLSTATE.

### POST /api/alumnos/importar

Importa la lista maestra de un grupo. El cuerpo es el archivo CSV **crudo**, con `Content-Type: text/csv; charset=utf-8` (no multipart; otro tipo, 415) y la cabecera `X-Pase-Lista: 1` (sin ella, 403, ver [Autenticación](#autenticación)). Desde el navegador: `fetch('/api/alumnos/importar', {method: 'POST', headers: {'Content-Type': 'text/csv; charset=utf-8', 'X-Pase-Lista': '1'}, body: archivo})`, con el `File` de un `<input type="file" accept=".csv">`.

```
no_cuenta,nombre,grupo
320068234,"Pérez López, Ana",3CM1
320068235,Luis Ñandú,3CM1
```

Encabezado obligatorio (`no_cuenta,nombre,grupo`, separado por comas). Excel en español exporta con punto y coma y se rechaza: guardar como **CSV UTF-8 (delimitado por comas)**. Se aceptan BOM de UTF-8, saltos `\n` o `\r\n`, campos entre comillas (`""` es una comilla) y acentos. Máximo 500 filas y 256 KiB. Cuenta de 9 dígitos, nombre de hasta 120 caracteres y grupo de hasta 40, ninguno vacío y sin caracteres de control ni invisibles (ancho cero, marca de orden de bytes dentro del texto); una cuenta repetida dentro del archivo es un error.

Se valida el archivo completo antes de escribir. Respuestas:

| Código | Cuándo | Cuerpo |
|---|---|---|
| 200 | Importado | `{"insertadas","actualizadas","omitidas","total"}` |
| 400 | Alguna fila inválida, encabezado, UTF-8 o `existentes` inválidos | `{"error", "total_errores", "filas": [{"fila": 3, "error": "..."}]}` (hasta 100 filas; `fila` es la línea del archivo, con el encabezado en la 1). No se importó nada |
| 409 | Alguna cuenta ya existe y no se indicó `existentes` | `{"escrito": false, "error", "existentes": [{"cuenta", "actual": {"nombre","grupo"}, "csv": {"nombre","grupo"}, "cambia", "tiene_rostro"}], "opciones": ["actualizar","omitir"]}`. **No se importó nada** |
| 409 | Carrera: sin `existentes`, otra importación insertó una cuenta del archivo entre la comprobación y la escritura | `{"escrito": true, "error", "insertadas", "actualizadas", "omitidas", "total"}`, sin `existentes`. **Las filas nuevas ya se escribieron**; reenviar con `?existentes=` es seguro |
| 403 | Falta `X-Pase-Lista: 1` o la petición viene de otro origen | `{"error": ...}` |
| 413 | El cuerpo pesa más de 256 KiB | `{"error": ...}` |
| 415 | El `Content-Type` no es `text/csv` | `{"error": ...}` |

El profesor decide qué hacer con las existentes y reenvía el mismo archivo con `?existentes=actualizar` (cambia solo `nombre` y `grupo` de las existentes e inserta las nuevas) o `?existentes=omitir` (inserta solo las nuevas). Cualquier otro valor, 400. Nunca se toca `embedding`, `consentimiento` ni `consentimiento_fecha`, y nunca se borra a nadie. La escritura es una sola sentencia SQL, es decir, una sola transacción: o entra todo o nada. El `INSERT` usa `ON CONFLICT (no_cuenta) DO NOTHING`; el único caso en que no es atómico del todo es una carrera con otra importación simultánea de la misma cuenta, entre la comprobación de existentes y la escritura: entonces la cuenta ajena cuenta como omitida y, si no se indicó `existentes`, la respuesta es 409 con `escrito: true` y los conteos (las filas nuevas ya quedaron escritas; reenviar con `?existentes=` es seguro). El otro 409, el de las existentes, trae `escrito: false`. Con una sola cuenta de profesor es un riesgo aceptado.

```bash
curl -sk -u profesor -H 'X-Pase-Lista: 1' -H 'Content-Type: text/csv; charset=utf-8' --data-binary @lista.csv https://127.0.0.1:1880/api/alumnos/importar
curl -sk -u profesor -H 'X-Pase-Lista: 1' -H 'Content-Type: text/csv; charset=utf-8' --data-binary @lista.csv "https://127.0.0.1:1880/api/alumnos/importar?existentes=omitir"
# Sin X-Pase-Lista: 403. Con otro tipo (por ejemplo text/plain): 415.
```

`--data-binary` envía el archivo tal cual (`-d` le quitaría los saltos de línea). Para probar con datos de verdad hay que usar un grupo de prueba: la importación inserta filas reales en `alumnos` y no hay endpoint para borrarlas (el rol de Node-RED no tiene DELETE).

### Métricas, sistema y prueba de reconocimiento

Lecturas de solo lectura (GET). Sin credenciales, responden 401; con `-u profesor`, curl pide la contraseña.

```bash
# Latencia de la NPU: filas de metricas_energia con origen 'npu' (limite y desde son opcionales)
curl -k -u profesor "https://127.0.0.1:1880/api/metricas/latencia?limite=5"
# Estado del sistema: lo último de la visión, la XIAO (cámara y actuadores) y la placa
curl -k -u profesor "https://127.0.0.1:1880/api/sistema/estado"
# Resultado de la última prueba de reconocimiento (pendiente, solicitado y resultado)
curl -k -u profesor "https://127.0.0.1:1880/api/enrolamiento/prueba"
```

### Registro del rostro y retiro del consentimiento

Cuatro endpoints del flujo `Registro` de `nodered/flows/flows.json`. El contrato completo (códigos, cuerpos y cómo avanza la inscripción) está en el [plan, sección 6.7](../docs/HIGH-LEVEL-PLAN.md#67-frontend-web-de-registro-web-vite--react--typescript); aquí, cómo probarlos. Los que cambian datos llevan `X-Pase-Lista: 1` (sin ella, 403, ver [Autenticación](#autenticación)). **Para verificar la API usa solo cuentas del rango sintético** (las `99900xxxx` de la importación): el registro escribe el consentimiento y, si llega un embedding, el rostro. Sin el servicio de visión corriendo en la placa, un registro queda en `esperando_vision` y se cancela solo a los 5 minutos.

```bash
# Progreso del registro (solo lectura; sin ningún registro: "abierto": false y "estado": "sin_registro")
curl -sk -u profesor https://127.0.0.1:1880/api/enrolamiento/estado

# Iniciar: JSON con consentimiento true. 202; 400, 404 (cuenta inexistente), 409 (registro abierto o el alumno ya tiene rostro)
curl -sk -u profesor -H 'X-Pase-Lista: 1' -H 'Content-Type: application/json' \
  -d '{"cuenta":"999000001","consentimiento":true}' -w '\n%{http_code}\n' https://127.0.0.1:1880/api/enrolamiento
# Sin consentimiento true: 400. Con otro Content-Type (text/plain): 415.

# Cancelar: 200 {"cancelado":true,...}; sin registro abierto, 200 {"cancelado":false,...} (idempotente)
curl -sk -u profesor -H 'X-Pase-Lista: 1' -X POST -w '\n%{http_code}\n' https://127.0.0.1:1880/api/enrolamiento/cancelar

# Retirar el rostro y el consentimiento: 200 {"cuenta","rostro":"pendiente","retirado":true|false}; 400, 404, 409
curl -sk -u profesor -H 'X-Pase-Lista: 1' -X DELETE -w '\n%{http_code}\n' https://127.0.0.1:1880/api/alumnos/999000001/rostro
```

Para ver lo que Node-RED publica a la visión sin que ella corra, usa la primera opción de [Depurar el tráfico MQTT](#depurar-el-tráfico-mqtt) (un `mqtt in` en `aula/vision/cmd` y otro en `aula/vision/galeria/#`, cada uno con un debug): al iniciar aparece `{"accion":"enrolar","cuenta","nombre"}`; al cancelar, `{"accion":"cancelar","cuenta"}`; al retirar un rostro, `aula/vision/galeria/baja` con `{"cuenta"}`. La segunda opción detendría Node-RED y con él los flujos que se quieren probar. Ningún nodo de estas rutas registra el payload ni el mensaje de los embeddings: un fallo de la base deja en el log solo el código SQLSTATE.

Riesgos conocidos del registro, aceptados por ahora:

- **MQTT caído al iniciar un registro.** `POST /api/enrolamiento` escribe el consentimiento y abre la inscripción antes de publicar `enrolar`, y responde 202 aunque el broker no esté disponible. La orden no llega, la visión no hace nada y el registro vence a los 5 minutos (`error` con `vencido`, y el consentimiento se revierte). La vista lo muestra como "Esperando al servicio de visión".
- **La visión se queda en "enrolando" tras un despliegue.** Al arrancar o desplegar Node-RED, la inscripción abierta se borra (vive en memoria) y no se publica `cancelar`: si la visión seguía capturando, queda ocupada hasta que termine o falle por sí sola, y su embedding se rechaza. Node-RED ya no puede destrabarla. Se resuelve reiniciando el servicio de visión (`orangepi-vision`, del host), y eso se coordina con Gustavo. Por eso no se despliega Node-RED con un registro abierto.
- **Hora de reserva de la visión.** Si su reloj falla, la visión publica `ts` con la hora de reserva (1970) o vacío. El embedding de la cuenta abierta con un `ts` ilegible, vacío o de antes de 2000 cierra el registro como `embedding_invalido` y revierte el consentimiento; los mensajes de progreso con ese `ts` se descartan y el registro vence a los 5 minutos. Un `ts` posterior a la hora actual más 5 minutos se descarta siempre.
- Un embedding que llega con el registro ya cerrado (cancelación, error o vencimiento con el embedding en vuelo) se retira en cuanto se guarda: el rostro queda sin guardar y la visión recibe `galeria/baja`. El retiro solo actúa sobre el consentimiento bajo el que se guardó ese embedding (compara `consentimiento_fecha`). Con una fila afectada se publica `baja`; con cero (ya se había retirado, o la cuenta tiene otro consentimiento) no borra nada y se publica `alta`, para que la visión recargue la galería desde la base. Si el retiro falla (la base caída), el par cuenta/fecha queda en la lista `retiros_pendientes` y se reintenta cada 15 s hasta que salga. La lista vive en el almacén de contexto `persistente` (`localfilesystem`, `/data/context` del volumen `nodered_data`, con `contextStorage` en `nodered/settings.js`), así que sobrevive a un reinicio: tras reiniciar, el primer tick de 15 s lo reintenta. Conserva cuentas y fechas, nunca embeddings. Con una escritura al disco como máximo cada segundo, un corte de energía puede perder el último cambio; la limpieza de arranque no toca un rostro con consentimiento, así que ante la duda revisar con el superusuario las cuentas con rostro que no deberían tenerlo. Borrar el volumen `nodered_data` borra también esta lista.

## Sesiones y reportes CSV

La pestaña `Sesiones` de `nodered/flows/flows.json` lleva las sesiones de clase (plan, sección 5.3) y el reporte. Las órdenes entran **solo por MQTT**, por `aula/sesion/cmd`, que publica la pantalla (`iniciar` y `terminar`); no hay endpoints HTTP de sesiones. Un segundo `iniciar` a menos de 5 s del último aceptado se descarta (la pantalla ya pide doble toque). Node-RED escribe en `sesiones` y después publica `aula/sesion/estado` (retenido, QoS 1); en cada conexión al broker reconcilia ese estado con la base (contrato MQTT, sección 5, y v0.10). El rol de Node-RED puede hacer `INSERT` y `UPDATE (fin)` en `sesiones`: un volumen creado antes de este cambio necesita `02-roles.sh` (ver [Volúmenes existentes](#volúmenes-existentes)).

### Reportes CSV

Al cerrarse una sesión (`terminar`, o `iniciar` con otra olvidada abierta), Node-RED ejecuta la consulta del plan, sección 7.3, escribe las filas en `/data/reportes/.entrada-<id>.json` y un nodo `exec` ejecuta `/usr/bin/python3 -I -B /etc/nodered/reporte_csv.py <id>`. El programa (`nodered/reporte_csv.py`, solo biblioteca estándar) escribe `/data/reportes/sesion-<id>.csv` y borra la entrada. `/data` es el volumen `nodered_data`: los reportes sobreviven a un redespliegue y no hay purga (decisión de retención de `orangepi/AGENTS.md`). El archivo es UTF-8 con BOM, separado por comas, con las columnas `no_cuenta,nombre,estado,hora`, y su permiso es `0600`. **Lleva nombres y números de cuenta: no lo pegues en un chat ni en un documento, y no lo subas al repo.**

Para ver qué hay y cuántas líneas tiene cada reporte (sin imprimir su contenido; `ls -la` muestra también los ocultos, y no debe haber ningún `.entrada-*`), desde `orangepi/`:

```bash
docker compose exec -T nodered sh -c 'ls -la /data/reportes'
docker compose exec -T nodered sh -c 'wc -l /data/reportes/*.csv'
```

Para sacarlo del volumen a una carpeta de la placa (guárdalo fuera del repo):

```bash
docker compose cp nodered:/data/reportes/sesion-<id>.csv ~/reportes/
```

Los reportes pasan por una cola de a lo más 1 cada 2 s (arranca a lo más un `python3` cada 2 s; si uno tarda más, se solapan hasta el límite de 30 s del `exec`), así que el CSV puede tardar unos segundos en aparecer.

**Un reporte fallido no se reintenta.** Si un reporte no aparece, `docker compose logs --tail=50 nodered` trae `Reporte de la sesión <id> falló: código N` (el programa no imprime datos de alumnos; los códigos son 1 entrada inválida o error al escribir, y 2 argumento incorrecto). El programa **borra siempre** `/data/reportes/.entrada-<id>.json`, también cuando falla, porque lleva nombres y números de cuenta: no queda nada que reintentar. Para regenerarlo a mano, con el superusuario se reconstruye la entrada con la consulta del plan (sección 7.3) y se vuelve a ejecutar el programa (sustituye `<id>` por el id de la sesión, en los tres lugares; se probó el 5 nov con una sesión de prueba y dio un CSV idéntico al original):

```bash
docker compose exec -T postgres sh -c 'psql -U "$POSTGRES_USER" -d "$POSTGRES_DB" -tA -v ON_ERROR_STOP=1 -v id=<id> -f -' <<'SQL' | docker compose exec -T nodered sh -c 'cat > /data/reportes/.entrada-<id>.json'
SELECT json_build_object('sesion', :'id'::int, 'filas', COALESCE(json_agg(r ORDER BY r.no_cuenta), '[]'::json))
FROM (
  SELECT a.no_cuenta, a.nombre, COALESCE(s.estado, 'falta') AS estado,
         to_char(s.hora AT TIME ZONE 'America/Mexico_City', 'YYYY-MM-DD HH24:MI:SS') AS hora
  FROM alumnos a
  LEFT JOIN asistencias s ON s.no_cuenta = a.no_cuenta AND s.sesion_id = :'id'::int
  WHERE a.grupo = (SELECT grupo FROM sesiones WHERE id = :'id'::int)
) r;
SQL
docker compose exec -T nodered /usr/bin/python3 -I -B /etc/nodered/reporte_csv.py <id>
```

**Retención manual.** No hay purga de reportes ni de la tabla `sesiones` (decisión de retención de [orangepi/AGENTS.md](AGENTS.md)): cada sesión suma un CSV de unos pocos KB y una fila, y a la larga la microSD se llenaría. Para revisar y quitar los CSV de más de 30 días y cualquier `.entrada-*` que haya quedado (no debería), desde `orangepi/` y después de sacar copia de lo que quieras conservar:

```bash
docker compose exec -T nodered sh -c 'find /data/reportes \( -name "sesion-*.csv" -mtime +30 -o -name ".entrada-*" \) -print'
docker compose exec -T nodered sh -c 'find /data/reportes \( -name "sesion-*.csv" -mtime +30 -o -name ".entrada-*" \) -delete'
```

Las filas viejas de `sesiones` solo las borra un superusuario con aprobación humana ([Prohibido sin aprobación humana](AGENTS.md#prohibido-sin-aprobación-humana)), y solo si ninguna `asistencias` las referencia.

**Pendiente:** un endpoint de descarga (sin fecha; por ahora el reporte se saca del volumen a mano).

### Reiniciar la pantalla

El nodo `inject` "Reiniciar pantalla" de la pestaña `Sesiones` publica `{"reiniciar":1}` en `aula/pantalla/reinicio` (QoS 1, sin retain) cuando se pulsa su botón en el editor de Node-RED. La pantalla sale con código 0 y systemd la levanta (`orangepi-pantalla.service`, `Restart=always`). No hay API para esto.

## Pase de lista

La pestaña `Pase de lista` de `nodered/flows/flows.json` consume `aula/vision/resultado` (QoS 1; lo publica el servicio de visión) y aplica las reglas de la sesión (plan, sección 5.1; contrato MQTT, sección 4, tabla de retroalimentación). Descarta, con un aviso en el log que no repite el contenido, el retain, el JSON inválido, los campos inválidos y todo resultado cuyo `ts` esté a más de 60 s del reloj de Node-RED (hacia atrás o hacia adelante; así cae también el `1970` de reserva de la visión). **Solo `modo: "pase"` registra asistencia**; `modo: "prueba"` se ignora por ahora.

- **Dónde quedan las asistencias.** En la tabla `asistencias` de Postgres, con `metodo = 'rostro'` y `confianza` igual a la `conf` del resultado. Una sola sentencia toma la sesión abierta de la base, comprueba que la cuenta sea del grupo de esa sesión y que tenga consentimiento y rostro vigentes en la base (`consentimiento` y `embedding IS NOT NULL`; solo comprueba que haya rostro, no lo lee) y hace el `INSERT ... ON CONFLICT (no_cuenta, sesion_id) DO NOTHING`. Los rechazos (desconocido, imagen insuficiente, duplicado y reconocido de otro grupo o sin rostro vigente, este último como `desconocido`) quedan en `intentos`. Sin sesión abierta no se escribe nada y no hay retroalimentación. El rol de Node-RED solo tiene `SELECT` e `INSERT` sobre ambas tablas (`sql/roles.sh`), así que corregir o borrar una asistencia es cosa del superusuario. Para consultarlas sin nombres: `SELECT estado, count(*) FROM asistencias WHERE sesion_id = <id> GROUP BY estado`.
- **El retardo.** Se calcula en SQL con el reloj de la base: `presente` si `now()` ≤ `inicio` + `tol_min` minutos de la sesión, `retardo` si no. No depende del `ts` de la visión.
- **Retroalimentación** en `aula/actuador/cmd` (QoS 1, sin retain), después de escribir: verde con servo y un pitido (`{"servo":"abrir","led":"verde","buzzer":1}`) si se registró (presente o retardo); rojo con doble pitido (`{"led":"rojo","buzzer":2}`) si es desconocido, ya estaba registrado o no es del grupo (o su rostro ya se retiró, aunque la visión aún no procese la `baja`); ámbar (`{"led":"ambar"}`) si es `dudoso` o `imagen_insuficiente`. `sin_rostro` y `error` no mandan nada. Si la base falla no se publica nada y el log trae solo el SQLSTATE. Mientras el firmware de la XIAO solo controle la tira, ignora `servo`, `led` y `buzzer` (contrato, sección 3).
- **Duplicados y concurrencia.** La `UNIQUE (no_cuenta, sesion_id)` garantiza una sola fila; el segundo de dos resultados seguidos del mismo alumno recibe rojo por duplicado y deja un intento `duplicado`.
- **Límite de entrada.** Un nodo `delay` deja pasar a lo más 1 resultado cada 200 ms (5 por segundo) hacia la base y descarta los que lleguen antes (no encola; la visión tarda unos 240 ms por ciclo, así que un reconocido legítimo justo después de un dudoso no se pierde), así que `intentos` crece a lo más unas 430 000 filas al día (unos 100 bytes por fila con su índice: unos 43 MB al día, 1,3 GB al mes) aunque alguien publique sin parar con la clave de `vision` o de `nodered` (plan, sección 13). Un resultado legítimo es uno por disparo del PIR.
- **Retención de `intentos` (manual).** Nada la purga (decisión de retención de `orangepi/AGENTS.md`) y el rol de Node-RED no tiene `DELETE`. Para ver cuánto ocupa: `SELECT count(*), min(hora), max(hora) FROM intentos` como superusuario. Quitar filas es un `DELETE` de superusuario, por ejemplo `DELETE FROM intentos WHERE hora < now() - interval '90 days'`, y **requiere aprobación humana** ([Prohibido sin aprobación humana](AGENTS.md#prohibido-sin-aprobación-humana)): `intentos` no guarda cuentas ni embeddings, pero es evidencia de auditoría. Las asistencias no se purgan.
- **Pendiente: la lista y el "último registro" de la pantalla.** Necesitan un tópico nuevo (su nombre y su payload se deciden con el contrato) y un cambio de la ACL de `pantalla` (hoy solo lee `aula/sesion/estado`, `aula/vision/enrolamiento`, `aula/actuador/estado` y `aula/pantalla/reinicio`): un cambio de contrato, ACL, entrypoint, compose y `.env.example` a la vez. Hasta entonces la pantalla muestra "sin datos" en esas vistas. Tampoco están el marcado manual (`marcar`, que sigue como propuesta; la zona gris queda ámbar sin registrar) ni el reenvío de `modo: "prueba"` al frontend.
- **Dónde observar la retroalimentación.** Solo `xiao` y `nodered` leen `aula/actuador/cmd` (ACL): `pantalla` y `vision` no pueden. En la verificación se suscribe como `xiao` con la XIAO apagada (con su usuario conectado, el broker expulsaría a la XIAO real) o se confía en el log de Node-RED (`Pase de lista: <resultado>`) y en la fila de `intentos`.

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

Usar `--build` después de cambiar `nodered/settings.js`, `nodered/Dockerfile`, `nodered/package.json`, `nodered/package-lock.json`, `nodered/reporte_csv.py` o cualquier archivo de `web/`. Estos archivos se copian a la imagen (los de `web/` se compilan en ella), así que sin reconstruirla el cambio no tiene efecto.

La etiqueta de la imagen está fijada en `docker-compose.yml` (`image: pase-lista/nodered:5.0.9-22`). Al reconstruir por un cambio, subir el sufijo (`-21`, `-22`...), como indica el comentario del archivo.

Para aplicar solo un cambio de Node-RED o del frontend, sin tocar los demás servicios, desde `orangepi/`:

```bash
docker compose up -d --no-deps --build nodered
```

`--no-deps` evita recrear Mosquitto y Postgres. Node-RED tarda hasta 60 s en estar `healthy`.

### Frontend web

El frontend (`web/`, Vite + React + TypeScript) se compila al construir la imagen de Node-RED y se sirve en `https://<ip>:1880/app/`. El editor sigue en `/`. Los archivos compilados quedan en `/usr/share/nodered-web`, un directorio de root que el contenedor no puede modificar. No habla MQTT: no hay listener WebSocket ni usuario MQTT para el frontend, y todo pasa por la API HTTP de Node-RED. El progreso en vivo del registro es un sondeo de `GET /api/enrolamiento/estado` (plan, sección 6.7), no MQTT. Para desarrollarlo desde una laptop, ver [web/README.md](../web/README.md).

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
| Sin límite de intentos ni registro de los 401 | La API no limita los intentos de contraseña ni deja registro de las peticiones rechazadas (401 o 403). Con una sola cuenta de profesor, la defensa es una contraseña larga (mínimo 16 caracteres, forzado al arrancar; `openssl rand -hex 16`). Pendiente (no es de esta ronda): limitar intentos y registrar los 401 sin escribir credenciales. |
| Certificado autofirmado: riesgo de MITM | HTTP Basic y `X-Galeria-Token` viajan por HTTPS con certificado autofirmado: protegen contra quien solo escucha, pero quien se interponga y presente otro certificado se llevaría la contraseña si el usuario acepta el aviso del navegador. Recomendación: instalar `nodered.crt` como certificado de confianza en la laptop del profesor (ver [Node-RED](#node-red)) y no aceptar avisos de certificado; el servicio de visión debe validarlo con `http.ca_file`. |
| Inyección de fórmulas en el reporte CSV | La lista maestra se importa tal cual: un nombre que empiece con `=`, `+`, `-` o `@` queda guardado así. El peligro aparece al abrir en Excel un CSV exportado. El reporte CSV lo neutraliza al exportar (`nodered/reporte_csv.py`): antepone `'` a un valor, o a un segmento tras `;` o `,`, que empiece con `=`, `+`, `-`, `@`, tabulador, retorno de carro, sus formas de ancho completo o el signo menos U+2212. No se implementa en la importación. |
| MQTT sin TLS | El listener 1883 va en texto plano: usuario y contraseña viajan sin cifrar. |
| Una contraseña MQTT filtrada | La de `xiao` es la más expuesta: viaja en claro por la LAN. Con la contraseña de un rol, alguien conectado a la LAN puede conectarse con ese usuario y, por `use_username_as_clientid`, expulsar al cliente legítimo; al caer su conexión se dispara su Will (por ejemplo `{"en_linea": false}` en `aula/camara/estado`, retenido), y el atacante puede dejar retenido un estado falso en los tópicos que ese rol puede escribir. La ACL limita qué tópicos toca cada rol, pero no evita esto. **Con la clave de `vision` o de `nodered` se puede publicar un `reconocido` falso:** marca la asistencia de una cuenta del grupo de la sesión abierta (acotado a cuentas con consentimiento y rostro vigentes) y abre el servo. Es un riesgo aceptado, porque la decisión de reconocer es de la visión y Node-RED no puede comprobarla. Cómo detectarlo: una ráfaga de asistencias en pocos segundos en la columna `hora` del reporte CSV (o de `asistencias`). Mitigaciones pendientes, la primera es la prioritaria: restringir el puerto 1883 en la cadena `DOCKER-USER` del host, porque solo la XIAO lo necesita desde la LAN (cambio del host: lo coordina Yordi con el equipo, y aún no se hizo); descartar en Node-RED los estados vencidos por `ts` (flujos del 11 nov, Yordi); TLS en MQTT (sin decidir, ver Pendiente). |
| Mensajes retenidos falsos | La ACL de Mosquitto 2 (`acl_file`) no puede impedir el retain por tópico. La defensa es que los consumidores de todo tópico con Retain = no ignoren lo retenido y lo vacío, en cualquier tópico (contrato MQTT, sección 1: la XIAO, el servicio de visión, incluidos `aula/camara/pir` y `aula/vision/cmd`, y Node-RED). Node-RED borra los retenidos de `*/cmd`, `aula/camara/pir`, `aula/camara/energia` y `aula/vision/resultado` en cada conexión al broker (flujo "Limpieza de retenidos"), lo que provoca un mensaje vacío en vivo en cada uno de esos tópicos, y su ingesta descarta `aula/camara/energia` con retain (con aviso) y vacío (en silencio). `aula/vision/resultado`, `aula/vision/embedding` y `aula/vision/enrolamiento` ya se descartan con retain en su flujo; para los demás tópicos con Retain = no (`aula/vision/galeria/alta`, `aula/vision/galeria/baja` y `aula/pantalla/reinicio`), el flujo que los consuma debe descartar retain, o se amplía la lista de la limpieza. Un plugin de autorización de broker que sí lo impidiera queda fuera: es una decisión humana, y no se instala sin ella. |
| Node-RED puede ejecutar código | El nodo `exec` del reporte CSV ejecuta comandos en el contenedor. Por eso el editor exige `adminAuth` (`nodered/settings.js`, línea 13); `settings.js` y `reporte_csv.py` no son escribibles desde el contenedor (`nodered/Dockerfile`); el comando es fijo, sin shell, y solo recibe el id entero de la sesión; los reportes pasan por una cola de 1 cada 2 s, un `iniciar` a menos de 5 s del anterior se descarta y el servicio tiene `pids_limit: 256` (`docker-compose.yml`). |
| La microSD se llena: sin purga de reportes ni de sesiones | Cada sesión deja un CSV en `/data/reportes` y una fila en `sesiones`, y nada los borra (decisión de retención: no hay purga). Limpieza manual en [Reportes CSV](#reportes-csv). |
| El panel de la pantalla no tiene autorización local | Quien esté frente a la pantalla táctil (ILI9341) puede abrir la puerta, probar la luz e iniciar o terminar la sesión: el panel no pide PIN ni pulsación larga, y el usuario MQTT `pantalla` puede publicar `aula/sesion/cmd` y `aula/actuador/cmd`. Mitigaciones actuales: iniciar y terminar piden un segundo toque, y "Abrir puerta" y encender la luz de prueba tienen un enfriamiento de 2 s (apagarla nunca se bloquea). Un PIN o una pulsación larga quedan para que el equipo lo decida. |
| La Guía de registro se puede mantener abierta con mensajes repetidos | Un `aula/vision/enrolamiento` con `estado` `esperando_pose` y `paso` ≤ 1 se toma como el inicio de un registro nuevo: reabre la Guía aunque el profesor la haya cerrado con "Salir", reemplaza una Guía de otra cuenta y reinicia el techo `GUIA_MAX_S`. Así, quien tenga las credenciales de `vision` o de `nodered` (los dos pueden escribir ese tópico según la ACL) puede mantener la pantalla en la Guía, sin los botones de puerta, luz y sesión, publicando ese mensaje cada pocos segundos. También la reabre un reintento de la primera pose tras una `rechazada`. Se acepta porque los dos roles son de confianza y la regla evita que un mensaje final perdido (QoS 0) oculte el registro siguiente. Para cerrarlo a bajo costo: tratarlo como inicio solo si la Guía actual lleva más de unos 30 s sin progreso, o una sola vez por cuenta dentro de `GUIA_VENCE_S`. |
| La luz de prueba puede apagar una luz que la visión encendió | "Probar luz LED" publica `luz:1` y, 3 s después, `luz:0` de forma automática. Si en ese lapso el servicio de visión encendió la tira para una captura, el `luz:0` de la pantalla la apaga. El contrato no define prioridad entre quienes publican `luz` (pantalla, Node-RED y visión). El firmware de la XIAO prevé apagar la tira sola a los 360 s del último `luz:1`, lo que acota el daño si un `luz:0` se pierde, pero no resuelve la prioridad. |
| Portainer controla el host | Portainer monta el socket de Docker (`/var/run/docker.sock`). Por eso su puerto solo escucha en la placa. |

### Riesgos abiertos

Problemas conocidos que no tienen decisión tomada. A diferencia de la tabla anterior, no son riesgos aceptados.

| Riesgo | Detalle |
|---|---|
| Residual del gate de `/api` (a partir de la imagen `5.0.9-23`) | El error de stderr de Express por JSON mal formado ya no sale sin enmascarar: el gate de `/api` rechaza antes de parsear el cuerpo (401, 403, 411 o 413), y `instalarEnmascaradoDeConsola()` (`nodered/registro-log.js`) enmascara lo que sale por `console.error` y `console.warn`. Quedan, como elemento menor: las rutas que no son `/api` (el editor, `/auth/token`) leen hasta 1 MB antes de `adminAuth`, inherente a Node-RED; el gate supone que `httpAdminRoot` y `httpNodeRoot` siguen en `/` (una prueba lo comprueba); y las excepciones no capturadas y los errores de arranque de `settings.js` los imprime Node sin enmascarar. Ver [nodered/AGENTS.md](../nodered/AGENTS.md#settings-e-imagen). |
| `metricas_energia` crece sin purga | No hay purga (decisión de retención). Origen `npu`: hasta 2 filas por segundo (unas 172 800 al día en el peor caso). Origen `sistema`: una fila cada 10 s (unas 8 640 al día). Origen `ina219`: sin límite de ritmo en la ingesta. Sin decidir: purga, límite o ambos. |
| Una prueba de reconocimiento durante una sesión abierta | Permitida por decisión del equipo. Un check-in real que coincida con una prueba puede necesitar repetirse. |
| El último resultado de la prueba vive en memoria | `GET /api/enrolamiento/prueba` lo devuelve hasta la siguiente prueba o el reinicio de Node-RED. Cuenta y nombre solo salen con `reconocido` o `dudoso`. |

### Pendiente

No se sirve `/api/galeria` sin la autenticación de la API y `GALERIA_TOKEN`: Node-RED no arranca si faltan sus variables. No se registra a ninguna persona real sin esa autenticación verificada en la placa y sin las ACL de Mosquitto (4 nov); antes de eso, solo identidades de prueba (LFW y miembros del equipo). Las ACL de Mosquitto ya están activas (contrato MQTT v0.5); la autenticación de la API está desplegada y verificada en la placa (2 nov).

#### Para el 11 de noviembre

| Pendiente | Estado en el repo |
|---|---|
| Sesiones y reporte CSV | Hecho (pestaña `Sesiones`, `nodered/reporte_csv.py`) y verificado en la placa el 5 nov con `5.0.9-19` (`orangepi/AGENTS.md`, pasos 27 a 33, 35 y 36; `02-roles.sh` aplicado). Falta el paso 34: pulsar "Reiniciar pantalla" en el editor. |
| Pase de lista en Node-RED | Hecho (pestaña `Pase de lista`, imagen `5.0.9-21`); verificado de punta a punta en la placa el 6 nov con la NPU (reconocido con fotos fijas y con cámara dinámica USB en vivo, retardo y presente registrados en Postgres, documentado en `docs/RESULTADOS-PRUEBAS.md`). La lista y el "último registro" de la pantalla ya se sincronizan en vivo mediante `aula/sesion/asistencia` (contrato v0.13). El marcado manual (`marcar`) y el reenvío de `modo: "prueba"` al frontend siguen pendientes. |
| `GALERIA_TOKEN` | Node-RED lo recibe por el compose. `GET /api/galeria` solo acepta el encabezado `X-Galeria-Token`; configurado y validado en la placa entre Node-RED y el servicio de visión en `/etc/pase-lista/vision.env`. |

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
4. Cambiar `NODERED_ADMIN_PASSWORD`, `NODERED_PROFESOR_PASSWORD`, `GALERIA_TOKEN` (y copiarlo a `/etc/pase-lista/vision.env`) y `NODERED_DB_PASSWORD` en `.env`, con `openssl rand -hex 16`, y las cinco contraseñas MQTT con el comando de [Generar las contraseñas MQTT](#generar-las-contraseñas-mqtt).
5. Ejecutar `docker compose up -d` para recrear los contenedores con los valores nuevos.
6. Ejecutar `docker compose exec postgres sh /docker-entrypoint-initdb.d/02-roles.sh` para aplicar la contraseña nueva del rol de Node-RED.
7. Copiar la contraseña nueva a cada cliente MQTT: la XIAO, el servicio de visión y la pantalla (ver [Copiar las contraseñas a los clientes](#copiar-las-contraseñas-a-los-clientes)).

Si el acceso pudo llegar al host, revisar también `POSTGRES_PASSWORD` y la cuenta de Portainer.

## Próximos pasos

- Para el diseño del sistema y los tópicos MQTT, ver [docs/HIGH-LEVEL-PLAN.md](../docs/HIGH-LEVEL-PLAN.md), sección 7.
- Para el esquema de la base de datos, ver `orangepi/sql/schema.sql`.
- Para el flujo de energía y la API, ver `nodered/flows/flows.json`.
