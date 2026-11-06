import { createContext, useContext, useEffect, useMemo, useState, type ReactNode } from 'react'
import { useEstadoLecturas } from './lecturasContexto'
import { antiguedadUltimaLectura, LECTURA_RECIENTE_MS } from './useLecturas'

export type EstadoPlaca = 'conectando' | 'vivo' | 'obsoleto' | 'sin-conexion'

export interface InfoPlaca {
  tipo: EstadoPlaca
  /** Antigüedad (ms) de la última lectura, o null si aún no hay lecturas. */
  edadMs: number | null
}

const PlacaContexto = createContext<InfoPlaca | null>(null)

/**
 * Única fuente del estado de la placa: la cabecera (indicador) y el aviso de
 * lecturas viejas de la vista de métricas leen de aquí, así el umbral de 60 s
 * y el instante en que se cruza son los mismos. Un solo tick de 1 s alimenta
 * la cuenta; solo se vuelven a pintar los consumidores del contexto (el
 * indicador y el aviso), no las vistas, porque `children` no cambia con el tick.
 */
export function PlacaProveedor({ children }: { children: ReactNode }) {
  const lecturas = useEstadoLecturas()
  const [ahora, setAhora] = useState(() => performance.now())

  useEffect(() => {
    const id = window.setInterval(() => setAhora(performance.now()), 1000)
    return () => window.clearInterval(id)
  }, [])

  const edadMs = antiguedadUltimaLectura(lecturas, ahora)
  let tipo: EstadoPlaca
  if (lecturas.error !== null) {
    tipo = 'sin-conexion'
  } else if (lecturas.lecturas === null) {
    tipo = 'conectando'
  } else if (edadMs !== null && edadMs < LECTURA_RECIENTE_MS) {
    tipo = 'vivo'
  } else {
    tipo = 'obsoleto'
  }

  const info = useMemo<InfoPlaca>(() => ({ tipo, edadMs }), [tipo, edadMs])
  return <PlacaContexto.Provider value={info}>{children}</PlacaContexto.Provider>
}

export function useEstadoPlaca(): InfoPlaca {
  const info = useContext(PlacaContexto)
  if (info === null) {
    throw new Error('useEstadoPlaca se usó fuera de PlacaProveedor')
  }
  return info
}
