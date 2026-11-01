# AutoList: pase de lista con reconocimiento facial en Edge AI

**Materia:** Fundamentos de Sistemas Embebidos 2027-1
**Profesor:** Eduardo Fragoso Navarro
**Equipo:** Gustavo · Yordi · Arnau
**Plataforma Edge AI:** Orange Pi Zero 3W (Allwinner A733, NPU de 3 TOPS, 4 GB de RAM)

> Última actualización: **1 nov 2026**. Este README se actualiza con cada avance significativo y resume el estado del proyecto. El detalle técnico vive en [`docs/HIGH-LEVEL-PLAN.md`](docs/HIGH-LEVEL-PLAN.md).

---

## Qué hace el proyecto

AutoList automatiza el pase de lista verificando la **presencia física** de cada alumno con reconocimiento facial ejecutado localmente en la NPU de la Orange Pi, sin servicios en la nube. Es la evolución del [proyecto previo del equipo](https://github.com/Yordi-JA/Pase-de-Asitencia-Bluetooth-Proyecto-Final-Sistemas-de-comunicaciones) (pase de lista por Bluetooth), que registraba la asistencia pero no verificaba la identidad.

El sistema:
- detecta al alumno con un PIR, toma fotos con la XIAO ESP32S3 Sense y lo identifica en la NPU;
- registra **presente** o **retardo** según la tolerancia de la sesión, con lista maestra y anti-doble registro;
- da retroalimentación física: servo como puerta simulada, LEDs RGB, buzzer y una tira LED de 12 V (vía MOSFET) que ilumina la cara;
- guarda todo en Postgres y lo muestra en un frontend web y en una pantalla táctil ILI9341;
- mide el consumo energético con un INA219 para el reporte de costo y consumo.

Alcance completo (incluye, opcional y fuera de alcance): [plan, sección 2](docs/HIGH-LEVEL-PLAN.md#2-alcance). Trazabilidad con los requisitos de la materia: [sección 3](docs/HIGH-LEVEL-PLAN.md#3-trazabilidad-con-los-requisitos-del-documento).

## Arquitectura

![Arquitectura de AutoList](docs/diagramas/arquitectura.png)

La fuente editable del diagrama es [`docs/diagramas/arquitectura.svg`](docs/diagramas/arquitectura.svg). Detalle de capas y protocolos: [sección 4](docs/HIGH-LEVEL-PLAN.md#4-arquitectura).

## Cómo funciona

1. **Pase de lista:** PIR → la visión enciende la luz y pide una ráfaga de fotos → filtro de calidad → detección, alineación y embedding en la NPU → comparación con la galería → Node-RED aplica las reglas de sesión, guarda en Postgres y manda el comando al servo y los LEDs. Detalle: [sección 5.1](docs/HIGH-LEVEL-PLAN.md#51-pase-de-lista-flujo-directo).
2. **Registro de alumnos:** el profesor importa la lista maestra (CSV) y, con el consentimiento del alumno, registra su rostro desde el frontend con 5 poses guiadas en la ILI9341. Detalle: [sección 5.4](docs/HIGH-LEVEL-PLAN.md#54-registro-de-alumnos-enrolamiento).
3. **Flujo inverso:** la pantalla publica por MQTT "Abrir puerta" y "Probar luz LED" hacia la XIAO. Detalle: [sección 5.2](docs/HIGH-LEVEL-PLAN.md#52-control-flujo-inverso).

Tópicos MQTT y esquema de la base: [sección 7](docs/HIGH-LEVEL-PLAN.md#7-datos) y [`docs/contrato-mqtt.md`](docs/contrato-mqtt.md).

## Componentes

| Componente | Tecnología | Carpeta | Estado |
|---|---|---|---|
| Stack de borde | Docker: Mosquitto, Node-RED (HTTPS), Postgres + pgvector, Portainer | `orangepi/`, `nodered/` | Funcionando: ingesta y API de energía |
| Servicio de visión | C++20 nativo con systemd, NPU (VIPLite), OpenCV 4.6 | `orangepi/vision` | Esqueleto con pruebas; faltan modelos y adaptadores reales |
| Firmware de campo | XIAO ESP32S3 Sense, Arduino C++, MQTT y HTTP | `firmware/xiao` | Sin iniciar (la XIAO llega la semana del 4 nov) |
| Pantalla | Python en el host (systemd), ILI9341 + táctil XPT2046, RTC DS3231 | `orangepi/pantalla`, `orangepi/systemd` | Sin iniciar |
| Frontend web | Vite + React + TypeScript, servido por Node-RED | `web/` | Sin iniciar |

Modelos decididos: RetinaFace para detección e InsightFace `w600k_mbf` (MobileFaceNet, embedding de 512) para reconocimiento ([sección 6.2](docs/HIGH-LEVEL-PLAN.md#62-servicio-de-visión-orangepivision-c-nativo)). Hardware y pines: [sección 8](docs/HIGH-LEVEL-PLAN.md#8-hardware). Estructura del repositorio: [sección 11](docs/HIGH-LEVEL-PLAN.md#11-estructura-del-repositorio).

## Entregables y avance

Fechas tentativas tomadas del [plan, sección 12.1](docs/HIGH-LEVEL-PLAN.md#121-hitos). Cada hito es un miércoles.

**Leyenda:** ✅ hecho · 🟡 parcial · ⬜ pendiente · ⏩ adelantado de un hito posterior

### Resumen

| Fecha | Hito | Estado |
|---|---|---|
| 28 oct | Proyecto y planeación | ✅ Terminado; la lista de compras pasó al 4 nov |
| 4 nov | Primer avance | 🟡 4 de 11 entregables listos, 1 parcial |
| 11 nov | Segundo avance | 🟡 Alineación y galería adelantadas en el servicio de visión |
| 18 nov | Tercer avance | 🟡 Filtro de nitidez adelantado |
| 25 nov | Entrega final | ⬜ |

### 28 oct: Proyecto y planeación

| Entregable | Estado | Evidencia |
|---|---|---|
| Especificación del proyecto | ✅ | [`docs/HIGH-LEVEL-PLAN.md`](docs/HIGH-LEVEL-PLAN.md) |
| Exposición | ✅ | Presentada en clase |
| Repositorio y estructura | ✅ | `orangepi/`, `nodered/`, `herramientas/` y guías para agentes (`AGENTS.md`) |
| ~~Lista de compras~~ | ➡️ | Reprogramada al 4 nov (ver nota) |

> **Nota sobre la lista de compras.** Se aplazó del 28 oct al 4 nov. Al reordenar el plan por dependencias, el primer avance absorbió trabajo que antes estaba en entregas posteriores (marcado con ⏩ en la tabla del 4 nov), así que el ajuste de una semana no recorre el resto del calendario.

### 4 nov: Primer avance

| Entregable | Estado | Evidencia |
|---|---|---|
| Lista de compras (reprogramada desde el 28 oct) | ⬜ | |
| NPU verificada en la imagen oficial de Orange Pi | ✅ | `vipcore` 1.13.0, `vpm_run` a 2.76 ms; prueba `orangepi/vision/apps/npu_smoke.cpp` |
| Toolchain Pegasus en la PC | ⬜ | Versión decidida: Acuity v2.0.10.2 |
| Modelo de la NPU convertido a `.nb` | ⬜ | |
| Overlays de SPI (`/dev/spidev*`) y unidad systemd de la pantalla | ⬜ | |
| Compose de la Orange Pi (Mosquitto, Node-RED, Postgres, Portainer) | ✅ | [`orangepi/docker-compose.yml`](orangepi/docker-compose.yml) |
| Esquema de Postgres | ✅ | [`orangepi/sql/schema.sql`](orangepi/sql/schema.sql) y rol de Node-RED con privilegios mínimos |
| Ingesta a `metricas_energia` | ✅ | Flujo de energía en Node-RED; se prueba con `herramientas/simular_energia.sh` hasta que llegue la XIAO |
| Primer gráfico de energía | 🟡 | La API `GET /api/metricas/energia` ya sirve los datos; falta la gráfica |
| ⏩ Arranque del esqueleto del frontend (antes 11 nov) | ⬜ | |
| ⏩ ACL de Mosquitto y usuarios por rol (antes 25 nov) | ⬜ | `acl_file` preparado y comentado en `orangepi/mosquitto/mosquitto.conf` |

### 11 nov: Segundo avance

| Entregable | Estado | Evidencia |
|---|---|---|
| Detección, alineación y embedding en la NPU | 🟡 | Alineación a 112×112 implementada y probada; faltan el detector y el embedder sobre la NPU |
| Galería y publicación del resultado | 🟡 | Galería en RAM con similitud coseno y contrato MQTT tipado; falta el adaptador MQTT real |
| Orden de enrolar con captura de 5 poses | 🟡 | Máquina de estados del registro probada con dobles en memoria |
| MOSFET y tira LED de 12 V; orquestación luz–captura | ⬜ | |
| XIAO: `/capture`, PIR e INA219 por MQTT; servo y LEDs por MQTT | ⬜ | La XIAO llega la semana del 4 nov |
| Pruebas unitarias de la XIAO (cámara, INA219, servo) | ⬜ | |
| Prueba temprana: servo moviéndose mientras la cámara captura | ⬜ | |
| RTC DS3231 e integración del hardware de la pantalla | ⬜ | |
| Flujos de pase de lista y de sesiones en Node-RED | ⬜ | |
| Reinicio de la pantalla por MQTT y nodo `exec` del reporte CSV | ⬜ | |
| API de alumnos e importación de CSV | ⬜ | |
| Esqueleto del frontend | ⬜ | |
| Menú base de la pantalla (sesión, lista y guía de registro) | ⬜ | |
| `httpNodeAuth` en Node-RED y `GALERIA_TOKEN` | ⬜ | Variable ya en `orangepi/.env.example` |

### 18 nov: Tercer avance

| Entregable | Estado | Evidencia |
|---|---|---|
| Filtro de calidad y mejora de imagen | 🟡 | Filtro de nitidez (varianza del Laplaciano) implementado y probado; falta la mejora de imagen (CLAHE y gamma) |
| Validaciones y chequeo antifraude del registro | ⬜ | |
| Experimentos E1–E5 ([sección 9](docs/HIGH-LEVEL-PLAN.md#9-experimentos)) | ⬜ | |
| Umbral de similitud calibrado con LFW | ⬜ | |
| Frontend de registro completo, con la pestaña de Métricas y Asistencia | ⬜ | |
| Pantalla completa con sus 3 vistas y los botones de control | ⬜ | |

### 25 nov: Entrega final

| Entregable | Estado |
|---|---|
| Integración del sistema completo | ⬜ |
| Video | ⬜ |
| Reporte de costo y consumo ([sección 10](docs/HIGH-LEVEL-PLAN.md#10-análisis-de-costo-y-consumo-para-el-reporte)) | ⬜ |
| Comprimido de código | ⬜ |
| Pulido del frontend y de la pantalla | ⬜ |
| Experimento E6 (comparación con el proyecto previo) | ⬜ |

## Próximos pasos

**Para el 4 nov** (lo que falta del primer avance):
- Hacer las compras: servo, INA219, PIR, MOSFET y tira LED de 12 V. Son el camino crítico del firmware de la XIAO y de la etapa de potencia.
- Instalar el toolchain Pegasus (Acuity v2.0.10.2) y convertir RetinaFace y `w600k_mbf` a `.nb`.
- Habilitar los overlays de SPI y crear la unidad systemd de la pantalla.
- Agregar la gráfica de energía sobre la API existente.
- Arrancar el esqueleto del frontend.
- Escribir las ACL de Mosquitto y los usuarios por rol.

**Después, en orden de dependencia:** modelos `.nb` → detector y embedder en la NPU → adaptadores reales de MQTT y HTTP en la visión → flujos de pase de lista y de registro → frontend de registro. El firmware de la XIAO arranca en cuanto llegue la placa. Dependencias completas: [sección 12.3](docs/HIGH-LEVEL-PLAN.md#123-dependencias-entre-tareas).

## Riesgos abiertos

Resumen de la [sección 13](docs/HIGH-LEVEL-PLAN.md#13-riesgos) y de los pendientes de [`orangepi/README.md`](orangepi/README.md#seguridad-y-riesgos-aceptados).

| Riesgo | Impacto | Mitigación |
|---|---|---|
| Compras sin hacer y XIAO por llegar | Todo el firmware de campo y la etapa de potencia se concentran en el hito del 11 nov | Comprar en la semana del 4 nov; probar la ingesta con el simulador mientras tanto |
| Compatibilidad del `.nb` con el driver de la NPU | Un `.nb` incompatible o una actualización del kernel rompen la inferencia | Acuity v2.0.10.2 para `vipcore` 1.13.0; no actualizar el kernel sin volver a verificar la NPU |
| Precisión del modelo cuantizado | Más falsos positivos o rechazos | Fallback a cuantización int16 o mixta, o a `w600k_r50` (mismo embedding de 512) |
| Los overlays de SPI no exponen `/dev/spidev*` | Bloquea la pantalla | Verificar con `ls /dev/spidev*`; activar con `orangepi-config` o `/boot/orangepiEnv.txt` |
| Conflictos de hardware en la XIAO: timers LEDC, I²C compartido con la cámara y GPIO3 de strapping | El servo, el INA219 o la consola USB pueden fallar | Verificaciones al recibir la placa, descritas en el plan |
| La cámara no se autentica | Alguien en la LAN podría inyectar o leer fotos | Decisión pendiente del equipo: HMAC, punto de acceso dedicado o TLS ([`orangepi/vision/README.md`](orangepi/vision/README.md#limitaciones-conocidas)) |
| La API y MQTT aún sin control de acceso por rol | Cualquier equipo de la red llega a la API de energía y todos los clientes MQTT comparten usuario | ACL el 4 nov, `httpNodeAuth` y `GALERIA_TOKEN` el 11 nov; mientras tanto no se registra a ninguna persona real |
| Todo el stack en 4 GB de RAM | Visión, Docker y pantalla compiten por memoria | Vigilar la RAM con las métricas del SoC |

## Documentación

| Documento | Contenido |
|---|---|
| [`docs/HIGH-LEVEL-PLAN.md`](docs/HIGH-LEVEL-PLAN.md) | Plan de alto nivel: alcance, arquitectura, flujos, datos, hardware, experimentos, plan semanal y riesgos |
| [`docs/contrato-mqtt.md`](docs/contrato-mqtt.md) | Contrato de tópicos y payloads MQTT entre módulos |
| [`orangepi/README.md`](orangepi/README.md) | Operación del stack de borde: credenciales, arranque, acceso, energía y seguridad |
| [`orangepi/vision/README.md`](orangepi/vision/README.md) | Servicio de visión: estructura, compilación, pruebas y lo que falta |
| [`AGENTS.md`](AGENTS.md) | Reglas del repo: carriles, ramas, commits y seguridad de datos |
| [`docs/diagramas/arquitectura.svg`](docs/diagramas/arquitectura.svg) | Fuente editable del diagrama de arquitectura |

## Instalación y ejecución

> Solo existen el stack de borde y el esqueleto de la visión. Esta sección se completará conforme lleguen el firmware, la pantalla y el frontend.

### Stack de borde (en la Orange Pi)

Requisitos: Docker Compose 2.23.1 o posterior, arm64 y `openssl`. Guía completa: [`orangepi/README.md`](orangepi/README.md).

```bash
git clone https://github.com/GustavoVaAsc/AutoList.git
cd AutoList/orangepi
cp .env.example .env && chmod 600 .env   # sustituir cada cambiar_esta_clave con: openssl rand -hex 16
docker compose up -d --build
docker compose ps                        # mosquitto, postgres y nodered deben estar healthy
```

- **Node-RED:** `https://<ip>:1880`. Para entrar por la IP de la placa, agregarla a `NODERED_TLS_SAN` en `.env`.
- **Portainer:** solo escucha en la placa. Desde otro equipo: `ssh -L 9443:localhost:9443 orangepi@<ip>` y abrir `https://localhost:9443`.
- **Lecturas de prueba:** `herramientas/simular_energia.sh 5 1`, desde la raíz del repo. Antes de que la XIAO publique datos reales, borrar las simuladas con `--limpiar` ([detalle](orangepi/README.md#simulador)).

`orangepi/.env` tiene credenciales reales: nunca se sube al repo.

### Servicio de visión (en la Orange Pi)

Requisitos: g++ 12, CMake 3.16 o posterior, `libopencv-dev` 4.6 y el runtime VIPLite del fabricante. Guía completa: [`orangepi/vision/README.md`](orangepi/vision/README.md).

```bash
cd orangepi/vision
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

Hoy `build/vision_service` es un recorrido en seco con dobles en memoria, y `build/npu_smoke` mide una inferencia en la NPU. Los modelos `.nb` no se versionan.

### Por definir

Firmware de la XIAO (`firmware/xiao`), pantalla ILI9341 (`orangepi/pantalla` y su unidad systemd) y frontend web (`web/`).
