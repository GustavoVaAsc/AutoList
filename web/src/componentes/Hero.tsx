import type { ReactNode } from 'react'

/** Sección de cabecera de cada vista: a todo el ancho, con el contenido centrado. */
export function Hero({ children }: { children: ReactNode }) {
  return (
    <div className="hero">
      <div className="contenedor">{children}</div>
    </div>
  )
}
