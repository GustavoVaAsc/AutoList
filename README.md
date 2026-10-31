# Pase de lista con reconocimiento facial en Edge AI

Proyecto de la materia **Fundamentos de Sistemas Embebidos 2027-1**: automatiza el
pase de lista verificando la presencia física de cada alumno con reconocimiento
facial ejecutado en la NPU de una Orange Pi Zero 3W, sin depender de servicios en
la nube. El sistema registra presente o retardo, da retroalimentación física
(puerta simulada con servo, LEDs, buzzer y tira LED de 12 V), almacena los datos
en Postgres e InfluxDB y los visualiza en Grafana.

## Hardware principal

| Componente | Dónde | Para qué |
|---|---|---|
| Orange Pi Zero 3W (4 GB, NPU de 3 TOPS) | Borde | Inferencia, Docker (Mosquitto, Node-RED, InfluxDB) y frontend |
| XIAO ESP32S3 Sense | Campo | Cámara, PIR, INA219 y todos los actuadores |
| ILI9341 con táctil XPT2046 | Borde (SPI) | Interfaz del alumno en el aula |
| RTC DS3231 | Borde (I²C) | Hora confiable cuando no hay NTP |
| PC | PC | Postgres con pgvector y Grafana |

El detalle completo del hardware, con los pines propuestos y las notas de
alimentación, está en la [sección 8 del plan](docs/HIGH-LEVEL-PLAN.md).

## Arquitectura por capas

```
Campo    XIAO ESP32S3 Sense ──HTTP fotos / MQTT──┐
                                                  │
Borde    Orange Pi Zero 3W                        │
          Servicio de visión C++ (NPU) <──MQTT── Mosquitto <──MQTT──┐
          Node-RED ──Ingesta──> InfluxDB                            │
                   ──SQL──> Postgres (pgvector)                     │
          Pantalla ILI9341 (Python, systemd) <──MQTT────────────────┘
                                                  │
PC        Postgres + Grafana <──SQL / API──────────┘
                                                  │
Administración  Navegador del profesor ──HTTP API / MQTT WebSocket──> Node-RED
```

- **Campo:** la XIAO lee sensores y maneja los actuadores.
- **Borde:** la Orange Pi infiere, decide las reglas de sesión y guarda.
- **PC:** se almacenan y visualizan los datos.
- **Administración:** el profesor opera el frontend de registro desde su laptop.

## Estructura del repositorio

```
firmware/
  xiao/                cámara, sensores y actuadores de la XIAO ESP32S3
  pruebas-unitarias/   cámara, INA219 y servo
orangepi/
  vision/              servicio de visión en C++ para la NPU
  pantalla/            programa Python de la ILI9341
  systemd/             unidad systemd de la pantalla
  docker-compose.yml   Mosquitto, Node-RED e InfluxDB
nodered/
  flows.json           flujos de ingesta, pase de lista y sesiones
pc/
  docker-compose.yml   Postgres con pgvector y Grafana
  sql/schema.sql       esquema de la base de datos
  grafana/dashboards/  paneles de Grafana exportados
web/                   frontend de registro (Vite + React + TypeScript)
herramientas/          enrolamiento por lote, calibración LFW, reporte CSV
docs/                  plan, protocolos, consentimiento, diagramas y mediciones
```

## Cómo se levanta cada parte

Cada parte tiene su propio `docker-compose.yml` y comparte las variables del
`.env` de la raíz. Antes de nada, copia la plantilla y rellénala:

```bash
cp .env.example .env
```

**Borde (Orange Pi):** Mosquitto, Node-RED e InfluxDB.

```bash
docker compose -f orangepi/docker-compose.yml --env-file .env up -d
```

**PC:** Postgres con pgvector y Grafana.

```bash
docker compose -f pc/docker-compose.yml --env-file .env up -d
```

**Frontend web:** se compila y el resultado se copia a `/data/public` de
Node-RED, que lo sirve con `httpStatic`. El build no se versiona.

```bash
cd web && npm install && npm run build
```

**Pantalla ILI9341:** no corre en Docker. Se ejecuta en el host como unidad
systemd (`orangepi/systemd/orangepi-pantalla.service`).

**Servicio de visión:** programa nativo de C++ que se compila y se ejecuta en la
Orange Pi; los modelos `.nb` convertidos no se versionan.

## Documentación

- [Plan en alto nivel](docs/HIGH-LEVEL-PLAN.md) — fuente de verdad del proyecto.
- [Índice de `docs/`](docs/README.md).
- [Contrato de telemetría](docs/protocolos.md) — tópicos MQTT, ACL, API HTTP y
  latencias.
- [Tabla de tópicos MQTT en JSON](docs/mqtt-topics.json) — la misma lista, legible
  por máquina.
- [Consentimiento de participantes](docs/consentimiento.md) — borrador para
  revisión del profesor.
