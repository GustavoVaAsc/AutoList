// Lógica pura de asistencia por sesión: validar e interpretar las respuestas de
// GET /api/sesiones y GET /api/sesiones/:id, elegir la sesión mostrada y
// detectar cambios de estado entre sondeos de una misma sesión abierta.
// Sin DOM, sin fetch y sin importar nada: `node --test web/pruebas/*.test.js`
// carga este archivo directo en Node 22.18+ quitando los tipos.

export type EstadoAsistencia = 'presente' | 'retardo' | 'falta' | 'pendiente'

export type MetodoAsistencia = 'rostro' | 'manual'

export type MotivoIntento = 'desconocido' | 'duplicado' | 'imagen_insuficiente'

export interface ResumenIntentos {
  desconocido: number
  duplicado: number
  imagenInsuficiente: number
  total: number
}

export interface ResumenConfianza {
  min: number | null
  max: number | null
  promedio: number | null
}

export interface ResumenSesion {
  id: number
  materia: string
  grupo: string
  inicio: string
  fin: string | null
  abierta: boolean
  tolMin: number
  totalGrupo: number
  presentes: number
  retardos: number
  faltas: number
  pendientes: number
  intentos: ResumenIntentos
  confianza: ResumenConfianza
}

export interface AlumnoSesion {
  cuenta: string
  nombre: string
  estado: EstadoAsistencia
  hora: string | null
  confianza: number | null
  metodo: MetodoAsistencia | null
}

export interface IntentoSesion {
  id: number
  hora: string
  motivo: MotivoIntento
  confianzaMax: number | null
}

export interface DetalleSesion {
  sesion: ResumenSesion
  alumnos: AlumnoSesion[]
  intentos: IntentoSesion[]
}

export type ResultadoSesiones =
  | { ok: true; sesiones: ResumenSesion[] }
  | { ok: false; motivo: string }

export type ResultadoDetalleSesion =
  | { ok: true; detalle: DetalleSesion }
  | { ok: false; motivo: string }

export const ETIQUETA_ESTADO_ASISTENCIA: Record<EstadoAsistencia, string> = {
  presente: 'Presente',
  retardo: 'Retardo',
  falta: 'Falta',
  pendiente: 'Pendiente',
}

export const ETIQUETA_METODO_ASISTENCIA: Record<MetodoAsistencia, string> = {
  rostro: 'Rostro',
  manual: 'Manual',
}

export const ETIQUETA_MOTIVO_INTENTO: Record<MotivoIntento, string> = {
  desconocido: 'No reconocido',
  duplicado: 'Ya registrado',
  imagen_insuficiente: 'Imagen insuficiente',
}

const MAX_INT_PG = 2147483647

function objeto(valor: unknown): Record<string, unknown> | null {
  return typeof valor === 'object' && valor !== null && !Array.isArray(valor)
    ? (valor as Record<string, unknown>)
    : null
}

function esIdEntero(valor: unknown): valor is number {
  return typeof valor === 'number' && Number.isInteger(valor) && valor >= 1 && valor <= MAX_INT_PG
}

function esConteo(valor: unknown): valor is number {
  return typeof valor === 'number' && Number.isInteger(valor) && valor >= 0
}

function esTextoNoVacio(valor: unknown): valor is string {
  return typeof valor === 'string' && valor.trim().length > 0
}

function esFechaIso(valor: unknown): valor is string {
  return typeof valor === 'string' && valor.length > 0 && Number.isFinite(Date.parse(valor))
}

function leerNumeroOpcional(valor: unknown): { ok: true; valor: number | null } | { ok: false } {
  if (valor === null) return { ok: true, valor: null }
  if (typeof valor === 'number' && Number.isFinite(valor)) return { ok: true, valor }
  return { ok: false }
}

function leerIntentosResumen(valor: unknown): ResumenIntentos | null {
  const o = objeto(valor)
  if (o === null) return null
  const { desconocido, duplicado, imagen_insuficiente: imagenInsuficiente, total } = o
  if (
    !esConteo(desconocido) ||
    !esConteo(duplicado) ||
    !esConteo(imagenInsuficiente) ||
    !esConteo(total)
  ) {
    return null
  }
  return { desconocido, duplicado, imagenInsuficiente, total }
}

function leerConfianzaResumen(valor: unknown): ResumenConfianza | null {
  const o = objeto(valor)
  if (o === null) return null
  const min = leerNumeroOpcional(o.min)
  const max = leerNumeroOpcional(o.max)
  const promedio = leerNumeroOpcional(o.promedio)
  if (!min.ok || !max.ok || !promedio.ok) return null
  return { min: min.valor, max: max.valor, promedio: promedio.valor }
}

function leerResumenSesion(valor: unknown): ResumenSesion | null {
  const o = objeto(valor)
  if (o === null) return null
  const {
    id,
    materia,
    grupo,
    inicio,
    fin,
    abierta,
    tol_min: tolMin,
    total_grupo: totalGrupo,
    presentes,
    retardos,
    faltas,
    pendientes,
  } = o
  if (
    !esIdEntero(id) ||
    !esTextoNoVacio(materia) ||
    !esTextoNoVacio(grupo) ||
    !esFechaIso(inicio) ||
    typeof abierta !== 'boolean' ||
    !esConteo(tolMin) ||
    !esConteo(totalGrupo) ||
    !esConteo(presentes) ||
    !esConteo(retardos) ||
    !esConteo(faltas) ||
    !esConteo(pendientes)
  ) {
    return null
  }
  if (abierta) {
    if (fin !== null) return null
  } else if (!esFechaIso(fin)) {
    return null
  }
  const intentos = leerIntentosResumen(o.intentos)
  const confianza = leerConfianzaResumen(o.confianza)
  if (intentos === null || confianza === null) return null

  return {
    id,
    materia,
    grupo,
    inicio,
    fin,
    abierta,
    tolMin,
    totalGrupo,
    presentes,
    retardos,
    faltas,
    pendientes,
    intentos,
    confianza,
  }
}

function esEstadoAsistencia(valor: unknown): valor is EstadoAsistencia {
  return valor === 'presente' || valor === 'retardo' || valor === 'falta' || valor === 'pendiente'
}

function esMetodoAsistencia(valor: unknown): valor is MetodoAsistencia {
  return valor === 'rostro' || valor === 'manual'
}

function esMotivoIntento(valor: unknown): valor is MotivoIntento {
  return valor === 'desconocido' || valor === 'duplicado' || valor === 'imagen_insuficiente'
}

function leerAlumnoSesion(valor: unknown): AlumnoSesion | null {
  const o = objeto(valor)
  if (o === null) return null
  const { cuenta, nombre, estado, hora, metodo } = o
  if (
    typeof cuenta !== 'string' ||
    !/^\d{9}$/.test(cuenta) ||
    !esTextoNoVacio(nombre) ||
    !esEstadoAsistencia(estado)
  ) {
    return null
  }
  if (hora !== null && !esFechaIso(hora)) return null
  if (metodo !== null && !esMetodoAsistencia(metodo)) return null
  const confianza = leerNumeroOpcional(o.confianza)
  if (!confianza.ok) return null
  return {
    cuenta,
    nombre,
    estado,
    hora,
    confianza: confianza.valor,
    metodo,
  }
}

function leerIntentoSesion(valor: unknown): IntentoSesion | null {
  const o = objeto(valor)
  if (o === null) return null
  const { id, hora, motivo } = o
  if (!esIdEntero(id) || !esFechaIso(hora) || !esMotivoIntento(motivo)) {
    return null
  }
  const confianzaMax = leerNumeroOpcional(o.confianza_max)
  if (!confianzaMax.ok) return null
  return {
    id,
    hora,
    motivo,
    confianzaMax: confianzaMax.valor,
  }
}

/** Valida el cuerpo JSON de GET /api/sesiones. */
export function leerSesiones(cuerpo: unknown): ResultadoSesiones {
  if (!Array.isArray(cuerpo)) {
    return { ok: false, motivo: 'La lista de sesiones no es un arreglo JSON.' }
  }
  const sesiones: ResumenSesion[] = []
  for (let i = 0; i < cuerpo.length; i++) {
    const sesion = leerResumenSesion(cuerpo[i])
    if (sesion === null) {
      return { ok: false, motivo: `La sesión en la posición ${i} tiene campos inválidos.` }
    }
    sesiones.push(sesion)
  }
  return { ok: true, sesiones }
}

/** Valida el cuerpo JSON de GET /api/sesiones/:id. */
export function leerDetalleSesion(cuerpo: unknown): ResultadoDetalleSesion {
  const o = objeto(cuerpo)
  if (o === null) {
    return { ok: false, motivo: 'El detalle de la sesión no es un objeto JSON.' }
  }
  const sesion = leerResumenSesion(o.sesion)
  if (sesion === null) {
    return { ok: false, motivo: 'El encabezado de la sesión tiene campos inválidos.' }
  }
  if (!Array.isArray(o.alumnos)) {
    return { ok: false, motivo: 'La lista nominal de la sesión no es un arreglo.' }
  }
  if (!Array.isArray(o.intentos)) {
    return { ok: false, motivo: 'La lista de intentos de la sesión no es un arreglo.' }
  }
  const alumnos: AlumnoSesion[] = []
  for (let i = 0; i < o.alumnos.length; i++) {
    const alumno = leerAlumnoSesion(o.alumnos[i])
    if (alumno === null) {
      return { ok: false, motivo: `El alumno en la posición ${i} tiene campos inválidos.` }
    }
    alumnos.push(alumno)
  }
  const intentos: IntentoSesion[] = []
  for (let i = 0; i < o.intentos.length; i++) {
    const intento = leerIntentoSesion(o.intentos[i])
    if (intento === null) {
      return { ok: false, motivo: `El intento en la posición ${i} tiene campos inválidos.` }
    }
    intentos.push(intento)
  }
  return { ok: true, detalle: { sesion, alumnos, intentos } }
}

/**
 * Conserva la sesión elegida por el profesor si sigue en la lista; si no,
 * prefiere una sesión abierta en curso y, si todas cerraron, la más reciente.
 */
export function elegirSesionActiva(
  sesiones: readonly ResumenSesion[],
  idElegido: number | null,
): number | null {
  if (sesiones.length === 0) return null
  if (idElegido !== null && sesiones.some((s) => s.id === idElegido)) {
    return idElegido
  }
  const abierta = sesiones.find((s) => s.abierta)
  if (abierta !== undefined) return abierta.id
  return sesiones[0]!.id
}

/**
 * Cuentas cuyo estado de asistencia cambió entre dos lecturas consecutivas de
 * la misma sesión (por ejemplo, de `pendiente` a `presente` o `retardo`). Al
 * cambiar de sesión o en la carga inicial devuelve arreglo vacío.
 */
export function cuentasConEstadoCambiado(
  antes: readonly AlumnoSesion[] | null,
  idAntes: number | null,
  despues: readonly AlumnoSesion[],
  idDespues: number,
): string[] {
  if (antes === null || idAntes === null || idAntes !== idDespues) return []
  const previo = new Map<string, EstadoAsistencia>()
  for (const a of antes) previo.set(a.cuenta, a.estado)
  const cambiadas: string[] = []
  for (const a of despues) {
    const estadoPrevio = previo.get(a.cuenta)
    if (estadoPrevio !== undefined && estadoPrevio !== a.estado) {
      cambiadas.push(a.cuenta)
    }
  }
  return cambiadas
}
