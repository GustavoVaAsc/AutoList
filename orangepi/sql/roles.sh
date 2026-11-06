#!/bin/sh
# Rol de la aplicación para Node-RED, sin privilegios de superusuario.
# Se ejecuta en el primer arranque, después de schema.sql (el compose los monta
# como 01-schema.sql y 02-roles.sh). Usa NODERED_DB_USER y NODERED_DB_PASSWORD.
#
# Se puede repetir: crea el rol si falta, le pone la contraseña actual del .env
# y reaplica los permisos. Revoca tres cosas: CREATE en el esquema public para
# PUBLIC, y UPDATE a nivel de tabla sobre alumnos y sobre sesiones para el rol
# (antes de conceder el UPDATE por columnas). Los permisos van en una sola transacción.
# Requiere que las tablas de schema.sql ya existan: en un volumen creado antes
# de agregarlas falla a mitad. El rol y su contraseña ya quedan aplicados (van
# antes y fuera de la transacción), pero la transacción de permisos se revierte
# completa. Ver el comentario del docker-compose.yml.
#
# Solo se concede lo que los flujos necesitan; no hay DELETE, TRUNCATE ni DDL.
# Tablas nuevas: agregar aquí sus GRANT explícitos con solo lo que Node-RED
# necesite. No se usan ALTER DEFAULT PRIVILEGES a propósito:
# darían permisos automáticos sobre tablas que todavía no se han revisado.
set -eu

: "${NODERED_DB_USER:?falta NODERED_DB_USER}"
: "${NODERED_DB_PASSWORD:?falta NODERED_DB_PASSWORD}"

# Las variables de psql (:"rol", :'clave') entrecomillan identificador y literal.
# Dentro de $$...$$ psql no las sustituye, por eso el CREATE se arma con format()
# y se ejecuta con \gexec solo si el rol no existe.
psql -v ON_ERROR_STOP=1 --username "$POSTGRES_USER" --dbname "$POSTGRES_DB" \
    -v rol="$NODERED_DB_USER" -v clave="$NODERED_DB_PASSWORD" <<'SQL'
SELECT format('CREATE ROLE %I LOGIN NOSUPERUSER NOCREATEDB NOCREATEROLE NOREPLICATION', :'rol')
 WHERE NOT EXISTS (SELECT FROM pg_roles WHERE rolname = :'rol')
\gexec

ALTER ROLE :"rol" PASSWORD :'clave';

BEGIN;

-- Nadie, salvo el dueño, crea objetos en el esquema public.
REVOKE CREATE ON SCHEMA public FROM PUBLIC;

GRANT SELECT, INSERT ON metricas_energia TO :"rol";
GRANT USAGE ON SEQUENCE metricas_energia_id_seq TO :"rol";

-- Registrar y retirar un rostro (plan, sección 5.4) son UPDATE, no DELETE. Solo
-- se pueden actualizar estas columnas: no_cuenta y fecha_alta quedan fijas. El
-- REVOKE hace que una repetición converja si antes había UPDATE en toda la tabla.
REVOKE UPDATE ON alumnos FROM :"rol";
GRANT SELECT, INSERT ON alumnos TO :"rol";
GRANT UPDATE (nombre, grupo, embedding, consentimiento, consentimiento_fecha) ON alumnos TO :"rol";

-- Iniciar una sesión es un INSERT, y cerrarla (terminar, o iniciar otra con una olvidada abierta)
-- es un UPDATE de "fin" (plan, sección 5.3): solo esa columna. Como en alumnos, el REVOKE hace
-- que una repetición converja si antes había UPDATE en toda la tabla.
REVOKE UPDATE ON sesiones FROM :"rol";
GRANT SELECT, INSERT ON sesiones TO :"rol";
GRANT UPDATE (fin) ON sesiones TO :"rol";
GRANT USAGE ON SEQUENCE sesiones_id_seq TO :"rol";

GRANT SELECT, INSERT ON asistencias TO :"rol";
GRANT USAGE ON SEQUENCE asistencias_id_seq TO :"rol";

GRANT SELECT, INSERT ON intentos TO :"rol";
GRANT USAGE ON SEQUENCE intentos_id_seq TO :"rol";

COMMIT;
SQL
