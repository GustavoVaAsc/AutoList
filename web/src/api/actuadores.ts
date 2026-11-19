// Cliente de POST /api/actuadores (pestaña Actuadores de nodered/flows/flows.json): las órdenes manuales a la
// XIAO. Aquí vive solo el fetch; la interpretación de la respuesta es pura y está en actuadoresRespuestas.ts.
// Las credenciales no se guardan: la API pide HTTP Basic y el navegador, que es del mismo origen, reenvía lo que
// la persona escribió. Todo método que cambia datos manda X-Pase-Lista: 1 (sin ella, 403): es la defensa contra
// CSRF de nodered/autenticacion-api.js.
import { interpretarOrden, SIN_RESPUESTA, type CuerpoOrden, type ResultadoOrden } from './actuadoresRespuestas'

const RUTA = '/api/actuadores'
// La API solo valida y publica por MQTT: más margen que una lectura, sin llegar al del diálogo de credenciales.
const TIEMPO_MAXIMO_MS = 15_000

/**
 * Manda una orden a los actuadores. No se cancela desde fuera: abortar no deshace lo que la API ya publicó. Sin
 * respuesta (sin red o plazo agotado) devuelve un fallo que dice que no se sabe si salió.
 */
export async function enviarOrden(cuerpo: CuerpoOrden): Promise<ResultadoOrden> {
  const controlador = new AbortController()
  const temporizador = setTimeout(() => controlador.abort(), TIEMPO_MAXIMO_MS)
  try {
    let respuesta: Response
    try {
      respuesta = await fetch(RUTA, {
        method: 'POST',
        signal: controlador.signal,
        headers: { Accept: 'application/json', 'Content-Type': 'application/json', 'X-Pase-Lista': '1' },
        body: JSON.stringify(cuerpo),
      })
    } catch {
      return { tipo: 'fallo', status: null, mensaje: SIN_RESPUESTA }
    }
    let json: unknown
    try {
      json = await respuesta.json()
    } catch {
      json = undefined
    }
    if (respuesta.status >= 400) {
      console.warn('POST', RUTA, 'respondió', respuesta.status, json)
    }
    return interpretarOrden(respuesta.status, json)
  } finally {
    clearTimeout(temporizador)
  }
}
