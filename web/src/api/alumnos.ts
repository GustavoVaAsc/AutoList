// Cliente de GET /api/alumnos y de POST /api/alumnos/importar
// (nodered/flows/flows.json, pestaña "Alumnos"). Aquí vive solo el fetch; la
// interpretación de las respuestas es pura y está en alumnosRespuestas.ts.
// Las credenciales no se guardan: la API pide HTTP Basic y el navegador, que es
// del mismo origen, muestra su diálogo y reenvía lo que la persona escribió.
import { ErrorApi, ErrorSinSesion } from './energia'
import { interpretarRespuesta, leerAlumnos, type Alumno, type Decision, type ResultadoImportacion } from './alumnosRespuestas'

const RUTA = '/api/alumnos'
const RUTA_IMPORTAR = '/api/alumnos/importar'
const TIEMPO_MAXIMO_MS = 8000
// La importación valida 500 filas y escribe en una transacción, pero un 256 KiB
// por Wi-Fi de aula no es instantáneo: 30 s antes de rendirse (120 s si falta
// iniciar sesión; ver enviarImportacion).
const TIEMPO_IMPORTAR_MS = 30_000

export interface ConsultaAlumnos {
  alumnos: Alumno[]
  /** Hora de la respuesta según la cabecera Date de la placa; sin ella, el reloj del navegador. */
  horaServidor: number
}

/**
 * Consulta la lista completa de alumnos (el filtro por grupo se hace en la
 * vista, con los grupos que trae la propia lista). Cancela con `senal` y
 * también si la API tarda más de `tiempoMaximoMs`; la primera consulta admite
 * más (TIEMPO_INICIAL_MS), porque espera el diálogo de usuario y contraseña.
 */
export async function pedirAlumnos(senal: AbortSignal, tiempoMaximoMs: number = TIEMPO_MAXIMO_MS): Promise<ConsultaAlumnos> {
  const controlador = new AbortController()
  const alCancelar = () => controlador.abort()
  senal.addEventListener('abort', alCancelar)
  const temporizador = setTimeout(() => controlador.abort(), tiempoMaximoMs)
  const tiempoAgotado = () => new ErrorApi('La API tardó demasiado en responder. Revisa la conexión con la placa.')
  try {
    let respuesta: Response
    try {
      respuesta = await fetch(RUTA, { signal: controlador.signal, headers: { Accept: 'application/json' } })
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
    const lectura = leerAlumnos(cuerpo)
    if (!lectura.ok) {
      throw new ErrorApi(lectura.motivo)
    }
    const fechaServidor = Date.parse(respuesta.headers.get('Date') ?? '')
    return { alumnos: lectura.alumnos, horaServidor: Number.isNaN(fechaServidor) ? Date.now() : fechaServidor }
  } finally {
    clearTimeout(temporizador)
    senal.removeEventListener('abort', alCancelar)
  }
}

/**
 * Sube el CSV crudo. Sin `decision`, si alguna cuenta ya existe la API
 * responde 409 y no escribe nada: la decisión es de la persona, y solo
 * entonces se reenvía con `?existentes=actualizar|omitir`. Devuelve siempre un
 * resultado (los fallos de red y de plazo incluidos). No se cancela desde
 * fuera: abortar un POST no deshace lo que la API ya escribió, así que la
 * importación sigue su curso aunque la persona cambie de vista.
 *
 * `tiempoMaximoMs` es mayor (TIEMPO_INICIAL_MS) cuando la sesión no está
 * iniciada: el navegador puede volver a pedir las credenciales con su diálogo y
 * la petición espera a que la persona las escriba.
 *
 * Tres requisitos de la API que el navegador no pone solo: Content-Type
 * text/csv (otro, 415), X-Pase-Lista: 1 en todo método que cambia datos (sin
 * ella, 403) y el cuerpo como el archivo mismo y no multipart (el `File` manda
 * Content-Length, que la API exige: 411 sin él).
 */
export async function enviarImportacion(
  archivo: File,
  decision: Decision | undefined,
  tiempoMaximoMs: number = TIEMPO_IMPORTAR_MS,
): Promise<ResultadoImportacion> {
  const controlador = new AbortController()
  const temporizador = setTimeout(() => controlador.abort(), tiempoMaximoMs)
  const ruta = decision === undefined ? RUTA_IMPORTAR : `${RUTA_IMPORTAR}?existentes=${decision}`
  try {
    let respuesta: Response
    try {
      respuesta = await fetch(ruta, {
        method: 'POST',
        signal: controlador.signal,
        headers: {
          Accept: 'application/json',
          'Content-Type': 'text/csv; charset=utf-8',
          'X-Pase-Lista': '1',
        },
        body: archivo,
      })
    } catch {
      // Sin respuesta no se sabe si la API alcanzó a escribir: se dice, para que la persona revise la lista.
      return {
        tipo: 'fallo',
        status: null,
        mensaje: controlador.signal.aborted
          ? 'La API tardó demasiado en responder. No se sabe si se importó el archivo: revisa la lista antes de reintentar.'
          : 'No se pudo conectar con la API. No se sabe si se importó el archivo: revisa la lista antes de reintentar.',
      }
    }
    let cuerpo: unknown
    try {
      cuerpo = await respuesta.json()
    } catch {
      cuerpo = undefined
    }
    return interpretarRespuesta(respuesta.status, cuerpo)
  } finally {
    clearTimeout(temporizador)
  }
}
