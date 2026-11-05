# Instrucciones para agentes

Este archivo rige a cualquier agente de código que trabaje en el repo (Claude Code, Codex, Cursor u otros). `orangepi/AGENTS.md` y `nodered/AGENTS.md` añaden reglas de su carpeta: léelos antes de editar archivos ahí.

## Contenido

- [Proyecto](#proyecto)
- [Equipo y carriles](#equipo-y-carriles)
- [Ramas e idioma](#ramas-e-idioma)
- [Commits](#commits)
- [Estructura del repositorio](#estructura-del-repositorio)
- [Seguridad y datos](#seguridad-y-datos)
- [Hardware compartido](#hardware-compartido)
- [Contrato MQTT](#contrato-mqtt)
- [Verificación](#verificación)
- [Ver también](#ver-también)

## Proyecto

Pase de lista con reconocimiento facial para la materia Fundamentos de Sistemas Embebidos (2027-1). La inferencia corre en la NPU de una Orange Pi Zero 3W. Una XIAO ESP32S3 Sense en campo lee la cámara, el PIR y el INA219, y maneja el servo, los LEDs, el buzzer y la tira LED de 12 V. Mosquitto, Node-RED, Postgres y Portainer corren en la placa (`docs/HIGH-LEVEL-PLAN.md`, sección 4).

`docs/HIGH-LEVEL-PLAN.md` es la fuente de verdad, salvo los tópicos, payloads y ACL de MQTT, que manda `docs/contrato-mqtt.md`. Si una decisión cambia, actualiza el plan en el mismo cambio. No contradigas el plan en silencio.

## Equipo y carriles

Reparto según `docs/HIGH-LEVEL-PLAN.md`, sección 12.2.

| Persona | Carril | Módulos |
|---|---|---|
| Gustavo | Visión y campo | Servicio de visión en la NPU, firmware de la XIAO (cámara, sensores y actuadores, salvo la tira LED) |
| Arnau | Hardware de la pantalla y documentación | Overlays de SPI, unidad systemd `orangepi-pantalla.service`, RTC DS3231, documentación y presentaciones del proyecto |
| Yordi | Edge, datos, frontend y pantalla | Docker, Node-RED, Postgres, API HTTP, frontend web, programa de la pantalla ILI9341, firmware de la tira LED de 12 V y su etapa de potencia (`firmware/xiao`) |

- El firmware de la tira LED y su etapa de potencia eran de Gustavo y pasaron a Yordi, con el acuerdo de Gustavo. El resto de `firmware/xiao` (cámara, PIR, INA219, servo, LEDs RGB y buzzer) sigue siendo de Gustavo, que también flashea la placa.
- El carril del agente sale de la petición de la persona o del nombre de la rama. Si no está claro, pregunta antes de editar.
- Un agente que trabaja en un carril no edita módulos de otro carril sin decirlo en su resumen.
- La documentación técnica de cada módulo (por ejemplo `orangepi/README.md`) la mantiene el carril de ese módulo. La documentación y las presentaciones del proyecto son de Arnau (plan, sección 12.2).
- Un cambio en un contrato compartido (tópicos MQTT, esquema SQL, endpoints HTTP) afecta a otros carriles. Ver [Contrato MQTT](#contrato-mqtt).

## Ramas e idioma

- Una rama por módulo, según el plan (sección 11). Ramas actuales: `vision/npu`, `edge/sistema` (el edge) y `firmware/xiao`. El plan sugiere `borde/nodered` como ejemplo; si vas a crear una rama nueva, pregunta qué nombre usar.
- Los módulos se fusionan a `main` cuando funcionan (plan, sección 11).
- Documentación, comentarios de código, mensajes de commit y textos visibles al usuario van en español. Los identificadores técnicos (tópicos, variables, nombres de archivo) se escriben como están en el código.

## Commits

- Formato Conventional Commits en español: `tipo(ámbito): Mensaje`, con mayúscula inicial y sin punto final. Ejemplo: `docs(plan): Se aplica la poda para empezar con implementaciones`.
- Antes de commitear, revisa `git status` y agrega solo los archivos que tocaste.
- No hagas commit, push ni abras un PR si la persona no lo pide.

## Estructura del repositorio

Estado actual, según el plan (sección 11):

```
docs/
  HIGH-LEVEL-PLAN.md      plan rector (documento vivo)
  contrato-mqtt.md        tópicos, payloads y ACL de MQTT (manda sobre el plan)
  diagramas/              diagramas
orangepi/                 stack de la placa (AGENTS.md propio)
  docker-compose.yml      Mosquitto, Node-RED, Postgres y Portainer
  .env.example            plantilla de credenciales; orangepi/.env no se versiona
  mosquitto/              configuración, ACL (acl) y entrypoint del broker
  vision/                 servicio de visión en C++ (de Gustavo)
  pantalla/               programa de la pantalla ILI9341 en Python (README propio; .venv ignorado por git)
  systemd/                unidades systemd: servicio de visión y pantalla
  sql/                    schema.sql y roles.sh (initdb de Postgres)
  README.md               operación del stack
nodered/                  imagen, ajustes y flujos (AGENTS.md propio)
  flows/flows.json        flujos; el editor de Node-RED los escribe aquí
web/                      frontend (Vite + React + TypeScript; README propio)
firmware/xiao/            firmware de la XIAO: por ahora la tira LED de 12 V (sketch Arduino, README propio; secrets.h ignorado por git)
herramientas/             simular_energia.sh
```

Aún no existe en el repo: el resto del firmware de la XIAO (de Gustavo) ni `firmware/pruebas-unitarias/`.

`docs/SPEC.md` y `docs/PODA.md` son notas internas ignoradas por git (`.gitignore`). No los versiones.

## Seguridad y datos

No negociable:

- `orangepi/.env` tiene credenciales reales. No lo leas, no lo imprimas, no lo cites y no lo incluyas en un commit. Solo `orangepi/.env.example` se versiona (`.gitignore`).
- Un secreto nuevo entra en `orangepi/.env.example` con un placeholder, como `cambiar_esta_clave`.
- No pegues la salida de `docker compose config` en ningún documento ni chat: imprime las contraseñas. Usa `docker compose config -q` o `--no-interpolate` (`orangepi/README.md`, sección Validar el compose sin mostrar secretos).
- Nunca imprimas el entorno de un contenedor. Eso incluye `docker inspect`, `env`, `printenv` y `echo $..._PASSWORD`. Las acciones que pueden imprimir secretos están en [Prohibido sin aprobación humana](orangepi/AGENTS.md#prohibido-sin-aprobación-humana).
- No subas fotos de personas al repo. Los modelos `.nb`, `.onnx` y `.tflite`, y los datasets, quedan fuera de git (`.gitignore`; plan, sección 11).
- Los embeddings biométricos nunca llegan al navegador ni al frontend. El registro exige consentimiento (plan, sección 5.4).
- `firmware/xiao/secrets.h` tiene el Wi-Fi y la clave MQTT de `xiao`. No lo leas, no lo imprimas, no lo cites y no lo incluyas en un commit. Solo `firmware/xiao/secrets.h.example` se versiona (`.gitignore`).
- `nodered/flows/flows.json` no contiene secretos. Las credenciales entran por variables de entorno (`orangepi/README.md`, sección Desplegar flujos).

## Hardware compartido

La Orange Pi y la XIAO ESP32S3 Sense son equipos físicos únicos que se turnan (plan, sección 12.4).

- La XIAO la flashea Gustavo desde su laptop con el IDE de Arduino. En la placa no hay `arduino-cli` ni PlatformIO y no se instalan: el firmware no se compila aquí.
- En la placa, toca solo contenedores, imágenes y volúmenes del proyecto `pase-lista`. Hay otros contenedores que no son tuyos.
- No uses `sudo`.
- Propiedad del host: los overlays de SPI y la unidad systemd de la pantalla son de Arnau; la imagen del sistema y la NPU, de Gustavo; el stack de Docker, de Yordi. Para cualquier otro cambio en el host, coordina con el equipo (plan, secciones 12.2 y 12.4).
- Si dejas el stack detenido o reiniciado, dilo en el resumen.

## Contrato MQTT

- `docs/contrato-mqtt.md` es la fuente de verdad de los tópicos, payloads y ACL entre carriles; cuando difiera del plan, manda el contrato. El plan, sección 7.1, es su resumen. Firmware, visión y Node-RED dependen de él (plan, sección 13).
- Si cambias un tópico, un payload o la ACL, actualiza `docs/contrato-mqtt.md` (con su entrada en el historial) y el plan, sección 7.1, en el mismo cambio, y avisa al carril afectado.
- La ACL vive en `orangepi/mosquitto/acl` y los usuarios MQTT (`xiao`, `vision`, `nodered`, `pantalla` y `simulador`) en `orangepi/mosquitto/entrypoint.sh`. Un usuario o permiso nuevo cambia el contrato, la ACL, el entrypoint, el compose y `orangepi/.env.example` a la vez.
- Los rangos de la ingesta de energía se validan en `nodered/flows/flows.json` (función "Validar v, i, p") y se documentan en `orangepi/README.md` (sección Energía). Si cambian, actualiza las dos cosas.

## Verificación

- El repo no tiene suite de pruebas automatizada completa. El plan prevé pruebas unitarias de cámara, INA219 y servo (sección 11), que no están en este checkout. Lo único automatizado fuera de `orangepi/vision/` son las pruebas de `nodered/pruebas/` (`node --test nodered/pruebas/*.test.js`, sin dependencias): el middleware de la API y las funciones de los flujos `Alumnos` y `Registro`; y las de `web/pruebas/` (`node --test web/pruebas/*.test.js`, sin dependencias): la lógica pura del frontend (Alumnos y la cuenta de las cifras). Estas últimas piden Node 22.18 o posterior, que carga el `.ts` quitando los tipos (`web/README.md`, sección Comandos). También hay pruebas de host de la lógica de la tira LED en `firmware/xiao/pruebas/` (`g++`, sin dependencias; comandos en `firmware/xiao/README.md`, sección Pruebas de host): no cubren el sketch, que solo se verifica en la placa.
- La verificación se hace en la placa, con la lista de cada `AGENTS.md` anidado. Para `orangepi/`, ver [Verificación](orangepi/AGENTS.md#verificación).
- No digas que algo está listo sin haberlo verificado. Si no pudiste ejecutar la verificación, di qué falta y por qué.

## Ver también

- [orangepi/AGENTS.md](orangepi/AGENTS.md): stack, comandos, verificación, invariantes y lista de prohibiciones del edge
- [nodered/AGENTS.md](nodered/AGENTS.md): flujos, imagen, credenciales y SQL de Node-RED
- [orangepi/README.md](orangepi/README.md): operación del stack
- [docs/HIGH-LEVEL-PLAN.md](docs/HIGH-LEVEL-PLAN.md): plan del proyecto
