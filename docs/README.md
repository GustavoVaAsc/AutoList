# Documentación del proyecto

Índice de los documentos de `docs/`.

| Documento | Qué es |
|---|---|
| [HIGH-LEVEL-PLAN.md](HIGH-LEVEL-PLAN.md) | Plan en alto nivel vigente del proyecto: problema, alcance, arquitectura, componentes, datos, hardware, experimentos, estructura del repositorio, roles y riesgos. Es la fuente de verdad del repositorio. |
| [protocolos.md](protocolos.md) | Contrato de telemetría: tópicos MQTT con sus payloads, reglas transversales, ACL de Mosquitto, API HTTP de Node-RED y tabla de latencias. |
| [mqtt-topics.json](mqtt-topics.json) | La misma tabla de tópicos MQTT en JSON, legible por máquina, para que el firmware, los flujos de Node-RED y el servicio de visión compartan una sola lista. |
| [consentimiento.md](consentimiento.md) | Borrador del formato de consentimiento de los participantes del reconocimiento facial. Pendiente de revisión del profesor. |
| [diagramas/](diagramas/) | Diagramas del proyecto. `arquitectura.png` es el diagrama de la arquitectura por capas (campo, borde y PC). |

## Documentos que no se versionan

El documento interno del equipo (notas de trabajo, registro de decisiones,
estado del proyecto y desglose semanal) **no se versiona**: está en
`.gitignore` y solo lo ve el equipo. Lo que ve el profesor es
[HIGH-LEVEL-PLAN.md](HIGH-LEVEL-PLAN.md).
