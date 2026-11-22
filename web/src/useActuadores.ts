import { useCallback, useEffect, useRef, useState } from 'react'
import { toast } from 'sonner'
import { enviarOrden } from './api/actuadores'
import { ENFRIAMIENTO_MS, avisoOrdenEnviada, type Orden } from './api/actuadoresRespuestas'

const AVISO_DEMASIADO_RAPIDO = { titulo: 'Demasiado rápido', descripcion: 'La orden no salió. Espera un segundo.' }

/** Lo que tarda la XIAO en publicar su estado nuevo y Node-RED en guardarlo, antes de volver a consultarlo. */
const ESPERA_ESTADO_MS = 600

export interface ControlActuadores {
  /** Una orden está en camino o el panel se enfría: los botones no hacen nada (aria-disabled). */
  ocupado: boolean
  /** La orden que espera respuesta (para decirlo en su botón); null si no hay. */
  enviando: string | null
  /** Manda la orden; devuelve false (y avisa) si no la mandó porque otra va en camino o el panel se enfría. */
  enviar: (orden: Orden) => boolean
}

/**
 * Órdenes manuales a los actuadores (POST /api/actuadores). Una a la vez, decidido con una ref y no con el
 * estado de React, y después de cada una el panel espera ENFRIAMIENTO_MS (el límite de la API es una por
 * segundo). El resultado sale en un toast: Sonner vive en `App`, así que sobrevive a que la persona cambie de
 * vista mientras responde la API. Una orden que salió pide `reconsultar` poco después, para que el estado
 * nuevo se vea sin esperar el sondeo.
 */
export function useActuadores(reconsultar: () => void): ControlActuadores {
  const [ocupado, setOcupado] = useState(false)
  const [enviando, setEnviando] = useState<string | null>(null)
  const enCurso = useRef(false)
  const temporizadores = useRef<number[]>([])
  const reconsultarActual = useRef(reconsultar)
  reconsultarActual.current = reconsultar

  useEffect(
    () => () => {
      for (const id of temporizadores.current) {
        window.clearTimeout(id)
      }
    },
    [],
  )

  const enviar = useCallback((orden: Orden): boolean => {
    if (enCurso.current) {
      toast.warning(AVISO_DEMASIADO_RAPIDO.titulo, { description: AVISO_DEMASIADO_RAPIDO.descripcion })
      return false
    }
    enCurso.current = true
    setOcupado(true)
    setEnviando(orden.id)
    void enviarOrden(orden.cuerpo).then((resultado) => {
      setEnviando(null)
      switch (resultado.tipo) {
        case 'enviada':
          toast.success(avisoOrdenEnviada(orden))
          temporizadores.current.push(window.setTimeout(() => reconsultarActual.current(), ESPERA_ESTADO_MS))
          break
        case 'demasiado-rapido':
          toast.warning(AVISO_DEMASIADO_RAPIDO.titulo, { description: AVISO_DEMASIADO_RAPIDO.descripcion })
          break
        case 'fallo':
          toast.error('No se pudo enviar la orden', { description: resultado.mensaje, duration: 8000 })
          break
      }
      temporizadores.current.push(
        window.setTimeout(() => {
          enCurso.current = false
          setOcupado(false)
        }, ENFRIAMIENTO_MS),
      )
    })
    return true
  }, [])

  return { ocupado, enviando, enviar }
}
