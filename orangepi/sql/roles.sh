#!/bin/sh
# Rol de la aplicación para Node-RED, sin privilegios de superusuario.
# Se ejecuta en el primer arranque, después de schema.sql (el compose los monta
# como 01-schema.sql y 02-roles.sh). Usa NODERED_DB_USER y NODERED_DB_PASSWORD.
#
# Se puede repetir: crea el rol si falta, le pone la contraseña actual del .env
# y reaplica los permisos. En un volumen que ya existía (initdb no vuelve a
# correr) se ejecuta a mano; ver el comentario del docker-compose.yml.
#
# Tablas nuevas (alumnos, sesiones...): agregar aquí sus GRANT explícitos con
# solo lo que Node-RED necesite. No se usan ALTER DEFAULT PRIVILEGES a propósito:
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

-- Nadie, salvo el dueño, crea objetos en el esquema public.
REVOKE CREATE ON SCHEMA public FROM PUBLIC;

GRANT SELECT, INSERT ON metricas_energia TO :"rol";
GRANT USAGE ON SEQUENCE metricas_energia_id_seq TO :"rol";
SQL
