import { useMemo } from 'react'
import { resumirLatencias, type MedicionLatencia } from '../api/latenciaRespuestas'
import { formatearFechaYHora, formatearMilisegundos } from '../formato'
import { Cifra } from './Cifra'
import { TablaLatencias } from './TablaLatencias'

interface Props {
  /** De la más nueva a la más vieja; no puede estar vacía. */
  mediciones: MedicionLatencia[]
}

interface RenglonProps {
  etiqueta: string
  valor: number | null
  /** Qué decir cuando no hay medición de esa etapa. */
  sinValor: string
}

/** Un renglón de cifras; una etapa sin medición lo dice con palabras y no con un cero. */
function Renglon({ etiqueta, valor, sinValor }: RenglonProps) {
  if (valor === null) {
    return (
      <div className="cifra">
        <dt className="cifra__etiqueta">{etiqueta}</dt>
        <dd className="cifra__valor">
          <span className="cifra__numero">
            <span className="tabla__vacio">{sinValor}</span>
          </span>
        </dd>
      </div>
    )
  }
  return <Cifra etiqueta={etiqueta} valor={valor} formato={formatearMilisegundos} unidad="ms" />
}

/**
 * La latencia de inferencia de la NPU: la última medición con su desglose
 * (captura, detección, embedding) y las mediciones recientes. Mismas piezas
 * que la hoja de intentos de Asistencia: cifras con puntos guía a la izquierda
 * y una tabla a la derecha (web/DESIGN.md, Composición).
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
        <p className="etiqueta-tecnica hora-lectura">
          Última medición: {formatearFechaYHora(ultima.t)} (hora de Ciudad de México)
        </p>
        <dl className="cifras cifras--renglones">
          <Renglon etiqueta="Total de la última medición" valor={ultima.total} sinValor="no se midió" />
          <Renglon etiqueta="Captura" valor={ultima.cap} sinValor="no corrió" />
          <Renglon etiqueta="Detección" valor={ultima.det} sinValor="no corrió" />
          <Renglon etiqueta="Embedding" valor={ultima.emb} sinValor="no corrió" />
        </dl>
        {resumen.mediana !== null && resumen.maxima !== null && (
          <dl className="cifras cifras--renglones">
            <Cifra
              etiqueta={`Mediana del total en ${resumen.conTotal} mediciones`}
              valor={resumen.mediana}
              formato={formatearMilisegundos}
              unidad="ms"
            />
            <Cifra
              etiqueta={`Máximo del total en ${resumen.conTotal} mediciones`}
              valor={resumen.maxima}
              formato={formatearMilisegundos}
              unidad="ms"
            />
          </dl>
        )}
        <p className="etiqueta-tecnica hora-lectura">
          El total es el ciclo completo del reconocimiento, captura incluida. Una etapa que no corrió
          (por ejemplo, la detección cuando la imagen no se pudo usar) no cuenta como cero.
        </p>
      </div>

      <div className="intentos__detalle">
        <h3 id="titulo-tabla-latencias" className="hoja__subtitulo">
          Mediciones recientes
        </h3>
        <TablaLatencias mediciones={mediciones} idTitulo="titulo-tabla-latencias" />
      </div>
    </section>
  )
}
