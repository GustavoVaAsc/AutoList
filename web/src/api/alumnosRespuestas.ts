// Lógica pura de la vista Alumnos: interpretar las respuestas de la API,
// validar el archivo antes de subirlo y redactar los resúmenes. Sin DOM, sin
// fetch y sin importar nada: la prueba `node --test web/pruebas/*.test.js` la
// carga directo (Node 22.18 o posterior quita los tipos solo), así que aquí
// solo va sintaxis de TypeScript que se borra sin más (nada de enum).
//
// El contrato está en docs/HIGH-LEVEL-PLAN.md (sección 6.7) y en
// orangepi/README.md (GET /api/alumnos y POST /api/alumnos/importar). La fuente
// de verdad son los flujos de Node-RED: el servidor manda y esta capa solo lo
// traduce, sin decidir por la persona.

export type Rostro = 'pendiente' | 'registrado'

/** Un alumno de GET /api/alumnos. Nunca lleva embeddings: aquí no hay dónde guardarlos. */
export interface Alumno {
  cuenta: string
  nombre: string
  grupo: string
  rostro: Rostro
}

export interface NombreYGrupo {
  nombre: string
  grupo: string
}

/** Una cuenta del CSV que ya existe, con lo que hay hoy y lo que trae el archivo. */
export interface Existente {
  cuenta: string
  actual: NombreYGrupo
  csv: NombreYGrupo
  /** El archivo trae otro nombre o grupo. */
  cambia: boolean
  tieneRostro: boolean
}

export interface Conteos {
  insertadas: number
  actualizadas: number
  omitidas: number
  total: number
}

export interface FilaInvalida {
  /** Línea del archivo; el encabezado es la 1. */
  fila: number
  error: string
}

/** Qué hacer con las cuentas que ya existen: lo decide el profesor. */
export type Decision = 'actualizar' | 'omitir'

export type ResultadoImportacion =
  | { tipo: 'exito'; conteos: Conteos }
  | { tipo: 'filas-invalidas'; error: string; totalErrores: number; filas: FilaInvalida[] }
  /** 409 con escrito:false. No se importó nada. */
  | { tipo: 'existentes'; error: string; existentes: Existente[] }
  /** 409 con escrito:true. Las filas nuevas ya se escribieron. */
  | { tipo: 'carrera'; error: string; conteos: Conteos }
  /** 401, 403, 413, 415, cualquier otro código, respuesta ilegible o sin conexión. */
  | { tipo: 'fallo'; status: number | null; mensaje: string }

/** Tope del cuerpo: el mismo de LIMITE_CUERPO_BYTES (nodered/autenticacion-api.js) y MAX_BYTES del flujo. */
export const MAX_BYTES = 256 * 1024

function objeto(valor: unknown): Record<string, unknown> | null {
  return typeof valor === 'object' && valor !== null && !Array.isArray(valor) ? (valor as Record<string, unknown>) : null
}

function esConteo(valor: unknown): valor is number {
  return typeof valor === 'number' && Number.isInteger(valor) && valor >= 0
}

function texto(valor: unknown): string | null {
  return typeof valor === 'string' ? valor : null
}

function leerConteos(cuerpo: Record<string, unknown>): Conteos | null {
  const { insertadas, actualizadas, omitidas, total } = cuerpo
  if (!esConteo(insertadas) || !esConteo(actualizadas) || !esConteo(omitidas) || !esConteo(total)) {
    return null
  }
  return { insertadas, actualizadas, omitidas, total }
}

function leerNombreYGrupo(valor: unknown): NombreYGrupo | null {
  const o = objeto(valor)
  const nombre = texto(o?.nombre)
  const grupo = texto(o?.grupo)
  return nombre === null || grupo === null ? null : { nombre, grupo }
}

function leerExistentes(valor: unknown): Existente[] | null {
  if (!Array.isArray(valor)) {
    return null
  }
  const lista: Existente[] = []
  for (const elemento of valor as unknown[]) {
    const o = objeto(elemento)
    const cuenta = texto(o?.cuenta)
    const actual = leerNombreYGrupo(o?.actual)
    const csv = leerNombreYGrupo(o?.csv)
    if (o === null || cuenta === null || actual === null || csv === null || typeof o.cambia !== 'boolean' || typeof o.tiene_rostro !== 'boolean') {
      return null
    }
    lista.push({ cuenta, actual, csv, cambia: o.cambia, tieneRostro: o.tiene_rostro })
  }
  return lista
}

function fallo(status: number | null, mensaje: string): ResultadoImportacion {
  return { tipo: 'fallo', status, mensaje }
}

const NO_RECONOCIDA = 'La placa respondió algo inesperado. No se sabe si se importó el archivo: revisa la lista antes de reintentar.'

/**
 * Traduce la respuesta de POST /api/alumnos/importar. `cuerpo` es el JSON ya
 * leído, o undefined si no era JSON. Los dos 409 se distinguen solo por
 * `escrito`: con false no se escribió nada y trae las cuentas existentes; con
 * true las filas nuevas ya se escribieron.
 */
export function interpretarRespuesta(status: number, cuerpo: unknown): ResultadoImportacion {
  const o = objeto(cuerpo)
  switch (status) {
    case 200: {
      const conteos = o === null ? null : leerConteos(o)
      return conteos === null ? fallo(status, NO_RECONOCIDA) : { tipo: 'exito', conteos }
    }
    case 400: {
      const filas: FilaInvalida[] = []
      if (Array.isArray(o?.filas)) {
        for (const elemento of o.filas as unknown[]) {
          const f = objeto(elemento)
          if (f !== null && typeof f.fila === 'number' && typeof f.error === 'string') {
            filas.push({ fila: f.fila, error: f.error })
          }
        }
      }
      const totalErrores = esConteo(o?.total_errores) ? Math.max(o.total_errores, filas.length) : filas.length
      return {
        tipo: 'filas-invalidas',
        error: texto(o?.error) ?? 'El archivo no es válido. No se importó nada.',
        totalErrores,
        filas,
      }
    }
    case 409: {
      if (o === null) {
        return fallo(status, NO_RECONOCIDA)
      }
      const error = texto(o.error) ?? ''
      if (o.escrito === true) {
        const conteos = leerConteos(o)
        return conteos === null ? fallo(status, NO_RECONOCIDA) : { tipo: 'carrera', error, conteos }
      }
      if (o.escrito === false) {
        const existentes = leerExistentes(o.existentes)
        return existentes === null || existentes.length === 0
          ? fallo(status, NO_RECONOCIDA)
          : { tipo: 'existentes', error, existentes }
      }
      return fallo(status, NO_RECONOCIDA)
    }
    case 401:
      return fallo(status, 'Falta iniciar sesión. No se importó nada.')
    case 403:
      return fallo(status, 'La placa rechazó la petición. Recarga la página e intenta de nuevo. No se importó nada.')
    case 413:
      return fallo(status, 'El archivo pesa más de 256 KiB. No se importó nada.')
    case 415:
      return fallo(status, 'Solo se aceptan archivos CSV. No se importó nada.')
    default:
      return fallo(status, 'La placa no pudo atender la petición. Revisa la lista antes de reintentar.')
  }
}

function formatearBytes(bytes: number): string {
  return bytes < 1024 ? `${bytes} B` : `${(bytes / 1024).toFixed(1)} KiB`
}

/**
 * Revisión mínima antes de subir: tamaño y que parezca texto. Devuelve el
 * motivo del rechazo, o null si pasa. El servidor manda: valida el contenido.
 * Se acepta por la extensión (.csv o .txt) sin mirar el tipo declarado, porque
 * depende del sistema: Windows con Excel declara .csv como
 * application/vnd.ms-excel, y otros sistemas lo declaran como
 * application/octet-stream o no declaran nada. Sin extensión, solo pasa un tipo
 * text/*.
 */
export function validarArchivoLocal(archivo: { name: string; size: number; type: string }): string | null {
  if (archivo.size === 0) {
    return 'El archivo está vacío.'
  }
  if (archivo.size > MAX_BYTES) {
    return `El archivo pesa ${formatearBytes(archivo.size)} y el máximo es 256 KiB.`
  }
  const extensionDeTexto = /\.(csv|txt)$/i.test(archivo.name)
  if (!extensionDeTexto && !archivo.type.toLowerCase().startsWith('text/')) {
    return 'Elige un archivo CSV. Desde Excel, guárdalo como CSV UTF-8.'
  }
  return null
}

function unir(partes: string[]): string {
  return partes.length <= 1 ? (partes[0] ?? '') : `${partes.slice(0, -1).join(', ')} y ${partes[partes.length - 1]}`
}

/** "3 nuevas, 1 actualizada y 2 omitidas": solo los conteos que no son cero. */
export function resumirConteos(c: Conteos): string {
  const partes: string[] = []
  if (c.insertadas > 0) {
    partes.push(`${c.insertadas} ${c.insertadas === 1 ? 'nueva' : 'nuevas'}`)
  }
  if (c.actualizadas > 0) {
    partes.push(`${c.actualizadas} ${c.actualizadas === 1 ? 'actualizada' : 'actualizadas'}`)
  }
  if (c.omitidas > 0) {
    partes.push(`${c.omitidas} ${c.omitidas === 1 ? 'omitida' : 'omitidas'}`)
  }
  return partes.length === 0 ? 'Sin cambios' : unir(partes)
}

export type LecturaAlumnos = { ok: true; alumnos: Alumno[] } | { ok: false; motivo: string }

/**
 * Valida GET /api/alumnos. Se copian solo los cuatro campos conocidos: aunque
 * la API algún día mandara más (embeddings no, pero cualquier otro campo), la
 * vista no los conserva.
 */
export function leerAlumnos(cuerpo: unknown): LecturaAlumnos {
  if (!Array.isArray(cuerpo)) {
    return { ok: false, motivo: 'se esperaba una lista de alumnos' }
  }
  const alumnos: Alumno[] = []
  for (const [posicion, elemento] of (cuerpo as unknown[]).entries()) {
    const o = objeto(elemento)
    const cuenta = texto(o?.cuenta)
    const nombre = texto(o?.nombre)
    const grupo = texto(o?.grupo)
    const rostro = o?.rostro
    if (cuenta === null || nombre === null || grupo === null || (rostro !== 'pendiente' && rostro !== 'registrado')) {
      return { ok: false, motivo: `el alumno ${posicion + 1} de la respuesta no tiene cuenta, nombre, grupo y estado del rostro válidos` }
    }
    alumnos.push({ cuenta, nombre, grupo, rostro })
  }
  return { ok: true, alumnos }
}

/** Grupos distintos, en orden alfabético. */
export function gruposDe(alumnos: Alumno[]): string[] {
  return [...new Set(alumnos.map((a) => a.grupo))].sort((a, b) => a.localeCompare(b, 'es'))
}

/** Cuentas presentes en ambas listas cuyo estado del rostro cambió. */
export function cuentasConRostroCambiado(previos: Alumno[], actuales: Alumno[]): string[] {
  const antes = new Map(previos.map((a) => [a.cuenta, a.rostro]))
  return actuales.filter((a) => antes.has(a.cuenta) && antes.get(a.cuenta) !== a.rostro).map((a) => a.cuenta)
}
