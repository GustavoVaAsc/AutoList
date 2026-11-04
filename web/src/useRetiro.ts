import { useCallback, useRef, useState } from 'react'
import { toast } from 'sonner'
import { retirarRostro } from './api/registro'
import type { Alumno } from './api/alumnosRespuestas'

export interface EstadoRetiro {
  /** El alumno cuyo rostro se va a retirar (el diálogo de confirmación está abierto); null si no hay. */
  porRetirar: Alumno | null
  /** Hay un retiro esperando respuesta: los botones del diálogo no hacen nada. */
  retirando: boolean
  /** Abre la confirmación. */
  pedir: (alumno: Alumno) => void
  /** Cierra la confirmación sin retirar nada. */
  cancelar: () => void
  /** Retira el rostro del alumno de la confirmación. */
  confirmar: () => void
}

/**
 * Retiro del rostro de un alumno desde Alumnos (DELETE /api/alumnos/:cuenta/rostro).
 * Es destructivo (borra datos biométricos), así que primero pide confirmación. El
 * resultado sale en un toast: Sonner vive en `App`, así que sobrevive a que la persona
 * cambie de vista mientras responde la API. Al terminar, con o sin éxito, recarga la
 * lista, que es la fuente de lo que quedó, y SOLO cuando la lista nueva está pintada avisa
 * con `alTerminar(cuenta)` para que la vista devuelva el foco a la acción de esa fila (la
 * fila cambia de botón al retirarse). Si la recarga falla o la vista se desmonta, no avisa:
 * el diálogo ya devolvió el foco al botón que lo abrió.
 */
export function useRetiro(recargar: () => Promise<void>, alTerminar: (cuenta: string) => void): EstadoRetiro {
  const [porRetirar, setPorRetirar] = useState<Alumno | null>(null)
  const [retirando, setRetirando] = useState(false)
  // Un solo retiro a la vez, decidido con una ref y no con el estado de React.
  const enCurso = useRef(false)

  const pedir = useCallback((alumno: Alumno) => {
    if (!enCurso.current) {
      setPorRetirar(alumno)
    }
  }, [])

  const cancelar = useCallback(() => {
    if (!enCurso.current) {
      setPorRetirar(null)
    }
  }, [])

  const confirmar = useCallback(() => {
    const alumno = porRetirar
    if (alumno === null || enCurso.current) {
      return
    }
    enCurso.current = true
    setRetirando(true)
    void retirarRostro(alumno.cuenta).then((resultado) => {
      enCurso.current = false
      setRetirando(false)
      setPorRetirar(null)
      switch (resultado.tipo) {
        case 'retirado':
          if (resultado.habiaRostro) {
            toast.success('Rostro retirado', { description: `${alumno.nombre} quedó pendiente de rostro.` })
          } else {
            toast('Sin cambios', { description: `${alumno.nombre} ya estaba pendiente de rostro.` })
          }
          break
        case 'no-existe':
          toast.warning('El alumno ya no está en la lista', { description: 'No se cambió nada.' })
          break
        case 'registro-abierto':
          toast.warning('Hay un registro abierto de este alumno', { description: 'Cancélalo en Registro antes de retirar su rostro.', duration: 8000 })
          break
        case 'fallo':
          toast.error('No se pudo retirar el rostro', { description: resultado.mensaje, duration: 8000 })
          break
      }
      recargar().then(
        () => alTerminar(alumno.cuenta),
        () => {},
      )
    })
  }, [porRetirar, recargar, alTerminar])

  return { porRetirar, retirando, pedir, cancelar, confirmar }
}
