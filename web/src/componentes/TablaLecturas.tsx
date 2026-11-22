import { useState } from 'react'
import type { Lectura } from '../api/energia'
import { formatearCorriente, formatearDia, formatearHora, formatearPotencia, formatearVoltaje } from '../formato'
import { cruzaDias } from '../serie'

interface Props {
  /** De la más vieja a la más nueva. */
  lecturas: Lectura[]
  /** Nombre accesible de la tabla (y de su botón "Ver todas"). */
  nombre: string
}

const FILAS_INICIALES = 10

/** Lecturas recientes, de la más nueva a la más vieja, como renglones de una bitácora. También es la alternativa accesible a la gráfica. */
export function TablaLecturas({ lecturas, nombre }: Props) {
  const [todas, setTodas] = useState(false)
  const recientes = [...lecturas].reverse()
  // Clave estable: `t` más el número de repetición, por si dos lecturas comparten milisegundo.
  const vistos = new Map<number, number>()
  const filas = recientes.map((l) => {
    const n = vistos.get(l.t) ?? 0
    vistos.set(l.t, n + 1)
    return { l, clave: `${l.t}-${n}` }
  })
  const visibles = todas ? filas : filas.slice(0, FILAS_INICIALES)
  const hayMas = recientes.length > FILAS_INICIALES
  // Si las lecturas abarcan varios días, la hora sola es ambigua: se añade el día.
  const conDia = cruzaDias(lecturas)

  return (
    <>
      <div className="hoja__desplazable">
        <table className="tabla tabla--lecturas" aria-label={nombre}>
          <thead>
            <tr>
              <th scope="col">Hora</th>
              <th scope="col" className="numerico">Potencia (W)</th>
              <th scope="col" className="numerico">Voltaje (V)</th>
              <th scope="col" className="numerico">Corriente (A)</th>
            </tr>
          </thead>
          <tbody>
            {visibles.map(({ l, clave }) => (
              <tr key={clave}>
                <th scope="row" className="tabla__cuenta">
                  <time dateTime={new Date(l.t).toISOString()}>
                    {conDia && <span className="tabla__dia">{formatearDia(l.t)}</span>}
                    {formatearHora(l.t)}
                  </time>
                </th>
                <td className="numerico">{formatearPotencia(l.p)}</td>
                <td className="numerico">{formatearVoltaje(l.v)}</td>
                <td className="numerico">{formatearCorriente(l.i)}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
      {hayMas && (
        <div className="hoja__pie">
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
