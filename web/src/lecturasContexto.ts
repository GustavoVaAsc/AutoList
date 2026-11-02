import { createContext, useContext } from 'react'
import type { EstadoLecturas } from './useLecturas'

// El sondeo vive en App, no en la vista de métricas: el indicador de la cabecera
// necesita saber si la placa responde sin importar qué vista está abierta.
export const LecturasContexto = createContext<EstadoLecturas | null>(null)

export function useEstadoLecturas(): EstadoLecturas {
  const estado = useContext(LecturasContexto)
  if (estado === null) {
    throw new Error('useEstadoLecturas se usó fuera de LecturasContexto.Provider')
  }
  return estado
}
