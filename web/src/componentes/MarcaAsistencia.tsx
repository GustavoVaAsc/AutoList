export type TipoMarca = 'presente' | 'pendiente' | 'falta'

interface Props {
  tipo: TipoMarca
}

/**
 * La marca de asistencia: una casilla de papel y lo que el profesor pone en
 * ella (web/DESIGN.md, "El motivo"). Presente es una palomita de pluma que
 * sale de la casilla, pendiente es la casilla vacía y falta es una tachita
 * roja. Está dibujada a mano, con trazos algo irregulares, para que se lea
 * como bolígrafo y no como un icono de biblioteca. El trazo lleva
 * `pathLength="1"`: así `stroke-dashoffset` lo recorre entero sin medir
 * (estilos.css, "Marca de asistencia"). Es decorativa: siempre va junto a un
 * texto que dice el estado, y por eso se oculta a los lectores de pantalla.
 */
export function MarcaAsistencia({ tipo }: Props) {
  return (
    <svg className={`marca-asistencia marca-asistencia--${tipo}`} viewBox="0 0 24 24" aria-hidden="true" focusable="false">
      <path className="marca-asistencia__caja" d="M3.8 5.6 L18.6 4.9 L19.4 19.3 L4.7 20.1 Z" />
      {tipo === 'presente' && (
        <path
          className="marca-asistencia__trazo"
          pathLength="1"
          d="M6.4 12.3 C8.1 14 9.5 16 10.7 18.9 C12.9 12.3 17.1 6.2 22.4 2.3"
        />
      )}
      {tipo === 'falta' && (
        <>
          <path className="marca-asistencia__trazo" pathLength="1" d="M7.6 8.6 L16.3 16.9" />
          <path className="marca-asistencia__trazo marca-asistencia__trazo--segundo" pathLength="1" d="M16.6 8 L7.7 17.2" />
        </>
      )}
    </svg>
  )
}
