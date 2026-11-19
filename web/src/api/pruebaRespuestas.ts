// Lógica pura de la prueba de reconocimiento (botón "Probar reconocimiento" de Métricas): validar la respuesta de
// GET /api/enrolamiento/prueba, traducir las de POST /api/enrolamiento/probar (pestaña Registro de
// nodered/flows/flows.json) y poner en palabras lo que decidió la visión.
// La prueba no marca asistencia ni necesita una sesión abierta; la respuesta no trae imagen.
// Sin DOM, sin fetch y sin importar nada: `node --test web/pruebas/*.test.js` carga este archivo
// directo en Node 22.18+ quitando los tipos.

export type DecisionPrueba = 'reconocido' | 'dudoso' | 'desconocido' | 'sin_rostro' | 'imagen_insuficiente' | 'error'

const DECISIONES: readonly DecisionPrueba[] = ['reconocido', 'dudoso', 'desconocido', 'sin_rostro', 'imagen_insuficiente', 'error']

/** Las etapas de la latencia de la visión, en ms; null si la etapa no corrió. */
export interface LatenciaPrueba {
  cap: number | null
  det: number | null
  emb: number | null
  total: number | null
}

export interface ResultadoPrueba {
  decision: DecisionPrueba
  /** Solo con reconocido o dudoso. */
  cuenta: string | null
  /** El nombre de esa cuenta en la lista de alumnos; null si no hay cuenta o ya no está en la lista. */
  nombre: string | null
  /** Similitud coseno del mejor candidato; null si no hubo candidato. */
  conf: number | null
  /** Diferencia con el segundo candidato; null si la galería tiene menos de dos alumnos. */
  margen: number | null
  /** Código fino de la decisión (`motivo` del contrato MQTT); null si no hay. */
  motivo: string | null
  /** null en un rechazo de la visión (no hubo ciclo que medir). */
  lat_ms: LatenciaPrueba | null
  /** Hora en que Node-RED recibió el resultado, en ms desde 1970 (UTC). */
  recibido: number
}

export interface Prueba {
  /** Se pidió una prueba y la visión aún no contesta (la API la da por perdida a los 15 s). */
  pendiente: boolean
  /** Hora de la última solicitud, en ms desde 1970 (UTC); null si no hubo ninguna desde que arrancó Node-RED. */
  solicitado: number | null
  resultado: ResultadoPrueba | null
}

export type LecturaPrueba = { ok: true; prueba: Prueba } | { ok: false; motivo: string }

function objeto(x: unknown): Record<string, unknown> | null {
  return typeof x === 'object' && x !== null && !Array.isArray(x) ? (x as Record<string, unknown>) : null
}

const numeroONulo = (x: unknown): number | null | undefined =>
  x === null ? null : typeof x === 'number' && Number.isFinite(x) ? x : undefined

const textoONulo = (x: unknown): string | null | undefined => (x === null ? null : typeof x === 'string' ? x : undefined)

function leerLatencia(x: unknown): LatenciaPrueba | null | undefined {
  if (x === null) {
    return null
  }
  const o = objeto(x)
  if (o === null) {
    return undefined
  }
  const etapas = { cap: numeroONulo(o.cap), det: numeroONulo(o.det), emb: numeroONulo(o.emb), total: numeroONulo(o.total) }
  if (etapas.cap === undefined || etapas.det === undefined || etapas.emb === undefined || etapas.total === undefined) {
    return undefined
  }
  return { cap: etapas.cap, det: etapas.det, emb: etapas.emb, total: etapas.total }
}

/**
 * Convierte el JSON de GET /api/enrolamiento/prueba en una `Prueba`. El motivo de un rechazo dice qué campo falla,
 * nunca el valor recibido (puede traer la cuenta de un alumno).
 */
export function leerPrueba(cuerpo: unknown): LecturaPrueba {
  const o = objeto(cuerpo)
  if (o === null) {
    return { ok: false, motivo: 'se esperaba un objeto con pendiente, solicitado y resultado' }
  }
  if (typeof o.pendiente !== 'boolean') {
    return { ok: false, motivo: 'pendiente no es verdadero ni falso' }
  }
  let solicitado: number | null = null
  if (o.solicitado !== null) {
    solicitado = typeof o.solicitado === 'string' ? Date.parse(o.solicitado) : Number.NaN
    if (Number.isNaN(solicitado)) {
      return { ok: false, motivo: 'solicitado no es una fecha' }
    }
  }
  if (o.resultado === null) {
    return { ok: true, prueba: { pendiente: o.pendiente, solicitado, resultado: null } }
  }
  const r = objeto(o.resultado)
  if (r === null) {
    return { ok: false, motivo: 'resultado no es un objeto' }
  }
  if (typeof r.decision !== 'string' || !DECISIONES.includes(r.decision as DecisionPrueba)) {
    return { ok: false, motivo: 'la decisión del resultado no es una de las conocidas' }
  }
  const recibido = typeof r.recibido === 'string' ? Date.parse(r.recibido) : Number.NaN
  if (Number.isNaN(recibido)) {
    return { ok: false, motivo: 'recibido no es una fecha' }
  }
  const cuenta = textoONulo(r.cuenta)
  const nombre = textoONulo(r.nombre)
  const conf = numeroONulo(r.conf)
  const margen = numeroONulo(r.margen)
  const motivo = textoONulo(r.motivo)
  const lat = leerLatencia(r.lat_ms)
  for (const [campo, valor] of [['cuenta', cuenta], ['nombre', nombre], ['conf', conf], ['margen', margen], ['motivo', motivo], ['lat_ms', lat]] as const) {
    if (valor === undefined) {
      return { ok: false, motivo: `${campo} del resultado no tiene el tipo esperado` }
    }
  }
  return {
    ok: true,
    prueba: {
      pendiente: o.pendiente,
      solicitado,
      resultado: {
        decision: r.decision as DecisionPrueba,
        cuenta: cuenta as string | null,
        nombre: nombre as string | null,
        conf: conf as number | null,
        margen: margen as number | null,
        motivo: motivo as string | null,
        lat_ms: lat as LatenciaPrueba | null,
        recibido,
      },
    },
  }
}

// ---- POST /api/enrolamiento/probar -------------------------------------------------------------------------------

export type ResultadoSolicitud =
  /** 202: la orden salió y la prueba queda pendiente. */
  | { tipo: 'solicitada' }
  /** 409 prueba_pendiente: ya había una en curso; se retoma su espera. */
  | { tipo: 'en-curso' }
  /** 409 registro_abierto: la visión está ocupada con un registro. */
  | { tipo: 'registro-abierto' }
  /** 429: otra solicitud hace menos de 3 s; esta no salió. */
  | { tipo: 'demasiado-rapido' }
  | { tipo: 'fallo'; status: number | null; mensaje: string }

/** Cuando no hubo respuesta (sin red o plazo agotado): puede que la API sí publicara la orden. */
export const SIN_RESPUESTA =
  'La placa no respondió. No se sabe si la prueba salió: revisa el resultado antes de reintentar.'

function fallo(status: number, mensaje: string): ResultadoSolicitud {
  return { tipo: 'fallo', status, mensaje }
}

/** Traduce la respuesta de POST /api/enrolamiento/probar. */
export function interpretarSolicitud(status: number, cuerpo: unknown): ResultadoSolicitud {
  const o = objeto(cuerpo)
  if (status === 202) {
    return o !== null && o.pendiente === true ? { tipo: 'solicitada' } : fallo(status, 'La placa respondió algo inesperado. Revisa el resultado antes de reintentar.')
  }
  if (status === 409) {
    if (o?.motivo === 'registro_abierto') {
      return { tipo: 'registro-abierto' }
    }
    if (o?.motivo === 'prueba_pendiente') {
      return { tipo: 'en-curso' }
    }
    return fallo(status, 'No se pudo pedir la prueba: hubo un conflicto. Intenta de nuevo.')
  }
  if (status === 429) {
    return { tipo: 'demasiado-rapido' }
  }
  if (status === 401) {
    return fallo(status, 'Falta iniciar sesión.')
  }
  if (status === 403) {
    return fallo(status, 'La placa rechazó la petición. Recarga la página e intenta de nuevo.')
  }
  return fallo(status, 'La placa no pudo atender la prueba. Intenta de nuevo en unos segundos.')
}

// ---- Qué decir ---------------------------------------------------------------------------------------------------

export const TITULO_DECISION: Record<DecisionPrueba, string> = {
  reconocido: 'Reconocido',
  dudoso: 'Dudoso',
  desconocido: 'Desconocido',
  sin_rostro: 'Sin rostro',
  imagen_insuficiente: 'Imagen insuficiente',
  error: 'Error',
}

const DETALLE_DECISION: Record<DecisionPrueba, string> = {
  reconocido: 'Reconoce a quien está frente a la cámara.',
  dudoso: 'La similitud quedó en la zona gris: no está seguro de quién es.',
  desconocido: 'No encontró a esta persona entre los rostros registrados.',
  sin_rostro: 'La cámara no vio ningún rostro.',
  imagen_insuficiente: 'La foto no sirvió para decidir.',
  error: 'No se pudo terminar la prueba.',
}

// Los códigos de `motivo` de aula/vision/resultado (contrato MQTT, sección 4).
const MOTIVOS: Record<string, string> = {
  captura_fallida: 'No se pudo tomar la foto.',
  imagen_insuficiente: 'La foto no tiene la nitidez suficiente.',
  sin_rostro: 'No se vio ningún rostro.',
  desconocido: 'Ningún rostro registrado se parece lo bastante.',
  galeria_vacia: 'Todavía no hay rostros registrados.',
  error_interno: 'El reconocimiento tuvo un error. Intenta de nuevo.',
  ocupado: 'El reconocimiento está ocupado con un registro o un pase de lista.',
  cuenta_invalida: 'La cuenta no es válida.',
}

/** Traduce el código de un motivo; sin motivo, texto vacío. Un código nuevo dice lo mismo que `error_interno`. */
export function traducirMotivoPrueba(motivo: string | null): string {
  if (motivo === null) {
    return ''
  }
  return MOTIVOS[motivo] ?? MOTIVOS.error_interno!
}

export type TonoPrueba = 'exito' | 'neutro' | 'error'

export interface ResumenResultado {
  titulo: string
  detalle: string
  /** Qué marca de asistencia lleva: presente con reconocido, falta con un error del servicio, pendiente con lo demás. */
  tono: TonoPrueba
}

/** El resultado en palabras: la decisión como título y, de detalle, el motivo traducido o, sin motivo, lo que significa la decisión. */
export function resumirResultado(resultado: ResultadoPrueba): ResumenResultado {
  return {
    titulo: TITULO_DECISION[resultado.decision],
    detalle: resultado.motivo !== null ? traducirMotivoPrueba(resultado.motivo) : DETALLE_DECISION[resultado.decision],
    tono: resultado.decision === 'reconocido' ? 'exito' : resultado.decision === 'error' ? 'error' : 'neutro',
  }
}

/** Qué muestra el recuadro: nada todavía, la espera, una prueba que no contestó o el resultado. */
export type FasePrueba = 'sin-prueba' | 'pendiente' | 'sin-respuesta' | 'resultado'

export function faseDePrueba(prueba: Prueba | null): FasePrueba {
  if (prueba === null || prueba.solicitado === null) {
    return 'sin-prueba'
  }
  if (prueba.pendiente) {
    return 'pendiente'
  }
  return prueba.resultado === null ? 'sin-respuesta' : 'resultado'
}

/** Lo que dice el aviso cuando la prueba no sale porque hay un registro abierto. */
export const AVISO_REGISTRO_ABIERTO = {
  titulo: 'Hay un registro abierto',
  descripcion: 'Termínalo o cancélalo en Registro antes de probar.',
}

export const TEXTO_PENDIENTE = {
  titulo: 'Tomando la foto…',
  detalle: 'Suele tardar unos segundos.',
}

export const TEXTO_SIN_RESPUESTA = {
  titulo: 'Sin resultado',
  detalle: 'No llegó ningún resultado a tiempo. Vuelve a probar.',
}
