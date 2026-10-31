# Node-RED

Aquí va el flujo de Node-RED (`flows.json`) que corre en la Orange Pi.

- Ingesta MQTT → InfluxDB (energía, métricas y latencias).
- Pase de lista: resultado de visión → reglas de sesión → Postgres, actuador y
  pantalla.
- Sesiones: iniciar, terminar y generar el reporte CSV.
- Nodo `exec` que reinicia por SSH la unidad systemd de la ILI9341.
- API HTTP y frontend: los endpoints de la sección 6.7 del plan y los archivos
  estáticos de `web/` servidos con `httpStatic` desde `/data/public`.

Capa: **borde**. Responsable: **Yordi** (sección 12.2 del plan).

`flows.json` se exporta al repo después de cada cambio. `nodered/data/` no se
versiona.
