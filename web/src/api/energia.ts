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

const RUTA = '/api/metricas/energia'
const TIEMPO_MAXIMO_MS = 8000

function esNumero(valor: unknown): valor is number {
  return typeof valor === 'number' && Number.isFinite(valor)
}

/** Convierte el JSON de la API en lecturas, de la más vieja a la más nueva. */
export function leerLecturas(cuerpo: unknown): Lectura[] {
  if (!Array.isArray(cuerpo)) {
    throw new ErrorApi('La API devolvió datos que la vista no reconoce (se esperaba una lista de lecturas). Avisa al responsable del borde.')
  }
  const lecturas = cuerpo.map((elemento: unknown, posicion): Lectura => {
    const fila = (typeof elemento === 'object' && elemento !== null ? elemento : {}) as Record<string, unknown>
    const t = typeof fila.hora === 'string' ? Date.parse(fila.hora) : NaN
    const { v, i, p } = fila
    if (Number.isNaN(t) || !esNumero(v) || !esNumero(i) || !esNumero(p)) {
      throw new ErrorApi(`La lectura ${posicion + 1} de la respuesta no tiene hora, voltaje, corriente y potencia válidos. Avisa al responsable del borde.`)
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
 * también si la API tarda más de TIEMPO_MAXIMO_MS (en cualquier punto, incluida
 * la lectura del cuerpo).
 */
export async function pedirLecturas(limite: number, senal: AbortSignal): Promise<Consulta> {
  const controlador = new AbortController()
  const alCancelar = () => controlador.abort()
  senal.addEventListener('abort', alCancelar)
  const temporizador = setTimeout(() => controlador.abort(), TIEMPO_MAXIMO_MS)
  const tiempoAgotado = () => new ErrorApi('La API tardó demasiado en responder. Revisa la conexión con la placa.')
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
        : new ErrorApi('No se pudo conectar con la API. Revisa que la placa esté encendida y conectada a la red del aula.')
    }
    if (!respuesta.ok) {
      throw new ErrorApi(`La API respondió con el error ${respuesta.status}. Espera unos segundos; si sigue igual, avisa al responsable del borde.`)
    }
    let cuerpo: unknown
    try {
      cuerpo = await respuesta.json()
    } catch (error) {
      if (senal.aborted) {
        throw error
      }
      throw controlador.signal.aborted ? tiempoAgotado() : new ErrorApi('La API respondió algo que no es JSON válido. Avisa al responsable del borde.')
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
