# Stack de la Orange Pi

Este directorio levanta con Docker Compose el stack del edge de la Orange Pi Zero 3W: broker MQTT, Node-RED, Postgres con pgvector y Portainer. El diseño general está en [docs/HIGH-LEVEL-PLAN.md](../docs/HIGH-LEVEL-PLAN.md).

Todos los comandos de esta guía se ejecutan desde `orangepi/`, salvo que se indique otra carpeta. El proyecto se llama `pase-lista` (`name:` en `docker-compose.yml`), así que contenedores y volúmenes llevan ese prefijo.

## Contenido

- [Qué levanta](#qué-levanta)
- [Requisitos](#requisitos)
- [Primer arranque](#primer-arranque)
- [Acceso](#acceso)
- [Energía](#energía)
- [Operación](#operación)
- [Seguridad y riesgos aceptados](#seguridad-y-riesgos-aceptados)

## Qué levanta

El archivo `docker-compose.yml` define cuatro servicios. Node-RED se conecta a Mosquitto y a Postgres por el nombre del servicio dentro de la red del compose (`mosquitto`, `postgres`).

| Servicio | Puerto | Protocolo | Quién puede conectarse | Autenticación |
|---|---|---|---|---|
| Mosquitto | 1883 | MQTT | Cualquier equipo que llegue a la placa | Usuario y contraseña (`MQTT_USER`, `MQTT_PASSWORD`) |
| Mosquitto | 9001 | MQTT sobre WebSocket | Cualquier equipo que llegue a la placa | Usuario y contraseña |
| Node-RED | 1880 | HTTPS, certificado autofirmado | Cualquier equipo que llegue a la placa | Editor: `adminAuth`. API `/api/...`: sin autenticación hoy |
| Postgres | 5432 | SQL | Solo la placa (`127.0.0.1`) | `POSTGRES_USER` es superusuario; Node-RED usa `NODERED_DB_USER` |
| Portainer CE | 9443 | HTTPS, certificado autofirmado | Solo la placa; desde otro equipo, por túnel SSH | Contraseña de administrador (`PORTAINER_ADMIN_PASSWORD`) |

Los puertos 1883, 9001 y 1880 se publican sin `127.0.0.1:` (`docker-compose.yml`, servicios `mosquitto` y `nodered`), así que cualquier equipo de la red que llegue a la placa los alcanza. Postgres y Portainer solo escuchan en el loopback de la placa.

Mosquitto no acepta conexiones anónimas en ningún listener (`mosquitto/mosquitto.conf`, `allow_anonymous false`).

## Requisitos

| Requisito | Detalle |
|---|---|
| Docker con Compose | Docker Compose 2.23.1 o posterior. Los secretos con origen `environment` lo exigen (`docker-compose.yml`, comentario del servicio `portainer`). |
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

Generar cada contraseña con `openssl rand -hex 16`. El comando imprime 32 caracteres hexadecimales. La plantilla pide usar solo letras y números, y Portainer exige al menos 12 caracteres.

Después, sustituir cada valor `cambiar_esta_clave` en `.env`:

| Variable | Uso | Cuándo se aplica |
|---|---|---|
| `MQTT_USER`, `MQTT_PASSWORD` | Usuario y contraseña de Mosquitto. Node-RED también los usa para conectarse. | En cada arranque de Mosquitto y de Node-RED |
| `POSTGRES_USER`, `POSTGRES_PASSWORD`, `POSTGRES_DB` | Superusuario y base de datos de Postgres. Node-RED no usa el superusuario. | Solo al crear el volumen `pgdata` |
| `NODERED_DB_USER`, `NODERED_DB_PASSWORD` | Rol de Node-RED en Postgres, con permisos mínimos (`sql/roles.sh`) | Al crear `pgdata`. Node-RED lee su copia en cada arranque, así que debe coincidir con la contraseña real del rol. |
| `NODERED_ADMIN_USER`, `NODERED_ADMIN_PASSWORD` | Login del editor de Node-RED | En cada arranque de Node-RED |
| `NODERED_TLS_SAN` | Nombres e IP del certificado HTTPS. Opcional; por omisión `DNS:localhost,IP:127.0.0.1`. | En cada arranque de Node-RED. Si cambia, el certificado se regenera. |
| `PORTAINER_ADMIN_PASSWORD` | Contraseña inicial del administrador de Portainer | Solo al crear `portainer_data` |
| `GALERIA_TOKEN` | Token de `GET /api/galeria`. Se usará desde el 11 nov; ningún contenedor lo recibe todavía. | Aún no se aplica |

Si Compose no encuentra una de las variables obligatorias, falla con un mensaje que indica cuál falta.

### NODERED_TLS_SAN

El certificado de Node-RED solo es válido para los nombres e IP que aparecen en `NODERED_TLS_SAN`. Para entrar por la IP de la placa, incluirla. Ejemplo con la IP `192.168.100.185`, que se debe sustituir por la de la placa:

```bash
NODERED_TLS_SAN=DNS:localhost,IP:127.0.0.1,IP:192.168.100.185,DNS:orangepi.local
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
| `MQTT_*`, `NODERED_ADMIN_*`, `NODERED_TLS_SAN` | Editar `.env` y ejecutar `docker compose up -d`. Compose recrea los contenedores con los valores nuevos. |
| `NODERED_DB_PASSWORD` | Editar `.env`, ejecutar `docker compose up -d` y luego `docker compose exec postgres sh /docker-entrypoint-initdb.d/02-roles.sh`. El script es repetible: crea el rol si falta y actualiza su contraseña. |
| `POSTGRES_PASSWORD` | Abrir psql con `docker compose exec postgres sh -c 'psql -U "$POSTGRES_USER" -d "$POSTGRES_DB"'`, ejecutar `\password` (pide la clave nueva sin mostrarla) y después igualar `.env`. |
| `PORTAINER_ADMIN_PASSWORD` | Cambiar la contraseña desde Portainer. La variable de `.env` ya no tiene efecto. |

La alternativa para cualquier valor de Postgres o Portainer es `docker compose down -v`, que borra todos los datos. Solo sirve en un volumen que se puede perder.

## Acceso

### Node-RED

El editor y la API responden en `https://<ip>:1880`. El navegador avisa del certificado autofirmado: aceptar la excepción, o instalar el certificado como de confianza.

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

`herramientas/simular_energia.sh` publica lecturas falsas por el mismo tópico y por el mismo flujo de ingesta. Necesita el stack levantado. Se ejecuta dentro del contenedor de Mosquitto, así que la contraseña no pasa por la línea de comandos del host.

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
| Reconstruir la imagen de Node-RED | `docker compose up -d --build` |
| Detener y quitar los contenedores (conserva los volúmenes) | `docker compose down` |
| Ver el estado de los servicios | `docker compose ps` |
| Seguir el log de un servicio | `docker compose logs -f nodered` |

Los nombres de servicio son `mosquitto`, `postgres`, `nodered` y `portainer`.

### Reconstruir Node-RED

Usar `--build` después de cambiar `nodered/settings.js`, `nodered/Dockerfile`, `nodered/package.json` o `nodered/package-lock.json`. Estos archivos se copian a la imagen, así que sin reconstruirla el cambio no tiene efecto.

La etiqueta de la imagen está fijada en `docker-compose.yml` (`image: pase-lista/nodered:5.0.9-3`). Al reconstruir por un cambio, subir el sufijo (`-4`, `-5`...), como indica el comentario del archivo.

### Desplegar flujos

Cada Deploy del editor de Node-RED escribe `nodered/flows/flows.json`. Antes de hacer commit, revisar el cambio:

```bash
git diff -- nodered/flows/flows.json
```

Revisar el diff también busca cambios que no se hicieron en el editor. Los respaldos y temporales que Node-RED crea junto a `flows.json` están en `.gitignore`. Las credenciales no van en el archivo: Node-RED las guarda en el volumen `/data`.

### Logs

Los logs de cada contenedor rotan: se guardan como máximo 3 archivos de 1 MB.

Mosquitto registra las conexiones y los rechazos de login (`not authorised`, con el cliente y su IP). Para revisar intentos fallidos: `docker compose logs mosquitto | grep "not authorised"`. Con la rotación, el historial alcanza unos días; si se necesita conservar evidencia, copiarlo antes de que rote.

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

Las tablas `alumnos`, `sesiones`, `asistencias` e `intentos` también vienen de `schema.sql`, así que un volumen creado antes de agregarlas no las tiene. En ese caso `02-roles.sh` falla a mitad: el rol y su contraseña ya se aplicaron, pero los permisos sobre las tablas que no existen no. Recrear el volumen con `down -v` (si solo hay datos de prueba) o consultar al equipo antes de crear las tablas a mano.

## Seguridad y riesgos aceptados

### Riesgos aceptados hoy

| Riesgo | Detalle |
|---|---|
| La API no tiene autenticación | `/api/metricas/energia` responde a cualquiera que llegue a la placa. Solo sirve lecturas de energía. |
| MQTT sin TLS | Los listeners 1883 y 9001 van en texto plano. |
| Un usuario MQTT para todo | Todos los clientes usan `MQTT_USER`. No hay ACL todavía. |
| Node-RED puede ejecutar código | Un nodo `exec`, previsto en el plan para el reporte CSV, ejecutaría comandos en el contenedor. Por eso el editor exige `adminAuth` (`nodered/settings.js`, línea 13), y `settings.js` no es escribible desde el contenedor (`nodered/Dockerfile`). |
| Portainer controla el host | Portainer monta el socket de Docker (`/var/run/docker.sock`). Por eso su puerto solo escucha en la placa. |

### Pendiente

No se sirve `/api/galeria` sin `httpNodeAuth` y `GALERIA_TOKEN` (11 nov). No se registra a ninguna persona real sin `httpNodeAuth` y sin las ACL de Mosquitto (4 nov); antes de eso, solo identidades de prueba (LFW y miembros del equipo).

#### Para el 4 de noviembre

| Pendiente | Estado en el repo |
|---|---|
| ACL de Mosquitto y usuarios por rol | `mosquitto/mosquitto.conf` tiene `acl_file` comentado, a la espera del archivo `acl` |

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
4. Cambiar `NODERED_ADMIN_PASSWORD`, `MQTT_PASSWORD` y `NODERED_DB_PASSWORD` en `.env`, con `openssl rand -hex 16`.
5. Ejecutar `docker compose up -d` para recrear los contenedores con los valores nuevos.
6. Ejecutar `docker compose exec postgres sh /docker-entrypoint-initdb.d/02-roles.sh` para aplicar la contraseña nueva del rol de Node-RED.
7. Actualizar `MQTT_PASSWORD` en todos los clientes MQTT: la XIAO, el servicio de visión, la pantalla y el frontend.

Si el acceso pudo llegar al host, revisar también `POSTGRES_PASSWORD` y la cuenta de Portainer.

## Next steps

- Para el diseño del sistema y los tópicos MQTT, ver [docs/HIGH-LEVEL-PLAN.md](../docs/HIGH-LEVEL-PLAN.md), sección 7.
- Para el esquema de la base de datos, ver `orangepi/sql/schema.sql`.
- Para el flujo de energía y la API, ver `nodered/flows/flows.json`.
