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
| `settings.js` | Ajustes: `adminAuth`, HTTPS, credenciales MQTT y bloqueo de módulos. | Sí, en `/etc/nodered/settings.js`. |
| `package.json`, `package-lock.json` | Versiones de Node-RED (5.0.9) y de `node-red-contrib-postgresql` (0.16.2). | Sí, para `npm ci`. |
| `Dockerfile` | Receta de la imagen `pase-lista/nodered`. | No. Lo usa `docker compose up -d --build`. |
| `.dockerignore` | Limita el contexto del build a `settings.js`, `package.json` y `package-lock.json`. | No aplica. |

## Flujos

- Cada Deploy del editor reescribe `nodered/flows/flows.json`. Antes de commitear, revisa `git diff -- nodered/flows/flows.json`: busca cambios que no hiciste en el editor (`orangepi/README.md`, sección Desplegar flujos).
- Exporta los cambios de flujos al repo después de cada cambio (plan, sección 11).
- Los respaldos y temporales que Node-RED crea junto a `flows.json` están en `.gitignore`. No los agregues al repo.
- Los ids de nodos son estables. El id del broker MQTT (`a1f0c0de00000002`) lo referencia `settings.js` (ver [Credenciales](#credenciales)).
- Si editas el JSON a mano, valida que siga siendo JSON antes de desplegar.

## Settings e imagen

- Cambiar `settings.js`, `Dockerfile`, `package.json` o `package-lock.json` exige reconstruir la imagen desde `orangepi/`: `docker compose up -d --build`.
- Antes de reconstruir, sube el sufijo de la etiqueta en la línea `image:` del servicio `nodered` de `orangepi/docker-compose.yml`: de `5.0.9-3` a `-4`, `-5` y así sucesivamente. El comentario del compose sobre la etiqueta de Node-RED lo pide.
- `settings.js` vive en `/etc/nodered`, un directorio de root que el contenedor no puede modificar (comentarios del `Dockerfile`). No lo muevas a `flows/` ni a `/data`.
- Node-RED no instala módulos. `settings.js` bloquea la instalación desde la paleta y desde los nodos function (bloque `externalModules` y `functionExternalModules`). Las dependencias van en `package.json` y `package-lock.json`.
- La imagen instala con `npm ci --omit=dev --ignore-scripts` (instrucción `RUN` del `Dockerfile`). Ninguna dependencia necesita scripts de instalación.
- Para subir una versión, regenera el lock con `npm install --package-lock-only --omit=dev --ignore-scripts` (comentario del `Dockerfile`).
- La imagen base está fijada por digest. Para actualizarla, cambia versión y digest a la vez (línea `FROM` del `Dockerfile`).

## Credenciales

- Nunca pongas credenciales en `flows.json`. Llegan como variables de entorno desde `orangepi/.env`, a través del compose.
- Postgres: el nodo de configuración usa campos de tipo `env`. El usuario sale de `NODERED_DB_USER` y la contraseña de `NODERED_DB_PASSWORD`. El nombre de la base es `${POSTGRES_DB}` (nodo "Postgres" en `flows.json`).
- MQTT: `settings.js` escribe en `/data/flows_cred.json` los textos `${MQTT_USER}` y `${MQTT_PASSWORD}` para el broker "Mosquitto". Node-RED los sustituye por las variables de entorno. Nunca escribas los valores reales.
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
- `GET /api/galeria` es para el servicio de visión, no para el frontend. Sus embeddings nunca llegan al navegador (plan, secciones 5.4 y 6.7).
- Los endpoints de plan, sección 6.7, son el contrato con el frontend. Si cambias una ruta o un campo, actualiza esa sección en el mismo cambio.

## Verificación

- Valida los cambios en la placa con la lista de [orangepi/AGENTS.md](../orangepi/AGENTS.md#verificación). Incluye el simulador, el broker y la API.
- Tras un Deploy, revisa las últimas líneas del log con `docker compose logs --tail=50 nodered` desde `orangepi/`. Los descartes y los errores de ingesta aparecen en el log, porque sus nodos de depuración tienen `console: true`.
- El estado `healthy` de Node-RED llega después de su periodo de arranque de 60 s (servicio `nodered` de `docker-compose.yml`). Revisa con `docker compose ps`.

## Ver también

- [AGENTS.md de la raíz](../AGENTS.md): reglas generales y seguridad
- [orangepi/AGENTS.md](../orangepi/AGENTS.md): stack, comandos, verificación y lista de prohibiciones
- [orangepi/README.md](../orangepi/README.md): operación y riesgos aceptados
- [docs/HIGH-LEVEL-PLAN.md](../docs/HIGH-LEVEL-PLAN.md): secciones 5.4, 6.4, 6.7 y 7
