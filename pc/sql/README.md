# Esquema de SQL

Aquí va el esquema de la base de datos de la PC (`schema.sql`).

- Postgres con la extensión `pgvector`: tablas `alumnos`, `sesiones`,
  `asistencias` e `intentos`.
- `alumnos.embedding` es un `vector(512)`; `NULL` significa alumno pendiente de
  rostro.
- La restricción `UNIQUE (no_cuenta, sesion_id)` de `asistencias` reemplaza al
  `checkIfAlreadyRegistered()` del proyecto previo (regla anti-doble registro).
- Si el profesor exige MariaDB, solo cambian `vector` → `BLOB` y `TIMESTAMPTZ`
  → `DATETIME`; el resto es SQL estándar.

Capa: **almacenamiento**. Responsable: **Yordi** (sección 12.2 del plan).
