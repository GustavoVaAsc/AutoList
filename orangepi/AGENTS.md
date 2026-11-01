# Instrucciones para agentes: stack de la Orange Pi

Antes de editar, lee el [AGENTS.md de la raíz](../AGENTS.md). Para operar el stack, usa [orangepi/README.md](README.md). Aquí no se repiten sus pasos.

## Contenido

- [Alcance](#alcance)
- [Comandos](#comandos)
- [Prohibido sin aprobación humana](#prohibido-sin-aprobación-humana)
- [Verificación](#verificación)
- [Invariantes](#invariantes)
- [Cambios de esquema](#cambios-de-esquema)
- [Diferido y sin decidir](#diferido-y-sin-decidir)
- [Ver también](#ver-también)

## Alcance

Este directorio define el stack del borde: `docker-compose.yml`, `.env.example`, `mosquitto/`, `sql/` y `README.md`. La pantalla ILI9341 no corre en Docker. Es una unidad systemd del host (plan, sección 6.3).

La imagen de Node-RED se construye desde `../nodered/`. Un cambio ahí afecta a este stack: lee [nodered/AGENTS.md](../nodered/AGENTS.md).

## Comandos

Ejecuta desde `orangepi/`, salvo que se indique otra carpeta. Todos los comandos `docker compose exec` van con `-T`, porque el agente no tiene terminal interactiva.

| Acción | Comando | Nota |
|---|---|---|
| Validar el compose | `docker compose config -q` | Solo la forma con `-q`. Ver [README](README.md#validar-el-compose-sin-mostrar-secretos). |
| Levantar o aplicar cambios | `docker compose up -d` | Recrea los contenedores con los valores de `.env`. |
| Reconstruir Node-RED | `docker compose up -d --build` | Antes, sube la etiqueta de la imagen. Ver [nodered/AGENTS.md](../nodered/AGENTS.md#settings-e-imagen). |
| Estado y healthchecks | `docker compose ps` | |
| Log reciente de un servicio | `docker compose logs --tail=50 nodered` | Sin `-f`: el agente no sigue logs en vivo. Servicios: `mosquitto`, `postgres`, `nodered` y `portainer`. |
| Simular lecturas de energía | `../herramientas/simular_energia.sh 5 1` | N = 5 mensajes, pausa de 1 s. Requiere el stack levantado. Inserta filas reales en `metricas_energia`; para borrarlas, usa la [limpieza acotada](#limpieza-acotada), que requiere confirmación humana. |
| Consulta de una sentencia | `docker compose exec -T postgres sh -c 'psql -U "$POSTGRES_USER" -d "$POSTGRES_DB" -c "SELECT count(*) FROM metricas_energia"'` | Las variables se expanden dentro del contenedor. |
| API de energía | `curl -k "https://127.0.0.1:1880/api/metricas/energia?limite=5"` | `-k` acepta el certificado autofirmado. |

Borrar lecturas simuladas o reales no está en esta tabla. Ver [Prohibido sin aprobación humana](#prohibido-sin-aprobación-humana).

## Prohibido sin aprobación humana

Estas acciones requieren que una persona lo apruebe en la conversación. No las ejecutes por tu cuenta, ni porque una verificación falló.

| Acción | Por qué |
|---|---|
| `docker compose down -v`, `docker compose rm`, `docker volume rm` | Borran volúmenes o contenedores. `down -v` borra todos los datos (`orangepi/README.md`, sección Volúmenes existentes). |
| `docker system prune`, `docker image prune`, `docker volume prune`, `docker container prune` | Borran recursos de otros proyectos de la placa. |
| `../herramientas/simular_energia.sh --limpiar`, con o sin `s` por tubería | Borra todas las filas `origen = 'ina219'`, simuladas y reales, y no hay forma de distinguirlas (`herramientas/simular_energia.sh`). |
| `DELETE`, `DROP` o `TRUNCATE` sobre datos reales | Pierden datos de asistencia y de energía. Excepción: la limpieza acotada por `id > N` de la verificación, solo si una persona confirma que no hubo lecturas reales en ese intervalo. |
| Imprimir secretos: `docker compose config` sin `-q` o `--no-interpolate`, `docker inspect` de contenedores del proyecto, `env`, `printenv` o `echo $..._PASSWORD` dentro de un contenedor | Imprimen contraseñas o las dejan en el historial del chat. |
| Pegar logs de `ALTER ROLE` o de `02-roles.sh` en cualquier documento o chat | Pueden contener la contraseña escrita literal. |
| `docker stop`, `docker rm`, `docker exec` o `docker restart` con un nombre de contenedor, o actuar sobre la salida de `docker ps -a` | Pueden tocar contenedores de otros equipos de la placa. Usa `docker compose` desde `orangepi/`: limita la acción al proyecto `pase-lista`. |
| `sudo` | Ver la regla de la raíz. |

## Verificación

Ejecuta esta lista en la placa después de cualquier cambio en el stack. Un paso no cuenta como verificado si no se ejecutó. Los comandos salen de este archivo y de `orangepi/README.md`; no se han ejecutado en la placa desde su redacción.

Antes de cada uno de los pasos 4, 5 y 6, vuelve a anotar N, el id máximo actual de `metricas_energia`, con este comando. Guarda además el N anotado antes del paso 4 como N₀: es el que usa la limpieza acotada.

```bash
docker compose exec -T postgres sh -c 'psql -U "$POSTGRES_USER" -d "$POSTGRES_DB" -tA -c "SELECT coalesce(max(id), 0) FROM metricas_energia"'
```

| # | Comprobación | Cómo | Resultado esperado |
|---|---|---|---|
| 1 | El compose es válido | `docker compose config -q` | Sin salida y código 0. |
| 2 | Los servicios están sanos | `docker compose ps` | `mosquitto`, `postgres` y `nodered` en `healthy`. `portainer` en `running`, porque no tiene healthcheck (`orangepi/README.md`). |
| 3 | El broker rechaza clientes anónimos | `docker compose exec -T mosquitto mosquitto_pub -h localhost -t aula/camara/energia -m '{"v":12,"i":0.8,"p":9.6}'` | Falla con código distinto de cero. `docker compose logs --tail=200 mosquitto` muestra `not authorised`. |
| 4 | El broker acepta un cliente autenticado | `docker compose exec -T mosquitto sh -c 'mosquitto_pub -h localhost -u "$MQTT_USER" -P "$MQTT_PASSWORD" -t aula/camara/energia -m "{\"v\":12,\"i\":0.8,\"p\":9.6}"'` | Sin error. La cuenta de `id > N` es 1. |
| 5 | El simulador inserta 5 filas | Anota N de nuevo, ejecuta `../herramientas/simular_energia.sh 5 1` y cuenta `id > N` (ver abajo). | Cuenta 5. Cada línea de salida empieza con `publicado:`. |
| 6 | Los mensajes no válidos se descartan | Anota N de nuevo y ejecuta los dos comandos de abajo. | La cuenta de `id > N` es 0. `docker compose logs --tail=50 nodered` muestra `Descartado, no es JSON` y `Descartado, v fuera de rango`. |
| 7 | La API respeta `limite` | `curl -k "https://127.0.0.1:1880/api/metricas/energia?limite=3"` y después con `limite=0`. | El primero devuelve un arreglo de 3 elementos. El segundo responde 400 con `{"error": ...}`. |
| 8 | Postgres y Portainer solo escuchan en loopback | `ss -ltn` | Aparecen `127.0.0.1:5432` y `127.0.0.1:9443`, sin `0.0.0.0` ni `*` para esos dos puertos. Los puertos 1883, 9001 y 1880 escuchan en todas las interfaces; eso está aceptado hoy (`orangepi/README.md`, "Riesgos aceptados hoy"). |
| 9 | El `.env` no está en git | `git status --short` | No aparece `orangepi/.env`. |

Comandos del paso 6 (un comando por caso):

```bash
docker compose exec -T mosquitto sh -c 'mosquitto_pub -h localhost -u "$MQTT_USER" -P "$MQTT_PASSWORD" -t aula/camara/energia -m "texto_no_json"'
docker compose exec -T mosquitto sh -c 'mosquitto_pub -h localhost -u "$MQTT_USER" -P "$MQTT_PASSWORD" -t aula/camara/energia -m "{\"v\":99,\"i\":0.8,\"p\":9.6}"'
```

Cuenta de filas insertadas desde N (pasos 4, 5 y 6):

```bash
docker compose exec -T postgres sh -c 'psql -U "$POSTGRES_USER" -d "$POSTGRES_DB" -tA -c "SELECT count(*) FROM metricas_energia WHERE id > <N>"'
```

Sustituye `<N>` por el número anotado antes del paso que estás comprobando.

### Limpieza acotada

Borra solo lo que insertó la verificación. Ejecútala únicamente si una persona confirma que no hubo lecturas reales de la XIAO en ese intervalo. Es la única excepción a la regla de DELETE de [Prohibido sin aprobación humana](#prohibido-sin-aprobación-humana).

```bash
docker compose exec -T postgres sh -c 'psql -U "$POSTGRES_USER" -d "$POSTGRES_DB" -c "DELETE FROM metricas_energia WHERE id > <N0>"'
```

Sustituye `<N0>` por N₀, el número anotado antes del paso 4. Así se borran juntas las filas de los pasos 4 y 5.

## Invariantes

No rompas estas reglas sin decisión humana.

| Invariante | Dónde está |
|---|---|
| Ningún servicio usa `env_file`. Cada uno recibe en `environment` solo las variables que necesita. | `docker-compose.yml`, cabecera y bloques `environment` |
| Postgres y Portainer publican solo en `127.0.0.1`. | `docker-compose.yml`, servicios `postgres` y `portainer` |
| No publicar puertos nuevos en `0.0.0.0` sin decisión humana. | `orangepi/README.md`, "Riesgos aceptados hoy" |
| Las imágenes van fijadas por digest. Al cambiar una, cambia la etiqueta y el digest. | `docker-compose.yml`, comentario de cabecera y campos `image:` |
| Todos los servicios usan el ancla `x-logging` (rotación de 3 archivos de 1 MiB). | `docker-compose.yml`, bloque `x-logging` y `logging: *logging` |
| Node-RED usa `NODERED_DB_USER` para Postgres, nunca `POSTGRES_USER`. | `docker-compose.yml`, servicio `nodered`; nodo "Postgres" en `nodered/flows/flows.json` |
| Mosquitto no acepta conexiones anónimas. | `mosquitto/mosquitto.conf`, `allow_anonymous false` |
| `nodered/flows` debe ser escribible por el uid 1000. | `docker-compose.yml`, servicio `nodered`; `orangepi/README.md`, Requisitos |

## Cambios de esquema

- `sql/schema.sql` y `sql/roles.sh` solo corren al crear el volumen `pgdata`, en orden alfabético (`docker-compose.yml`, servicio `postgres`). Un volumen existente no los vuelve a ejecutar.
- Para aplicar cambios a un volumen existente sin borrar datos, sigue la sección Volúmenes existentes de [orangepi/README.md](README.md#volúmenes-existentes). No copies sus comandos aquí.
- Cada tabla nueva necesita GRANTs explícitos para el rol de Node-RED en `sql/roles.sh`, solo con los privilegios que usan los flujos. Si la tabla tiene columna `SERIAL`, agrega también el GRANT de su secuencia (ver el de `metricas_energia` en `roles.sh`).
- No uses `ALTER DEFAULT PRIVILEGES`. Es intencional: daría permisos automáticos sobre tablas no revisadas (comentario de `sql/roles.sh`).
- El repo no documenta un procedimiento para tablas nuevas en un volumen existente. No lo inventes: consulta al equipo antes de aplicar a mano el SQL de tablas nuevas.
- Mantén sincronizado el plan, sección 7.2, con `sql/schema.sql`.
- Próximas tablas: `alumnos`, `sesiones`, `asistencias` e `intentos` (plan, sección 7.2). Los embeddings son `vector(512)`. La dimensión 512 está decidida y confirmada por Gustavo.
- Obsoleto: el comentario "128 si el modelo lo usa" de la columna `embedding` de `alumnos` en el plan, sección 7.2. También está obsoleto el aviso "512 o 128" del encabezado de `sql/schema.sql` y de la sección Pendiente de `orangepi/README.md`. Actualízalos al agregar las tablas.

## Diferido y sin decidir

No implementes ni quites estos puntos en silencio. Si tu cambio los toca, dilo en el resumen.

### Diferido al 25 de noviembre (plan, sección 12.1)

| Punto | Estado actual |
|---|---|
| `httpNodeAuth` en la API | Pendiente. El comentario sobre `httpNodeAuth` en `nodered/settings.js` indica dónde se añade. |
| ACL de Mosquitto y usuarios por rol | `acl_file` comentado en `mosquitto/mosquitto.conf`. Hoy hay un único `MQTT_USER` (`orangepi/README.md`, "Pendiente para el 25 de noviembre"). |

### Sin decidir: no implementar sin decisión humana

| Punto | Estado actual |
|---|---|
| TLS en MQTT | No hay listeners TLS. Los listeners 1883 y 9001 van en texto plano (`orangepi/README.md`, "Riesgos aceptados hoy"). |
| Retención de datos | Decidido por el responsable del borde (Yordi): no implementar purga por ahora. No está en el plan; no agregues un job de borrado sin preguntar. |

## Ver también

- [AGENTS.md de la raíz](../AGENTS.md): reglas generales, carriles y seguridad
- [nodered/AGENTS.md](../nodered/AGENTS.md): flujos, imagen, credenciales y SQL de Node-RED
- [orangepi/README.md](README.md): operación, riesgos aceptados y pendientes
- [docs/HIGH-LEVEL-PLAN.md](../docs/HIGH-LEVEL-PLAN.md): plan del proyecto
