# Frontend web de registro

Aquí va el frontend de registro de alumnos: Vite + React + TypeScript.

- Dos vistas: Alumnos (lista por grupo, búsqueda, re-registrar y borrar rostro)
  y Registro (consentimiento, miniaturas de las 5 poses, advertencia de
  parecido, prueba inmediata y control manual del servo, los LEDs y el buzzer).
- `mqtt.js` sobre WebSocket (puerto 9001) para el progreso del registro en vivo.
- La API HTTP de Node-RED para todo lo demás.
- Sin vista previa en vivo en la v1: el servidor de cámara de la ESP32 maneja
  mal un stream abierto mientras recibe capturas.

Capa: **administración**. Responsable: **Yordi** (sección 12.2 del plan).

El build no se versiona: se genera y se copia a `/data/public` de Node-RED.
