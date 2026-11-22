// Lógica pura del estado del sistema: validar la respuesta de GET /api/sistema/estado (pestaña Sistema de
// nodered/flows/flows.json) y decir con palabras cómo están el reconocimiento, el nodo del aula (la XIAO) y los
// actuadores. Los textos de la hoja y de la cabecera salen de aquí para poder probarlos.
// Sin DOM, sin fetch y sin importar nada: `node --test web/pruebas/*.test.js` carga este archivo
// directo en Node 22.18+ quitando los tipos.

export type ModoVision = 'inactivo' | 'pase' | 'enrolando'
export type EstadoServo = 'abierto' | 'cerrado'
export type ColorLed = 'azul' | 'verde' | 'rojo' | 'ambar' | 'apagado'

/** aula/vision/estado. Lo que no llegó (el Will solo trae `en_linea`) queda en null. */
export interface EstadoVision {
  enLinea: boolean
  modo: ModoVision | null
  galeria: number | null
  modelo: string | null
}

/** aula/camara/estado: la salud de la XIAO. `camara`, `corte` y `nvs` los manda el firmware en marcha. */
export interface EstadoCamara {
  enLinea: boolean
  rssi: number | null
  heap: number | null
  temp: number | null
  camara: boolean | null
  corte: boolean | null
  nvs: boolean | null
}

/** aula/actuador/estado. `luz` es true si la tira está encendida. */
export interface EstadoActuadores {
  servo: EstadoServo | null
  led: ColorLed | null
  luz: boolean | null
}

/** aula/sistema/metricas: la placa (temperatura del SoC en °C; CPU y RAM en %). */
export interface MetricasSistema {
  temp: number
  cpu: number
  ram: number
}

/** Un estado con la hora (ms desde 1970, UTC) en que Node-RED lo recibió. */
export interface Recibido<T> {
  t: number
  datos: T
}

/** Cada entrada es null si Node-RED no ha recibido nada desde que arrancó. */
export interface Sistema {
  vision: Recibido<EstadoVision> | null
  camara: Recibido<EstadoCamara> | null
  actuadores: Recibido<EstadoActuadores> | null
  metricas: Recibido<MetricasSistema> | null
}

export type ResultadoSistema = { ok: true; sistema: Sistema } | { ok: false; motivo: string }

type Objeto = Record<string, unknown>
type Lectura<T> = { ok: true; valor: T } | { ok: false; campo: string }

const MODOS: readonly string[] = ['inactivo', 'pase', 'enrolando']
const SERVOS: readonly string[] = ['abierto', 'cerrado']
const LEDS: readonly string[] = ['azul', 'verde', 'rojo', 'ambar', 'apagado']

function esObjeto(x: unknown): x is Objeto {
  return typeof x === 'object' && x !== null && !Array.isArray(x)
}

// Un campo opcional: ausente es "no llegó" (la API omite lo que el dispositivo no mandó); si viene, debe cumplir
// `valido`, y un null no lo cumple.
function opcional<T>(
  o: Objeto,
  campo: string,
  valido: (x: unknown) => boolean,
  convertir?: (x: unknown) => T,
): Lectura<T | null> {
  const x = o[campo]
  if (x === undefined) return { ok: true, valor: null }
  if (!valido(x)) return { ok: false, campo }
  return { ok: true, valor: convertir === undefined ? (x as T) : convertir(x) }
}

// Un campo obligatorio y no nulo.
function obligatorio<T>(o: Objeto, campo: string, valido: (x: unknown) => boolean): Lectura<T> {
  const x = o[campo]
  return valido(x) ? { ok: true, valor: x as T } : { ok: false, campo }
}

const esBooleano = (x: unknown) => typeof x === 'boolean'
const entre = (min: number, max: number) => (x: unknown) =>
  typeof x === 'number' && Number.isFinite(x) && x >= min && x <= max
const esEnteroNoNegativo = (x: unknown) => typeof x === 'number' && Number.isInteger(x) && x >= 0
const esTexto = (x: unknown) => typeof x === 'string' && x.length <= 100
const uno = (opciones: readonly string[]) => (x: unknown) => typeof x === 'string' && opciones.includes(x)

type Valores<T extends Record<string, Lectura<unknown>>> = { [K in keyof T]: T[K] extends Lectura<infer V> ? V : never }

/** Junta lecturas de campos: el primer campo inválido manda el rechazo. */
function campos<T extends Record<string, Lectura<unknown>>>(
  lecturas: T,
): { ok: true; valores: Valores<T> } | { ok: false; campo: string } {
  const valores: Record<string, unknown> = {}
  for (const [nombre, lectura] of Object.entries(lecturas)) {
    if (!lectura.ok) return { ok: false, campo: lectura.campo }
    valores[nombre] = lectura.valor
  }
  return { ok: true, valores: valores as Valores<T> }
}

function leerVision(d: Objeto): Lectura<EstadoVision> {
  const r = campos({
    enLinea: obligatorio<boolean>(d, 'en_linea', esBooleano),
    modo: opcional<ModoVision>(d, 'modo', uno(MODOS)),
    galeria: opcional<number>(d, 'galeria', esEnteroNoNegativo),
    modelo: opcional<string>(d, 'modelo', esTexto),
  })
  return r.ok ? { ok: true, valor: r.valores } : r
}

function leerCamara(d: Objeto): Lectura<EstadoCamara> {
  const r = campos({
    enLinea: obligatorio<boolean>(d, 'en_linea', esBooleano),
    rssi: opcional<number>(d, 'rssi', entre(-200, 20)),
    heap: opcional<number>(d, 'heap', esEnteroNoNegativo),
    temp: opcional<number>(d, 'temp', entre(-40, 150)),
    camara: opcional<boolean>(d, 'camara', esBooleano),
    corte: opcional<boolean>(d, 'corte', esBooleano),
    nvs: opcional<boolean>(d, 'nvs', esBooleano),
  })
  return r.ok ? { ok: true, valor: r.valores } : r
}

function leerActuadores(d: Objeto): Lectura<EstadoActuadores> {
  const r = campos({
    servo: opcional<EstadoServo>(d, 'servo', uno(SERVOS)),
    led: opcional<ColorLed>(d, 'led', uno(LEDS)),
    luz: opcional<boolean>(d, 'luz', (x) => x === 0 || x === 1, (x) => x === 1),
  })
  return r.ok ? { ok: true, valor: r.valores } : r
}

function leerMetricas(d: Objeto): Lectura<MetricasSistema> {
  const r = campos({
    temp: obligatorio<number>(d, 'temp', entre(-40, 150)),
    cpu: obligatorio<number>(d, 'cpu', entre(0, 100)),
    ram: obligatorio<number>(d, 'ram', entre(0, 100)),
  })
  return r.ok ? { ok: true, valor: r.valores } : r
}

type Entrada<T> = { ok: true; valor: Recibido<T> | null } | { ok: false; motivo: string }

function leerEntrada<T>(cuerpo: Objeto, clave: string, leerDatos: (d: Objeto) => Lectura<T>): Entrada<T> {
  const entrada = cuerpo[clave]
  if (!esObjeto(entrada)) {
    return { ok: false, motivo: `falta ${clave} en la respuesta` }
  }
  const { recibido, datos } = entrada
  if (recibido === null && datos === null) {
    return { ok: true, valor: null }
  }
  const t = typeof recibido === 'string' && recibido !== '' ? Date.parse(recibido) : Number.NaN
  if (Number.isNaN(t)) {
    return { ok: false, motivo: `${clave}: la hora de recepción no es una fecha` }
  }
  if (!esObjeto(datos)) {
    return { ok: false, motivo: `${clave}: trae hora de recepción pero no datos` }
  }
  const leidos = leerDatos(datos)
  if (!leidos.ok) {
    return { ok: false, motivo: `${clave}: el campo ${leidos.campo} no es válido` }
  }
  return { ok: true, valor: { t, datos: leidos.valor } }
}

/**
 * Convierte el JSON de GET /api/sistema/estado. Los campos que el contrato no conoce se ignoran (contrato
 * MQTT, sección 1). El motivo de un rechazo nombra la entrada y el campo, nunca el valor recibido.
 */
export function leerSistema(cuerpo: unknown): ResultadoSistema {
  if (!esObjeto(cuerpo)) {
    return { ok: false, motivo: 'se esperaba un objeto con vision, camara, actuadores y metricas' }
  }
  const vision = leerEntrada(cuerpo, 'vision', leerVision)
  if (!vision.ok) return vision
  const camara = leerEntrada(cuerpo, 'camara', leerCamara)
  if (!camara.ok) return camara
  const actuadores = leerEntrada(cuerpo, 'actuadores', leerActuadores)
  if (!actuadores.ok) return actuadores
  const metricas = leerEntrada(cuerpo, 'metricas', leerMetricas)
  if (!metricas.ok) return metricas
  return {
    ok: true,
    sistema: { vision: vision.valor, camara: camara.valor, actuadores: actuadores.valor, metricas: metricas.valor },
  }
}

// ---- Qué decir de cada uno ----------------------------------------------------------------------

export type Conexion = 'en-linea' | 'fuera-de-linea' | 'sin-datos'

export interface FilaSistema {
  clave: 'vision' | 'camara' | 'actuadores'
  nombre: string
  /** Lo que dice la marca de asistencia: palomita (en línea), tachita (fuera de línea) o casilla vacía (sin datos). */
  conexion: Conexion
  /** La conexión en palabras: lo que lleva la información; la marca solo lo refuerza. */
  estado: string
  /** Los campos relevantes del último estado, uno por elemento; vacío si no hay ninguno que decir. */
  detalle: string[]
  /** Hora (ms) en que Node-RED recibió ese estado; null si nunca llegó. */
  t: number | null
}

const ESTADO: Record<Conexion, string> = {
  'en-linea': 'En línea',
  'fuera-de-linea': 'Fuera de línea',
  'sin-datos': 'Sin datos',
}

/** Con esta señal (dBm) o menos, el nodo del aula se conecta mal. */
export const SENAL_DEBIL_DBM = -80
/** Con menos de esta memoria libre (bytes) el nodo del aula puede quedarse sin recursos. */
export const MEMORIA_MINIMA_BYTES = 30 * 1024

function detalleVision(d: EstadoVision): string[] {
  const detalle: string[] = []
  if (d.modo === 'inactivo') detalle.push('Modo inactivo')
  if (d.modo === 'pase') detalle.push('Modo pase de lista')
  if (d.modo === 'enrolando') detalle.push('Registrando un rostro')
  if (d.galeria !== null) detalle.push(`${d.galeria} ${d.galeria === 1 ? 'rostro registrado' : 'rostros registrados'}`)
  return detalle
}

// Dice si la cámara está encendida y, de lo demás (señal, memoria, memoria flash, cortes), solo lo que falla.
function detalleCamara(d: EstadoCamara): string[] {
  const detalle: string[] = []
  if (d.camara !== null) detalle.push(d.camara ? 'Cámara encendida' : 'Cámara apagada')
  if (d.rssi !== null && d.rssi <= SENAL_DEBIL_DBM) detalle.push('Señal débil')
  if (d.heap !== null && d.heap < MEMORIA_MINIMA_BYTES) detalle.push('Poca memoria')
  if (d.nvs === false) detalle.push('Falla de la memoria interna')
  if (d.corte === true) detalle.push('Capturas suspendidas por temperatura')
  return detalle
}

function detalleActuadores(d: EstadoActuadores): string[] {
  const detalle: string[] = []
  if (d.servo !== null) detalle.push(`Puerta ${d.servo === 'abierto' ? 'abierta' : 'cerrada'}`)
  if (d.led !== null) detalle.push(`LED ${d.led === 'ambar' ? 'ámbar' : d.led}`)
  if (d.luz !== null) detalle.push(d.luz ? 'Luz encendida' : 'Luz apagada')
  return detalle
}

function fila(clave: FilaSistema['clave'], nombre: string, conexion: Conexion, detalle: string[], t: number | null): FilaSistema {
  return { clave, nombre, conexion, estado: ESTADO[conexion], detalle, t }
}

function conexionDe(estado: Recibido<{ enLinea: boolean }> | null): Conexion {
  return estado === null ? 'sin-datos' : estado.datos.enLinea ? 'en-linea' : 'fuera-de-linea'
}

/**
 * Las tres filas de la hoja Sistema. El Will (`en_linea` false) deja a la visión y a la cámara fuera de línea y
 * sin detalle: el resto de sus campos ya no vale. Los actuadores no tienen Will propio: viven en la XIAO, así que
 * con la cámara fuera de línea también ellos lo están (con su último estado conocido); sin datos de la cámara no
 * hay nada que lo contradiga. Sin respuesta todavía (`sistema` null) no hay filas.
 */
export function filasSistema(sistema: Sistema | null): FilaSistema[] {
  if (sistema === null) return []
  const { vision, camara, actuadores } = sistema

  const conexionVision = conexionDe(vision)
  const conexionCamara = conexionDe(camara)
  const filaVision = fila(
    'vision',
    'Reconocimiento',
    conexionVision,
    vision !== null && conexionVision === 'en-linea' ? detalleVision(vision.datos) : [],
    vision?.t ?? null,
  )
  const filaCamara = fila(
    'camara',
    'Nodo del aula',
    conexionCamara,
    camara !== null && conexionCamara === 'en-linea' ? detalleCamara(camara.datos) : [],
    camara?.t ?? null,
  )

  let filaActuadores: FilaSistema
  if (actuadores === null) {
    filaActuadores = fila('actuadores', 'Actuadores', 'sin-datos', [], null)
  } else if (conexionCamara === 'fuera-de-linea') {
    const ultimo = detalleActuadores(actuadores.datos).map((texto) => `Último estado: ${texto.toLowerCase()}`)
    filaActuadores = fila('actuadores', 'Actuadores', 'fuera-de-linea', ['Sin conexión con el nodo del aula', ...ultimo], actuadores.t)
  } else {
    filaActuadores = fila('actuadores', 'Actuadores', 'en-linea', detalleActuadores(actuadores.datos), actuadores.t)
  }
  return [filaVision, filaCamara, filaActuadores]
}

export interface DetalleCabecera {
  /** Lo que no está en línea, con su nombre: "nodo del aula fuera de línea". */
  texto: string
}

const TEXTO_CONEXION: Record<Conexion, string> = {
  'en-linea': 'en línea',
  'fuera-de-linea': 'fuera de línea',
  'sin-datos': 'sin datos',
}
// Qué se dice primero: lo caído, luego lo que no se sabe.
const GRAVEDAD: Record<Conexion, number> = { 'fuera-de-linea': 2, 'sin-datos': 1, 'en-linea': 0 }

/**
 * El detalle del indicador de la cabecera: la placa dice si responde y esto solo añade lo que falla, con su
 * nombre (el reconocimiento y el nodo del aula). Si todo está en línea no hay detalle, y tampoco mientras no hay
 * respuesta de la API. Iguales: "reconocimiento y nodo del aula fuera de línea". Distintos: lo peor primero
 * ("nodo del aula fuera de línea, reconocimiento sin datos").
 */
export function detalleCabecera(sistema: Sistema | null): DetalleCabecera | null {
  if (sistema === null) return null
  const partes = [
    { nombre: 'reconocimiento', conexion: conexionDe(sistema.vision) },
    { nombre: 'nodo del aula', conexion: conexionDe(sistema.camara) },
  ]
  const fallan = partes.filter((p) => p.conexion !== 'en-linea')
  if (fallan.length === 0) return null
  if (fallan.length === 2 && fallan[0]!.conexion === fallan[1]!.conexion) {
    return { texto: `${fallan[0]!.nombre} y ${fallan[1]!.nombre} ${TEXTO_CONEXION[fallan[0]!.conexion]}` }
  }
  fallan.sort((a, b) => GRAVEDAD[b.conexion] - GRAVEDAD[a.conexion])
  return { texto: fallan.map((p) => `${p.nombre} ${TEXTO_CONEXION[p.conexion]}`).join(', ') }
}
