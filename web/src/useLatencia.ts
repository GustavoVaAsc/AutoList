import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { ErrorApi, ErrorSinSesion, TIEMPO_INICIAL_MS } from './api/energia'
import { pedirLatencias } from './api/latencia'
import type { MedicionLatencia } from './api/latenciaRespuestas'

export interface EstadoLatencia {
  /** Mediciones de la última consulta correcta, de la más nueva a la más vieja; null antes de la primera. */
  mediciones: MedicionLatencia[] | null
  /** Mensaje del último fallo; se borra en cuanto una consulta sale bien. */
  error: string | null
  /** Hora (ms) de la última respuesta correcta, según el reloj de la placa. */
  horaServidor: number | null
  /** La API respondió 401: el sondeo queda detenido hasta que la persona reintente. */
  sinSesion: boolean
  /** Vuelve a consultar ya (y retoma el sondeo); abre otra vez el diálogo si hace falta. */
  reintentar: () => void
}

type DatosLatencia = Omit<EstadoLatencia, 'reintentar'>

/**
 * Consulta `GET /api/metricas/latencia` cada `cadaMs` milisegundos, con el mismo
 * sondeo que useLecturas: sin consultas encima de otra, en pausa con la pestaña
 * oculta y detenido ante un 401 para no reabrir el diálogo de HTTP Basic en bucle.
 * La primera consulta de cada montaje admite TIEMPO_INICIAL_MS por ese diálogo.
 */
export function useLatencia(limite: number, cadaMs: number): EstadoLatencia {
  const [estado, setEstado] = useState<DatosLatencia>({
    mediciones: null,
    error: null,
    horaServidor: null,
    sinSesion: false,
  })
  const [intento, setIntento] = useState(0)
  const reintentar = useCallback(() => setIntento((n) => n + 1), [])
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
        const { mediciones, horaServidor } = await pedirLatencias(
          limite,
          cancelacion.signal,
          primeraPendiente.current ? TIEMPO_INICIAL_MS : undefined,
        )
        if (cancelacion.signal.aborted) {
          return
        }
        primeraPendiente.current = false
        setEstado({ mediciones, error: null, horaServidor, sinSesion: false })
      } catch (error) {
        if (cancelacion.signal.aborted) {
          return
        }
        sinSesion = error instanceof ErrorSinSesion
        const mensaje =
          error instanceof ErrorApi ? error.message : 'Ocurrió un error inesperado al consultar la latencia.'
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
