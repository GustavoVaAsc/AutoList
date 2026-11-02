import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { pedirLecturas, ErrorApi, ErrorSinSesion, TIEMPO_INICIAL_MS, type Lectura } from './api/energia'

export interface EstadoLecturas {
  /** Lecturas de la última consulta correcta, de la más vieja a la más nueva. */
  lecturas: Lectura[] | null
  /** Mensaje del último fallo; se borra en cuanto una consulta sale bien. */
  error: string | null
  /** Hora (ms) de la última respuesta correcta, según el reloj de la placa. */
  horaServidor: number | null
  /** performance.now() al recibir esa respuesta; mide el tiempo transcurrido sin depender del reloj de la laptop. */
  recibidoEn: number | null
  /**
   * La API pidió usuario y contraseña y no se completó el inicio de sesión (401).
   * El sondeo está detenido: reintentar solo reabriría el diálogo del navegador cada
   * pocos segundos. Se retoma con `reintentar` o recargando la página.
   */
  sinSesion: boolean
  /** Vuelve a consultar ya (y retoma el sondeo); abre otra vez el diálogo si hace falta. */
  reintentar: () => void
}

type DatosLecturas = Omit<EstadoLecturas, 'reintentar'>

/** Una lectura más nueva que esto se considera "en vivo". */
export const LECTURA_RECIENTE_MS = 60_000

/**
 * Antigüedad (ms) de la última lectura en el instante `ahora` (performance.now()),
 * o null si aún no hay lecturas. Parte de la hora de la placa y suma el tiempo
 * transcurrido desde la respuesta, así sigue contando entre consultas.
 */
export function antiguedadUltimaLectura(estado: EstadoLecturas, ahora: number): number | null {
  const ultima = estado.lecturas?.[estado.lecturas.length - 1]
  if (ultima === undefined || estado.horaServidor === null || estado.recibidoEn === null) {
    return null
  }
  return Math.max(0, estado.horaServidor - ultima.t + (ahora - estado.recibidoEn))
}

/**
 * Consulta la API cada `cadaMs` milisegundos.
 *
 * Estados del sondeo:
 * - en curso: hay una consulta pendiente; no se lanza otra encima (`enCurso`).
 * - en espera: terminó la consulta (bien o mal) y hay un temporizador agendado.
 * - pausado: el temporizador venció con la pestaña oculta; no hay consulta ni
 *   temporizador hasta que la pestaña reaparece (alCambiarVisibilidad).
 * - cancelado: el efecto se limpió (desmontaje o cambio de parámetros); ningún
 *   resultado tardío toca el estado ni agenda nada.
 * - sin sesión: la API respondió 401; no hay temporizador ni consultas por
 *   visibilidad hasta `reintentar`.
 *
 * La primera consulta de cada montaje admite hasta TIEMPO_INICIAL_MS, porque puede
 * esperar el diálogo de usuario y contraseña del navegador; tras su primera
 * respuesta correcta rige el plazo normal.
 */
export function useLecturas(limite: number, cadaMs: number): EstadoLecturas {
  const [estado, setEstado] = useState<DatosLecturas>({
    lecturas: null,
    error: null,
    horaServidor: null,
    recibidoEn: null,
    sinSesion: false,
  })
  const [intento, setIntento] = useState(0)
  const reintentar = useCallback(() => setIntento((n) => n + 1), [])
  // Vale mientras ninguna consulta de este montaje haya salido bien.
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
        const { lecturas, horaServidor } = await pedirLecturas(
          limite,
          cancelacion.signal,
          primeraPendiente.current ? TIEMPO_INICIAL_MS : undefined,
        )
        if (cancelacion.signal.aborted) {
          return
        }
        primeraPendiente.current = false
        setEstado({ lecturas, error: null, horaServidor, recibidoEn: performance.now(), sinSesion: false })
      } catch (error) {
        if (cancelacion.signal.aborted) {
          return
        }
        sinSesion = error instanceof ErrorSinSesion
        const mensaje = error instanceof ErrorApi ? error.message : 'Ocurrió un error inesperado al consultar la API.'
        setEstado((anterior) => ({ ...anterior, error: mensaje, sinSesion }))
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
  }, [limite, cadaMs, intento])

  return useMemo(() => ({ ...estado, reintentar }), [estado, reintentar])
}
