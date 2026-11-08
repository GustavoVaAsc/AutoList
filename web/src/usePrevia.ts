import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { TIEMPO_INICIAL_MS } from './api/energia'
import { ErrorPrevia, pedirPrevia, type ResultadoPrevia } from './api/previa'

// La previsualización se consulta cada segundo: es una guía de colocación y el
// profesor la mira mientras el alumno se acomoda. Un segundo es el mismo ritmo
// que el progreso del registro (useRegistro).
const CONSULTA_MS = 1000

export interface EstadoPrevia {
  /** Lo que respondió la placa en la última consulta; null mientras no llegue nada. */
  resultado: ResultadoPrevia | null
  /** URL de datos del cuadro recibido; null si no hay cuadro. */
  imagenUrl: string | null
  /**
   * Número del cuadro: uno por cuadro recibido, contando desde 1. Identifica
   * al cuadro, no su contenido: la URL de datos es el cuadro en base64, así que
   * dos cuadros con los mismos bytes (una escena quieta) tienen la misma URL.
   */
  numeroCuadro: number
  /** Vuelve a consultar ya y retoma el sondeo; abre otra vez el diálogo de credenciales si hace falta. */
  reintentar: () => void
}

/**
 * Sondea GET /api/camara/previa cada segundo, con el mismo patrón que useLecturas
 * y useAlumnos: la siguiente consulta se agenda al terminar la anterior, se pausa
 * con la pestaña oculta (y consulta de inmediato al volver) y se detiene con un 401
 * (reintentar solo reabriría el diálogo de credenciales del navegador).
 *
 * La imagen no se guarda: cada cuadro recibido sustituye al anterior en el
 * estado y, si la respuesta no trae cuadro, la imagen se suelta.
 * Su URL es de datos (`data:`), no de objeto (`blob:`), porque la CSP de `/app/`
 * (`img-src 'self' data:`) no admite el esquema `blob:`; por eso no hay ninguna
 * URL que revocar. Al desmontar la vista se aborta la petición en vuelo.
 */
export function usePrevia(): EstadoPrevia {
  const [resultado, setResultado] = useState<ResultadoPrevia | null>(null)
  const [imagenUrl, setImagenUrl] = useState<string | null>(null)
  // Contador de cuadros recibidos. El panel lo usa para distinguir un cuadro
  // de otro (y no por su contenido, que con una escena quieta se repite) y
  // para remontar la imagen en cada cuadro nuevo.
  const [numeroCuadro, setNumeroCuadro] = useState(0)
  const [intento, setIntento] = useState(0)
  // Vale mientras ninguna consulta de este montaje haya salido bien: la primera
  // espera al diálogo de credenciales del navegador y necesita más margen.
  const primeraPendiente = useRef(true)
  const reintentar = useCallback(() => setIntento((n) => n + 1), [])

  useEffect(() => {
    const cancelacion = new AbortController()
    let temporizador: number | undefined
    let enCurso = false
    let sinSesion = false

    async function consultar() {
      enCurso = true
      sinSesion = false
      try {
        const { resultado: nuevo, imagenUrl } = await pedirPrevia(
          cancelacion.signal,
          primeraPendiente.current ? TIEMPO_INICIAL_MS : undefined,
        )
        if (cancelacion.signal.aborted) {
          return
        }
        // Hasta que el borde conteste sin pedir credenciales, la consulta espera
        // al diálogo del navegador: un 401 lo deja pendiente para el reintento.
        if (nuevo.tipo !== 'sin-sesion') {
          primeraPendiente.current = false
        }
        sinSesion = nuevo.tipo === 'sin-sesion'
        // Cada cuadro sustituye al anterior en el estado: si la respuesta no
        // trae cuadro, se limpia la imagen, para que los bytes del rostro de un
        // alumno no queden vivos en el estado sin ningún uso.
        setImagenUrl(imagenUrl)
        // Cada cuadro recibido lleva su número, aunque tenga los mismos bytes
        // que el anterior: con la escena quieta, la URL de datos se repite y
        // solo el número distingue un cuadro de otro.
        if (imagenUrl !== null) {
          setNumeroCuadro((n) => n + 1)
        }
        setResultado(nuevo)
      } catch (error) {
        if (cancelacion.signal.aborted) {
          return
        }
        const mensaje = error instanceof ErrorPrevia ? error.message : 'Ocurrió un error inesperado al consultar la API.'
        // Un fallo tampoco es un cuadro: la imagen anterior se suelta.
        setImagenUrl(null)
        setResultado({ tipo: 'error', mensaje })
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
      }, CONSULTA_MS)
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
  }, [intento])

  return useMemo(() => ({ resultado, imagenUrl, numeroCuadro, reintentar }), [resultado, imagenUrl, numeroCuadro, reintentar])
}
