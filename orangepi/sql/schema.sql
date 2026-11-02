-- Esquema de Postgres (plan, sección 7.2). Se ejecuta una sola vez, al crear
-- el volumen de datos por primera vez (/docker-entrypoint-initdb.d).
-- Las tablas van en el orden que piden las llaves foráneas. El embedding es
-- vector(512), dimensión fijada por el equipo (Gustavo).

CREATE EXTENSION IF NOT EXISTS vector;

-- Zona horaria de la base del proyecto (plan, sección 7.2). Los timestamptz se
-- guardan siempre en UTC; esto solo decide en qué día cae una hora al hacer
-- ::date, date_trunc('day', ...) o CURRENT_DATE. Aplica a las sesiones nuevas de
-- cualquier rol, sin tocar la zona de los logs (log_timezone sigue en UTC).
-- ALTER DATABASE no acepta current_database() directo, de ahí el EXECUTE.
-- Las consultas de reporte igual escriben la zona (AT TIME ZONE 'America/Mexico_City').
DO $$
BEGIN
  EXECUTE format('ALTER DATABASE %I SET timezone = %L',
                 current_database(), 'America/Mexico_City');
END
$$;

CREATE TABLE alumnos (
  no_cuenta      VARCHAR(9) PRIMARY KEY CHECK (no_cuenta ~ '^[0-9]{9}$'),
  nombre         TEXT        NOT NULL,
  grupo          TEXT        NOT NULL,
  embedding      vector(512),            -- NULL = pendiente de rostro
  consentimiento BOOLEAN     NOT NULL DEFAULT FALSE,
  consentimiento_fecha TIMESTAMPTZ,      -- cuándo el profesor registró el consentimiento; formato: docs/consentimiento.md (pendiente)
  fecha_alta     TIMESTAMPTZ NOT NULL DEFAULT now(),
  -- Sin consentimiento no puede haber embedding, y el consentimiento siempre
  -- tiene fecha. Registrar: primero consentimiento=true y
  -- consentimiento_fecha=now() (con embedding NULL) y después el embedding.
  -- Retirar: embedding=NULL, consentimiento=false y consentimiento_fecha=NULL
  -- (plan, sección 5.4).
  CONSTRAINT alumnos_embedding_requiere_consentimiento
    CHECK (embedding IS NULL OR consentimiento),
  CONSTRAINT alumnos_consentimiento_con_fecha
    CHECK (consentimiento = (consentimiento_fecha IS NOT NULL))
);
-- Al retirar un rostro, autovacuum procesa la tabla (y su TOAST, donde viven los
-- vectores grandes) en cuanto hay una sola tupla muerta, y su espacio queda
-- reutilizable. Los bytes pueden seguir en el archivo hasta que otra fila los
-- sobrescriba (y en el WAL o en copias); esto reduce la ventana, no la elimina.
ALTER TABLE alumnos SET (
  autovacuum_vacuum_threshold = 0,
  autovacuum_vacuum_scale_factor = 0,
  toast.autovacuum_vacuum_threshold = 0,
  toast.autovacuum_vacuum_scale_factor = 0
);
-- Sin índice vectorial: la galería es de unos 40 alumnos y la comparación se
-- hace en el servicio de visión; un escaneo secuencial basta para las
-- consultas de auditoría.

CREATE TABLE sesiones (
  id             SERIAL PRIMARY KEY,
  materia        TEXT        NOT NULL,
  grupo          TEXT        NOT NULL,
  inicio         TIMESTAMPTZ NOT NULL DEFAULT now(),
  fin            TIMESTAMPTZ,
  tolerancia_min INT         NOT NULL DEFAULT 10
);
-- A lo más una sesión abierta (fin IS NULL) a la vez.
CREATE UNIQUE INDEX sesiones_una_abierta_idx ON sesiones ((true)) WHERE fin IS NULL;

CREATE TABLE asistencias (
  id        SERIAL PRIMARY KEY,
  no_cuenta VARCHAR(9)  NOT NULL REFERENCES alumnos,
  sesion_id INT         NOT NULL REFERENCES sesiones,
  hora      TIMESTAMPTZ NOT NULL DEFAULT now(),
  estado    TEXT        NOT NULL CHECK (estado IN ('presente','retardo')),
  confianza REAL,
  metodo    TEXT        NOT NULL CHECK (metodo IN ('rostro','manual')),
  UNIQUE (no_cuenta, sesion_id)          -- reemplaza checkIfAlreadyRegistered()
);

-- Quien no se reconoce no dio consentimiento: aquí no se guarda su embedding.
CREATE TABLE intentos (
  id            SERIAL PRIMARY KEY,
  hora          TIMESTAMPTZ NOT NULL DEFAULT now(),
  sesion_id     INT REFERENCES sesiones,
  confianza_max REAL,
  motivo        TEXT CHECK (motivo IN ('desconocido','imagen_insuficiente','duplicado'))
);

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
