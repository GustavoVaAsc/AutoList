import { useMemo } from 'react'
import { resumirLatencias, type MedicionLatencia } from '../api/latenciaRespuestas'
import { formatearFechaYHora, formatearMilisegundos } from '../formato'
import { Cifra } from './Cifra'
import { TablaLatencias } from './TablaLatencias'
import { Vacio } from './Vacio'

interface Props {
  /** De la más nueva a la más vieja; no puede estar vacía. */
  mediciones: MedicionLatencia[]
}

interface RenglonProps {
  etiqueta: string
  valor: number | null
}

/** Un renglón de cifras; una etapa sin medición lleva un guion y no un cero. */
function Renglon({ etiqueta, valor }: RenglonProps) {
  if (valor === null) {
    return (
      <div className="cifra">
        <dt className="cifra__etiqueta">{etiqueta}</dt>
        <dd className="cifra__valor">
          <span className="cifra__numero">
            <Vacio />
          </span>
        </dd>
      </div>
    )
  }
  return <Cifra etiqueta={etiqueta} valor={valor} formato={formatearMilisegundos} unidad="ms" />
}

/**
 * La latencia de inferencia: la última medición con su desglose (captura,
 * detección, embedding) y, plegadas, las mediciones recientes. Mismas piezas
 * que la hoja de Reconocimiento de Asistencia: cifras con puntos guía a la
 * izquierda y una tabla a la derecha (web/DESIGN.md, Composición).
 */
export function HojaLatencia({ mediciones }: Props) {
  const ultima = mediciones[0]!
  const resumen = useMemo(() => resumirLatencias(mediciones), [mediciones])

  return (
    <section className="hoja hoja--intentos" aria-labelledby="titulo-latencia">
      <div className="intentos__resumen">
        <h2 id="titulo-latencia" className="hoja__subtitulo">
          Latencia de inferencia
        </h2>
        <p className="etiqueta-tecnica hora-lectura">Última medición: {formatearFechaYHora(ultima.t)}</p>
        <dl className="cifras cifras--renglones">
          <Renglon etiqueta="Última" valor={ultima.total} />
          <Renglon etiqueta="Captura" valor={ultima.cap} />
          <Renglon etiqueta="Detección" valor={ultima.det} />
          <Renglon etiqueta="Embedding" valor={ultima.emb} />
        </dl>
        {resumen.mediana !== null && resumen.maxima !== null && (
          <dl className="cifras cifras--renglones">
            <Cifra
              etiqueta={`Mediana (${resumen.conTotal})`}
              valor={resumen.mediana}
              formato={formatearMilisegundos}
              unidad="ms"
            />
            <Cifra
              etiqueta={`Máximo (${resumen.conTotal})`}
              valor={resumen.maxima}
              formato={formatearMilisegundos}
              unidad="ms"
            />
          </dl>
        )}
      </div>

      <div className="intentos__detalle">
        <details className="plegable">
          <summary>Ver tabla</summary>
          <TablaLatencias mediciones={mediciones} nombre="Mediciones de latencia" />
        </details>
      </div>
    </section>
  )
}
