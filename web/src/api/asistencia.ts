// Cliente de solo lectura para la asistencia por sesión (pestaña "Asistencia" de
// nodered/flows/flows.json): GET /api/sesiones, GET /api/sesiones/:id y
// GET /api/sesiones/:id/csv.
import {
  leerDetalleSesion,
  leerSesiones,
  type DetalleSesion,
  type ResumenSesion,
} from './asistenciaRespuestas'
import { ErrorApi, ErrorSinSesion } from './energia'

const RUTA_SESIONES = '/api/sesiones'
const TIEMPO_MAXIMO_MS = 8000

export interface ConsultaSesiones {
  sesiones: ResumenSesion[]
  horaServidor: number
}

export interface ConsultaDetalleSesion {
  /** null cuando el servidor responde 404 (la sesión no existe). */
  detalle: DetalleSesion | null
  horaServidor: number
}

export type ResultadoDescargaCsv =
  | { ok: true; blob: Blob; nombreArchivo: string }
  | { ok: false; mensaje: string }

function horaDeRespuesta(respuesta: Response): number {
  const fecha = Date.parse(respuesta.headers.get('Date') ?? '')
  return Number.isNaN(fecha) ? Date.now() : fecha
}

async function pedirJson(
  url: string,
  senal: AbortSignal,
  tiempoMaximoMs: number,
  permitirNoEncontrado: boolean,
): Promise<{ cuerpo: unknown; noEncontrado: boolean; horaServidor: number }> {
  const controlador = new AbortController()
  const alCancelar = () => controlador.abort()
  senal.addEventListener('abort', alCancelar)
  const temporizador = setTimeout(() => controlador.abort(), tiempoMaximoMs)
  const tiempoAgotado = () =>
    new ErrorApi('La API tardó demasiado en responder. Revisa la conexión con la placa.')
  try {
    let respuesta: Response
    try {
      respuesta = await fetch(url, {
        signal: controlador.signal,
        headers: { Accept: 'application/json' },
      })
    } catch (error) {
      if (senal.aborted) throw error
      throw controlador.signal.aborted
        ? tiempoAgotado()
        : new ErrorApi(
            'No se pudo conectar con la API. Revisa que la placa esté encendida y conectada a la red del aula.',
          )
    }
    const horaServidor = horaDeRespuesta(respuesta)
    if (respuesta.status === 401) {
      throw new ErrorSinSesion(
        'La API pide usuario y contraseña del profesor y no se completó el inicio de sesión.',
      )
    }
    if (permitirNoEncontrado && respuesta.status === 404) {
      return { cuerpo: null, noEncontrado: true, horaServidor }
    }
    if (!respuesta.ok) {
      throw new ErrorApi(
        `La API respondió con el error ${respuesta.status}. Espera unos segundos; si sigue igual, avisa al responsable del borde.`,
      )
    }
    let cuerpo: unknown
    try {
      cuerpo = await respuesta.json()
    } catch (error) {
      if (senal.aborted) throw error
      throw controlador.signal.aborted
        ? tiempoAgotado()
        : new ErrorApi('La API respondió algo que no es JSON válido. Avisa al responsable del borde.')
    }
    return { cuerpo, noEncontrado: false, horaServidor }
  } finally {
    clearTimeout(temporizador)
    senal.removeEventListener('abort', alCancelar)
  }
}

export async function pedirSesiones(
  limite: number,
  senal: AbortSignal,
  tiempoMaximoMs: number = TIEMPO_MAXIMO_MS,
): Promise<ConsultaSesiones> {
  const { cuerpo, horaServidor } = await pedirJson(
    `${RUTA_SESIONES}?limite=${limite}`,
    senal,
    tiempoMaximoMs,
    false,
  )
  const leido = leerSesiones(cuerpo)
  if (!leido.ok) {
    throw new ErrorApi(`La API devolvió una lista de sesiones inválida (${leido.motivo})`)
  }
  return { sesiones: leido.sesiones, horaServidor }
}

export async function pedirDetalleSesion(
  id: number,
  senal: AbortSignal,
  tiempoMaximoMs: number = TIEMPO_MAXIMO_MS,
): Promise<ConsultaDetalleSesion> {
  const { cuerpo, noEncontrado, horaServidor } = await pedirJson(
    `${RUTA_SESIONES}/${encodeURIComponent(String(id))}`,
    senal,
    tiempoMaximoMs,
    true,
  )
  if (noEncontrado) {
    return { detalle: null, horaServidor }
  }
  const leido = leerDetalleSesion(cuerpo)
  if (!leido.ok) {
    throw new ErrorApi(`La API devolvió un detalle de sesión inválido (${leido.motivo})`)
  }
  return { detalle: leido.detalle, horaServidor }
}

/**
 * Descarga el reporte CSV generado al cierre de la sesión (`/data/reportes/sesion-<id>.csv`).
 * Usa `fetch` para conservar el manejo de errores dentro de la vista sin sacar al
 * navegador hacia una página JSON cruda si el archivo aún no existe.
 */
export async function descargarCsvSesion(id: number): Promise<ResultadoDescargaCsv> {
  const controlador = new AbortController()
  const temporizador = setTimeout(() => controlador.abort(), TIEMPO_MAXIMO_MS)
  try {
    let respuesta: Response
    try {
      respuesta = await fetch(`${RUTA_SESIONES}/${encodeURIComponent(String(id))}/csv`, {
        signal: controlador.signal,
        headers: { Accept: 'text/csv, application/json;q=0.5' },
      })
    } catch {
      return {
        ok: false,
        mensaje: controlador.signal.aborted
          ? 'La placa tardó demasiado en entregar el archivo CSV.'
          : 'No se pudo conectar con la placa para descargar el archivo CSV.',
      }
    }
    if (respuesta.status === 401) {
      return { ok: false, mensaje: 'Inicia sesión con el usuario y la contraseña del profesor.' }
    }
    if (respuesta.status === 404) {
      return {
        ok: false,
        mensaje: 'Todavía no existe el reporte CSV de esta sesión en la placa.',
      }
    }
    if (!respuesta.ok) {
      return { ok: false, mensaje: `La placa respondió con el error ${respuesta.status}.` }
    }
    const blob = await respuesta.blob()
    return { ok: true, blob, nombreArchivo: `sesion-${id}.csv` }
  } catch {
    return { ok: false, mensaje: 'No se pudo terminar de leer el archivo CSV.' }
  } finally {
    clearTimeout(temporizador)
  }
}
