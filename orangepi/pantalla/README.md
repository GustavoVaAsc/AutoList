# Pantalla ILI9341

Aquí va el programa en Python de la pantalla ILI9341 con táctil XPT2046.

- Corre en el host de la Orange Pi como unidad systemd, **fuera de Docker**:
  necesita SPI, GPIO e I²C.
- Tres vistas: sesión, lista y guía de registro.
- Habla con Node-RED solo por MQTT: publica `aula/sesion/cmd` y se suscribe a
  `aula/vision/enrolamiento` y `aula/sesion/estado`.
- El RTC DS3231 va por I²C y da la hora cuando no hay NTP.

Capa: **borde**. Responsable: **Arnau** (sección 12.2 del plan).
