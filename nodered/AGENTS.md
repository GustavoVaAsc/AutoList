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
| `settings.js` | Ajustes: `adminAuth`, HTTPS, credenciales MQTT, bloqueo de módulos, `httpStatic` (el frontend en `/app/`, con CSP solo en esa ruta), cabeceras de seguridad (`nosniff` y `X-Frame-Options: DENY` en el editor, con `httpAdminMiddleware`) y la autenticación de la API (`httpNodeMiddleware`). | Sí, en `/etc/nodered/settings.js`. |
| `autenticacion-api.js` | El `httpNodeMiddleware` de la API: Basic del profesor en `/api/*` y solo `X-Galeria-Token` en `GET /api/galeria`. Lo carga `settings.js` con `require("./autenticacion-api")`. | Sí, en `/etc/nodered/autenticacion-api.js` (mismo directorio de root que `settings.js`). |
| `pruebas/` | Pruebas con `node:test` (sin dependencias) del middleware y de las funciones del flujo `Alumnos`. Ver [Verificación](#verificación). | No (`.dockerignore`). |
| `package.json`, `package-lock.json` | Versiones de Node-RED (5.0.9) y de `node-red-contrib-postgresql` (0.16.2). | Sí, para `npm ci`. |
| `Dockerfile` | Receta de la imagen `pase-lista/nodered`, en dos etapas: la primera compila `web/` con Node 22 y la final copia solo su `dist/` a `/usr/share/nodered-web`. | No. Lo usa `docker compose up -d --build`. |
| `.dockerignore` | Limita el contexto del build a `settings.js`, `autenticacion-api.js`, `package.json` y `package-lock.json`. Un archivo nuevo que deba entrar a la imagen se agrega aquí y en el `COPY` del `Dockerfile`. | No aplica. |

## Flujos

- Cada Deploy del editor reescribe `nodered/flows/flows.json`. Antes de commitear, revisa `git diff -- nodered/flows/flows.json`: busca cambios que no hiciste en el editor (`orangepi/README.md`, sección Desplegar flujos).
- Exporta los cambios de flujos al repo después de cada cambio (plan, sección 11).
- Los respaldos y temporales que Node-RED crea junto a `flows.json` están en `.gitignore`. No los agregues al repo.
- Los ids de nodos son estables. El id del broker MQTT (`a1f0c0de00000002`) lo referencia `settings.js` (ver [Credenciales](#credenciales)).
- Si editas el JSON a mano, valida que siga siendo JSON antes de desplegar.
- La pestaña "Limpieza de retenidos" (`a1f0c0de00000030`) publica, en cada conexión de Node-RED al broker (lo detecta un nodo `status` sobre su `mqtt out`; sirve también tras reiniciar Mosquitto), un retenido vacío en `aula/actuador/cmd`, `aula/sesion/cmd`, `aula/vision/cmd`, `aula/camara/pir` y `aula/camara/energia`, con el broker "Mosquitto" y sin credenciales propias. Eso provoca en cada conexión un mensaje vacío en vivo (sin retain) en cada uno de esos tópicos, que los consumidores ignoran por vacío (contrato MQTT, sección 1). Borra restos de órdenes y lecturas retenidas: la ACL no puede impedir el retain. La función "Validar v, i, p" descarta los mensajes de `aula/camara/energia` con retain (con aviso) y los de payload vacío (en silencio). Los flujos del 11 nov deben descartar retain en los demás tópicos con Retain = no (`aula/vision/resultado`, `aula/vision/embedding`, `aula/vision/enrolamiento`, `aula/vision/galeria/alta`, `aula/vision/galeria/baja`, `aula/pantalla/reinicio` y `aula/sistema/metricas`), o se amplía la lista de la limpieza. No quites nada de esto sin decisión humana.

## Settings e imagen

- Cambiar `settings.js`, `autenticacion-api.js`, `Dockerfile`, `package.json`, `package-lock.json` o cualquier archivo de `web/` exige reconstruir la imagen desde `orangepi/`: `docker compose up -d --build`.
- Antes de reconstruir, sube el sufijo de la etiqueta en la línea `image:` del servicio `nodered` de `orangepi/docker-compose.yml`: de `5.0.9-16` a `-17`, `-18` y así sucesivamente. El comentario del compose sobre la etiqueta de Node-RED lo pide.
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
- La API usa `NODERED_PROFESOR_USER` y `NODERED_PROFESOR_PASSWORD` (HTTP Basic) y `GALERIA_TOKEN` (`GET /api/galeria`). Sin cualquiera de las tres, `settings.js` lanza una excepción y Node-RED no arranca: la API falla cerrado y no responde 503. El usuario no puede llevar `:`. Tampoco arranca si `NODERED_PROFESOR_PASSWORD`, `GALERIA_TOKEN` o `NODERED_ADMIN_PASSWORD` contienen `cambiar_esta_clave` (cae también `cambiar_esta_clave_larga`), si dos de esos tres son iguales, si `GALERIA_TOKEN` tiene menos de 32 caracteres o `NODERED_PROFESOR_PASSWORD` menos de 16, o si alguno de esos dos tiene menos de 8 caracteres distintos (`validarSecretosApi`; los mensajes nombran variables, nunca valores). `NODERED_ADMIN_PASSWORD` solo se sujeta al placeholder y a no repetirse, sin mínimos de longitud ni de variedad. Los tres valores van por el compose y nunca en `flows.json`.

## SQL

- Toda consulta va parametrizada: `$1`, `$2` y así sucesivamente en el campo `query`, con los valores en `msg.params`. Ejemplo del flujo Energía:

  ```sql
  INSERT INTO metricas_energia (origen, v, i, p) VALUES ('ina219', $1, $2, $3)
  ```

  La función anterior al nodo fija `msg.params = [datos.v, datos.i, datos.p];`, con valores ya validados.

- La base tiene `timezone = America/Mexico_City` (`orangepi/sql/schema.sql`), pero una consulta que agrupe o compare por día escribe la zona explícita: `(columna AT TIME ZONE 'America/Mexico_City')::date`, no `columna::date` ni `CURRENT_DATE` a secas. Los `timestamptz` siguen en UTC. Si una consulta nueva depende de la zona de la sesión, el pool de Node-RED la toma al reconectar: tras cambiar la zona de la base, `docker compose restart nodered`.
- Nunca concatenes en `query` valores que llegan por MQTT o HTTP. Los embeddings viajan siempre como parámetros enlazados (`$1::vector`).
- Los nodos catch y debug de las rutas de embeddings (`aula/vision/embedding`, `/api/galeria`, registro) registran solo el error, nunca el mensaje ni el payload completos: un embedding en el log es un dato biométrico. En la pestaña `Alumnos`, "Responder 500" registra solo el SQLSTATE (`msg.error.code`) y, sin él, un mensaje truncado con las secuencias de 9 dígitos enmascaradas: ni números de cuenta ni el `detail` de Postgres llegan al log.
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
- La API se autentica con el `httpNodeMiddleware` de `settings.js` (`autenticacion-api.js`), no con `httpNodeAuth` (global: bloquearía al servicio de visión). Todo lo que sirva un `http in` exige Basic del profesor, salvo `GET /api/galeria`, que solo acepta `X-Galeria-Token`. No quites ni debilites esa excepción ni agregues otra sin decisión humana: las credenciales del profesor no deben bastar para descargar embeddings. Una ruta nueva no necesita autenticación en el flujo: ya la tiene. Contra CSRF, el middleware exige en todo método que no sea `GET` ni `HEAD` la cabecera `X-Pase-Lista: 1` y rechaza (403) un `Origin` o un `Sec-Fetch-Site` de otro origen; un endpoint nuevo que cambie datos hereda esa protección, y el frontend debe mandar la cabecera en cada `fetch` que lo llame. La importación del CSV además exige `Content-Type: text/csv` (415). **Ningún `http in` con método GET o HEAD cambia datos ni publica en MQTT**: esos métodos no exigen `X-Pase-Lista`, así que la defensa contra CSRF depende de esta regla. `nodered/pruebas/get-solo-lectura.test.js` la comprueba sobre `flows.json` (desde cada `http in` GET sigue los cables, los `link out` y los `catch`, y falla si llega a un `mqtt out` o a un `postgresql` con INSERT, UPDATE, DELETE, TRUNCATE o ALTER, o sin consulta fija): un flujo nuevo que la rompa hace fallar `node --test`. Una operación que cambie datos va en POST, PUT, PATCH o DELETE. El middleware también limita el cuerpo a 256 KiB; el flujo del CSV repite ese tope (`MAX_BYTES` en "Validar CSV y existentes"): si cambias uno, cambia el otro.
- El frontend no habla MQTT: no hay usuario `frontend` ni listener WebSocket. Sus comandos al actuador y a la sesión entran por la API, y Node-RED los publica en `aula/actuador/cmd` y `aula/sesion/cmd` con el usuario `nodered` (contrato MQTT, sección 6). El mecanismo del progreso en vivo se decide el 11 de noviembre.
- `GET /api/galeria` es para el servicio de visión, no para el frontend. Sus embeddings nunca llegan al navegador (plan, secciones 5.4 y 6.7). Responde `{"embeddings":[{"cuenta","embedding"}]}`, el formato que lee `orangepi/vision/src/gallery_store.cpp`: si lo cambias, avisa a Gustavo y actualiza el contrato MQTT (sección 4).
- `GET /api/alumnos` nunca selecciona la columna `embedding` (solo `embedding IS NULL` para el estado del rostro). La importación del CSV tiene dos 409: el de las cuentas existentes sin decisión del profesor (`escrito: false`, no se importó nada, trae `existentes`) y el de la carrera con otra importación (`escrito: true`, las filas nuevas ya se escribieron, trae los conteos); quien consume la respuesta debe leer `escrito`. Escribe con una sola sentencia SQL (una transacción), solo toca `nombre` y `grupo` de las existentes y no usa DELETE; el rol de Node-RED tampoco lo tiene.
- Los endpoints de plan, sección 6.7, son el contrato con el frontend. Si cambias una ruta o un campo, actualiza esa sección en el mismo cambio.

## Verificación

- Antes de la placa, desde la raíz del repo y sin Docker: `node --test nodered/pruebas/*.test.js`. Prueba el middleware (Basic, token de la galería, tiempo constante, tope de cuerpo, falla cerrado) y las funciones del flujo `Alumnos` (validación del CSV, 400, 409, formato de la galería), que se extraen del propio `flows.json`. No prueba el nodo de Postgres ni Express: eso es de la placa. Si cambias una función de esos flujos, el cambio va en `flows.json` y la prueba lo lee de ahí.
- Valida los cambios en la placa con la lista de [orangepi/AGENTS.md](../orangepi/AGENTS.md#verificación). Incluye el simulador, el broker (autenticación y ACL) y la API.
- Tras un Deploy, revisa las últimas líneas del log con `docker compose logs --tail=50 nodered` desde `orangepi/`. Los descartes y los errores de ingesta aparecen en el log, porque sus nodos de depuración tienen `console: true`.
- El estado `healthy` de Node-RED llega después de su periodo de arranque de 60 s (servicio `nodered` de `docker-compose.yml`). Revisa con `docker compose ps`.

## Ver también

- [AGENTS.md de la raíz](../AGENTS.md): reglas generales y seguridad
- [orangepi/AGENTS.md](../orangepi/AGENTS.md): stack, comandos, verificación y lista de prohibiciones
- [orangepi/README.md](../orangepi/README.md): operación y riesgos aceptados
- [docs/HIGH-LEVEL-PLAN.md](../docs/HIGH-LEVEL-PLAN.md): secciones 5.4, 6.4, 6.7 y 7
