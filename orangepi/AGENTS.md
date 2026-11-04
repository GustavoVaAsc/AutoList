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

Este directorio define el stack del edge: `docker-compose.yml`, `.env.example`, `mosquitto/` (con la ACL `acl`), `sql/` y `README.md`. La pantalla ILI9341 no corre en Docker. Es una unidad systemd del host (plan, sección 6.3).

La imagen de Node-RED se construye desde `../nodered/`. Un cambio ahí afecta a este stack: lee [nodered/AGENTS.md](../nodered/AGENTS.md). Esa imagen también compila el frontend de `../web/` (contexto adicional del build) y lo sirve en `/app/`: un cambio en `web/` exige reconstruirla.

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
| API de energía | `curl -k -u profesor "https://127.0.0.1:1880/api/metricas/energia?limite=5"` | `-k` acepta el certificado autofirmado. `-u profesor` pide la contraseña: lo ejecuta una persona (el agente no tiene terminal interactiva y no lee `.env`). |

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

Ejecuta esta lista en la placa después de cualquier cambio en el stack. Un paso no cuenta como verificado si no se ejecutó. Los comandos salen de este archivo y de `orangepi/README.md`; los pasos 1 a 10 se ejecutaron en la placa el 2 de noviembre de 2026, tras activar la ACL. Los pasos 11 a 14 (autenticación de la API, galería, importación del CSV y frontend) se ejecutaron en la placa el 2 de noviembre de 2026, con la imagen `5.0.9-12`, la primera con autenticación. Las 3 filas de prueba del grupo `PRUEBA` (cuentas 999000001 a 999000003) quedaron en `alumnos`, porque el rol de Node-RED no puede borrarlas. Los pasos 15 a 25 (flujo `Registro`, búsqueda, retiro del rostro y vista de Registro) **no se han ejecutado en la placa**; los corre Yordi tras desplegar, con esas cuentas de prueba y con el servicio de visión apagado. El paso 25 es obligatorio: ningún dato de prueba debe quedar con consentimiento o rostro. **Antes de cualquier comando `docker compose` con este compose, `orangepi/.env` debe tener `NODERED_PROFESOR_USER`, `NODERED_PROFESOR_PASSWORD` y `GALERIA_TOKEN`** (`:?` en el compose: sin ellas ni `ps` ni `config -q` funcionan). Node-RED tampoco arranca si `NODERED_PROFESOR_PASSWORD`, `GALERIA_TOKEN` o `NODERED_ADMIN_PASSWORD` contienen `cambiar_esta_clave`, si dos de esos tres son iguales, si `GALERIA_TOKEN` tiene menos de 32 caracteres, si `NODERED_PROFESOR_PASSWORD` tiene menos de 16 o si alguno de esos dos tiene menos de 8 caracteres distintos; `openssl rand -hex 16` cumple todo (`orangepi/.env.example`).

Antes de cada uno de los pasos 4, 5 y 6, vuelve a anotar N, el id máximo actual de `metricas_energia`, con este comando. Guarda además el N anotado antes del paso 4 como N₀: es el que usa la limpieza acotada.

```bash
docker compose exec -T postgres sh -c 'psql -U "$POSTGRES_USER" -d "$POSTGRES_DB" -tA -c "SELECT coalesce(max(id), 0) FROM metricas_energia"'
```

| # | Comprobación | Cómo | Resultado esperado |
|---|---|---|---|
| 1 | El compose es válido | `docker compose config -q` | Sin salida y código 0. |
| 2 | Los servicios están sanos | `docker compose ps` | `mosquitto`, `postgres` y `nodered` en `healthy`. `portainer` en `running`, porque no tiene healthcheck (`orangepi/README.md`). |
| 3 | El broker rechaza clientes anónimos | `docker compose exec -T mosquitto mosquitto_pub -h localhost -t aula/camara/energia -m '{"v":12,"i":0.8,"p":9.6}'` | Falla con código distinto de cero. `docker compose logs --tail=200 mosquitto` muestra `not authorised`. |
| 4 | El broker acepta un cliente autenticado | `docker compose exec -T mosquitto sh -c 'd=$(mktemp -d /run/mosquitto/xdg.XXXXXX); printf -- "-u simulador\n-P %s\n" "$MQTT_SIMULADOR_PASSWORD" > "$d/mosquitto_pub"; XDG_CONFIG_HOME="$d" mosquitto_pub -h localhost -t aula/camara/energia -m "{\"v\":12,\"i\":0.8,\"p\":9.6}"; rm -rf "$d"'` | Sin error. La cuenta de `id > N` es 1. |
| 5 | El simulador inserta 5 filas | Anota N de nuevo, ejecuta `../herramientas/simular_energia.sh 5 1` y cuenta `id > N` (ver abajo). | Cuenta 5. Cada línea de salida empieza con `publicado:`. |
| 6 | Los mensajes no válidos se descartan | Anota N de nuevo y ejecuta los dos comandos de abajo. | La cuenta de `id > N` es 0. `docker compose logs --tail=50 nodered` muestra `Descartado, no es JSON` y `Descartado, v fuera de rango`. |
| 7 | La API respeta `limite` | `curl -k -u profesor "https://127.0.0.1:1880/api/metricas/energia?limite=3"` y después con `limite=0`. | El primero devuelve un arreglo de 3 elementos. El segundo responde 400 con `{"error": ...}`. |
| 8 | Postgres y Portainer solo escuchan en loopback | `ss -ltn` | Aparecen `127.0.0.1:5432` y `127.0.0.1:9443`, sin `0.0.0.0` ni `*` para esos dos puertos. Los puertos 1883 y 1880 escuchan en todas las interfaces; eso está aceptado hoy (`orangepi/README.md`, "Riesgos aceptados hoy"). El 9001 ya no existe: no debe aparecer. |
| 9 | El `.env` no está en git | `git status --short` | No aparece `orangepi/.env`. |
| 10 | La ACL descarta lo que no está permitido | Los comandos de abajo, con Node-RED detenido. | Imprime `permitido` y, al agotarse el tiempo, `Timed out`; nunca `denegado`. |
| 11 | La API exige Basic | Los `curl` de [orangepi/README.md](README.md#probar-con-curl) a `/api/alumnos` y `/api/metricas/energia`: sin credenciales, con contraseña errónea y con `-u profesor`. | 401 con `WWW-Authenticate: Basic` en los dos primeros; 200 con un arreglo JSON en el tercero. |
| 12 | `GET /api/galeria` solo acepta el token | Los `curl` de [orangepi/README.md](README.md#probar-con-curl) a `/api/galeria`: sin nada, con `-u profesor`, con `X-Galeria-Token`, y el token contra `/api/alumnos`. | 401, 401, 200 con `{"embeddings":[...]}` (no imprimir el cuerpo) y 401. El servicio de visión carga la galería con el mismo token. |
| 13 | La importación del CSV valida y decide | Con un CSV de un grupo de prueba (cuentas `99900xxxx`), los `curl` de [POST /api/alumnos/importar](README.md#post-apialumnosimportar): un archivo con una fila inválida, el válido, el mismo otra vez, y con `?existentes=omitir` y `?existentes=actualizar`. Todos con `-H 'X-Pase-Lista: 1'` y `Content-Type: text/csv`. Además: sin `X-Pase-Lista` debe dar 403 y con `Content-Type: text/plain`, 415. | 400 con la fila; 200 `insertadas` = filas; 409 con las existentes y su rostro; 200 con `omitidas` = filas; 200 con `actualizadas` = filas. Las filas de prueba quedan en `alumnos` (el rol no puede borrarlas): consultar con el superusuario y coordinar su limpieza con una persona. |
| 14 | El frontend sigue funcionando con la autenticación | Abrir `https://<ip>:1880/app/` en un navegador. | Aparece el diálogo de usuario y contraseña; con las del profesor, la vista de energía carga. Con "Cancelar", aviso del 401 con el botón "Reintentar" y sin que el diálogo reaparezca solo cada 5 s. |
| 15 | El flujo `Registro` cargó y el rol de Node-RED puede escribir lo que necesita | Tras `docker compose up -d --no-deps --build nodered` (con la etiqueta subida): `docker compose logs --tail=50 nodered` y el comando de privilegios de abajo. **No despliegues Node-RED con un registro abierto**: al arrancar, el registro se cierra y se pierde (se borra la inscripción y se quitan los consentimientos sin rostro). | Sin errores de nodos; unos 3 s después del arranque aparece `Arranque: se borran las inscripciones pendientes y los consentimientos sin rostro`. Los tres privilegios dan `t`. Si alguno da `f`, aplica `02-roles.sh` como dice [orangepi/README.md](README.md#volúmenes-existentes) (no se amplía el rol a mano). |
| 16 | `GET /api/enrolamiento/estado` | Los `curl` de [orangepi/README.md](README.md#registro-del-rostro-y-retiro-del-consentimiento): sin credenciales y con `-u profesor`. | 401 y 200 con `"abierto": false` y `"estado": "sin_registro"`, sin campos de más (nada de embeddings). |
| 17 | `POST /api/enrolamiento` valida y abre el registro | Con la cuenta de prueba 999000001, y con el suscriptor del paso 17 esperando en otra terminal: sin `X-Pase-Lista`; con `Content-Type: text/plain`; con `"consentimiento": false`; con una cuenta de 8 dígitos; con una cuenta inexistente; el válido; el mismo otra vez. Después, la consulta de la cuenta de abajo. | 403, 415, 400, 400, 404, 202 (`abierto: true`) y 409 `registro_abierto`. `GET .../estado` da `esperando_vision`. La consulta da `t`, `t`, `t`. El suscriptor imprime `aula/vision/cmd {"accion":"enrolar","cuenta":"999000001","nombre":"..."}`. |
| 18 | `POST /api/enrolamiento/cancelar` | Con el registro del paso 17 abierto y el suscriptor del paso 18 esperando en otra terminal: cancelar, consultar el estado y la cuenta; cancelar otra vez. | 200 `"cancelado": true`; el estado pasa a `cancelado` con `abierto: false`; la consulta da `f`, `f`, `t`; el suscriptor imprime `aula/vision/cmd {"accion":"cancelar","cuenta":"999000001"}`. La segunda cancelación: 200 `"cancelado": false` y no publica. |
| 19 | El progreso que publica la visión llega a la API (la visión, apagada) | Abre un registro (paso 17) y publica como `vision` los mensajes de abajo, uno por uno, consultando el estado después de cada uno. Al final, con el registro ya cerrado, la prueba del retenido (los 4 comandos de abajo): publicarlo, reiniciar Node-RED, leer el log, borrarlo y comprobar que no quedó nada. | `aceptada` de `frente` queda en `poses[0]`; el de otra cuenta y el de 2020 no cambian nada (el log dice `Descartado`); `completado` pasa a `guardando` y el registro sigue abierto; `error` cierra con su motivo y la cuenta vuelve a `f`, `f`, `t`. El retenido: tras el reinicio y `healthy`, el log trae `Descartado mensaje retenido en aula/vision/enrolamiento`; tras borrarlo, el suscriptor como `pantalla` no imprime nada y termina por tiempo. |
| 20 | `aula/vision/embedding` solo se guarda si encaja (caso negativo) | Con un registro abierto y la visión apagada, publica el embedding de `dim` 3 de abajo. | El registro se cierra como `error` con motivo `embedding_invalido`, la cuenta vuelve a `f`, `f`, `t` y no aparece `galeria/alta`. **El camino positivo no se prueba aquí con un vector inventado**: lo cubren las pruebas unitarias (`node --test nodered/pruebas/*.test.js`), y en la placa se prueba con el servicio de visión real y el consentimiento de un integrante del equipo. |
| 21 | `DELETE /api/alumnos/:cuenta/rostro` | Con 999000001 pendiente: el `DELETE`; con una cuenta de 5 dígitos; con una inexistente; sin `X-Pase-Lista`; con un registro abierto de esa cuenta; y el suscriptor del paso 21 esperando. Con el rostro real del integrante del paso 20, el `DELETE` y la consulta de su cuenta. | 200 `"retirado": false` y el suscriptor no imprime nada (se corta a los 20 s); 400; 404; 403; 409 `registro_abierto`. Con rostro: 200 `"retirado": true`, `embedding IS NULL`, consentimiento en `f`, y el suscriptor imprime `aula/vision/galeria/baja {"cuenta":"..."}`. Un segundo `DELETE` da 200 `"retirado": false`. |
| 22 | Un registro sin respuesta de la visión vence | Inicia un registro (paso 17) y, **después**, lanza el suscriptor del paso 22 (`-W 360`); no hagas nada durante 5 minutos y medio. | `GET .../estado` da `abierto: false`, `estado: "error"`, `motivo: "vencido"`; el suscriptor imprime `aula/vision/cmd {"accion":"cancelar",...}`; la cuenta vuelve a `f`, `f`, `t`. |
| 23 | Alumnos: búsqueda y retiro del rostro | En el navegador, `https://<ip>:1880/app/#/alumnos`: buscar por nombre sin acentos y por cuenta; "Retirar rostro" de un integrante del equipo con rostro (Escape, Cancelar y confirmar); "Registrar rostro" de un pendiente. | La lista filtra y los conteos siguen a lo visible; el diálogo abre con el foco en "Cancelar" y Escape no retira nada; confirmar da el aviso "Rostro retirado", la fila pasa a pendiente y el foco vuelve a su botón; "Registrar rostro" abre Registro con ese alumno elegido. |
| 24 | Registro en el navegador | `https://<ip>:1880/app/#/registro`: elegir un alumno de prueba, marcar el consentimiento, iniciar, mirar el progreso, recargar la página, cancelar. | "Iniciar registro" está deshabilitado sin la casilla. Con el registro abierto y la visión apagada dice "Esperando al servicio de visión" con el tiempo transcurrido y las 5 poses pendientes (no simula avance). Al recargar se retoma el registro. "Cancelar registro" lo cierra y la vista dice "Registro cancelado". |
| 25 | **Obligatorio al terminar:** no quedan datos de prueba | Con el superusuario, la consulta de limpieza de abajo. | Da 0. Si no, retira lo que quede (paso 21, o un superusuario con `UPDATE ... SET embedding = NULL, consentimiento = false, consentimiento_fecha = NULL` sobre esas cuentas; el rol de Node-RED no puede borrar filas). |

Privilegios del paso 15 (el rol sale de `NODERED_DB_USER`, expandido dentro del contenedor; no imprime secretos):

```bash
docker compose exec -T postgres sh -c "psql -U \"\$POSTGRES_USER\" -d \"\$POSTGRES_DB\" -tA -c \"SELECT c, has_column_privilege('\$NODERED_DB_USER', 'alumnos', c, 'UPDATE') FROM unnest(ARRAY['embedding','consentimiento','consentimiento_fecha']) AS c\""
```

Consulta de la cuenta de prueba (pasos 17, 18, 19, 20, 22 y 21; consentimiento, fecha escrita, rostro vacío):

```bash
docker compose exec -T postgres sh -c "psql -U \"\$POSTGRES_USER\" -d \"\$POSTGRES_DB\" -tA -c \"SELECT consentimiento, consentimiento_fecha IS NOT NULL, embedding IS NULL FROM alumnos WHERE no_cuenta = '999000001'\""
```

Consulta del paso 25:

```bash
docker compose exec -T postgres sh -c "psql -U \"\$POSTGRES_USER\" -d \"\$POSTGRES_DB\" -tA -c \"SELECT count(*) FROM alumnos WHERE no_cuenta LIKE '99900%' AND (embedding IS NOT NULL OR consentimiento)\""
```

Suscriptor de los pasos 17, 18, 21 y 22 (observa como `vision`, con la visión apagada porque con su usuario en marcha se expulsarían entre sí; la contraseña va en el archivo del cliente, como en los pasos 4 y 6). Se lanza **antes** de la acción, en otra terminal; nadie publica como `vision` en estos pasos. Para el 17 y el 18, `-t aula/vision/cmd`; para el 21, `-t aula/vision/galeria/baja`; para el 22, `-W 360` en lugar de `-W 20` y lanzado **después** de abrir el registro (con `-C 1`, si se lanza antes se queda con el `enrolar` y termina):

```bash
docker compose exec -T mosquitto sh -c 'd=$(mktemp -d /run/mosquitto/xdg.XXXXXX); printf -- "-u vision\n-P %s\n" "$MQTT_VISION_PASSWORD" > "$d/mosquitto_sub"; XDG_CONFIG_HOME="$d" mosquitto_sub -h localhost -t aula/vision/cmd -W 20 -C 1 -v; rm -rf "$d"'
```

Comandos del paso 19 (publican como `vision`, con la visión apagada). Todos llevan `ts`, que es obligatorio y no puede ser anterior al inicio del registro (con 2 s de margen). `$(date ...)` se evalúa dentro del contenedor, en UTC, el mismo reloj que usa Node-RED:

```bash
docker compose exec -T mosquitto sh -c 'd=$(mktemp -d /run/mosquitto/xdg.XXXXXX); printf -- "-u vision\n-P %s\n" "$MQTT_VISION_PASSWORD" > "$d/mosquitto_pub"; XDG_CONFIG_HOME="$d" mosquitto_pub -h localhost -t aula/vision/enrolamiento -m "{\"ts\":\"$(date -u +%Y-%m-%dT%H:%M:%SZ)\",\"cuenta\":\"999000001\",\"paso\":1,\"total\":5,\"pose\":\"frente\",\"estado\":\"aceptada\"}"; rm -rf "$d"'
docker compose exec -T mosquitto sh -c 'd=$(mktemp -d /run/mosquitto/xdg.XXXXXX); printf -- "-u vision\n-P %s\n" "$MQTT_VISION_PASSWORD" > "$d/mosquitto_pub"; XDG_CONFIG_HOME="$d" mosquitto_pub -h localhost -t aula/vision/enrolamiento -m "{\"ts\":\"$(date -u +%Y-%m-%dT%H:%M:%SZ)\",\"cuenta\":\"999000002\",\"paso\":1,\"total\":5,\"pose\":\"frente\",\"estado\":\"aceptada\"}"; rm -rf "$d"'
docker compose exec -T mosquitto sh -c 'd=$(mktemp -d /run/mosquitto/xdg.XXXXXX); printf -- "-u vision\n-P %s\n" "$MQTT_VISION_PASSWORD" > "$d/mosquitto_pub"; XDG_CONFIG_HOME="$d" mosquitto_pub -h localhost -t aula/vision/enrolamiento -m "{\"ts\":\"2020-01-01T00:00:00Z\",\"cuenta\":\"999000001\",\"paso\":2,\"total\":5,\"pose\":\"izquierda\",\"estado\":\"aceptada\"}"; rm -rf "$d"'
docker compose exec -T mosquitto sh -c 'd=$(mktemp -d /run/mosquitto/xdg.XXXXXX); printf -- "-u vision\n-P %s\n" "$MQTT_VISION_PASSWORD" > "$d/mosquitto_pub"; XDG_CONFIG_HOME="$d" mosquitto_pub -h localhost -t aula/vision/enrolamiento -m "{\"ts\":\"$(date -u +%Y-%m-%dT%H:%M:%SZ)\",\"cuenta\":\"999000001\",\"paso\":5,\"total\":5,\"pose\":\"frente_2\",\"estado\":\"completado\"}"; rm -rf "$d"'
docker compose exec -T mosquitto sh -c 'd=$(mktemp -d /run/mosquitto/xdg.XXXXXX); printf -- "-u vision\n-P %s\n" "$MQTT_VISION_PASSWORD" > "$d/mosquitto_pub"; XDG_CONFIG_HOME="$d" mosquitto_pub -h localhost -t aula/vision/enrolamiento -m "{\"ts\":\"$(date -u +%Y-%m-%dT%H:%M:%SZ)\",\"cuenta\":\"999000001\",\"paso\":3,\"total\":5,\"pose\":\"derecha\",\"estado\":\"error\",\"motivo\":\"npu\"}"; rm -rf "$d"'
```

El retenido del paso 19. El broker de Node-RED habla MQTT 3.1.1 (`protocolVersion 4`): un retenido publicado en vivo llega a Node-RED con `retain = false`, así que el descarte por la bandera solo se ve cuando Node-RED (re)conecta y el broker le entrega el retenido guardado. Por eso se publica, se reinicia Node-RED (esto cierra cualquier registro abierto: hazlo con el registro ya cerrado) y se lee el log. Se borra al terminar: sin eso, el broker lo volvería a entregar en cada reconexión. Los cuatro comandos, en orden:

```bash
# 1. Publicar el retenido (queda guardado en el broker)
docker compose exec -T mosquitto sh -c 'd=$(mktemp -d /run/mosquitto/xdg.XXXXXX); printf -- "-u vision\n-P %s\n" "$MQTT_VISION_PASSWORD" > "$d/mosquitto_pub"; XDG_CONFIG_HOME="$d" mosquitto_pub -h localhost -r -t aula/vision/enrolamiento -m "{\"ts\":\"$(date -u +%Y-%m-%dT%H:%M:%SZ)\",\"cuenta\":\"999000001\",\"paso\":2,\"total\":5,\"pose\":\"izquierda\",\"estado\":\"aceptada\"}"; rm -rf "$d"'
# 2. Reiniciar Node-RED, esperar a que `docker compose ps` lo muestre `healthy` (hasta 60 s) y leer el log:
#    debe incluir "Descartado mensaje retenido en aula/vision/enrolamiento"
docker compose restart nodered
docker compose ps
docker compose logs --tail=50 nodered
# 3. Borrar el retenido (-r -n: retenido vacío)
docker compose exec -T mosquitto sh -c 'd=$(mktemp -d /run/mosquitto/xdg.XXXXXX); printf -- "-u vision\n-P %s\n" "$MQTT_VISION_PASSWORD" > "$d/mosquitto_pub"; XDG_CONFIG_HOME="$d" mosquitto_pub -h localhost -r -n -t aula/vision/enrolamiento; rm -rf "$d"'
# 4. Comprobar como `pantalla` (puede leer ese tópico) que no quedó nada: no imprime nada y termina por tiempo
docker compose exec -T mosquitto sh -c 'd=$(mktemp -d /run/mosquitto/xdg.XXXXXX); printf -- "-u pantalla\n-P %s\n" "$MQTT_PANTALLA_PASSWORD" > "$d/mosquitto_sub"; XDG_CONFIG_HOME="$d" mosquitto_sub -h localhost -t aula/vision/enrolamiento -W 3 -v; rm -rf "$d"'
```

Comando del paso 20 (caso negativo, con un registro abierto; un vector de 3 números, que Node-RED rechaza por `dim`):

```bash
docker compose exec -T mosquitto sh -c 'd=$(mktemp -d /run/mosquitto/xdg.XXXXXX); printf -- "-u vision\n-P %s\n" "$MQTT_VISION_PASSWORD" > "$d/mosquitto_pub"; XDG_CONFIG_HOME="$d" mosquitto_pub -h localhost -q 1 -t aula/vision/embedding -m "{\"ts\":\"$(date -u +%Y-%m-%dT%H:%M:%SZ)\",\"cuenta\":\"999000001\",\"dim\":3,\"modelo\":\"prueba\",\"embedding\":[0.6,0.8,0.0]}"; rm -rf "$d"'
```

Comandos del paso 6 (un comando por caso):

```bash
docker compose exec -T mosquitto sh -c 'd=$(mktemp -d /run/mosquitto/xdg.XXXXXX); printf -- "-u simulador\n-P %s\n" "$MQTT_SIMULADOR_PASSWORD" > "$d/mosquitto_pub"; XDG_CONFIG_HOME="$d" mosquitto_pub -h localhost -t aula/camara/energia -m "texto_no_json"; rm -rf "$d"'
docker compose exec -T mosquitto sh -c 'd=$(mktemp -d /run/mosquitto/xdg.XXXXXX); printf -- "-u simulador\n-P %s\n" "$MQTT_SIMULADOR_PASSWORD" > "$d/mosquitto_pub"; XDG_CONFIG_HOME="$d" mosquitto_pub -h localhost -t aula/camara/energia -m "{\"v\":99,\"i\":0.8,\"p\":9.6}"; rm -rf "$d"'
```

Los comandos de los pasos 4, 6 y 10 no pasan la contraseña con `-P`, que quedaría visible en `/proc/<pid>/cmdline` del host: la escriben, dentro del contenedor, en el archivo de configuración del cliente (`mosquitto_pub` o `mosquitto_sub`) de un directorio temporal del tmpfs `/run/mosquitto`, que se indica con `XDG_CONFIG_HOME` y se borra al terminar.

Comandos del paso 10. Con `use_username_as_clientid`, un cliente con el usuario `nodered` expulsa a Node-RED; por eso se detiene antes y se vuelve a levantar después (queda sin ingesta mientras tanto). Mosquitto descarta en silencio la publicación denegada: se comprueba con el suscriptor, no con el código del publicador.

```bash
docker compose stop nodered
docker compose exec -T mosquitto sh -c 'd=$(mktemp -d /run/mosquitto/xdg.XXXXXX); printf -- "-u nodered\n-P %s\n" "$MQTT_NODERED_PASSWORD" > "$d/mosquitto_sub"; printf -- "-u simulador\n-P %s\n" "$MQTT_SIMULADOR_PASSWORD" > "$d/mosquitto_pub"; XDG_CONFIG_HOME="$d" mosquitto_sub -h localhost -t "aula/camara/#" -C 2 -W 5 & sleep 1; XDG_CONFIG_HOME="$d" mosquitto_pub -h localhost -t aula/camara/estado -m denegado; XDG_CONFIG_HOME="$d" mosquitto_pub -h localhost -t aula/camara/energia -m permitido; wait; rm -rf "$d"'
docker compose start nodered
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
| La base del proyecto (`POSTGRES_DB`) tiene `timezone = America/Mexico_City`, fijada con `ALTER DATABASE` y no por rol ni por sesión. `log_timezone` se queda en `Etc/UTC` y no se toca `TZ` de Postgres ni de Mosquitto. Las consultas de reporte que agrupen o comparen por día escriben la zona explícita: `(columna AT TIME ZONE 'America/Mexico_City')::date`. | `sql/schema.sql` (bloque `DO`); `orangepi/README.md`, Volúmenes existentes |
| Mosquitto no acepta conexiones anónimas. | `mosquitto/mosquitto.conf`, `allow_anonymous false` |
| Un usuario MQTT por rol (`xiao`, `vision`, `nodered`, `pantalla` y `simulador`), sin usuario compartido ni `frontend`, con la ACL de `mosquitto/acl` y `use_username_as_clientid true`. Un usuario o permiso nuevo cambia el contrato (sección 6), la ACL, el entrypoint, el compose y `.env.example` a la vez. | `mosquitto/acl`, `mosquitto/entrypoint.sh`, `docs/contrato-mqtt.md` |
| Mosquitto solo publica el puerto 1883. No hay WebSocket (9001): el frontend no habla MQTT. | `docker-compose.yml`, servicio `mosquitto`; `mosquitto/mosquitto.conf` |
| Ninguna contraseña MQTT va en `-P` ni en la línea de comandos de un proceso: se escribe en el archivo de configuración del cliente (`XDG_CONFIG_HOME`), en `/run/mosquitto`. `/run/mosquitto` es un tmpfs, para que ni el `passwd.tmp` en claro toque la microSD. | `herramientas/simular_energia.sh`, `mosquitto/entrypoint.sh`, `docker-compose.yml` (`tmpfs`) |
| El healthcheck de Mosquitto no usa credenciales: un cliente con el usuario de un servicio lo expulsaría cada minuto. | `docker-compose.yml`, servicio `mosquitto` |
| `nodered/flows` debe ser escribible por el uid 1000. | `docker-compose.yml`, servicio `nodered`; `orangepi/README.md`, Requisitos |

## Cambios de esquema

- `sql/schema.sql` y `sql/roles.sh` solo corren al crear el volumen `pgdata`, en orden alfabético (`docker-compose.yml`, servicio `postgres`). Un volumen existente no los vuelve a ejecutar.
- Para aplicar cambios a un volumen existente sin borrar datos, sigue la sección Volúmenes existentes de [orangepi/README.md](README.md#volúmenes-existentes). No copies sus comandos aquí.
- Cada tabla nueva necesita GRANTs explícitos para el rol de Node-RED en `sql/roles.sh`, solo con los privilegios que usan los flujos. Si la tabla tiene columna `SERIAL`, agrega también el GRANT de su secuencia (ver el de `metricas_energia` en `roles.sh`).
- No uses `ALTER DEFAULT PRIVILEGES`. Es intencional: daría permisos automáticos sobre tablas no revisadas (comentario de `sql/roles.sh`).
- El repo no documenta un procedimiento para tablas nuevas en un volumen existente. No lo inventes: consulta al equipo antes de aplicar a mano el SQL de tablas nuevas.
- Mantén sincronizado el plan, sección 7.2, con `sql/schema.sql`.
- La zona horaria de la base está en `sql/schema.sql` (`ALTER DATABASE ... SET timezone`). Un volumen creado antes de ese ajuste sigue en UTC hasta que se aplica el paso de Volúmenes existentes del README y se reinicia Node-RED (`docker compose restart nodered`), para que su pool reconecte. Es un ajuste de configuración, no una tabla nueva: la regla de consultar al equipo no aplica.
- Las tablas `alumnos`, `sesiones`, `asistencias` e `intentos` ya están en `sql/schema.sql` (plan, sección 7.2). Los embeddings son `vector(512)`, dimensión confirmada por Gustavo. `intentos` no guarda embeddings: quien no se reconoce no dio consentimiento.
- `alumnos` tiene los CHECK `embedding IS NULL OR consentimiento` y `consentimiento = (consentimiento_fecha IS NOT NULL)`. Registrar son dos UPDATE: primero el consentimiento y su fecha, con el embedding aún NULL (lo escribe el endpoint HTTP del profesor), y después el embedding con `AND consentimiento AND embedding IS NULL` (lo escribe el manejador de MQTT, que nunca escribe el consentimiento). Retirar el rostro es el inverso (embedding NULL, consentimiento false, fecha NULL). El rol de Node-RED solo puede actualizar `nombre`, `grupo`, `embedding`, `consentimiento` y `consentimiento_fecha`, y no tiene DELETE (`sql/roles.sh`).
- Corregir una asistencia ya registrada lo hace solo el superusuario: el rol de Node-RED tiene SELECT e INSERT sobre `asistencias`. Borrar la identidad completa de una persona (`nombre`, `no_cuenta`) también es un procedimiento de superusuario, porque `asistencias` referencia la fila.
- No se sirve `/api/galeria` sin la autenticación de la API y `GALERIA_TOKEN`: Node-RED no arranca sin sus variables (`nodered/autenticacion-api.js`). No se registra a ninguna persona real sin esa autenticación verificada en la placa y sin las ACL de Mosquitto (4 nov); antes de eso, solo identidades de prueba (LFW y miembros del equipo).
- En `psql`, los embeddings se pasan con `\set` o con `PREPARE`/`EXECUTE`, nunca como literales pegados en la sentencia.

## Diferido y sin decidir

No implementes ni quites estos puntos en silencio. Si tu cambio los toca, dilo en el resumen.

### Sin decidir: no implementar sin decisión humana

| Punto | Estado actual |
|---|---|
| TLS en MQTT | No hay listeners TLS. El listener 1883 va en texto plano (`orangepi/README.md`, "Riesgos aceptados hoy"). |
| Retención de datos | Decidido por el responsable del edge (Yordi): no implementar purga por ahora. No está en el plan; no agregues un job de borrado sin preguntar. |

## Ver también

- [AGENTS.md de la raíz](../AGENTS.md): reglas generales, carriles y seguridad
- [nodered/AGENTS.md](../nodered/AGENTS.md): flujos, imagen, credenciales y SQL de Node-RED
- [orangepi/README.md](README.md): operación, riesgos aceptados y pendientes
- [docs/HIGH-LEVEL-PLAN.md](../docs/HIGH-LEVEL-PLAN.md): plan del proyecto
