-- Esquema de Postgres (plan, sección 7.2). Se ejecuta una sola vez, al crear
-- el volumen de datos por primera vez (/docker-entrypoint-initdb.d).
-- Las tablas alumnos, sesiones, asistencias e intentos se agregan cuando se
-- fije la dimensión del embedding (512 o 128).

CREATE EXTENSION IF NOT EXISTS vector;

CREATE TABLE metricas_energia (
  id            SERIAL PRIMARY KEY,
  origen        TEXT        NOT NULL,     -- ina219, npu, sistema
  v             REAL,                     -- voltaje del INA219
  i             REAL,                     -- corriente del INA219
  p             REAL,                     -- potencia del INA219
  lat_cap_ms    INT,                      -- latencia de captura
  lat_det_ms    INT,                      -- latencia de detección
  lat_emb_ms    INT,                      -- latencia de embedding
  lat_total_ms  INT,                      -- latencia total de inferencia
  temp_soc      REAL,                     -- temperatura del SoC
  hora          TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- Para GET /api/metricas/energia: filtra por origen y ordena por hora e id,
-- de más nuevo a más viejo.
CREATE INDEX IF NOT EXISTS metricas_energia_origen_hora_idx
  ON metricas_energia (origen, hora DESC, id DESC);
