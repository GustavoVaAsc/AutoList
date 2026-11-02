import type { ReactNode } from 'react'

interface Props {
  titulo: string
  children: ReactNode
}

/** Tarjeta para cuando no hay nada que mostrar (sin datos, o vista pendiente). */
export function EstadoVacio({ titulo, children }: Props) {
  return (
    <div className="tarjeta estado">
      <p className="estado__titulo">{titulo}</p>
      <p className="estado__texto">{children}</p>
    </div>
  )
}
