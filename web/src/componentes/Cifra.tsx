interface Props {
  etiqueta: string
  valor: string
  /** Vacía si la cifra no lleva unidad (un conteo). */
  unidad?: string
  /** La cifra protagonista de la vista: estilo hero. */
  destacada?: boolean
}

/** Una cifra con su unidad en la línea base; es un par término/descripción de una lista. */
export function Cifra({ etiqueta, valor, unidad, destacada = false }: Props) {
  return (
    <div className={`cifra${destacada ? ' cifra--destacada' : ''}`}>
      <dt className="cifra__etiqueta">{etiqueta}</dt>
      <dd className="cifra__valor">
        <span className="cifra__numero">{valor}</span>
        {unidad !== undefined && <span className="cifra__unidad">{unidad}</span>}
      </dd>
    </div>
  )
}
