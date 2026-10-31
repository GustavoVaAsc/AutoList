# Pruebas unitarias del firmware

Aquí van las pruebas unitarias del firmware de la XIAO ESP32S3 Sense.

- Cobertura mínima definida en el plan: cámara, INA219 y servo.
- Lo que no se prueba en la placa no se prueba en ninguna parte.
- Sirven para validar el reparto de los 4 timers LEDC del ESP32-S3 (servo,
  buzzer y los tres canales RGB) y que el servo no frena la captura.

Capa: **campo**. Responsable: **Arnau** (sección 12.2 del plan).
