import { ETIQUETA_POSE, marcaDePose, textoPose, type PoseRegistro } from '../api/registroRespuestas'
import { MarcaAsistencia } from './MarcaAsistencia'

interface Props {
  poses: PoseRegistro[]
  /** Id del encabezado que titula la tabla. */
  idTitulo: string
}

/**
 * Las cinco poses como renglones de la hoja: número, lo que se le pide al alumno,
 * su marca de asistencia (palomita si la foto se aceptó, tachita si se rechazó, casilla
 * vacía en los demás casos) y el estado en palabras. Una foto rechazada lleva debajo el
 * motivo, traducido. Todo sale de lo que informó la visión por Node-RED: sin fotos ni
 * miniaturas (el contrato no define cómo llegarían al navegador).
 */
export function ProgresoRegistro({ poses, idTitulo }: Props) {
  return (
    <table className="tabla tabla--poses" aria-labelledby={idTitulo}>
      <thead>
        <tr>
          <th scope="col" className="tabla__n" aria-hidden="true">
            N.º
          </th>
          <th scope="col">Pose</th>
          <th scope="col">Estado</th>
        </tr>
      </thead>
      <tbody>
        {poses.map((p, posicion) => {
          const { estado, detalle } = textoPose(p)
          return (
            <tr key={p.pose} className="tabla__fila">
              <td className="tabla__n" aria-hidden="true">
                {posicion + 1}
              </td>
              <th scope="row" className="pose__nombre">
                {ETIQUETA_POSE[p.pose]}
                {detalle !== null && <span className="pose__detalle">{detalle}</span>}
              </th>
              <td>
                <span className={`rostro rostro--pose-${p.estado}`}>
                  <MarcaAsistencia tipo={marcaDePose(p.estado)} />
                  {estado}
                </span>
              </td>
            </tr>
          )
        })}
      </tbody>
    </table>
  )
}
