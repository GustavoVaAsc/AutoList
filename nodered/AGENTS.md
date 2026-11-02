# Instrucciones para agentes: Node-RED

Antes de editar, lee el [AGENTS.md de la raíz](../AGENTS.md). Para el stack que rodea a Node-RED (broker, Postgres y compose), lee [orangepi/AGENTS.md](../orangepi/AGENTS.md).

## Contenido

- [Archivos](#archivos)
- [Flujos](#flujos)
- [Settings e imagen](#settings-e-imagen)
- [Credenciales](#credenciales)
- [SQL](#sql)
- [Validación de payloads](#validación-de-payloads)
- [Nodo exec](#nodo-exec)
- [API HTTP](#api-http)
- [Verificación](#verificación)
- [Ver también](#ver-también)

## Archivos

| Archivo | Función | ¿Va dentro de la imagen? |
|---|---|---|
| `flows/flows.json` | Flujos de Node-RED. Es la única ruta de `nodered/` que el compose monta en el contenedor (`/flows`). El volumen `/data` es aparte y no es de este repo. | No. Se monta (`orangepi/docker-compose.yml`, servicio `nodered`). |
| `settings.js` | Ajustes: `adminAuth`, HTTPS, credenciales MQTT, bloqueo de módulos, `httpStatic` (el frontend en `/app/`, con CSP solo en esa ruta) y cabeceras de seguridad: `nosniff` y `X-Frame-Options: DENY` en el editor y la API (`httpAdminMiddleware` y `httpNodeMiddleware`). | Sí, en `/etc/nodered/settings.js`. |
| `package.json`, `package-lock.json` | Versiones de Node-RED (5.0.9) y de `node-red-contrib-postgresql` (0.16.2). | Sí, para `npm ci`. |
| `Dockerfile` | Receta de la imagen `pase-lista/nodered`, en dos etapas: la primera compila `web/` con Node 22 y la final copia solo su `dist/` a `/usr/share/nodered-web`. | No. Lo usa `docker compose up -d --build`. |
| `.dockerignore` | Limita el contexto del build a `settings.js`, `package.json` y `package-lock.json`. | No aplica. |

## Flujos

- Cada Deploy del editor reescribe `nodered/flows/flows.json`. Antes de commitear, revisa `git diff -- nodered/flows/flows.json`: busca cambios que no hiciste en el editor (`orangepi/README.md`, sección Desplegar flujos).
- Exporta los cambios de flujos al repo después de cada cambio (plan, sección 11).
- Los respaldos y temporales que Node-RED crea junto a `flows.json` están en `.gitignore`. No los agregues al repo.
- Los ids de nodos son estables. El id del broker MQTT (`a1f0c0de00000002`) lo referencia `settings.js` (ver [Credenciales](#credenciales)).
- Si editas el JSON a mano, valida que siga siendo JSON antes de desplegar.
- La pestaña "Limpieza de retenidos" (`a1f0c0de00000030`) publica, en cada conexión de Node-RED al broker (lo detecta un nodo `status` sobre su `mqtt out`; sirve también tras reiniciar Mosquitto), un retenido vacío en `aula/actuador/cmd`, `aula/sesion/cmd`, `aula/vision/cmd`, `aula/camara/pir` y `aula/camara/energia`, con el broker "Mosquitto" y sin credenciales propias. Eso provoca en cada conexión un mensaje vacío en vivo (sin retain) en cada uno de esos tópicos, que los consumidores ignoran por vacío (contrato MQTT, sección 1). Borra restos de órdenes y lecturas retenidas: la ACL no puede impedir el retain. La función "Validar v, i, p" descarta los mensajes de `aula/camara/energia` con retain (con aviso) y los de payload vacío (en silencio). Los flujos del 11 nov deben descartar retain en los demás tópicos con Retain = no (`aula/vision/resultado`, `aula/vision/embedding`, `aula/vision/enrolamiento`, `aula/vision/galeria/alta`, `aula/vision/galeria/baja`, `aula/pantalla/reinicio` y `aula/sistema/metricas`), o se amplía la lista de la limpieza. No quites nada de esto sin decisión humana.

## Settings e imagen

- Cambiar `settings.js`, `Dockerfile`, `package.json`, `package-lock.json` o cualquier archivo de `web/` exige reconstruir la imagen desde `orangepi/`: `docker compose up -d --build`.
- Antes de reconstruir, sube el sufijo de la etiqueta en la línea `image:` del servicio `nodered` de `orangepi/docker-compose.yml`: de `5.0.9-11` a `-12`, `-13` y así sucesivamente. El comentario del compose sobre la etiqueta de Node-RED lo pide.
- `settings.js` vive en `/etc/nodered`, un directorio de root que el contenedor no puede modificar (comentarios del `Dockerfile`). No lo muevas a `flows/` ni a `/data`.
- Node-RED no instala módulos. `settings.js` bloquea la instalación desde la paleta y desde los nodos function (bloque `externalModules` y `functionExternalModules`). Las dependencias van en `package.json` y `package-lock.json`.
- La imagen instala con `npm ci --omit=dev --ignore-scripts` (instrucción `RUN` del `Dockerfile`). Ninguna dependencia necesita scripts de instalación.
- Para subir una versión, regenera el lock con `npm install --package-lock-only --omit=dev --ignore-scripts` (comentario del `Dockerfile`).
- El frontend se compila en la imagen: `web/` entra al build como contexto adicional (`additional_contexts` del servicio `nodered`), y el contexto principal sigue siendo `nodered/`. No cambies el contexto a la raíz del repo: metería `orangepi/.env` en él. `/usr/share/nodered-web` es de root, igual que `/etc/nodered`.
- `httpStatic` sirve el frontend en `/app/`; el editor sigue en `/`. Vite compila con `base: '/app/'` (`web/vite.config.ts`): si cambias uno, cambia el otro.
- Las imágenes base (`nodered/node-red` y `node`) están fijadas por digest. Para actualizarlas, cambia versión y digest a la vez (líneas `FROM` del `Dockerfile`).

## Credenciales

- Nunca pongas credenciales en `flows.json`. Llegan como variables de entorno desde `orangepi/.env`, a través del compose.
- Postgres: el nodo de configuración usa campos de tipo `env`. El usuario sale de `NODERED_DB_USER` y la contraseña de `NODERED_DB_PASSWORD`. El nombre de la base es `${POSTGRES_DB}` (nodo "Postgres" en `flows.json`).
- MQTT: Node-RED se conecta como el usuario `nodered` (contrato MQTT, sección 6). `settings.js` escribe en `/data/flows_cred.json` el usuario `nodered` y el texto `${MQTT_NODERED_PASSWORD}` para el broker "Mosquitto". Node-RED sustituye el texto por la variable de entorno. Nunca escribas el valor real. El broker usa el usuario como client id (`use_username_as_clientid`), así que el `clientid` del nodo en `flows.json` se ignora, y otro cliente con el usuario `nodered` expulsaría a Node-RED.
- `credentialSecret: false` solo es seguro mientras las credenciales sean referencias `${VAR}`, como indica el comentario junto a esa opción en `settings.js`.
- El id del broker en `ID_BROKER_MQTT` (`settings.js`) debe coincidir con el id del nodo "Mosquitto" en `flows.json`. Si cambias uno, cambia el otro en el mismo cambio.
- El editor usa `NODERED_ADMIN_USER` y `NODERED_ADMIN_PASSWORD`. Sin ellas, `settings.js` no arranca (la excepción al inicio del archivo).

## SQL

- Toda consulta va parametrizada: `$1`, `$2` y así sucesivamente en el campo `query`, con los valores en `msg.params`. Ejemplo del flujo Energía:

  ```sql
  INSERT INTO metricas_energia (origen, v, i, p) VALUES ('ina219', $1, $2, $3)
  ```

  La función anterior al nodo fija `msg.params = [datos.v, datos.i, datos.p];`, con valores ya validados.

- Nunca concatenes en `query` valores que llegan por MQTT o HTTP. Los embeddings viajan siempre como parámetros enlazados (`$1::vector`).
- Los nodos catch y debug de las rutas de embeddings (`aula/vision/embedding`, `/api/galeria`, registro) registran solo `msg.error.message`, nunca el mensaje ni el payload completos: un embedding en el log es un dato biométrico.
- El nodo se conecta con el rol `NODERED_DB_USER`, nunca con el superusuario. Para tablas nuevas, ver [orangepi/AGENTS.md](../orangepi/AGENTS.md#cambios-de-esquema).

## Validación de payloads

- Valida tipo, tamaño y rango antes de usar un valor. El patrón está en la función "Validar v, i, p" de `flows.json`: texto, máximo 1024 caracteres, objeto JSON, campos numéricos finitos y rangos por campo.
- Un rechazo sale por la salida de descarte. El flujo Energía lo envía a la salida 2, que termina en el nodo "Descartados". No lo mandes al nodo catch.
- El nodo catch es para errores de ejecución, como la base de datos. En la API, el catch responde 500 (nodo "Responder 500").
- En la API, un parámetro inválido responde 400 por la salida 2 de "Validar limite y desde".
- Si cambias un rango, actualiza también la sección Energía de `orangepi/README.md`, que lo documenta.

## Nodo exec

- Conserva disponible el nodo `exec`. No lo quites sin decisión humana: lo exige el punto 5 del PDF del curso (plan, sección 3, y sección 6.4, punto 4), porque el reporte CSV lo ejecuta.
- Hoy `flows.json` no tiene ningún nodo `exec`.
- Un nodo `exec` ejecuta comandos dentro del contenedor. Por eso el editor exige `adminAuth` (`settings.js`, primer bloque de validación). No pases a `exec` datos que vienen de MQTT o HTTP sin validarlos antes.

## API HTTP

- El editor y la API responden en `https://<ip>:1880` con un certificado autofirmado. Desde la placa: `curl -k "https://127.0.0.1:1880/api/..."`. `-k` no verifica el certificado.
- La API no tiene autenticación hoy. El comentario sobre `httpNodeAuth` en `settings.js` indica dónde se añade, y `httpNodeAuth` entra el 11 de noviembre. No lo implementes ni lo quites en silencio.
- El frontend no habla MQTT: no hay usuario `frontend` ni listener WebSocket. Sus comandos al actuador y a la sesión entran por la API, y Node-RED los publica en `aula/actuador/cmd` y `aula/sesion/cmd` con el usuario `nodered` (contrato MQTT, sección 6). El mecanismo del progreso en vivo se decide el 11 de noviembre.
- `GET /api/galeria` es para el servicio de visión, no para el frontend. Sus embeddings nunca llegan al navegador (plan, secciones 5.4 y 6.7).
- Los endpoints de plan, sección 6.7, son el contrato con el frontend. Si cambias una ruta o un campo, actualiza esa sección en el mismo cambio.

## Verificación

- Valida los cambios en la placa con la lista de [orangepi/AGENTS.md](../orangepi/AGENTS.md#verificación). Incluye el simulador, el broker (autenticación y ACL) y la API.
- Tras un Deploy, revisa las últimas líneas del log con `docker compose logs --tail=50 nodered` desde `orangepi/`. Los descartes y los errores de ingesta aparecen en el log, porque sus nodos de depuración tienen `console: true`.
- El estado `healthy` de Node-RED llega después de su periodo de arranque de 60 s (servicio `nodered` de `docker-compose.yml`). Revisa con `docker compose ps`.

## Ver también

- [AGENTS.md de la raíz](../AGENTS.md): reglas generales y seguridad
- [orangepi/AGENTS.md](../orangepi/AGENTS.md): stack, comandos, verificación y lista de prohibiciones
- [orangepi/README.md](../orangepi/README.md): operación y riesgos aceptados
- [docs/HIGH-LEVEL-PLAN.md](../docs/HIGH-LEVEL-PLAN.md): secciones 5.4, 6.4, 6.7 y 7
