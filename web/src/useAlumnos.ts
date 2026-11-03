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
  /**
   * Consulta ya y retoma el sondeo; abre otra vez el diálogo de credenciales si hace falta. Sirve
   * tras importar y tras retirar un rostro. Devuelve una promesa que se cumple cuando la lista
   * nueva ya está pintada, y se rechaza si la consulta falla o la vista se desmonta antes. Quien
   * no la use puede ignorarla: nunca queda un rechazo sin atender.
   */
  recargar: () => Promise<void>
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
  const primeraPendiente = useRef(true)
  // Promesas de `recargar` que esperan la consulta en curso, y las que ya la vieron salir bien y
  // esperan a que su lista esté pintada (se cumplen en un efecto, después del commit).
  const esperas = useRef<{ ok: () => void; fallo: (motivo: Error) => void }[]>([])
  const porCumplir = useRef<{ ok: () => void; fallo: (motivo: Error) => void }[]>([])
  const recargar = useCallback(() => {
    const promesa = new Promise<void>((ok, fallo) => {
      esperas.current.push({ ok, fallo })
    })
    promesa.catch(() => {}) // quien la ignora no deja un rechazo sin atender
    setIntento((n) => n + 1)
    return promesa
  }, [])

  useEffect(() => {
    const pendientes = porCumplir.current
    porCumplir.current = []
    pendientes.forEach((p) => p.ok())
  }, [estado])

  // Al desmontar, las que esperan se rechazan: nadie debe actuar sobre una vista que ya no está.
  useEffect(
    () => () => {
      const todas = [...esperas.current.splice(0), ...porCumplir.current.splice(0)]
      todas.forEach((p) => p.fallo(new Error('La vista se desmontó antes de recibir la lista')))
    },
    [],
  )

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
        porCumplir.current.push(...esperas.current.splice(0))
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
        esperas.current.splice(0).forEach((p) => p.fallo(new Error(mensaje)))
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
