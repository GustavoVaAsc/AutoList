import type { ReactNode } from 'react'

interface Props {
  /** Título de la vista (el h1). Sin él, la vista pone su propio h1 (Alumnos lo lleva en el encabezado de la hoja). */
  titulo?: string
  /** Una línea que dice qué hay en la vista. */
  descripcion?: string
  children: ReactNode
}

/**
 * Estructura de cada vista: cabecera (h1 y descripción, si se piden) y bloques
 * debajo. Sus hijos directos entran escalonados (estilos.css, "Movimiento de
 * las vistas"), así que cada bloque de la vista debe ser un hijo directo de aquí.
 */
export function Pagina({ titulo, descripcion, children }: Props) {
  return (
    <div className="contenedor pagina">
      {titulo !== undefined && (
        <header className="pagina__cabecera">
          <h1 className="titulo-vista" tabIndex={-1}>{titulo}</h1>
          {descripcion !== undefined && <p className="entradilla">{descripcion}</p>}
        </header>
      )}
      {children}
    </div>
  )
}
