interface Props {
  id: string
  /** Etiqueta visible del campo. */
  etiqueta: string
  valor: string
  alCambiar: (valor: string) => void
}

// Los nombres de la lista maestra miden hasta 120 caracteres; la búsqueda no necesita más.
const MAX_BUSQUEDA = 120

/**
 * Campo de búsqueda de las hojas de alumnos (Alumnos y Registro). Como el filtro de
 * grupo, es un espacio por llenar de la hoja: sin caja, sobre su línea de tinta
 * (web/DESIGN.md, "Composición"). El filtrado es del cliente (api/registroRespuestas.ts,
 * `filtrarAlumnos`); aquí solo se captura el texto. `type="search"` trae el botón nativo
 * para borrarlo y la tecla Escape que lo vacía.
 */
export function CampoBusqueda({ id, etiqueta, valor, alCambiar }: Props) {
  return (
    <div className="campo campo--busqueda">
      <label htmlFor={id} className="campo__etiqueta">
        {etiqueta}
      </label>
      <input
        id={id}
        type="search"
        className="entrada entrada--campo"
        value={valor}
        maxLength={MAX_BUSQUEDA}
        autoComplete="off"
        spellCheck={false}
        onChange={(e) => alCambiar(e.target.value)}
      />
    </div>
  )
}
