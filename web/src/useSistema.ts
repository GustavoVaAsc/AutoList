import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { ErrorSinSesion, TIEMPO_INICIAL_MS, mensajeDeError } from './api/energia'
import { pedirSistema } from './api/sistema'
import type { Sistema } from './api/sistemaRespuestas'

export interface EstadoSistema {
  /** Lo último que Node-RED sabe de la visión, la XIAO y la placa, de la última consulta correcta; null antes de la primera. */
  sistema: Sistema | null
  /** Mensaje del último fallo; se borra en cuanto una consulta sale bien. */
  error: string | null
  /** Hora (ms) de la última respuesta correcta, según el reloj de la placa. */
  horaServidor: number | null
  /** performance.now() al recibir esa respuesta; mide el tiempo transcurrido sin depender del reloj de la laptop. */
  recibidoEn: number | null
  /** La API respondió 401: el sondeo queda detenido hasta que la persona reintente. */
  sinSesion: boolean
  /** Vuelve a consultar ya (y retoma el sondeo); abre otra vez el diálogo si hace falta. */
  reintentar: () => void
}

type DatosSistema = Omit<EstadoSistema, 'reintentar'>

/**
 * Consulta `GET /api/sistema/estado` cada `cadaMs` milisegundos, con el mismo sondeo que useLecturas y
 * useLatencia: sin consultas encima de otra, en pausa con la pestaña oculta y detenido ante un 401 para no
 * reabrir el diálogo de HTTP Basic en bucle. La primera consulta de cada montaje admite TIEMPO_INICIAL_MS por
 * ese diálogo. Vive en App (como useLecturas): la cabecera necesita saber cómo están la visión y la cámara
 * en cualquier vista.
 */
export function useSistema(cadaMs: number): EstadoSistema {
  const [estado, setEstado] = useState<DatosSistema>({
    sistema: null,
    error: null,
    horaServidor: null,
    recibidoEn: null,
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
        const { sistema, horaServidor } = await pedirSistema(
          cancelacion.signal,
          primeraPendiente.current ? TIEMPO_INICIAL_MS : undefined,
        )
        if (cancelacion.signal.aborted) {
          return
        }
        primeraPendiente.current = false
        setEstado({ sistema, error: null, horaServidor, recibidoEn: performance.now(), sinSesion: false })
      } catch (error) {
        if (cancelacion.signal.aborted) {
          return
        }
        sinSesion = error instanceof ErrorSinSesion
        const mensaje = mensajeDeError(error)
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
  }, [cadaMs, intento])

  return useMemo(() => ({ ...estado, reintentar }), [estado, reintentar])
}
