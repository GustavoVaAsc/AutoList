import { formatearAntiguedad } from '../formato'
import { useEstadoPlaca } from '../estadoPlaca'

/**
 * Aviso de lecturas viejas, con el mismo estado que la cabecera (estadoPlaca).
 * No es una región live: el indicador de la cabecera es el único que anuncia.
 */
export function AvisoObsoleta() {
  const { tipo, edadMs } = useEstadoPlaca()
  if (tipo !== 'obsoleto' || edadMs === null) {
    return null
  }
  return (
    <div className="aviso aviso--advertencia">
      <p className="aviso__titulo">Sin lecturas recientes</p>
      <p>
        La última llegó hace {formatearAntiguedad(edadMs)}. Revisa que el nodo del aula esté encendido.
      </p>
    </div>
  )
}
