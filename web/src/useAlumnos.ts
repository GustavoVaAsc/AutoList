import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { pedirAlumnos } from './api/alumnos'
import { cuentasConRostroCambiado, type Alumno } from './api/alumnosRespuestas'
import { ErrorApi, ErrorSinSesion, TIEMPO_INICIAL_MS } from './api/energia'

export interface EstadoAlumnos {
  /** Lista de la última consulta correcta; null mientras no haya llegado ninguna. */
  alumnos: Alumno[] | null
  /** Mensaje del último fallo; se borra en cuanto una consulta sale bien. */
  error: string | null
  /** Hora (ms) de la última respuesta correcta, según el reloj de la placa. */
  horaServidor: number | null
  /** La API pidió usuario y contraseña y no se completó el inicio de sesión (401): el sondeo está detenido. */
  sinSesion: boolean
  /** Cuentas cuyo rostro cambió entre la consulta anterior y la última (SIN_CAMBIOS si ninguna). */
  cambiadas: readonly string[]
  /** Consulta ya y retoma el sondeo; abre otra vez el diálogo de credenciales si hace falta. Sirve tras importar. */
  recargar: () => void
}

type DatosAlumnos = Omit<EstadoAlumnos, 'recargar'>

// La misma lista vacía cada vez que nada cambió: un arreglo nuevo en cada sondeo
// haría correr de nuevo el efecto de resaltado de TablaAlumnos.
const SIN_CAMBIOS: readonly string[] = []

/**
 * Consulta GET /api/alumnos cada `cadaMs` milisegundos, con el mismo patrón
 * que useLecturas: la siguiente consulta se agenda al terminar la anterior,
 * se pausa con la pestaña oculta y se detiene con un 401 (reintentar solo
 * reabriría el diálogo del navegador). La primera consulta de cada montaje
 * admite hasta TIEMPO_INICIAL_MS por ese diálogo. Un fallo no borra la lista
 * que ya se tenía.
 */
export function useAlumnos(cadaMs: number): EstadoAlumnos {
  const [estado, setEstado] = useState<DatosAlumnos>({
    alumnos: null,
    error: null,
    horaServidor: null,
    sinSesion: false,
    cambiadas: SIN_CAMBIOS,
  })
  const [intento, setIntento] = useState(0)
  const recargar = useCallback(() => setIntento((n) => n + 1), [])
  const primeraPendiente = useRef(true)

  useEffect(() => {
    const cancelacion = new AbortController()
    let temporizador: number | undefined
    let enCurso = false
    let sinSesion = false

    async function consultar() {
      enCurso = true
      sinSesion = false
      try {
        const { alumnos, horaServidor } = await pedirAlumnos(cancelacion.signal, primeraPendiente.current ? TIEMPO_INICIAL_MS : undefined)
        if (cancelacion.signal.aborted) {
          return
        }
        primeraPendiente.current = false
        setEstado((anterior) => {
          const cambiadas = anterior.alumnos === null ? SIN_CAMBIOS : cuentasConRostroCambiado(anterior.alumnos, alumnos)
          return { alumnos, error: null, horaServidor, sinSesion: false, cambiadas: cambiadas.length === 0 ? SIN_CAMBIOS : cambiadas }
        })
      } catch (error) {
        if (cancelacion.signal.aborted) {
          return
        }
        sinSesion = error instanceof ErrorSinSesion
        const mensaje = error instanceof ErrorApi ? error.message : 'Ocurrió un error inesperado al consultar la API.'
        setEstado((anterior) => ({ ...anterior, error: mensaje, sinSesion, cambiadas: SIN_CAMBIOS }))
      } finally {
        enCurso = false
        if (!cancelacion.signal.aborted && !sinSesion) {
          agendar()
        }
      }
    }

    function agendar() {
      temporizador = window.setTimeout(() => {
        if (!document.hidden) {
          void consultar()
        }
      }, cadaMs)
    }

    function alCambiarVisibilidad() {
      if (!document.hidden && !enCurso && !sinSesion) {
        window.clearTimeout(temporizador)
        void consultar()
      }
    }

    document.addEventListener('visibilitychange', alCambiarVisibilidad)
    void consultar()
    return () => {
      cancelacion.abort()
      window.clearTimeout(temporizador)
      document.removeEventListener('visibilitychange', alCambiarVisibilidad)
    }
  }, [cadaMs, intento])

  return useMemo(() => ({ ...estado, recargar }), [estado, recargar])
}
