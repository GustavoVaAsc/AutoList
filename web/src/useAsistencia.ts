import { useCallback, useEffect, useRef, useState } from 'react'
import { pedirDetalleSesion, pedirSesiones } from './api/asistencia'
import {
  cuentasConEstadoCambiado,
  elegirSesionActiva,
  type DetalleSesion,
  type ResumenSesion,
} from './api/asistenciaRespuestas'
import { ErrorSinSesion, TIEMPO_INICIAL_MS, mensajeDeError } from './api/energia'

const LIMITE_SESIONES = 50
const SIN_CAMBIOS: readonly string[] = []

export interface EstadoAsistenciaVista {
  /** Lista de sesiones más recientes; null antes de la primera respuesta correcta. */
  sesiones: ResumenSesion[] | null
  /** Detalle nominal de la sesión seleccionada; null si no hay sesiones o mientras carga la elegida. */
  detalle: DetalleSesion | null
  /** Id de la sesión actualmente seleccionada. */
  idSeleccionado: number | null
  /** True cuando el profesor eligió otra sesión y aún se espera su detalle. */
  cargandoDetalle: boolean
  /** Último fallo de red o validación; se limpia cuando una consulta responde bien. */
  error: string | null
  /** Reloj del servidor en la última respuesta correcta. */
  horaServidor: number | null
  /** La API respondió 401: el sondeo queda detenido hasta que la persona reintente. */
  sinSesion: boolean
  /** Cuentas cuyo estado cambió entre dos sondeos de la misma sesión. */
  cambiadas: readonly string[]
  /** Cambia la sesión mostrada y solicita su detalle de inmediato. */
  seleccionarSesion: (id: number) => void
  /** Reintenta de inmediato (por ejemplo tras un 401). */
  reintentar: () => void
}

type DatosInternos = Omit<EstadoAsistenciaVista, 'seleccionarSesion' | 'reintentar'>

/**
 * Consulta periódicamente `GET /api/sesiones` y `GET /api/sesiones/:id`. Se pausa
 * cuando la pestaña está oculta y se detiene ante un 401 para no reabrir el
 * diálogo de HTTP Basic en bucle.
 */
export function useAsistencia(cadaMs: number): EstadoAsistenciaVista {
  const [idElegido, setIdElegido] = useState<number | null>(null)
  const [intento, setIntento] = useState(0)
  const primeraPendiente = useRef(true)

  const [estado, setEstado] = useState<DatosInternos>({
    sesiones: null,
    detalle: null,
    idSeleccionado: null,
    cargandoDetalle: false,
    error: null,
    horaServidor: null,
    sinSesion: false,
    cambiadas: SIN_CAMBIOS,
  })

  const seleccionarSesion = useCallback((id: number) => {
    setIdElegido(id)
    setEstado((anterior) => ({
      ...anterior,
      idSeleccionado: id,
      cargandoDetalle: anterior.detalle?.sesion.id !== id,
      cambiadas: SIN_CAMBIOS,
    }))
  }, [])

  const reintentar = useCallback(() => {
    setIntento((n) => n + 1)
  }, [])

  useEffect(() => {
    const cancelacion = new AbortController()
    let temporizador: number | undefined
    let enCurso = false
    let sinSesionLocal = false

    async function consultar() {
      enCurso = true
      sinSesionLocal = false
      const topeMs = primeraPendiente.current ? TIEMPO_INICIAL_MS : undefined
      try {
        const { sesiones, horaServidor: horaLista } = await pedirSesiones(
          LIMITE_SESIONES,
          cancelacion.signal,
          topeMs,
        )
        if (cancelacion.signal.aborted) return
        primeraPendiente.current = false

        const idActivo = elegirSesionActiva(sesiones, idElegido)
        if (idActivo === null) {
          setEstado({
            sesiones,
            detalle: null,
            idSeleccionado: null,
            cargandoDetalle: false,
            error: null,
            horaServidor: horaLista,
            sinSesion: false,
            cambiadas: SIN_CAMBIOS,
          })
          return
        }

        const { detalle, horaServidor: horaDetalle } = await pedirDetalleSesion(
          idActivo,
          cancelacion.signal,
          topeMs,
        )
        if (cancelacion.signal.aborted) return

        setEstado((anterior) => {
          const cambiadas =
            detalle === null
              ? SIN_CAMBIOS
              : cuentasConEstadoCambiado(
                  anterior.detalle?.alumnos ?? null,
                  anterior.detalle?.sesion.id ?? null,
                  detalle.alumnos,
                  detalle.sesion.id,
                )
          return {
            sesiones,
            detalle,
            idSeleccionado: idActivo,
            cargandoDetalle: false,
            error: null,
            horaServidor: horaDetalle,
            sinSesion: false,
            cambiadas: cambiadas.length === 0 ? SIN_CAMBIOS : cambiadas,
          }
        })
      } catch (error) {
        if (cancelacion.signal.aborted) return
        sinSesionLocal = error instanceof ErrorSinSesion
        const mensaje = mensajeDeError(error)
        setEstado((anterior) => ({
          ...anterior,
          cargandoDetalle: false,
          error: mensaje,
          sinSesion: sinSesionLocal,
          cambiadas: SIN_CAMBIOS,
        }))
      } finally {
        enCurso = false
        if (!cancelacion.signal.aborted && !sinSesionLocal) {
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
      if (document.hidden) {
        window.clearTimeout(temporizador)
      } else if (!enCurso && !sinSesionLocal) {
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
  }, [cadaMs, idElegido, intento])

  return {
    ...estado,
    seleccionarSesion,
    reintentar,
  }
}
