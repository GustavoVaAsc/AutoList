// Cliente de la prueba de reconocimiento (pestaña Registro de nodered/flows/flows.json): GET /api/enrolamiento/prueba
// y POST /api/enrolamiento/probar. Aquí viven solo los fetch; la interpretación de las respuestas es pura y está en
// pruebaRespuestas.ts.
// Las credenciales no se guardan: la API pide HTTP Basic y el navegador, que es del mismo origen, reenvía lo que
// la persona escribió. El POST manda X-Pase-Lista: 1 (sin ella, 403): es la defensa contra CSRF de
// nodered/autenticacion-api.js. No lleva cuerpo: la orden a la visión no lleva cuenta.
import { pedirJson } from './asistencia'
import { ErrorApi } from './energia'
import { SIN_RESPUESTA, interpretarSolicitud, leerPrueba, type Prueba, type ResultadoSolicitud } from './pruebaRespuestas'

const RUTA_ESTADO = '/api/enrolamiento/prueba'
const RUTA_PROBAR = '/api/enrolamiento/probar'
const TIEMPO_MAXIMO_MS = 8000
// El POST solo valida y publica por MQTT: más margen que una lectura, sin llegar al del diálogo de credenciales.
const TIEMPO_SOLICITUD_MS = 15_000

export interface ConsultaPrueba {
  prueba: Prueba
  /** Hora de la respuesta según la cabecera Date de la placa; sin ella, el reloj del navegador. */
  horaServidor: number
}

/**
 * Consulta lo último que Node-RED sabe de la prueba. Cancela con `senal` y también si la API tarda más de
 * `tiempoMaximoMs` (la primera consulta de cada montaje admite más, TIEMPO_INICIAL_MS, por el diálogo de usuario y
 * contraseña). Un 401 lanza ErrorSinSesion (pedirJson).
 */
export async function pedirPrueba(senal: AbortSignal, tiempoMaximoMs: number = TIEMPO_MAXIMO_MS): Promise<ConsultaPrueba> {
  const { cuerpo, horaServidor } = await pedirJson(RUTA_ESTADO, senal, tiempoMaximoMs, false)
  const lectura = leerPrueba(cuerpo)
  if (!lectura.ok) {
    throw new ErrorApi(`La API devolvió una prueba inválida (${lectura.motivo}). Avisa al responsable del borde.`)
  }
  return { prueba: lectura.prueba, horaServidor }
}

/**
 * Pide una prueba de reconocimiento. No se cancela desde fuera: abortar no deshace lo que la API ya publicó. Sin
 * respuesta (sin red o plazo agotado) devuelve un fallo que dice que no se sabe si salió.
 */
export async function solicitarPrueba(): Promise<ResultadoSolicitud> {
  const controlador = new AbortController()
  const temporizador = setTimeout(() => controlador.abort(), TIEMPO_SOLICITUD_MS)
  try {
    let respuesta: Response
    try {
      respuesta = await fetch(RUTA_PROBAR, {
        method: 'POST',
        signal: controlador.signal,
        headers: { Accept: 'application/json', 'X-Pase-Lista': '1' },
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
    return interpretarSolicitud(respuesta.status, json)
  } finally {
    clearTimeout(temporizador)
  }
}
