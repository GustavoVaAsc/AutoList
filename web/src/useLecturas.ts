import { useEffect, useState } from 'react'
import { pedirLecturas, ErrorApi, type Lectura } from './api/energia'

export interface EstadoLecturas {
  /** Lecturas de la última consulta correcta, de la más vieja a la más nueva. */
  lecturas: Lectura[] | null
  /** Mensaje del último fallo; se borra en cuanto una consulta sale bien. */
  error: string | null
  /** Hora (ms) de la última respuesta correcta, según el reloj de la placa. */
  horaServidor: number | null
  /** performance.now() al recibir esa respuesta; mide el tiempo transcurrido sin depender del reloj de la laptop. */
  recibidoEn: number | null
}

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
 */
export function useLecturas(limite: number, cadaMs: number): EstadoLecturas {
  const [estado, setEstado] = useState<EstadoLecturas>({
    lecturas: null,
    error: null,
    horaServidor: null,
    recibidoEn: null,
  })

  useEffect(() => {
    const cancelacion = new AbortController()
    let temporizador: number | undefined
    let enCurso = false

    async function consultar() {
      enCurso = true
      try {
        const { lecturas, horaServidor } = await pedirLecturas(limite, cancelacion.signal)
        if (cancelacion.signal.aborted) {
          return
        }
        setEstado({ lecturas, error: null, horaServidor, recibidoEn: performance.now() })
      } catch (error) {
        if (cancelacion.signal.aborted) {
          return
        }
        const mensaje = error instanceof ErrorApi ? error.message : 'Ocurrió un error inesperado al consultar la API.'
        setEstado((anterior) => ({ ...anterior, error: mensaje }))
      } finally {
        enCurso = false
        if (!cancelacion.signal.aborted) {
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
      if (!document.hidden && !enCurso) {
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
  }, [limite, cadaMs])

  return estado
}
