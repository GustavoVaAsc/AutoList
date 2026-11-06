// Cliente de los endpoints de registro (nodered/flows/flows.json, pestaña "Registro"):
// GET /api/enrolamiento/estado, POST /api/enrolamiento, POST /api/enrolamiento/cancelar
// y DELETE /api/alumnos/:cuenta/rostro. Aquí vive solo el fetch; la interpretación de
// las respuestas es pura y está en registroRespuestas.ts.
// Las credenciales no se guardan: la API pide HTTP Basic y el navegador, que es del
// mismo origen, muestra su diálogo y reenvía lo que la persona escribió.
// Todo método que cambia datos manda X-Pase-Lista: 1 (sin ella, 403): es la defensa
// contra CSRF de nodered/autenticacion-api.js.
import { ErrorApi, ErrorSinSesion } from './energia'
import {
  interpretarCancelacion,
  interpretarInicio,
  interpretarRetiro,
  leerRegistro,
  type Registro,
  type ResultadoCancelacion,
  type ResultadoInicio,
  type ResultadoRetiro,
} from './registroRespuestas'

const RUTA_ESTADO = '/api/enrolamiento/estado'
const RUTA_INICIAR = '/api/enrolamiento'
const RUTA_CANCELAR = '/api/enrolamiento/cancelar'
const TIEMPO_MAXIMO_MS = 8000
// Los cambios escriben en la base y publican por MQTT: más margen que una lectura,
// pero no tanto como el diálogo de credenciales (120 s cuando falta iniciar sesión).
const TIEMPO_CAMBIO_MS = 15_000

export interface ConsultaRegistro {
  registro: Registro
  /** Hora de la respuesta según la cabecera Date de la placa; sin ella, el reloj del navegador. */
  horaServidor: number
}

/**
 * Consulta el estado del registro. Cancela con `senal` y también si la API tarda
 * más de `tiempoMaximoMs`; la primera consulta de cada montaje admite más
 * (TIEMPO_INICIAL_MS de energia.ts), porque espera el diálogo de usuario y contraseña.
 */
export async function pedirEstadoRegistro(senal: AbortSignal, tiempoMaximoMs: number = TIEMPO_MAXIMO_MS): Promise<ConsultaRegistro> {
  const controlador = new AbortController()
  const alCancelar = () => controlador.abort()
  senal.addEventListener('abort', alCancelar)
  const temporizador = setTimeout(() => controlador.abort(), tiempoMaximoMs)
  const tiempoAgotado = () => new ErrorApi('La API tardó demasiado en responder. Revisa la conexión con la placa.')
  try {
    let respuesta: Response
    try {
      respuesta = await fetch(RUTA_ESTADO, { signal: controlador.signal, headers: { Accept: 'application/json' } })
    } catch (error) {
      if (senal.aborted) {
        throw error
      }
      throw controlador.signal.aborted
        ? tiempoAgotado()
        : new ErrorApi('No se pudo conectar con la API. Revisa que la placa esté encendida y conectada a la red del aula.')
    }
    if (respuesta.status === 401) {
      throw new ErrorSinSesion('La API pide usuario y contraseña del profesor y no se completó el inicio de sesión.')
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
    const lectura = leerRegistro(cuerpo)
    if (!lectura.ok) {
      throw new ErrorApi(lectura.motivo)
    }
    const fechaServidor = Date.parse(respuesta.headers.get('Date') ?? '')
    return { registro: lectura.registro, horaServidor: Number.isNaN(fechaServidor) ? Date.now() : fechaServidor }
  } finally {
    clearTimeout(temporizador)
    senal.removeEventListener('abort', alCancelar)
  }
}

interface RespuestaCruda {
  status: number
  cuerpo: unknown
}

/**
 * Envía una petición que cambia datos y devuelve el código y el JSON (undefined si
 * no era JSON), o null si no hubo respuesta (sin red o plazo agotado): en ese caso no
 * se sabe si la API alcanzó a escribir. No se cancela desde fuera: abortar no deshace
 * lo que la API ya escribió.
 */
async function enviarCambio(ruta: string, metodo: 'POST' | 'DELETE', cuerpo?: unknown, tiempoMaximoMs: number = TIEMPO_CAMBIO_MS): Promise<RespuestaCruda | null> {
  const controlador = new AbortController()
  const temporizador = setTimeout(() => controlador.abort(), tiempoMaximoMs)
  const cabeceras: Record<string, string> = { Accept: 'application/json', 'X-Pase-Lista': '1' }
  if (cuerpo !== undefined) {
    cabeceras['Content-Type'] = 'application/json'
  }
  try {
    let respuesta: Response
    try {
      respuesta = await fetch(ruta, {
        method: metodo,
        signal: controlador.signal,
        headers: cabeceras,
        body: cuerpo === undefined ? undefined : JSON.stringify(cuerpo),
      })
    } catch {
      return null
    }
    let json: unknown
    try {
      json = await respuesta.json()
    } catch {
      json = undefined
    }
    return { status: respuesta.status, cuerpo: json }
  } finally {
    clearTimeout(temporizador)
  }
}

const SIN_RESPUESTA = (accion: string) =>
  `No hubo respuesta de la API (sin conexión o tardó demasiado). No se sabe si ${accion}: revisa el estado antes de reintentar.`

/**
 * Inicia el registro. La API exige Content-Type application/json (otro, 415) y que
 * `consentimiento` sea exactamente true (sin él, 400): el frontend no lo manda si la
 * persona no marcó la casilla.
 */
export async function iniciarRegistro(cuenta: string): Promise<ResultadoInicio> {
  const r = await enviarCambio(RUTA_INICIAR, 'POST', { cuenta, consentimiento: true })
  return r === null ? { tipo: 'fallo', status: null, mensaje: SIN_RESPUESTA('se inició el registro') } : interpretarInicio(r.status, r.cuerpo)
}

/** Cancela el registro abierto (idempotente: sin registro abierto responde 200). */
export async function cancelarRegistro(): Promise<ResultadoCancelacion> {
  const r = await enviarCambio(RUTA_CANCELAR, 'POST')
  return r === null ? { tipo: 'fallo', status: null, mensaje: SIN_RESPUESTA('se canceló el registro') } : interpretarCancelacion(r.status, r.cuerpo)
}

/**
 * Retira el rostro y el consentimiento del alumno. La cuenta va en la ruta, así que
 * se codifica por si algún día deja de ser solo dígitos.
 */
export async function retirarRostro(cuenta: string): Promise<ResultadoRetiro> {
  const r = await enviarCambio(`/api/alumnos/${encodeURIComponent(cuenta)}/rostro`, 'DELETE')
  return r === null ? { tipo: 'fallo', status: null, mensaje: SIN_RESPUESTA('se retiró el rostro') } : interpretarRetiro(r.status, r.cuerpo)
}
