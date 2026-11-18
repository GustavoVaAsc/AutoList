import { createContext, useContext } from 'react'
import type { EstadoSistema } from './useSistema'

// El sondeo vive en App, no en la vista de métricas: la cabecera dice cómo están la visión y la cámara sin
// importar qué vista esté abierta (mismo patrón que lecturasContexto).
export const SistemaContexto = createContext<EstadoSistema | null>(null)

export function useEstadoSistema(): EstadoSistema {
  const estado = useContext(SistemaContexto)
  if (estado === null) {
    throw new Error('useEstadoSistema se usó fuera de SistemaContexto.Provider')
  }
  return estado
}
