#!/usr/bin/env bash
# Prueba de humo de punta a punta del servicio de vision.
#
# Levanta su propio broker MQTT (puerto 1884), su propia camara falsa, arranca el
# servicio en --dry-run con una configuracion temporal, publica un PIR y espera el
# resultado en aula/vision/resultado.
#
# NO toca los contenedores del proyecto pase-lista: el broker real corre en el 1883 y
# los contenedores de ese proyecto no se tocan. Todo lo que este script levanta se
# limpia al salir, pase lo que pase.
#
# Uso: tools/humo.sh
# Salida: 0 si llego el resultado, distinto de cero si no.

set -u -o pipefail

RAIZ="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${BUILD:-$RAIZ/build}"

PUERTO_BROKER=1884
PUERTO_CAMARA=18080
HOST_BROKER=127.0.0.1
TOPICO_RESULTADO="aula/vision/resultado"
TOPICO_PIR="aula/camara/pir"
ESPERA_RESULTADO=30
NOMBRE_BROKER="vision-mosquitto-humo"

TEMPORAL=""
PID_SERVICIO=""
PID_CAMARA=""
PID_SONDA=""
BROKER_PROPIO=0
CODIGO=1

log() { printf '[humo] %s\n' "$*"; }
error() { printf '[humo] ERROR: %s\n' "$*" >&2; }

# Espera a que un puerto TCP acepte conexiones (host, puerto, segundos).
espera_puerto() {
  local host="$1" puerto="$2" limite="$3" intento=0
  while [ "$intento" -lt "$((limite * 5))" ]; do
    if (exec 3<>"/dev/tcp/$host/$puerto") 2>/dev/null; then
      return 0
    fi
    sleep 0.2
    intento=$((intento + 1))
  done
  return 1
}

limpiar() {
  local pid
  for pid in "$PID_SERVICIO" "$PID_CAMARA" "$PID_SONDA"; do
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null
      # Se le da margen para que el servicio salga limpio (publica el estado con
      # en_linea:false y cierra el broker) antes de insistir con SIGKILL.
      sleep 2
      kill -9 "$pid" 2>/dev/null
    fi
  done
  PID_SERVICIO=""
  PID_CAMARA=""
  PID_SONDA=""
  if [ "$BROKER_PROPIO" = "1" ]; then
    docker rm -f "$NOMBRE_BROKER" >/dev/null 2>&1
  fi
  if [ -n "$TEMPORAL" ] && [ -d "$TEMPORAL" ]; then
    rm -f "$TEMPORAL"/* 2>/dev/null
    rmdir "$TEMPORAL" 2>/dev/null
  fi
}
trap limpiar EXIT
trap 'limpiar; exit 130' INT
trap 'limpiar; exit 143' TERM

# --- Comprobaciones previas ---------------------------------------------------
for binario in vision_service camara_falsa mqtt_probe; do
  if [ ! -x "$BUILD/$binario" ]; then
    error "falta $BUILD/$binario: compila antes (cmake -S . -B build && cmake --build build -j4)"
    exit 1
  fi
done

TEMPORAL="$(mktemp -d /tmp/vision-humo-XXXXXX)"

# --- Broker de pruebas --------------------------------------------------------
if espera_puerto "$HOST_BROKER" "$PUERTO_BROKER" 1; then
  # El puerto esta ocupado: se reutiliza el broker de pruebas que ya haya.
  EXISTENTE="$(docker ps --filter "name=vision-mosquitto" --format '{{.Names}}' 2>/dev/null | head -1)"
  if [ -z "$EXISTENTE" ]; then
    error "el puerto $PUERTO_BROKER esta ocupado y no es el broker de pruebas de vision"
    error "no se levanta otro broker: el de la prueba seria una copia de otra cosa"
    exit 1
  fi
  log "se reutiliza el broker de pruebas $EXISTENTE (puerto $PUERTO_BROKER); no se borra al salir"
else
  CONF_BROKER="$TEMPORAL/mosquitto.conf"
  cat >"$CONF_BROKER" <<'CONF'
listener 1884
allow_anonymous true
CONF
  chmod 644 "$CONF_BROKER"
  if ! docker run --rm -d --name "$NOMBRE_BROKER" -p "127.0.0.1:$PUERTO_BROKER:1884" \
      -v "$CONF_BROKER":/mosquitto/config/mosquitto.conf:ro eclipse-mosquitto:2 >/dev/null; then
    error "no se pudo levantar el broker de pruebas (docker run)"
    exit 1
  fi
  BROKER_PROPIO=1
  if ! espera_puerto "$HOST_BROKER" "$PUERTO_BROKER" 20; then
    error "el broker de pruebas no acepto conexiones en el puerto $PUERTO_BROKER"
    exit 1
  fi
  log "broker de pruebas levantado en $HOST_BROKER:$PUERTO_BROKER ($NOMBRE_BROKER)"
fi

# --- Camara falsa -------------------------------------------------------------
if espera_puerto 127.0.0.1 "$PUERTO_CAMARA" 1; then
  error "el puerto $PUERTO_CAMARA esta ocupado: la camara falsa no puede levantarse"
  exit 1
fi
"$BUILD/camara_falsa" --puerto "$PUERTO_CAMARA" >"$TEMPORAL/camara.log" 2>&1 &
PID_CAMARA=$!
if ! espera_puerto 127.0.0.1 "$PUERTO_CAMARA" 20; then
  error "la camara falsa no acepto conexiones en el puerto $PUERTO_CAMARA"
  cat "$TEMPORAL/camara.log" >&2
  exit 1
fi
log "camara falsa escuchando en 127.0.0.1:$PUERTO_CAMARA/capture"

# --- Configuracion temporal ---------------------------------------------------
CONFIG="$TEMPORAL/vision.conf"
cat >"$CONFIG" <<CONF
{
  "mqtt": {"host": "$HOST_BROKER", "port": $PUERTO_BROKER, "usuario": "",
           "clave_env": "MQTT_PASSWORD", "client_id": "vision-humo"},
  "captura": {"url": "http://127.0.0.1:$PUERTO_CAMARA/capture", "rafaga": 2,
              "espera_luz_ms": 100, "http_timeout_ms": 2000, "max_body_bytes": 2097152},
  "galeria": {"url": "http://127.0.0.1:1/api/galeria", "token_env": "GALERIA_TOKEN",
              "timeout_ms": 1000},
  "umbrales": {"min_sharpness": 0.0, "min_recognized_score": 0.6, "min_dudoso_score": 0.45},
  "servicio": {"poll_ms": 20, "max_inbox_events": 16, "modelo": "w600k_mbf-uint8-v1",
               "acepta_png": false},
  "http": {"allow_invalid_certificates": false}
}
CONF

# --- Servicio -----------------------------------------------------------------
"$BUILD/vision_service" --dry-run --config "$CONFIG" >"$TEMPORAL/servicio.log" 2>&1 &
PID_SERVICIO=$!
sleep 1
if ! kill -0 "$PID_SERVICIO" 2>/dev/null; then
  error "el servicio no arranco"
  cat "$TEMPORAL/servicio.log" >&2
  exit 1
fi
log "servicio arrancado (pid $PID_SERVICIO)"

# --- Sonda: escuchar el resultado y publicar el PIR ---------------------------
"$BUILD/mqtt_probe" --host "$HOST_BROKER" --puerto "$PUERTO_BROKER" \
  --escuchar "$TOPICO_RESULTADO" --segundos "$ESPERA_RESULTADO" \
  >"$TEMPORAL/resultado.txt" 2>"$TEMPORAL/resultado.err" &
PID_SONDA=$!
# La sonda se suscribe al conectarse: se le da un instante antes de publicar.
sleep 1

log "publicando un PIR en $TOPICO_PIR"
if ! "$BUILD/mqtt_probe" --host "$HOST_BROKER" --puerto "$PUERTO_BROKER" \
    --publicar "$TOPICO_PIR" '{"mov":1}' >"$TEMPORAL/publicar.log" 2>&1; then
  error "no se pudo publicar el PIR"
  cat "$TEMPORAL/publicar.log" >&2
  exit 1
fi

# --- Espera del resultado -----------------------------------------------------
FIN=$((SECONDS + ESPERA_RESULTADO))
while [ "$SECONDS" -lt "$FIN" ]; do
  if [ -s "$TEMPORAL/resultado.txt" ]; then
    break
  fi
  sleep 0.5
done

if [ ! -s "$TEMPORAL/resultado.txt" ]; then
  error "no llego ningun mensaje a $TOPICO_RESULTADO en $ESPERA_RESULTADO s"
  echo "--- log del servicio ---" >&2
  cat "$TEMPORAL/servicio.log" >&2
  echo "--- log de la sonda ---" >&2
  cat "$TEMPORAL/resultado.err" >&2
  exit 1
fi

echo "--- resultado recibido en $TOPICO_RESULTADO ---"
cat "$TEMPORAL/resultado.txt"
CODIGO=0
log "prueba de humo correcta"
exit "$CODIGO"
