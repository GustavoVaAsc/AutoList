// Cliente de solo lectura de la latencia de inferencia de la NPU:
// GET /api/metricas/latencia (pestaña Energía de nodered/flows/flows.json).
// La validación de la respuesta vive en latenciaRespuestas.ts.
import { pedirJson } from './asistencia'
import { ErrorApi } from './energia'
import { leerLatencias, type MedicionLatencia } from './latenciaRespuestas'

const RUTA = '/api/metricas/latencia'
const TIEMPO_MAXIMO_MS = 8000

export interface ConsultaLatencias {
  /** De la más nueva a la más vieja. */
  mediciones: MedicionLatencia[]
  /** Hora de la respuesta según el reloj de la placa (cabecera Date). */
  horaServidor: number
}

/**
 * Consulta las últimas `limite` mediciones. Cancela con `senal` y también si la
 * API tarda más de `tiempoMaximoMs`. Un 401 lanza ErrorSinSesion (pedirJson).
 */
export async function pedirLatencias(
  limite: number,
  senal: AbortSignal,
  tiempoMaximoMs: number = TIEMPO_MAXIMO_MS,
): Promise<ConsultaLatencias> {
  const { cuerpo, horaServidor } = await pedirJson(`${RUTA}?limite=${limite}`, senal, tiempoMaximoMs, false)
  const leido = leerLatencias(cuerpo)
  if (!leido.ok) {
    console.warn(leido.motivo)
    throw new ErrorApi('La placa respondió algo inesperado. Intenta de nuevo.')
  }
  return { mediciones: leido.mediciones, horaServidor }
}
