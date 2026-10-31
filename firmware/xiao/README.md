# Firmware de la XIAO ESP32S3 Sense

Aquí va el firmware Arduino (C++) de la XIAO ESP32S3 Sense, el nodo de campo
del sistema.

- Captura de fotos bajo demanda por HTTP (`/capture?res=UXGA`) y publicación por
  MQTT de `aula/camara/pir`, `aula/camara/energia` y `aula/camara/estado`.
- Suscripción a `aula/actuador/cmd` y `aula/sesion/estado`; publicación de
  `aula/actuador/estado`.
- Actuadores: servo (puerta simulada), LEDs RGB, buzzer y etapa de potencia con
  MOSFET para la tira LED de 12 V.

Capa: **campo**. Responsable: **Arnau** (sección 12.2 del plan).

Los modelos `.nb` y los datasets no se versionan; van en releases.
