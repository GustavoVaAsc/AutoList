import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { ErrorSinSesion, TIEMPO_INICIAL_MS, mensajeDeError } from './api/energia'
import { cancelarRegistro, iniciarRegistro, pedirEstadoRegistro } from './api/registro'
import {
  AVISO_REGISTRO_PERDIDO,
  avisoDeCancelacion,
  avisoDeInicio,
  siguienteVista,
  VISTA_INICIAL,
  type AvisoRegistro,
  type EstadoVista,
  type EventoVista,
} from './api/registroRespuestas'

// Mientras hay un registro abierto, el progreso se consulta cada segundo (decisión del
// proyecto: sondeo, no MQTT en el navegador; plan, sección 6.7). Es el ritmo con que la
// visión publica el avance de las poses. Si la primera consulta falla, se reintenta más
// despacio: aún no se sabe si hay un registro abierto.
const CONSULTA_ABIERTO_MS = 1000
const REINTENTO_INICIAL_MS = 3000

export interface EstadoRegistroVista {
  vista: EstadoVista
  /** Mensaje del último fallo al consultar; se borra en cuanto una consulta sale bien. */
  error: string | null
  /** La API pidió usuario y contraseña y no se completó el inicio de sesión (401): el sondeo está detenido. */
  sinSesion: boolean
  /** Hora (ms) de la última respuesta correcta, según el reloj de la placa. */
  horaServidor: number | null
  /** Milisegundos desde que arrancó el registro mostrado, según el reloj de la placa (null sin registro). */
  transcurridoMs: number | null
  /** Aviso de la última acción que no terminó bien; null si no hay. */
  aviso: AvisoRegistro | null
  /** Hay una acción (iniciar o cancelar) esperando respuesta. */
  ocupado: 'iniciar' | 'cancelar' | null
  iniciar: (cuenta: string) => void
  cancelar: () => void
  /** "Registrar a otro alumno": quita el resultado del registro anterior. */
  descartarResultado: () => void
  /** Consulta ya y retoma el sondeo; abre otra vez el diálogo de credenciales si hace falta. */
  reintentar: () => void
  descartarAviso: () => void
}

/**
 * Estado de la vista Registro. Consulta GET /api/enrolamiento/estado una vez al
 * montarse (si hay un registro abierto, se retoma) y cada segundo mientras haya uno
 * abierto: la siguiente consulta se agenda al terminar la anterior, se pausa con la
 * pestaña oculta y se detiene con un 401 (reintentar solo reabriría el diálogo del
 * navegador), igual que useAlumnos y useLecturas. La fase de la vista sale de la
 * máquina pura `siguienteVista` (api/registroRespuestas.ts).
 *
 * `vistaRef` guarda la fase al instante: el ciclo de consultas decide si se agenda
 * otra justo después de despachar un evento, antes de que React repinte.
 */
export function useRegistro(): EstadoRegistroVista {
  const [vista, setVista] = useState<EstadoVista>(VISTA_INICIAL)
  const vistaRef = useRef(vista)
  const [datos, setDatos] = useState<{ error: string | null; sinSesion: boolean; horaServidor: number | null }>({
    error: null,
    sinSesion: false,
    horaServidor: null,
  })
  const [aviso, setAviso] = useState<AvisoRegistro | null>(null)
  const [ocupado, setOcupado] = useState<'iniciar' | 'cancelar' | null>(null)
  const ocupadoRef = useRef<'iniciar' | 'cancelar' | null>(null)
  const [intento, setIntento] = useState(0)
  const consultarRef = useRef<() => void>(() => {})

  const despachar = useCallback((evento: EventoVista) => {
    const anterior = vistaRef.current
    const siguiente = siguienteVista(anterior, evento)
    if (siguiente !== anterior) {
      vistaRef.current = siguiente
      setVista(siguiente)
      if (anterior.fase === 'abierto' && siguiente.fase === 'libre') {
        setAviso(AVISO_REGISTRO_PERDIDO)
      }
    }
  }, [])

  useEffect(() => {
    const cancelacion = new AbortController()
    let temporizador: number | undefined
    let enCurso = false
    // Una orden de consultar que llegó con otra consulta en vuelo (tras iniciar o cancelar):
    // no se pierde, se atiende al terminar esa.
    let repetir = false
    let sinSesion = false
    let primeraPendiente = true

    function agendar() {
      window.clearTimeout(temporizador)
      const fase = vistaRef.current.fase
      const espera = fase === 'abierto' ? CONSULTA_ABIERTO_MS : fase === 'comprobando' ? REINTENTO_INICIAL_MS : null
      if (espera === null || cancelacion.signal.aborted || sinSesion) {
        return
      }
      temporizador = window.setTimeout(() => {
        if (!document.hidden) {
          void consultar()
        }
      }, espera)
    }

    async function consultar() {
      window.clearTimeout(temporizador)
      if (enCurso) {
        repetir = true
        return
      }
      enCurso = true
      sinSesion = false
      try {
        const { registro, horaServidor } = await pedirEstadoRegistro(cancelacion.signal, primeraPendiente ? TIEMPO_INICIAL_MS : undefined)
        if (cancelacion.signal.aborted) {
          return
        }
        primeraPendiente = false
        despachar({ tipo: 'estado', registro })
        setDatos({ error: null, sinSesion: false, horaServidor })
      } catch (error) {
        if (cancelacion.signal.aborted) {
          return
        }
        sinSesion = error instanceof ErrorSinSesion
        const mensaje = mensajeDeError(error)
        setDatos((anterior) => ({ ...anterior, error: mensaje, sinSesion }))
      } finally {
        enCurso = false
        if (repetir && !cancelacion.signal.aborted) {
          repetir = false
          void consultar()
        } else {
          agendar()
        }
      }
    }

    function alCambiarVisibilidad() {
      if (!document.hidden && !enCurso && !sinSesion) {
        void consultar()
      }
    }

    consultarRef.current = () => void consultar()
    document.addEventListener('visibilitychange', alCambiarVisibilidad)
    void consultar()
    return () => {
      cancelacion.abort()
      window.clearTimeout(temporizador)
      document.removeEventListener('visibilitychange', alCambiarVisibilidad)
      consultarRef.current = () => {}
    }
  }, [despachar, intento])

  const iniciar = useCallback(
    (cuenta: string) => {
      // Un solo envío a la vez, decidido con una ref y no con el estado de React.
      if (ocupadoRef.current !== null) {
        return
      }
      ocupadoRef.current = 'iniciar'
      setOcupado('iniciar')
      setAviso(null)
      despachar({ tipo: 'inicio-enviado' })
      void iniciarRegistro(cuenta).then((resultado) => {
        ocupadoRef.current = null
        setOcupado(null)
        const avisoInicio = avisoDeInicio(resultado)
        setAviso(avisoInicio)
        if (resultado.tipo === 'iniciado' || resultado.tipo === 'ocupado') {
          // Se abrió el registro (este o el que ya estaba): la consulta lo trae y empieza el sondeo.
          consultarRef.current()
        } else {
          despachar({ tipo: 'inicio-fallido' })
          if (resultado.tipo === 'fallo' && resultado.status === null) {
            // Sin respuesta no se sabe si la API alcanzó a abrir el registro: se pregunta.
            consultarRef.current()
          }
        }
      })
    },
    [despachar],
  )

  const cancelar = useCallback(() => {
    if (ocupadoRef.current !== null) {
      return
    }
    ocupadoRef.current = 'cancelar'
    setOcupado('cancelar')
    setAviso(null)
    void cancelarRegistro().then((resultado) => {
      ocupadoRef.current = null
      setOcupado(null)
      setAviso(avisoDeCancelacion(resultado))
      // Con o sin confirmación, lo que vale es lo que dice la API ahora.
      consultarRef.current()
    })
  }, [])

  const descartarResultado = useCallback(() => {
    setAviso(null)
    despachar({ tipo: 'descartar-resultado' })
  }, [despachar])

  const reintentar = useCallback(() => setIntento((n) => n + 1), [])
  const descartarAviso = useCallback(() => setAviso(null), [])

  const registro = vista.registro
  const transcurridoMs =
    registro !== null && registro.inicio !== null && datos.horaServidor !== null ? Math.max(0, datos.horaServidor - registro.inicio) : null

  return useMemo(
    () => ({
      vista,
      error: datos.error,
      sinSesion: datos.sinSesion,
      horaServidor: datos.horaServidor,
      transcurridoMs,
      aviso,
      ocupado,
      iniciar,
      cancelar,
      descartarResultado,
      reintentar,
      descartarAviso,
    }),
    [vista, datos, transcurridoMs, aviso, ocupado, iniciar, cancelar, descartarResultado, reintentar, descartarAviso],
  )
}
