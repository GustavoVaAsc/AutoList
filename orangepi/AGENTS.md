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

Ejecuta esta lista en la placa después de cualquier cambio en el stack. Un paso no cuenta como verificado si no se ejecutó. Los comandos salen de este archivo y de `orangepi/README.md`; los pasos 1 a 10 se ejecutaron en la placa el 2 de noviembre de 2026, tras activar la ACL. Los pasos 11 a 14 (autenticación de la API, galería, importación del CSV y frontend) se ejecutaron en la placa el 2 de noviembre de 2026, con la imagen `5.0.9-12`, la primera con autenticación. Las 3 filas de prueba del grupo `PRUEBA` (cuentas 999000001 a 999000003) quedaron en `alumnos`, porque el rol de Node-RED no puede borrarlas. Los pasos 15 a 25 (flujo `Registro`, búsqueda, retiro del rostro y vista de Registro) se ejecutaron en la placa el 3 de noviembre de 2026, con la imagen `5.0.9-17` (los pasos 23 y 24, Yordi en el navegador; el 21, sin la parte del rostro real, que espera a la visión); los pasos 15 a 20 y 25 se repitieron el 5 de noviembre con la imagen `5.0.9-18`. El paso 26 (retiro tardío atado a la fecha) se ejecutó el 5 de noviembre. Los pasos 27 a 33, 35 y 36 (sesiones y reporte CSV) se ejecutaron en la placa el 5 de noviembre con la imagen `5.0.9-19`; el 34 (pulsar "Reiniciar pantalla" en el editor) se ejecutó el 5 de noviembre durante la validación de la pantalla (cerró el programa en código 0 y pantalla en negro). Los pasos 37 a 42 (pase de lista, imagen `5.0.9-20`) se ejecutaron en la placa el 5 de noviembre de 2026, con la XIAO y la visión apagadas; el 43 (`reconocido` → asistencia) **no se ha ejecutado**: necesita la visión real. Se corren con el servicio de visión y la unidad de la pantalla apagados (con su usuario en marcha, `vision` y `pantalla` se expulsarían). Los pasos con cuentas usan las de prueba del grupo `PRUEBA`. El paso 25 es obligatorio al terminar los de registro: ningún dato de prueba debe quedar con consentimiento o rostro; el 36 dice qué queda de los pasos de sesiones. **Antes de cualquier comando `docker compose` con este compose, `orangepi/.env` debe tener `NODERED_PROFESOR_USER`, `NODERED_PROFESOR_PASSWORD` y `GALERIA_TOKEN`** (`:?` en el compose: sin ellas ni `ps` ni `config -q` funcionan). Node-RED tampoco arranca si `NODERED_PROFESOR_PASSWORD`, `GALERIA_TOKEN` o `NODERED_ADMIN_PASSWORD` contienen `cambiar_esta_clave`, si dos de esos tres son iguales, si `GALERIA_TOKEN` tiene menos de 32 caracteres, si `NODERED_PROFESOR_PASSWORD` tiene menos de 16 o si alguno de esos dos tiene menos de 8 caracteres distintos; `openssl rand -hex 16` cumple todo (`orangepi/.env.example`).

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
| 26 | El retiro tardío va atado a la fecha del consentimiento | Con la cuenta 999000001 sin consentimiento ni rostro (paso 25 en 0), el bloque del paso 26 de abajo. Corre en una transacción con `ROLLBACK`, como superusuario, y cambia al rol de Node-RED con `SET LOCAL ROLE`: no deja datos. El `psql` lleva `-q`, así que no salen las etiquetas de comando (`UPDATE 0`, `SET`, `ROLLBACK`). | La salida completa son dos líneas: `t` (`formato_ok`: la fecha que devuelve el `UPDATE` del embedding es `…\.\d{6}Z`) y `999000001`, del retiro con la fecha correcta. El retiro con la fecha +1 µs y el segundo retiro dan 0 filas y no imprimen nada. |
| 27 | La pestaña `Sesiones` cargó, el rol puede escribir lo que necesita y la imagen trae Python | Tras `docker compose up -d --no-deps --build nodered` (etiqueta `5.0.9-20`): `docker compose logs --tail=50 nodered` y los comandos de privilegios y de Python del paso 27 de abajo. Si los privilegios no salen como se espera, aplica `02-roles.sh` como dice [orangepi/README.md](README.md#volúmenes-existentes) (no se amplía el rol a mano). | Sin errores de nodos; unos segundos después del arranque aparecen `Conectado al broker: se reconcilia aula/sesion/estado con la base` y `Estado de la sesión reconciliado con la base`. Privilegios: `t\|t\|f\|f\|t` (INSERT, UPDATE de `fin`, sin UPDATE de `materia`, sin DELETE, secuencia). Python: `Python 3.14.10` (la versión que la imagen traía el 5 de noviembre; si cambió, basta con que sea Python 3 reciente) y `-rw-r--r-- 1 root root … /etc/nodered/reporte_csv.py`. |
| 28 | Iniciar una sesión escribe en la base y publica el estado retenido | Los comandos del paso 28 de abajo, en orden: publicar `iniciar` como `pantalla`, esperar 2 s, leer el retenido como `pantalla` y consultar `sesiones` como superusuario. Si la lectura no trae nada, repítela: Node-RED escribe en la base antes de publicar. | El retenido es `{"activa":true,"id":N,"materia":"PRUEBA","grupo":"PRUEBA","inicio":"…+00:00","tol_min":10}`. `sesiones` tiene una fila con `abierta = t` y esa `id`. |
| 29 | Iniciar otra vez cierra la anterior y genera su reporte; un segundo `iniciar` en menos de 5 s se descarta | Con la sesión del paso 28 abierta y **pasados al menos 5 s** desde ese `iniciar`: publica el `iniciar` **dos veces seguidas** (la segunda llega a menos de 5 s de la primera) y espera 3 s (la cola de reportes deja pasar 1 cada 2 s); después lee el retenido, consulta `sesiones` (comandos 2 y 3 del paso 28) y lista los reportes (comandos del paso 29 y el listado de abajo). | Una sola fila `abierta = t` (la nueva, `id` N+1) y ninguna sesión N+2: la segunda orden se descartó y el log trae `Descartado: un iniciar llegó a menos de 5 s del anterior`. La anterior tiene `fin`. El retenido trae el id nuevo. Aparece `sesion-N.csv` (la sesión cerrada) en `/data/reportes` y el log trae `Iniciar cerró la sesión olvidada N`. Sin `.entrada-*` y sin errores `23505`. |
| 30 | Terminar cierra, publica y genera el CSV | `terminar` como `pantalla`, espera 3 s, el retenido, `sesiones`, el listado y el conteo de líneas de los reportes (sin imprimirlos): comandos del paso 30 de abajo. | El retenido es `{"activa":false,"id":N+1}`; la sesión tiene `fin` y no queda ninguna abierta. `sesion-<N+1>.csv` existe con permisos `-rw-------` y su conteo es 1 + (alumnos del grupo `PRUEBA`) líneas (4 con las 3 cuentas de prueba). No queda ningún `.entrada-*`. El log dice `Reporte de la sesión N+1 escrito en /data/reportes/sesion-N+1.csv`. No se imprimen nombres. |
| 31 | Terminar sin sesión abierta no cambia nada | Otro `terminar` con todo cerrado; cuenta de `sesiones` y de `/data/reportes` antes y después; el retenido. | Las cuentas no cambian y no hay un CSV nuevo. El retenido sigue siendo `{"activa":false,"id":N+1}` (Node-RED lo republicó desde la base; el log dice `Terminar sin sesión abierta`). |
| 32 | Un `iniciar` inválido se descarta sin publicar | Los cinco comandos de inválidos del paso 32 de abajo; antes, `max(id)` de `sesiones`, y después, el retenido y el log. | El `max(id)` no cambia y el retenido sigue igual. `docker compose logs --tail=50 nodered` trae un `Descartado, …` por caso (tol_min, materia con salto de línea, accion desconocida, `marcar` y JSON roto), sin repetir el contenido del mensaje. |
| 33 | Un retenido en `aula/sesion/cmd` se descarta | Los comandos del retenido del paso 33 de abajo, en orden: Node-RED **detenido** antes de publicar (si está conectado, el broker le entrega el retenido en vivo con `retain = false` y se acepta como una orden normal: el 5 nov así se abrió una sesión de más). | `max(id)` de `sesiones` no cambia. El log, tras el arranque, trae `Descartado mensaje retenido en aula/sesion/cmd` y `Estado de la sesión reconciliado con la base`. Al final se borra el retenido. |
| 34 | El reinicio de la pantalla se publica | El suscriptor del paso 34 de abajo, lanzado antes, en otra terminal, y pulsar el botón del nodo "Reiniciar pantalla" en el editor (`https://<ip>:1880/`, pestaña Sesiones). Después, el mismo suscriptor otra vez. | El primero imprime `aula/pantalla/reinicio {"reiniciar":1}`. El segundo, lanzado después, no imprime nada y termina por tiempo: no se publicó con retain. |
| 35 | La reconciliación restaura el retenido perdido | Los comandos del paso 35 de abajo: con una sesión abierta, detener Node-RED, borrar el retenido de `aula/sesion/estado` como `nodered`, comprobar que no hay nada, levantar Node-RED y leer otra vez. | Tras el arranque, el retenido vuelve a ser `{"activa":true,"id":M,…}` de la sesión abierta. El último comando del paso, `terminar`, deja la base sin sesión abierta. |
| 36 | Qué queda de los pasos de sesiones | La consulta del paso 36 de abajo, como superusuario. | Las sesiones con `materia = 'PRUEBA'` **quedan**: el rol de Node-RED no tiene `DELETE` sobre `sesiones` (`sql/roles.sh`), y los CSV quedan en `/data/reportes`. Borrarlas es un `DELETE` de superusuario y requiere la aprobación de una persona ([Prohibido sin aprobación humana](#prohibido-sin-aprobación-humana)): se confirma que no hay sesiones reales ni `asistencias` que las referencien y se borra `WHERE materia = 'PRUEBA'`. Los CSV de prueba se quitan por id con `docker compose exec -T nodered sh -c 'rm -f /data/reportes/sesion-<id>.csv'`. Mientras tanto, la consulta las lista y ninguna debe estar abierta. |
| 37 | La pestaña `Pase de lista` cargó y el rol puede escribir lo que necesita | Tras `docker compose up -d --no-deps --build nodered` (etiqueta `5.0.9-20`): `docker compose logs --tail=50 nodered` y el comando de privilegios del paso 37 de abajo. Si algún privilegio no sale como se espera, aplica `02-roles.sh` como dice [orangepi/README.md](README.md#volúmenes-existentes) (no se amplía el rol a mano). | Sin errores de nodos. Privilegios: `t\|t\|t\|t\|f\|f\|t\|t` (INSERT en `asistencias` e `intentos`, SELECT en `alumnos` y `sesiones`, sin UPDATE ni DELETE en `asistencias`, secuencias de ambas). |
| 38 | La sentencia del pase de lista es válida y aplica las reglas (solo SQL; no es evidencia de un pase de lista real) | El bloque del paso 38 de abajo, con las cuentas de prueba (999000001 en el grupo `PRUEBA`) y **sin ninguna sesión abierta**. Corre en una transacción con `ROLLBACK`, como el 26, toma la sentencia del propio `flows.json` y cambia al rol de Node-RED con `SET LOCAL ROLE`: no deja datos. Comprueba la sintaxis del `ON CONFLICT` dentro del CTE y los privilegios, que las pruebas de Node no pueden. Antes del `SET LOCAL ROLE`, el superusuario le da a 999000001 consentimiento y un rostro de relleno (el `CHECK` exige consentimiento si hay embedding) y se asegura de que 999000002 (otra cuenta de prueba del grupo `PRUEBA`, paso 30) no tenga ninguno; todo se revierte con el `ROLLBACK`. | Las salidas, en orden: `retardo`, `duplicado`, `fuera_de_grupo` (cuenta inexistente), `fuera_de_grupo` (cuenta del grupo sin rostro), `desconocido`, `imagen_insuficiente`, `dudoso`; los intentos de la sesión: `desconocido\|3` (las dos `fuera_de_grupo` y el desconocido), `duplicado\|1`, `imagen_insuficiente\|1`; la asistencia: `retardo\|1`; con una sesión nueva `presente`; y sin sesión abierta no imprime nada. |
| 39 | Un retenido en `aula/vision/resultado` se descarta | Los comandos del retenido del paso 39 de abajo, en orden, **sin sesión abierta** y con Node-RED **detenido** antes de publicar (publicado en vivo, el broker se lo entrega con `retain = false`, como en el 33). Antes y después, `SELECT count(*) FROM intentos`. | El log, tras el arranque, trae `Descartado mensaje retenido en aula/vision/resultado` (y no `Sin sesión abierta`). La cuenta de `intentos` no cambia. Al final se borra el retenido. La limpieza de retenidos de Node-RED también borra este tópico en cada conexión, pero llega después de la suscripción (el paso 33 lo vio así con `aula/sesion/cmd`); si el log no trae el descarte, la limpieza se adelantó y hay que repetir el paso. |
| 40 | Los resultados vencidos, de prueba, sin rostro, rotos o sin sesión no hacen nada (visión apagada, **sin sesión abierta**) | El comando del paso 40 de abajo; antes y después, `SELECT count(*) FROM intentos` y el log. | La cuenta de `intentos` no cambia. El log trae, en orden: `Descartado, ts a más de 60 s del reloj de Node-RED` (dos veces: el 1970 de reserva y el 2099), `Descartado, modo prueba ignorado …`, `Sin retroalimentación: la visión informó sin_rostro`, `Descartado, no es JSON` y `Sin sesión abierta: no se registra ni se manda retroalimentación (desconocido)`. Ninguna línea trae cuentas. |
| 41 | Con sesión abierta, desconocido, imagen insuficiente y dudoso mandan la retroalimentación y dejan su intento | `iniciar` como en el paso 28 (grupo `PRUEBA`), y el comando del paso 41 de abajo para `desconocido`, `imagen_insuficiente`, `dudoso` y `sin_rostro`. El suscriptor es `xiao` (solo `xiao` y `nodered` leen `aula/actuador/cmd`; `pantalla` no): **con la XIAO apagada**, porque el broker expulsaría a la real; antes de suscribirse, `docker compose logs mosquitto \| grep xiao \| tail -n 2`: el último evento debe ser una desconexión, o no debe haber ninguno desde el arranque. Es solo una pista (el log rota y no lista a los clientes conectados): **la condición real es la XIAO apagada, confirmada a mano**. Si se la expulsó, hay que reiniciarla para que republique `en_linea: true`. Si no se puede apagar, se omite el suscriptor y se comprueba el log (`Pase de lista: <resultado>`) y la consulta de `intentos`. | Los suscriptores imprimen `aula/actuador/cmd {"led":"rojo","buzzer":2}`, `aula/actuador/cmd {"led":"ambar"}` y `aula/actuador/cmd {"led":"ambar"}`; el de `sin_rostro` no imprime nada y termina por tiempo. Tras los cuatro, la consulta de `intentos` de la sesión da `desconocido\|1` e `imagen_insuficiente\|1` (el dudoso no se guarda). `asistencias`: 0 filas. |
| 42 | Qué queda de los pasos del pase de lista | La consulta del paso 42 de abajo, como superusuario, y `terminar` (paso 30) para cerrar la sesión. | No queda ninguna sesión abierta y ninguna asistencia de sesiones `PRUEBA` (0). Los `intentos` de esas sesiones **quedan**: el rol de Node-RED no tiene `DELETE`. Para borrar las sesiones `PRUEBA` (paso 36), un superusuario con aprobación humana borra antes sus `intentos` (`sesion_id` referencia a `sesiones`). |
| 43 | **Pendiente (como el 21):** `reconocido` → asistencia con la visión real | Con el servicio de visión encendido y el rostro de un integrante del equipo que haya dado su consentimiento (paso 24 con rostro real), una sesión abierta del grupo de esa persona y la unidad de la pantalla apagada: pararse frente a la cámara; repetir; y con otro integrante de un grupo distinto. Consultar `asistencias` de la sesión sin nombres (`SELECT estado, metodo, count(*) FROM asistencias WHERE sesion_id = <id> GROUP BY 1, 2`) y observar `aula/actuador/cmd` como en el 41. | La primera vez: una fila `presente` (o `retardo` si pasó la tolerancia) con `metodo = 'rostro'` y `{"servo":"abrir","led":"verde","buzzer":1}`. La segunda: sigue una fila, `{"led":"rojo","buzzer":2}` y un intento `duplicado`. El de otro grupo: sin fila nueva, rojo y un intento `desconocido`. Los datos reales de esta prueba se quedan en la base: coordinarlo con la persona que dio su consentimiento. |

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

Bloque del paso 26 (retiro tardío atado a la fecha). Corre como superusuario con `-v rol=...` tomado del entorno del contenedor (no imprime secretos) y termina en `ROLLBACK`, así que no deja datos. Pone el consentimiento como lo hace el `POST` del profesor, cambia al rol de Node-RED, ejecuta el `UPDATE` del embedding de "Validar embedding" con un vector generado (norma 1, 512 componentes) y prueba el retiro de "UPDATE retirar rostro tardío" con la fecha +1 µs, con la correcta y otra vez:

```bash
docker compose exec -T postgres sh -c 'psql -q -U "$POSTGRES_USER" -d "$POSTGRES_DB" -tA -v ON_ERROR_STOP=1 -v rol="$NODERED_DB_USER"' <<'SQL'
BEGIN;
UPDATE alumnos SET consentimiento = true, consentimiento_fecha = now() WHERE no_cuenta = '999000001' AND embedding IS NULL;
SET LOCAL ROLE :"rol";
UPDATE alumnos SET embedding = ('[' || array_to_string(array_fill(1/sqrt(512.0), ARRAY[512]), ',') || ']')::vector
WHERE no_cuenta = '999000001' AND consentimiento AND embedding IS NULL
RETURNING no_cuenta,
  to_char(consentimiento_fecha AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS.US"Z"') AS consentimiento_fecha
\gset f_
SELECT :'f_consentimiento_fecha' ~ '^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{6}Z$' AS formato_ok;
SELECT to_char((:'f_consentimiento_fecha')::timestamptz AT TIME ZONE 'UTC' + interval '1 microsecond',
               'YYYY-MM-DD"T"HH24:MI:SS.US"Z"') AS f_mas
\gset
PREPARE retiro(text, text) AS
  UPDATE alumnos SET embedding = NULL, consentimiento = false, consentimiento_fecha = NULL
  WHERE no_cuenta = $1 AND consentimiento_fecha = $2::timestamptz
  RETURNING no_cuenta;
EXECUTE retiro('999000001', :'f_mas');
EXECUTE retiro('999000001', :'f_consentimiento_fecha');
EXECUTE retiro('999000001', :'f_consentimiento_fecha');
ROLLBACK;
SQL
```

Comandos del paso 27 (privilegios del rol de Node-RED sobre `sesiones` y Python de la imagen; no imprimen secretos):

```bash
docker compose exec -T postgres sh -c "psql -U \"\$POSTGRES_USER\" -d \"\$POSTGRES_DB\" -tA -c \"SELECT has_table_privilege('\$NODERED_DB_USER', 'sesiones', 'INSERT'), has_column_privilege('\$NODERED_DB_USER', 'sesiones', 'fin', 'UPDATE'), has_column_privilege('\$NODERED_DB_USER', 'sesiones', 'materia', 'UPDATE'), has_table_privilege('\$NODERED_DB_USER', 'sesiones', 'DELETE'), has_sequence_privilege('\$NODERED_DB_USER', 'sesiones_id_seq', 'USAGE')\""
docker compose exec -T nodered sh -c '/usr/bin/python3 --version; ls -l /etc/nodered/reporte_csv.py'
```

Comandos del paso 28 (publican y leen como `pantalla`, que puede escribir `aula/sesion/cmd` y leer `aula/sesion/estado`; la contraseña va en el archivo del cliente y el directorio se borra sin `rm -rf`). El `iniciar` es el mismo que publica la pantalla, con el grupo de prueba:

```bash
# 1. Iniciar (el mismo comando se repite en el paso 29)
docker compose exec -T mosquitto sh -c 'd=$(mktemp -d /run/mosquitto/xdg.XXXXXX); printf -- "-u pantalla\n-P %s\n" "$MQTT_PANTALLA_PASSWORD" > "$d/mosquitto_pub"; XDG_CONFIG_HOME="$d" mosquitto_pub -h localhost -q 1 -t aula/sesion/cmd -m "{\"accion\":\"iniciar\",\"materia\":\"PRUEBA\",\"grupo\":\"PRUEBA\",\"tol_min\":10}"; rm -f "$d/mosquitto_pub"; rmdir "$d"'
# 2. Esperar a que Node-RED escriba y publique, y leer el retenido de estado (llega al suscribirse;
#    termina a los 5 s si no hay; si no trae nada, repite este comando)
sleep 2
docker compose exec -T mosquitto sh -c 'd=$(mktemp -d /run/mosquitto/xdg.XXXXXX); printf -- "-u pantalla\n-P %s\n" "$MQTT_PANTALLA_PASSWORD" > "$d/mosquitto_sub"; XDG_CONFIG_HOME="$d" mosquitto_sub -h localhost -t aula/sesion/estado -C 1 -W 5 -v; rm -f "$d/mosquitto_sub"; rmdir "$d"'
# 3. Las últimas sesiones, como superusuario (sin nombres de alumnos)
docker compose exec -T postgres sh -c 'psql -U "$POSTGRES_USER" -d "$POSTGRES_DB" -tA -c "SELECT id, materia, grupo, tolerancia_min, fin IS NULL AS abierta FROM sesiones ORDER BY id DESC LIMIT 3"'
```

Comandos del paso 29. Node-RED descarta un segundo `iniciar` que llegue a menos de 5 s del último aceptado (límite contra un goteo de órdenes y contra la reentrega duplicada de QoS 1): por eso se espera al menos 5 s desde el `iniciar` del paso 28 y los dos publicados aquí van seguidos (el primero se acepta, el segundo se descarta). Después se lee el retenido y se consulta `sesiones` (comandos 2 y 3 del paso 28) y se lista `/data/reportes`:

```bash
sleep 5
docker compose exec -T mosquitto sh -c 'd=$(mktemp -d /run/mosquitto/xdg.XXXXXX); printf -- "-u pantalla\n-P %s\n" "$MQTT_PANTALLA_PASSWORD" > "$d/mosquitto_pub"; XDG_CONFIG_HOME="$d" mosquitto_pub -h localhost -q 1 -t aula/sesion/cmd -m "{\"accion\":\"iniciar\",\"materia\":\"PRUEBA\",\"grupo\":\"PRUEBA\",\"tol_min\":10}"; rm -f "$d/mosquitto_pub"; rmdir "$d"'
docker compose exec -T mosquitto sh -c 'd=$(mktemp -d /run/mosquitto/xdg.XXXXXX); printf -- "-u pantalla\n-P %s\n" "$MQTT_PANTALLA_PASSWORD" > "$d/mosquitto_pub"; XDG_CONFIG_HOME="$d" mosquitto_pub -h localhost -q 1 -t aula/sesion/cmd -m "{\"accion\":\"iniciar\",\"materia\":\"PRUEBA\",\"grupo\":\"PRUEBA\",\"tol_min\":10}"; rm -f "$d/mosquitto_pub"; rmdir "$d"'
sleep 3
```

Listado de los reportes (pasos 29, 30 y 31). Muestra también los archivos ocultos, para ver si quedó algún `.entrada-*` (no debe quedar ninguno: el programa lo borra siempre). Solo nombres y permisos, nunca el contenido:

```bash
docker compose exec -T nodered sh -c 'ls -la /data/reportes'
```

Comandos del paso 30 (terminar, esperar a la cola de reportes y contar las líneas de cada reporte sin imprimirlos; `wc` imprime solo conteos y los nombres `sesion-<id>.csv`):

```bash
docker compose exec -T mosquitto sh -c 'd=$(mktemp -d /run/mosquitto/xdg.XXXXXX); printf -- "-u pantalla\n-P %s\n" "$MQTT_PANTALLA_PASSWORD" > "$d/mosquitto_pub"; XDG_CONFIG_HOME="$d" mosquitto_pub -h localhost -q 1 -t aula/sesion/cmd -m "{\"accion\":\"terminar\"}"; rm -f "$d/mosquitto_pub"; rmdir "$d"'
sleep 3
docker compose exec -T nodered sh -c 'wc -l /data/reportes/*.csv'
docker compose logs --tail=50 nodered
```

Comandos del paso 32 (cinco `iniciar` inválidos o no soportados; ninguno debe cambiar `sesiones` ni el retenido). Anota antes `SELECT max(id) FROM sesiones` y compara después:

```bash
docker compose exec -T mosquitto sh -c 'd=$(mktemp -d /run/mosquitto/xdg.XXXXXX); printf -- "-u pantalla\n-P %s\n" "$MQTT_PANTALLA_PASSWORD" > "$d/mosquitto_pub"; XDG_CONFIG_HOME="$d" mosquitto_pub -h localhost -q 1 -t aula/sesion/cmd -m "{\"accion\":\"iniciar\",\"materia\":\"PRUEBA\",\"grupo\":\"PRUEBA\",\"tol_min\":601}"; rm -f "$d/mosquitto_pub"; rmdir "$d"'
docker compose exec -T mosquitto sh -c 'd=$(mktemp -d /run/mosquitto/xdg.XXXXXX); printf -- "-u pantalla\n-P %s\n" "$MQTT_PANTALLA_PASSWORD" > "$d/mosquitto_pub"; XDG_CONFIG_HOME="$d" mosquitto_pub -h localhost -q 1 -t aula/sesion/cmd -m "{\"accion\":\"iniciar\",\"materia\":\"PRU\nEBA\",\"grupo\":\"PRUEBA\",\"tol_min\":10}"; rm -f "$d/mosquitto_pub"; rmdir "$d"'
docker compose exec -T mosquitto sh -c 'd=$(mktemp -d /run/mosquitto/xdg.XXXXXX); printf -- "-u pantalla\n-P %s\n" "$MQTT_PANTALLA_PASSWORD" > "$d/mosquitto_pub"; XDG_CONFIG_HOME="$d" mosquitto_pub -h localhost -q 1 -t aula/sesion/cmd -m "{\"accion\":\"pausar\"}"; rm -f "$d/mosquitto_pub"; rmdir "$d"'
docker compose exec -T mosquitto sh -c 'd=$(mktemp -d /run/mosquitto/xdg.XXXXXX); printf -- "-u pantalla\n-P %s\n" "$MQTT_PANTALLA_PASSWORD" > "$d/mosquitto_pub"; XDG_CONFIG_HOME="$d" mosquitto_pub -h localhost -q 1 -t aula/sesion/cmd -m "{\"accion\":\"marcar\",\"cuenta\":\"999000001\"}"; rm -f "$d/mosquitto_pub"; rmdir "$d"'
docker compose exec -T mosquitto sh -c 'd=$(mktemp -d /run/mosquitto/xdg.XXXXXX); printf -- "-u pantalla\n-P %s\n" "$MQTT_PANTALLA_PASSWORD" > "$d/mosquitto_pub"; XDG_CONFIG_HOME="$d" mosquitto_pub -h localhost -q 1 -t aula/sesion/cmd -m "{\"accion\":\"iniciar\",\"materia\":\"PRUEBA\""; rm -f "$d/mosquitto_pub"; rmdir "$d"'
docker compose logs --tail=50 nodered
```

El retenido del paso 33. A diferencia del paso 19, aquí el mensaje sí tendría efecto en vivo, así que se publica con Node-RED **detenido**: un retenido publicado mientras Node-RED está conectado le llega con `retain = false` (MQTT 3.1.1) y se ejecuta. Se publica como `pantalla` un `iniciar` válido con `-r`; al arrancar Node-RED, el broker se lo entrega con la bandera retain (el log lo descarta); al final se borra para que no se repita en cada conexión. Antes y después, `SELECT max(id) FROM sesiones` no cambia:

```bash
# 1. Detener Node-RED y publicar el retenido (queda guardado en el broker)
docker compose stop nodered
docker compose exec -T mosquitto sh -c 'd=$(mktemp -d /run/mosquitto/xdg.XXXXXX); printf -- "-u pantalla\n-P %s\n" "$MQTT_PANTALLA_PASSWORD" > "$d/mosquitto_pub"; XDG_CONFIG_HOME="$d" mosquitto_pub -h localhost -q 1 -r -t aula/sesion/cmd -m "{\"accion\":\"iniciar\",\"materia\":\"PRUEBA\",\"grupo\":\"PRUEBA\",\"tol_min\":10}"; rm -f "$d/mosquitto_pub"; rmdir "$d"'
# 2. Levantar Node-RED, esperar a `healthy` (hasta 60 s) y leer el log
docker compose start nodered
docker compose ps
docker compose logs --tail=50 nodered
# 3. Borrar el retenido (-r -n: retenido vacío; `pantalla` puede escribir ese tópico)
docker compose exec -T mosquitto sh -c 'd=$(mktemp -d /run/mosquitto/xdg.XXXXXX); printf -- "-u pantalla\n-P %s\n" "$MQTT_PANTALLA_PASSWORD" > "$d/mosquitto_pub"; XDG_CONFIG_HOME="$d" mosquitto_pub -h localhost -q 1 -r -n -t aula/sesion/cmd; rm -f "$d/mosquitto_pub"; rmdir "$d"'
```

Suscriptor del paso 34 (como `pantalla`, que puede leer `aula/pantalla/reinicio`; la unidad de la pantalla debe estar apagada). Se lanza **antes** de pulsar el botón del nodo "Reiniciar pantalla" en el editor; el segundo uso, con `-W 3` y lanzado **después** de pulsarlo, no debe imprimir nada:

```bash
docker compose exec -T mosquitto sh -c 'd=$(mktemp -d /run/mosquitto/xdg.XXXXXX); printf -- "-u pantalla\n-P %s\n" "$MQTT_PANTALLA_PASSWORD" > "$d/mosquitto_sub"; XDG_CONFIG_HOME="$d" mosquitto_sub -h localhost -t aula/pantalla/reinicio -W 60 -C 1 -v; rm -f "$d/mosquitto_sub"; rmdir "$d"'
```

Comandos del paso 35 (con una sesión abierta por el `iniciar` del paso 28; el broker pierde el retenido de estado y Node-RED lo reconstruye al conectar). Con Node-RED detenido, `nodered` puede publicar sin expulsar a nadie, como en el paso 10:

```bash
docker compose stop nodered
docker compose exec -T mosquitto sh -c 'd=$(mktemp -d /run/mosquitto/xdg.XXXXXX); printf -- "-u nodered\n-P %s\n" "$MQTT_NODERED_PASSWORD" > "$d/mosquitto_pub"; XDG_CONFIG_HOME="$d" mosquitto_pub -h localhost -q 1 -r -n -t aula/sesion/estado; rm -f "$d/mosquitto_pub"; rmdir "$d"'
# Comprobar que no queda nada (no imprime nada y termina a los 3 s)
docker compose exec -T mosquitto sh -c 'd=$(mktemp -d /run/mosquitto/xdg.XXXXXX); printf -- "-u pantalla\n-P %s\n" "$MQTT_PANTALLA_PASSWORD" > "$d/mosquitto_sub"; XDG_CONFIG_HOME="$d" mosquitto_sub -h localhost -t aula/sesion/estado -W 3 -v; rm -f "$d/mosquitto_sub"; rmdir "$d"'
docker compose start nodered
# Esperar a `healthy` (hasta 60 s) y leer el retenido (comando 2 del paso 28): debe volver a ser la sesión abierta
docker compose ps
# Cerrar la sesión con el `terminar` del paso 30
```

Consulta del paso 36 (sesiones de prueba que quedan, sin nombres de alumnos):

```bash
docker compose exec -T postgres sh -c 'psql -U "$POSTGRES_USER" -d "$POSTGRES_DB" -tA -c "SELECT id, grupo, fin IS NULL AS abierta FROM sesiones WHERE materia = '"'"'PRUEBA'"'"' ORDER BY id"'
```

Privilegios del paso 37 (el rol sale de `NODERED_DB_USER`, expandido dentro del contenedor; no imprime secretos):

```bash
docker compose exec -T postgres sh -c "psql -U \"\$POSTGRES_USER\" -d \"\$POSTGRES_DB\" -tA -c \"SELECT has_table_privilege('\$NODERED_DB_USER', 'asistencias', 'INSERT'), has_table_privilege('\$NODERED_DB_USER', 'intentos', 'INSERT'), has_table_privilege('\$NODERED_DB_USER', 'alumnos', 'SELECT'), has_table_privilege('\$NODERED_DB_USER', 'sesiones', 'SELECT'), has_table_privilege('\$NODERED_DB_USER', 'asistencias', 'UPDATE'), has_table_privilege('\$NODERED_DB_USER', 'asistencias', 'DELETE'), has_sequence_privilege('\$NODERED_DB_USER', 'asistencias_id_seq', 'USAGE'), has_sequence_privilege('\$NODERED_DB_USER', 'intentos_id_seq', 'USAGE')\""
```

Bloque del paso 38 (desde `orangepi/`, con las cuentas de prueba 999000001 y 999000002 del grupo `PRUEBA` y sin sesión abierta: el índice `sesiones_una_abierta_idx` rechazaría la sesión de prueba y `ON_ERROR_STOP` la revertiría). `jq` saca la sentencia de `flows.json`, así que la prueba nunca se desfasa del flujo. Va como superusuario con `-v rol=...` del entorno del contenedor y termina en `ROLLBACK`: no deja datos. Antes del `SET LOCAL ROLE`, el superusuario hace los `UPDATE` que dejan a 999000001 con rostro y consentimiento vigentes y a 999000002 sin ninguno (el rol de Node-RED no podría, y el `ROLLBACK` los revierte). Una sesión que empezó hace 11 minutos con tolerancia de 10 da `retardo`; la segunda, recién abierta, da `presente`. Los `\echo` rotulan cada salida (el `psql` lleva `-q`, así que no salen las etiquetas de comando):

```bash
Q=$(jq -r '.[] | select(.id == "a1f0c0de000000b4") | .query' ../nodered/flows/flows.json)
{
cat <<'SQL'
BEGIN;
INSERT INTO sesiones (materia, grupo, inicio, tolerancia_min) VALUES ('PRUEBA', 'PRUEBA', now() - interval '11 minutes', 10);
UPDATE alumnos SET consentimiento = true, consentimiento_fecha = now(), embedding = ('[' || array_to_string(array_fill(1/sqrt(512.0), ARRAY[512]), ',') || ']')::vector WHERE no_cuenta = '999000001' AND embedding IS NULL;
UPDATE alumnos SET embedding = NULL, consentimiento = false, consentimiento_fecha = NULL WHERE no_cuenta = '999000002';
SET LOCAL ROLE :"rol";
SQL
printf 'PREPARE pase(text, text, real) AS\n%s;\n' "$Q"
cat <<'SQL'
\echo 1. reconocido del grupo, 11 min después de iniciar con tolerancia 10 (retardo)
EXECUTE pase('reconocido', '999000001', 0.9);
\echo 2. el mismo otra vez (duplicado)
EXECUTE pase('reconocido', '999000001', 0.9);
\echo 3. reconocido que no es del grupo (fuera_de_grupo)
EXECUTE pase('reconocido', '999000099', 0.9);
\echo 4. reconocido de una cuenta del grupo sin rostro ni consentimiento (fuera_de_grupo)
EXECUTE pase('reconocido', '999000002', 0.9);
\echo 5. desconocido (desconocido)
EXECUTE pase('desconocido', NULL, 0.3);
\echo 6. imagen_insuficiente (imagen_insuficiente)
EXECUTE pase('imagen_insuficiente', NULL, NULL);
\echo 7. dudoso (dudoso, no escribe)
EXECUTE pase('dudoso', '999000001', 0.6);
\echo intentos de la sesion (desconocido|3, duplicado|1, imagen_insuficiente|1)
SELECT motivo, count(*) FROM intentos WHERE sesion_id = (SELECT id FROM sesiones WHERE fin IS NULL) GROUP BY motivo ORDER BY motivo;
\echo asistencias de la sesion (retardo|1)
SELECT estado, count(*) FROM asistencias WHERE sesion_id = (SELECT id FROM sesiones WHERE fin IS NULL) GROUP BY estado;
RESET ROLE;
UPDATE sesiones SET fin = now() WHERE fin IS NULL;
INSERT INTO sesiones (materia, grupo, tolerancia_min) VALUES ('PRUEBA', 'PRUEBA', 10);
SET LOCAL ROLE :"rol";
\echo 8. sesion recien abierta, el mismo alumno (presente)
EXECUTE pase('reconocido', '999000001', 0.9);
RESET ROLE;
UPDATE sesiones SET fin = now() WHERE fin IS NULL;
SET LOCAL ROLE :"rol";
\echo 9. sin sesion abierta (no imprime nada)
EXECUTE pase('desconocido', NULL, 0.3);
ROLLBACK;
SQL
} | docker compose exec -T postgres sh -c 'psql -q -U "$POSTGRES_USER" -d "$POSTGRES_DB" -tA -v ON_ERROR_STOP=1 -v rol="$NODERED_DB_USER"'
```

El retenido del paso 39. Se publica como `vision` (la visión, apagada) con Node-RED **detenido**: un retenido publicado mientras Node-RED está conectado le llega con `retain = false` (MQTT 3.1.1) y se procesaría como un resultado normal, como pasó con el paso 33. El resultado es un `desconocido` válido y con `ts` reciente (si el descarte por retain fallara, con una sesión abierta quedaría un intento; sin sesión, el log diría `Sin sesión abierta` en vez de `Descartado mensaje retenido`). La pestaña `Limpieza de retenidos` también lo borra en cada conexión de Node-RED, pero el borrado manual del comando 3 se conserva: es idempotente y no hace depender el paso de esa pestaña (solo `nodered` puede leer este tópico, así que nadie más puede comprobar que quedó vacío):

```bash
# 0. Cuenta de intentos antes (y después del paso 3)
docker compose exec -T postgres sh -c 'psql -U "$POSTGRES_USER" -d "$POSTGRES_DB" -tA -c "SELECT count(*) FROM intentos"'
# 1. Detener Node-RED y publicar el retenido (queda guardado en el broker)
docker compose stop nodered
docker compose exec -T mosquitto sh -c 'd=$(mktemp -d /run/mosquitto/xdg.XXXXXX); printf -- "-u vision\n-P %s\n" "$MQTT_VISION_PASSWORD" > "$d/mosquitto_pub"; XDG_CONFIG_HOME="$d" mosquitto_pub -h localhost -q 1 -r -t aula/vision/resultado -m "{\"ts\":\"$(date -u +%Y-%m-%dT%H:%M:%SZ)\",\"modo\":\"pase\",\"decision\":\"desconocido\",\"cuenta\":null,\"conf\":null,\"lat_ms\":{\"cap\":1,\"det\":1,\"emb\":1,\"total\":3}}"; rm -f "$d/mosquitto_pub"; rmdir "$d"'
# 2. Levantar Node-RED, esperar a `healthy` (hasta 60 s) y leer el log
docker compose start nodered
docker compose ps
docker compose logs --tail=50 nodered
# 3. Borrar el retenido (-r -n: retenido vacío; `vision` puede escribir ese tópico)
docker compose exec -T mosquitto sh -c 'd=$(mktemp -d /run/mosquitto/xdg.XXXXXX); printf -- "-u vision\n-P %s\n" "$MQTT_VISION_PASSWORD" > "$d/mosquitto_pub"; XDG_CONFIG_HOME="$d" mosquitto_pub -h localhost -q 1 -r -n -t aula/vision/resultado; rm -f "$d/mosquitto_pub"; rmdir "$d"'
```

Comando del paso 40 (seis resultados como `vision`, con la visión apagada y sin sesión abierta; `$ahora` es la hora del contenedor, en UTC, el mismo reloj que usa Node-RED). Ninguno debe cambiar `intentos`. Después, `docker compose logs --tail=50 nodered`:

```bash
docker compose exec -T mosquitto sh -c 'd=$(mktemp -d /run/mosquitto/xdg.XXXXXX); printf -- "-u vision\n-P %s\n" "$MQTT_VISION_PASSWORD" > "$d/mosquitto_pub"; ahora=$(date -u +%Y-%m-%dT%H:%M:%SZ); lat="{\"cap\":1,\"det\":1,\"emb\":1,\"total\":3}"; pub() { XDG_CONFIG_HOME="$d" mosquitto_pub -h localhost -q 1 -t aula/vision/resultado -m "$1"; }; pub "{\"ts\":\"1970-01-01T00:00:00.000+00:00\",\"modo\":\"pase\",\"decision\":\"desconocido\",\"cuenta\":null,\"conf\":null,\"lat_ms\":$lat}"; pub "{\"ts\":\"2099-01-01T00:00:00Z\",\"modo\":\"pase\",\"decision\":\"desconocido\",\"cuenta\":null,\"conf\":null,\"lat_ms\":$lat}"; pub "{\"ts\":\"$ahora\",\"modo\":\"prueba\",\"decision\":\"desconocido\",\"cuenta\":null,\"conf\":null,\"lat_ms\":$lat}"; pub "{\"ts\":\"$ahora\",\"modo\":\"pase\",\"decision\":\"sin_rostro\",\"cuenta\":null,\"conf\":null,\"lat_ms\":$lat}"; pub "{\"ts\":\"$ahora\",\"modo\":\"pase\""; pub "{\"ts\":\"$ahora\",\"modo\":\"pase\",\"decision\":\"desconocido\",\"cuenta\":null,\"conf\":null,\"lat_ms\":$lat}"; rm -f "$d/mosquitto_pub"; rmdir "$d"'
```

Comando del paso 41 (con la sesión del `iniciar` del paso 28 abierta, la XIAO apagada y la visión apagada). Se lanza una vez por decisión: `desconocido`, `imagen_insuficiente`, `dudoso` y `sin_rostro`. El suscriptor (`xiao`) arranca antes que el publicador (`vision`), imprime el primer mensaje de `aula/actuador/cmd` y termina; si no llega ninguno, termina a los 8 s sin imprimir nada. Con `xiao` conectado el broker expulsaría a la XIAO real, y con la XIAO en marcha este paso no se hace. Antes, desde `orangepi/`, el comando de abajo debe mostrar como último evento una desconexión de `xiao`, o no mostrar nada si no hubo ninguno desde el arranque. Es solo una pista: el log rota y no lista a los clientes conectados, así que la condición real es la XIAO apagada, confirmada a mano. Si la XIAO llegó a ser expulsada, reiniciarla para que republique `en_linea: true`:

```bash
docker compose logs mosquitto | grep xiao | tail -n 2
```

Y entonces el comando:

```bash
docker compose exec -T mosquitto sh -c 'd=$(mktemp -d /run/mosquitto/xdg.XXXXXX); printf -- "-u xiao\n-P %s\n" "$MQTT_XIAO_PASSWORD" > "$d/mosquitto_sub"; printf -- "-u vision\n-P %s\n" "$MQTT_VISION_PASSWORD" > "$d/mosquitto_pub"; XDG_CONFIG_HOME="$d" mosquitto_sub -h localhost -t aula/actuador/cmd -C 1 -W 8 -v & sleep 1; XDG_CONFIG_HOME="$d" mosquitto_pub -h localhost -q 1 -t aula/vision/resultado -m "{\"ts\":\"$(date -u +%Y-%m-%dT%H:%M:%SZ)\",\"modo\":\"pase\",\"decision\":\"$1\",\"cuenta\":null,\"conf\":null,\"lat_ms\":{\"cap\":1,\"det\":1,\"emb\":1,\"total\":3}}"; wait; rm -f "$d/mosquitto_sub" "$d/mosquitto_pub"; rmdir "$d"' sh desconocido
```

Los intentos de la sesión de prueba abierta (pasos 41 y 43; sin cuentas ni nombres):

```bash
docker compose exec -T postgres sh -c 'psql -U "$POSTGRES_USER" -d "$POSTGRES_DB" -tA -c "SELECT i.motivo, count(*) FROM intentos i WHERE i.sesion_id = (SELECT max(id) FROM sesiones WHERE materia = '"'"'PRUEBA'"'"') GROUP BY i.motivo ORDER BY i.motivo"'
```

Consulta del paso 42 (qué queda de los pasos del pase de lista; sin cuentas ni nombres):

```bash
docker compose exec -T postgres sh -c 'psql -U "$POSTGRES_USER" -d "$POSTGRES_DB" -tA -c "SELECT (SELECT count(*) FROM asistencias a JOIN sesiones s ON s.id = a.sesion_id WHERE s.materia = '"'"'PRUEBA'"'"') AS asistencias_prueba, (SELECT count(*) FROM intentos i JOIN sesiones s ON s.id = i.sesion_id WHERE s.materia = '"'"'PRUEBA'"'"') AS intentos_prueba, (SELECT count(*) FROM sesiones WHERE fin IS NULL) AS abiertas"'
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
- `sesiones`: el rol de Node-RED tiene `SELECT`, `INSERT` y `UPDATE (fin)`, y la secuencia; no puede cambiar `materia`, `grupo`, `inicio` ni `tolerancia_min`, y no tiene DELETE (`sql/roles.sh`). Iniciar es una sola sentencia con CTE que cierra la sesión abierta y hace el `INSERT` (plan, sección 5.3); `sesiones_una_abierta_idx` rechaza (23505) a la segunda de dos que lleguen a la vez, y el flujo entonces publica el estado de la base. Un volumen creado con el `UPDATE` de tabla completa necesita `02-roles.sh` para quedar con `UPDATE (fin)` ([orangepi/README.md](README.md#volúmenes-existentes)); sin aplicarlo, los flujos funcionan igual. Las sesiones de prueba de la verificación (`materia = 'PRUEBA'`) las borra solo un superusuario con aprobación humana (paso 36).
- Corregir una asistencia ya registrada lo hace solo el superusuario: el rol de Node-RED tiene SELECT e INSERT sobre `asistencias` e `intentos` (el pase de lista escribe ahí: `nodered/AGENTS.md`, sección Pase de lista). Borrar la identidad completa de una persona (`nombre`, `no_cuenta`) también es un procedimiento de superusuario, porque `asistencias` referencia la fila.
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
