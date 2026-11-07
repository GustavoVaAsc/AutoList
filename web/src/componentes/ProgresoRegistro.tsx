import { useEffect, useRef, useState } from 'react'
import { ETIQUETA_POSE, marcaDePose, textoPose, type PoseRegistro } from '../api/registroRespuestas'
import { MarcaAsistencia } from './MarcaAsistencia'

interface Props {
  poses: PoseRegistro[]
  /** Id del encabezado que titula la tabla. */
  idTitulo: string
}

const RESALTE_POSE_MS = 1300

/**
 * Las cinco poses como renglones de la hoja: número, lo que se le pide al alumno,
 * su marca de asistencia (palomita si la foto se aceptó, tachita si se rechazó, casilla
 * vacía en los demás casos) y el estado en palabras. Una foto rechazada lleva debajo el
 * motivo, traducido. Cuando una pose pasa a `aceptada` en vivo, la fila resalta y la
 * palomita se traza con tinta de pluma. Todo sale de lo que informó la visión por
 * Node-RED: sin fotos ni miniaturas (el contrato no define cómo llegarían al navegador).
 */
export function ProgresoRegistro({ poses, idTitulo }: Props) {
  const previos = useRef<Map<string, PoseRegistro['estado']> | null>(null)
  const [recienAceptadas, setRecienAceptadas] = useState<ReadonlySet<string>>(new Set())

  useEffect(() => {
    if (previos.current === null) {
      const inicial = new Map<string, PoseRegistro['estado']>()
      for (const p of poses) inicial.set(p.pose, p.estado)
      previos.current = inicial
      return
    }
    const nuevas: string[] = []
    for (const p of poses) {
      const antes = previos.current.get(p.pose)
      if (antes !== undefined && antes !== 'aceptada' && p.estado === 'aceptada') {
        nuevas.push(p.pose)
      }
      previos.current.set(p.pose, p.estado)
    }
    if (nuevas.length === 0) return
    setRecienAceptadas((actuales) => {
      const siguiente = new Set(actuales)
      for (const pose of nuevas) siguiente.add(pose)
      return siguiente
    })
    const id = window.setTimeout(() => {
      setRecienAceptadas((actuales) => {
        const siguiente = new Set(actuales)
        for (const pose of nuevas) siguiente.delete(pose)
        return siguiente
      })
    }, RESALTE_POSE_MS)
    return () => window.clearTimeout(id)
  }, [poses])

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
            <tr
              key={p.pose}
              className="tabla__fila"
              data-recien-aceptada={recienAceptadas.has(p.pose) ? '' : undefined}
            >
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
