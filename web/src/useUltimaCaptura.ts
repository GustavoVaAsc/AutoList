import { useState } from 'react'
import { capturaVisible, seguirCaptura, SEGUIMIENTO_INICIAL, type Captura, type Registro, type SeguimientoCaptura } from './api/registroRespuestas'

/**
 * La última foto tomada del registro mostrado (aceptada o rechazada; solo con el registro abierto, ver `capturaVisible`). Se recalcula al
 * pintar con cada consulta nueva (patrón "ajustar el estado al cambiar la entrada" de
 * React: `seguirCaptura` devuelve el mismo objeto si nada cambió, así que no hay bucle).
 * Solo vive en memoria: al desmontarse la sección o cerrarse el registro se pierde.
 */
export function useUltimaCaptura(registro: Registro): Captura | null {
  const [seguimiento, setSeguimiento] = useState<SeguimientoCaptura>(SEGUIMIENTO_INICIAL)
  const siguiente = seguirCaptura(seguimiento, registro)
  if (siguiente !== seguimiento) {
    setSeguimiento(siguiente)
  }
  return capturaVisible(registro, siguiente.captura)
}
