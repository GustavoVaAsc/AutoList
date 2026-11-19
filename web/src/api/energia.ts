// Cliente de GET /api/metricas/energia (nodered/flows/flows.json, flujo Energía).
// La API devuelve un arreglo de { hora, v, i, p } de la lectura más nueva a la
// más vieja. Aquí se valida la forma, porque el navegador no debe confiar en
// que la respuesta sea la esperada.

export interface Lectura {
  /** Instante de la lectura, en milisegundos desde 1970 (UTC). */
  t: number
  /** Voltaje (V). */
  v: number
  /** Corriente (A). */
  i: number
  /** Potencia (W). */
  p: number
}

/** Fallo al consultar la API; `mensaje` va en español y se muestra tal cual. */
export class ErrorApi extends Error {}

/**
 * La API respondió 401: la persona canceló el diálogo de usuario y contraseña del
 * navegador o las credenciales no valen. Reintentar solo vuelve a abrir el diálogo,
 * así que el sondeo se detiene hasta que la persona lo pida (useLecturas).
 */
export class ErrorSinSesion extends ErrorApi {}

/**
 * El mensaje que ve la persona para un fallo de una consulta: el de un ErrorApi ya
 * viene redactado; cualquier otro error se deja en la consola y se dice algo corto.
 */
export function mensajeDeError(error: unknown): string {
  if (error instanceof ErrorApi) {
    return error.message
  }
  console.error(error)
  return 'La placa respondió algo inesperado. Intenta de nuevo.'
}

const RUTA = '/api/metricas/energia'
const TIEMPO_MAXIMO_MS = 8000
/**
 * Tiempo máximo de la primera consulta de cada montaje (hasta su primera respuesta
 * correcta). La API pide HTTP Basic: el navegador muestra su diálogo de usuario y
 * contraseña y la petición espera a que la persona lo complete; con 8 s se
 * cancelaría antes de que alcance a escribirlas. Después el navegador ya tiene las
 * credenciales y rige TIEMPO_MAXIMO_MS.
 */
export const TIEMPO_INICIAL_MS = 120_000

function esNumero(valor: unknown): valor is number {
  return typeof valor === 'number' && Number.isFinite(valor)
}

/** Convierte el JSON de la API en lecturas, de la más vieja a la más nueva. */
export function leerLecturas(cuerpo: unknown): Lectura[] {
  if (!Array.isArray(cuerpo)) {
    console.warn('La respuesta de lecturas no es una lista')
    throw new ErrorApi('La placa respondió algo inesperado. Intenta de nuevo.')
  }
  const lecturas = cuerpo.map((elemento: unknown, posicion): Lectura => {
    const fila = (typeof elemento === 'object' && elemento !== null ? elemento : {}) as Record<string, unknown>
    const t = typeof fila.hora === 'string' ? Date.parse(fila.hora) : NaN
    const { v, i, p } = fila
    if (Number.isNaN(t) || !esNumero(v) || !esNumero(i) || !esNumero(p)) {
      console.warn(`La lectura ${posicion + 1} no tiene hora, voltaje, corriente y potencia válidos`)
      throw new ErrorApi('La placa respondió algo inesperado. Intenta de nuevo.')
    }
    return { t, v, i, p }
  })
  return lecturas.sort((a, b) => a.t - b.t)
}

/** Lecturas y el instante, según el reloj de la placa, en que la API respondió. */
export interface Consulta {
  lecturas: Lectura[]
  /**
   * Hora de la respuesta (ms desde 1970), tomada de la cabecera Date de HTTP,
   * que pone el servidor. Así la antigüedad de una lectura se calcula con el
   * mismo reloj que la selló y no depende de la hora de la laptop. Si la
   * cabecera falta o no se entiende, respaldo: el reloj del navegador.
   */
  horaServidor: number
}

/**
 * Consulta las últimas `limite` lecturas de energía. Cancela con `senal` y
 * también si la API tarda más de `tiempoMaximoMs` (en cualquier punto, incluida
 * la lectura del cuerpo).
 */
export async function pedirLecturas(
  limite: number,
  senal: AbortSignal,
  tiempoMaximoMs: number = TIEMPO_MAXIMO_MS,
): Promise<Consulta> {
  const controlador = new AbortController()
  const alCancelar = () => controlador.abort()
  senal.addEventListener('abort', alCancelar)
  const temporizador = setTimeout(() => controlador.abort(), tiempoMaximoMs)
  const tiempoAgotado = () => new ErrorApi('La placa tardó en responder.')
  try {
    let respuesta: Response
    try {
      respuesta = await fetch(`${RUTA}?limite=${limite}`, {
        signal: controlador.signal,
        headers: { Accept: 'application/json' },
      })
    } catch (error) {
      if (senal.aborted) {
        throw error
      }
      throw controlador.signal.aborted
        ? tiempoAgotado()
        : new ErrorApi('No hay conexión con la placa.')
    }
    if (respuesta.status === 401) {
      throw new ErrorSinSesion('Falta iniciar sesión.')
    }
    if (!respuesta.ok) {
      console.warn('La placa respondió', respuesta.status, respuesta.url)
      throw new ErrorApi('La placa no pudo atender la petición. Intenta de nuevo en unos segundos.')
    }
    let cuerpo: unknown
    try {
      cuerpo = await respuesta.json()
    } catch (error) {
      if (senal.aborted) {
        throw error
      }
      if (!controlador.signal.aborted) {
        console.warn('La respuesta de', respuesta.url, 'no es JSON válido')
      }
      throw controlador.signal.aborted ? tiempoAgotado() : new ErrorApi('La placa respondió algo inesperado. Intenta de nuevo.')
    }
    const fechaServidor = Date.parse(respuesta.headers.get('Date') ?? '')
    return {
      lecturas: leerLecturas(cuerpo),
      horaServidor: Number.isNaN(fechaServidor) ? Date.now() : fechaServidor,
    }
  } finally {
    clearTimeout(temporizador)
    senal.removeEventListener('abort', alCancelar)
  }
}
