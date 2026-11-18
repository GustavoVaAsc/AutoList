import { useEffect, useState } from 'react'
import { antiguedadMs, filasSistema, type Conexion, type MetricasSistema, type Recibido, type Sistema } from '../api/sistemaRespuestas'
import { formatearAntiguedad, formatearPorcentaje, formatearTemperatura } from '../formato'
import { Cifra } from './Cifra'
import { MarcaAsistencia, type TipoMarca } from './MarcaAsistencia'

interface Props {
  sistema: Sistema
  /** Hora (ms) de la última respuesta de la API, según el reloj de la placa (cabecera Date). */
  horaServidor: number
  /** performance.now() al recibir esa respuesta. */
  recibidoEn: number
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

/** Un renglón de cifras; sin medición lo dice con palabras y no con un cero. */
function Renglon({ etiqueta, datos, valor, formato, unidad }: RenglonProps) {
  if (datos === null) {
    return (
      <div className="cifra">
        <dt className="cifra__etiqueta">{etiqueta}</dt>
        <dd className="cifra__valor">
          <span className="cifra__numero">
            <span className="tabla__vacio">sin datos</span>
          </span>
        </dd>
      </div>
    )
  }
  return <Cifra etiqueta={etiqueta} valor={valor(datos.datos)} formato={formato} unidad={unidad} />
}

/** Un tick por segundo para que el "hace N s" siga contando entre consultas; solo repinta esta hoja. */
function useAhora(): number {
  const [ahora, setAhora] = useState(() => performance.now())
  useEffect(() => {
    const id = window.setInterval(() => setAhora(performance.now()), 1000)
    return () => window.clearInterval(id)
  }, [])
  return ahora
}

/**
 * El sistema: cómo están el servicio de visión, la cámara (XIAO) y los actuadores, y la temperatura, la CPU y la
 * RAM de la placa. Mismas piezas que la hoja de latencia: cifras con puntos guía a la izquierda y una tabla a la
 * derecha (web/DESIGN.md, Composición). Cada estado lleva la marca de asistencia y la palabra que dice lo mismo.
 * "Hace N s" cuenta desde que Node-RED recibió el mensaje, con la hora de la placa.
 */
export function HojaSistema({ sistema, horaServidor, recibidoEn }: Props) {
  const ahora = useAhora()
  const filas = filasSistema(sistema)
  const hace = (t: number | null) =>
    t === null ? null : `hace ${formatearAntiguedad(antiguedadMs(t, horaServidor, recibidoEn, ahora))}`
  const metricas = sistema.metricas

  return (
    <section className="hoja hoja--intentos" aria-labelledby="titulo-sistema">
      <div className="intentos__resumen">
        <h2 id="titulo-sistema" className="hoja__subtitulo">
          Sistema
        </h2>
        <p className="etiqueta-tecnica hora-lectura">
          {metricas !== null
            ? `Placa medida ${hace(metricas.t)}`
            : 'Node-RED no ha medido la placa desde que arrancó.'}
        </p>
        <dl className="cifras cifras--renglones">
          <Renglon etiqueta="Temperatura del SoC" datos={metricas} valor={(m) => m.temp} formato={formatearTemperatura} unidad="°C" />
          <Renglon etiqueta="CPU" datos={metricas} valor={(m) => m.cpu} formato={formatearPorcentaje} unidad="%" />
          <Renglon etiqueta="RAM" datos={metricas} valor={(m) => m.ram} formato={formatearPorcentaje} unidad="%" />
        </dl>
        <p className="etiqueta-tecnica hora-lectura">
          La temperatura es la de la zona más caliente del SoC; la CPU, el promedio de los últimos 10 s.
        </p>
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
                <th scope="col">Último mensaje</th>
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
                  <td>{f.detalle.length > 0 ? f.detalle.join(' · ') : <span className="tabla__vacio">sin detalle</span>}</td>
                  <td>{f.t === null ? <span className="tabla__vacio">nunca</span> : hace(f.t)}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
        <p className="etiqueta-tecnica hora-lectura">
          La hora es la de Node-RED al recibir el mensaje; los estados retenidos también se reciben al reconectar con el
          broker. La visión publica solo cuando cambia de modo y la XIAO, cada 30 s. Una caída se ve cuando el broker publica
          el aviso de desconexión de la visión o de la XIAO; los actuadores comparten la conexión de la XIAO.
        </p>
      </div>
    </section>
  )
}
