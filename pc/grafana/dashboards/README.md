# Paneles de Grafana

Aquí van los dashboards de Grafana de la PC, exportados como JSON.

- Cuatro paneles según el alcance vigente: asistencia por sesión, latencia de
  inferencia, potencia y confianza.
- Fuentes de datos: Postgres (asistencia) e InfluxDB (métricas y latencias).
- Los dashboards se exportan al repo después de cada cambio; `pc/grafana/data/`
  no se versiona.

Capa: **almacenamiento y visualización**. Responsable: **Arnau** (sección 12.2
del plan).
