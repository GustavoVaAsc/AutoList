// Cliente de solo lectura del estado del sistema: GET /api/sistema/estado (pestaña Sistema de
// nodered/flows/flows.json). La validación de la respuesta vive en sistemaRespuestas.ts.
import { pedirJson } from './asistencia'
import { ErrorApi } from './energia'
import { leerSistema, type Sistema } from './sistemaRespuestas'

const RUTA = '/api/sistema/estado'
const TIEMPO_MAXIMO_MS = 8000

export interface ConsultaSistema {
  sistema: Sistema
  /** Hora de la respuesta según el reloj de la placa (cabecera Date). */
  horaServidor: number
}

/**
 * Consulta lo último que Node-RED sabe de la visión, la XIAO y la placa. Cancela con `senal` y también si la
 * API tarda más de `tiempoMaximoMs`. Un 401 lanza ErrorSinSesion (pedirJson).
 */
export async function pedirSistema(
  senal: AbortSignal,
  tiempoMaximoMs: number = TIEMPO_MAXIMO_MS,
): Promise<ConsultaSistema> {
  const { cuerpo, horaServidor } = await pedirJson(RUTA, senal, tiempoMaximoMs, false)
  const leido = leerSistema(cuerpo)
  if (!leido.ok) {
    throw new ErrorApi(`La API devolvió un estado del sistema inválido (${leido.motivo}). Avisa al responsable del borde.`)
  }
  return { sistema: leido.sistema, horaServidor }
}
