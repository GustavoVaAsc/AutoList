import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { toast } from 'sonner'
import { ErrorApi, ErrorSinSesion, TIEMPO_INICIAL_MS } from './api/energia'
import { pedirPrueba, solicitarPrueba } from './api/prueba'
import type { Prueba } from './api/pruebaRespuestas'

// Mientras hay una prueba pendiente, el resultado se consulta cada segundo (como el progreso del registro: sondeo,
// no MQTT en el navegador). Sin prueba pendiente no hay sondeo: la hoja solo consulta al montarse.
const CONSULTA_PENDIENTE_MS = 1000

export interface EstadoPruebaVista {
  /** Lo último que dijo la API; null antes de la primera respuesta. */
  prueba: Prueba | null
  /** Mensaje del último fallo al consultar; se borra en cuanto una consulta sale bien. */
  error: string | null
  /** La API respondió 401: el sondeo queda detenido hasta que la persona reintente. */
  sinSesion: boolean
  /** Hora (ms) de la última respuesta correcta, según el reloj de la placa. */
  horaServidor: number | null
  /** performance.now() al recibir esa respuesta: con `horaServidor` da la antigüedad de lo que se ve sin depender del reloj de la laptop. */
  recibidoEn: number | null
  /** La orden va en camino, hasta que una consulta hecha después de que la API respondió confirma el estado nuevo. */
  enviando: boolean
  solicitar: () => void
  /** Consulta ya y retoma el sondeo; abre otra vez el diálogo de credenciales si hace falta. */
  reintentar: () => void
}

interface Datos {
  prueba: Prueba | null
  error: string | null
  sinSesion: boolean
  horaServidor: number | null
  recibidoEn: number | null
}

/**
 * Estado de la prueba de reconocimiento. Consulta GET /api/enrolamiento/prueba una vez al montarse (si hay una prueba
 * pendiente o un resultado, se retoma) y cada segundo solo mientras haya una pendiente: la siguiente consulta se
 * agenda al terminar la anterior, se pausa con la pestaña oculta y se detiene con un 401 (reintentar solo reabriría
 * el diálogo del navegador), igual que useRegistro. `solicitar` manda la orden una a la vez (una ref, no el estado
 * de React); los errores salen en un toast: Sonner vive en `App`.
 */
export function usePrueba(): EstadoPruebaVista {
  const [datos, setDatos] = useState<Datos>({ prueba: null, error: null, sinSesion: false, horaServidor: null, recibidoEn: null })
  const [enviando, setEnviando] = useState(false)
  const enviandoRef = useRef(false)
  const [intento, setIntento] = useState(0)
  // `esperarResultado`: la orden salió (o puede haber salido): se sigue consultando aunque esta consulta falle.
  const consultarRef = useRef<(esperarResultado: boolean) => Promise<void>>(() => Promise.resolve())

  useEffect(() => {
    const cancelacion = new AbortController()
    let temporizador: number | undefined
    let enCurso = false
    // Una orden de consultar que llegó con otra en vuelo (tras pedir la prueba): no se pierde, se atiende al terminar esa.
    // Quien la pidió espera (en `esperas`) a que termine la consulta repetida, no la que ya iba: esa pudo salir antes de
    // que la API supiera de la prueba nueva.
    let repetir = false
    const esperas: Array<() => void> = []
    let sinSesion = false
    let pendiente = false
    let primeraPendiente = true

    function agendar() {
      window.clearTimeout(temporizador)
      if (!pendiente || cancelacion.signal.aborted || sinSesion) {
        return
      }
      temporizador = window.setTimeout(() => {
        if (!document.hidden) {
          void consultar()
        }
      }, CONSULTA_PENDIENTE_MS)
    }

    async function consultar(): Promise<void> {
      window.clearTimeout(temporizador)
      if (enCurso) {
        repetir = true
        return new Promise<void>((resolver) => {
          esperas.push(resolver)
        })
      }
      enCurso = true
      sinSesion = false
      try {
        const { prueba, horaServidor } = await pedirPrueba(cancelacion.signal, primeraPendiente ? TIEMPO_INICIAL_MS : undefined)
        if (cancelacion.signal.aborted) {
          return
        }
        primeraPendiente = false
        pendiente = prueba.pendiente
        setDatos({ prueba, error: null, sinSesion: false, horaServidor, recibidoEn: performance.now() })
      } catch (error) {
        if (cancelacion.signal.aborted) {
          return
        }
        sinSesion = error instanceof ErrorSinSesion
        const mensaje = error instanceof ErrorApi ? error.message : 'Ocurrió un error inesperado al consultar la prueba.'
        setDatos((anterior) => ({ ...anterior, error: mensaje, sinSesion }))
      } finally {
        enCurso = false
        if (repetir && !cancelacion.signal.aborted) {
          repetir = false
          const quienes = esperas.splice(0)
          await consultar()
          quienes.forEach((resolver) => resolver())
        } else {
          esperas.splice(0).forEach((resolver) => resolver())
          agendar()
        }
      }
    }

    function alCambiarVisibilidad() {
      if (!document.hidden && pendiente && !enCurso && !sinSesion) {
        void consultar()
      }
    }

    consultarRef.current = (esperarResultado) => {
      if (esperarResultado) {
        pendiente = true
      }
      return consultar()
    }
    document.addEventListener('visibilitychange', alCambiarVisibilidad)
    void consultar()
    return () => {
      cancelacion.abort()
      window.clearTimeout(temporizador)
      document.removeEventListener('visibilitychange', alCambiarVisibilidad)
      consultarRef.current = () => Promise.resolve()
    }
  }, [intento])

  const solicitar = useCallback(() => {
    if (enviandoRef.current) {
      return
    }
    enviandoRef.current = true
    setEnviando(true)
    void solicitarPrueba().then(async (resultado) => {
      switch (resultado.tipo) {
        case 'solicitada':
          break
        case 'en-curso':
          toast.info('Ya hay una prueba en curso', { description: 'Se muestra la espera de esa prueba.' })
          break
        case 'registro-abierto':
          toast.warning('Hay un registro abierto', { description: 'La visión está ocupada con él. Termínalo o cancélalo antes de probar el reconocimiento.' })
          break
        case 'demasiado-rapido':
          toast.warning('Demasiado rápido', { description: 'La prueba no salió. Espera unos segundos entre pruebas.' })
          break
        case 'fallo':
          toast.error('No se pudo pedir la prueba', { description: resultado.mensaje, duration: 8000 })
          break
      }
      // Con o sin confirmación, lo que vale es lo que dice la API ahora (sin respuesta no se sabe si la orden salió). El botón
      // sigue desactivado hasta que esa consulta termina: `consultar` espera a una que empiece después de la respuesta del POST.
      if (resultado.tipo !== 'registro-abierto' && resultado.tipo !== 'demasiado-rapido' && !(resultado.tipo === 'fallo' && resultado.status !== null)) {
        await consultarRef.current(true)
      }
      enviandoRef.current = false
      setEnviando(false)
    })
  }, [])

  const reintentar = useCallback(() => setIntento((n) => n + 1), [])

  return useMemo(() => ({ ...datos, enviando, solicitar, reintentar }), [datos, enviando, solicitar, reintentar])
}
