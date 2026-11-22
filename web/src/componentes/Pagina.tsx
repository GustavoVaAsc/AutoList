import type { ReactNode } from 'react'

interface Props {
  /** Título de la vista (el h1). Sin él, la vista pone su propio h1 (Alumnos lo lleva en el encabezado de la hoja). */
  titulo?: string
  children: ReactNode
}

/**
 * Estructura de cada vista: cabecera (el h1, si se pide) y bloques
 * debajo. Sus hijos directos entran escalonados (estilos.css, "Movimiento de
 * las vistas"), así que cada bloque de la vista debe ser un hijo directo de aquí.
 */
export function Pagina({ titulo, children }: Props) {
  return (
    <div className="contenedor pagina">
      {titulo !== undefined && (
        <header className="pagina__cabecera">
          <h1 className="titulo-vista" tabIndex={-1}>{titulo}</h1>
        </header>
      )}
      {children}
    </div>
  )
}
