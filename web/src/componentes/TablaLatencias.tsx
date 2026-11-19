import { useState } from 'react'
import type { MedicionLatencia } from '../api/latenciaRespuestas'
import { diaDe, formatearDia, formatearHora, formatearMilisegundos } from '../formato'
import { Vacio } from './Vacio'

interface Props {
  /** De la más nueva a la más vieja. */
  mediciones: MedicionLatencia[]
  /** Nombre accesible de la tabla (y de su botón "Ver todas"). */
  nombre: string
}

const FILAS_INICIALES = 10

/** Una etapa que no corrió lleva un guion: un hueco o un cero parecerían una medición. */
function Etapa({ valor }: { valor: number | null }) {
  return valor === null ? <Vacio /> : <>{formatearMilisegundos(valor)}</>
}

/**
 * Mediciones recientes, de la más nueva a la más vieja, como renglones de una bitácora.
 * Va en la columna de detalle de `hoja--intentos` (por eso `tabla--intentos`: sin relleno en los bordes).
 */
export function TablaLatencias({ mediciones, nombre }: Props) {
  const [todas, setTodas] = useState(false)
  // Clave estable: `t` más el número de repetición, por si dos mediciones comparten milisegundo.
  const vistos = new Map<number, number>()
  const filas = mediciones.map((m) => {
    const n = vistos.get(m.t) ?? 0
    vistos.set(m.t, n + 1)
    return { m, clave: `${m.t}-${n}` }
  })
  const visibles = todas ? filas : filas.slice(0, FILAS_INICIALES)
  const hayMas = mediciones.length > FILAS_INICIALES
  // Si las mediciones abarcan varios días, la hora sola es ambigua: se añade el día.
  const primera = mediciones[0]
  const ultima = mediciones[mediciones.length - 1]
  const conDia = primera !== undefined && ultima !== undefined && diaDe(primera.t) !== diaDe(ultima.t)

  return (
    <>
      <div className="hoja__desplazable">
        <table className="tabla tabla--intentos tabla--latencias" aria-label={nombre}>
          <thead>
            <tr>
              <th scope="col">Hora</th>
              <th scope="col" className="numerico">Captura (ms)</th>
              <th scope="col" className="numerico">Detección (ms)</th>
              <th scope="col" className="numerico">Embedding (ms)</th>
              <th scope="col" className="numerico">Total (ms)</th>
            </tr>
          </thead>
          <tbody>
            {visibles.map(({ m, clave }) => (
              <tr key={clave}>
                <th scope="row" className="tabla__cuenta">
                  <time dateTime={new Date(m.t).toISOString()}>
                    {conDia && <span className="tabla__dia">{formatearDia(m.t)}</span>}
                    {formatearHora(m.t)}
                  </time>
                </th>
                <td className="numerico"><Etapa valor={m.cap} /></td>
                <td className="numerico"><Etapa valor={m.det} /></td>
                <td className="numerico"><Etapa valor={m.emb} /></td>
                <td className="numerico"><Etapa valor={m.total} /></td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
      {hayMas && (
        <div>
          <button
            type="button"
            className="boton"
            aria-label={`${todas ? 'Ver menos' : 'Ver todas'}: ${nombre}`}
            aria-expanded={todas}
            onClick={() => setTodas(!todas)}>
            {todas ? 'Ver menos' : 'Ver todas'}
          </button>
        </div>
      )}
    </>
  )
}
