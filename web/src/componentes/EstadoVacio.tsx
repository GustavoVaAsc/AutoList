import type { ReactNode } from 'react'
import { MarcaAsistencia } from './MarcaAsistencia'

interface Props {
  titulo: string
  children: ReactNode
  /** La siguiente acción, si hay una (un botón o un enlace). */
  accion?: ReactNode
  /** Va dentro de otra hoja (la lista de Alumnos): no se dibuja como hoja propia. */
  anidado?: boolean
  /** Casilla grande: para la vista que no tiene otra cosa que mostrar (Registro). */
  grande?: boolean
}

/**
 * Para cuando no hay nada que mostrar: una casilla vacía, la causa y, si
 * existe, la acción que sigue. La casilla es la marca de "pendiente" de la
 * hoja (web/DESIGN.md): todavía no hay nada marcado.
 */
export function EstadoVacio({ titulo, children, accion, anidado = false, grande = false }: Props) {
  return (
    <div className={`${anidado ? 'estado' : 'hoja estado'}${grande ? ' estado--grande' : ''}`}>
      <MarcaAsistencia tipo="pendiente" />
      <div className="estado__cuerpo">
        <h2 className="estado__titulo">{titulo}</h2>
        <p className="estado__texto">{children}</p>
        {accion}
      </div>
    </div>
  )
}
