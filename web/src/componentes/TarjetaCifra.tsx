import type { ReactNode } from 'react'

interface Props {
  etiqueta: string
  valor: string
  unidad: string
  /** La cifra que se grafica: más grande y con el acento. */
  destacada?: boolean
  /** Dato de apoyo bajo la cifra (solo si sale de los datos reales). */
  nota?: ReactNode
}

/** Una cifra con su unidad; es un par término/descripción de una lista. */
export function TarjetaCifra({ etiqueta, valor, unidad, destacada = false, nota }: Props) {
  return (
    <div className={`tarjeta cifra${destacada ? ' cifra--destacada' : ''}`}>
      <dt className="cifra__etiqueta">{etiqueta}</dt>
      <dd className="cifra__valor">
        <span className="cifra__numero">{valor}</span>
        <span className="cifra__unidad">{unidad}</span>
      </dd>
      {nota !== undefined && <dd className="cifra__nota">{nota}</dd>}
    </div>
  )
}
