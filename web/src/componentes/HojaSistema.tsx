import { filasSistema, type Conexion, type MetricasSistema, type Recibido, type Sistema } from '../api/sistemaRespuestas'
import { formatearHoraODia, formatearPorcentaje, formatearTemperatura } from '../formato'
import { Cifra } from './Cifra'
import { MarcaAsistencia, type TipoMarca } from './MarcaAsistencia'
import { Vacio } from './Vacio'

interface Props {
  sistema: Sistema
  /** Hora (ms) de la última respuesta de la API, según la placa: con ella se decide si una hora lleva también la fecha. */
  horaServidor: number
}

// La marca de la hoja: palomita (en línea), tachita roja (fuera de línea, el Will) y casilla vacía (sin datos).
const MARCA: Record<Conexion, TipoMarca> = {
  'en-linea': 'presente',
  'fuera-de-linea': 'falta',
  'sin-datos': 'pendiente',
}
// Colores del estado en palabras, los mismos de la columna del rostro de Alumnos: pluma si está bien, rojo si falta.
const CLASE: Record<Conexion, string> = {
  'en-linea': 'rostro--presente',
  'fuera-de-linea': 'rostro--falta',
  'sin-datos': 'rostro--pendiente',
}

interface RenglonProps {
  etiqueta: string
  datos: Recibido<MetricasSistema> | null
  valor: (m: MetricasSistema) => number
  formato: (n: number) => string
  unidad: string
}

/** Un renglón de cifras; sin medición lleva un guion y no un cero. */
function Renglon({ etiqueta, datos, valor, formato, unidad }: RenglonProps) {
  if (datos === null) {
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
  return <Cifra etiqueta={etiqueta} valor={valor(datos.datos)} formato={formato} unidad={unidad} />
}

/**
 * El sistema: cómo están el reconocimiento, el nodo del aula y los actuadores, y la temperatura, la CPU y la
 * RAM de la placa. Mismas piezas que la hoja de latencia: cifras con puntos guía a la izquierda y una tabla a la
 * derecha (web/DESIGN.md, Composición). Cada estado lleva la marca de asistencia y la palabra que dice lo mismo.
 * La hora de cada mensaje es la de la placa al recibirlo; lleva la fecha si no es de hoy.
 */
export function HojaSistema({ sistema, horaServidor }: Props) {
  const filas = filasSistema(sistema)
  const metricas = sistema.metricas

  return (
    <section className="hoja hoja--intentos" aria-labelledby="titulo-sistema">
      <div className="intentos__resumen">
        <h2 id="titulo-sistema" className="hoja__subtitulo">
          Sistema
        </h2>
        <p className="etiqueta-tecnica hora-lectura">
          {metricas !== null ? `Placa medida: ${formatearHoraODia(metricas.t, horaServidor)}` : 'Aún no hay medición de la placa.'}
        </p>
        <dl className="cifras cifras--renglones">
          <Renglon etiqueta="Temperatura" datos={metricas} valor={(m) => m.temp} formato={formatearTemperatura} unidad="°C" />
          <Renglon etiqueta="CPU" datos={metricas} valor={(m) => m.cpu} formato={formatearPorcentaje} unidad="%" />
          <Renglon etiqueta="RAM" datos={metricas} valor={(m) => m.ram} formato={formatearPorcentaje} unidad="%" />
        </dl>
      </div>

      <div className="intentos__detalle">
        <h3 id="titulo-tabla-sistema" className="hoja__subtitulo">
          Servicios y dispositivos
        </h3>
        <div className="hoja__desplazable">
          <table className="tabla tabla--intentos tabla--sistema" aria-labelledby="titulo-tabla-sistema">
            <thead>
              <tr>
                <th scope="col">Componente</th>
                <th scope="col">Estado</th>
                <th scope="col">Detalle</th>
                <th scope="col">Último dato</th>
              </tr>
            </thead>
            <tbody>
              {filas.map((f) => (
                <tr key={f.clave}>
                  <th scope="row">{f.nombre}</th>
                  <td>
                    <span className={`rostro ${CLASE[f.conexion]}`}>
                      <MarcaAsistencia tipo={MARCA[f.conexion]} />
                      {f.estado}
                    </span>
                  </td>
                  <td>{f.detalle.length > 0 ? f.detalle.join(' · ') : <Vacio />}</td>
                  <td>{f.t === null ? <Vacio /> : formatearHoraODia(f.t, horaServidor)}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </div>
    </section>
  )
}
