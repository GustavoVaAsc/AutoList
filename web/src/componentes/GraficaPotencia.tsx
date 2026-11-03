import { CartesianGrid, ComposedChart, Line, ResponsiveContainer, Tooltip, XAxis, YAxis } from 'recharts'
import { formatearDiaYHoraCorta, formatearFechaYHora, formatearHora, formatearPotencia } from '../formato'
import { VENTANA_MS, type Serie } from '../serie'

interface Props {
  serie: Serie
}

// Colores de los tokens del tema (estilos.css): la gráfica sigue el tema Claro u Oscuro.
const COLOR_TEXTO = 'var(--suave)'
const COLOR_LINEA = 'var(--tinta)'

interface PropsTooltip {
  active?: boolean
  payload?: ReadonlyArray<{ value?: unknown; payload?: { t?: number } }>
}

/** Tooltip sobre una superficie elevada: la hora y la cifra en tipografía tabular. */
function TooltipPotencia({ active, payload }: PropsTooltip) {
  const punto = payload?.[0]
  const t = punto?.payload?.t
  if (!active || punto === undefined || typeof t !== 'number' || typeof punto.value !== 'number') {
    return null
  }
  return (
    <div className="tooltip">
      <p className="etiqueta-tecnica">{formatearFechaYHora(t)}</p>
      <p className="tooltip__valor">
        {formatearPotencia(punto.value)} <span className="etiqueta-tecnica">W</span>
      </p>
    </div>
  )
}

/**
 * Línea de la potencia (W) de la ventana visible, trazada en tinta y sin
 * relleno: una gráfica de papel, como la de una bitácora. Solo lleva las reglas
 * horizontales que dan la lectura de la escala (no hay cuadrícula de fondo en
 * la página). Los huecos largos cortan la línea. La tabla de lecturas tiene los
 * mismos datos.
 * Movimiento: la gráfica se revela una sola vez al montar (animación CSS del
 * contenedor, en estilos.css), de izquierda a derecha como un trazo. Los datos
 * que se refrescan cada 5 s no se animan: animarlos sería ruido sin información.
 */
export function GraficaPotencia({ serie }: Props) {
  const { puntos, visibles, omitidas, ultimaOmitida, cruzaDias } = serie
  const potencias = visibles.map((l) => l.p)
  const primera = visibles[0]!
  const ultima = visibles[visibles.length - 1]!
  const resumen =
    `${visibles.length} lecturas, de las ${formatearHora(primera.t)} a las ${formatearHora(ultima.t)}: ` +
    `potencia mínima ${formatearPotencia(Math.min(...potencias))} W, ` +
    `máxima ${formatearPotencia(Math.max(...potencias))} W.`

  return (
    <>
      <div className="grafica" role="img" aria-label="Gráfica de potencia; los datos están en la tabla" aria-describedby="resumen-grafica">
        <ResponsiveContainer width="100%" height="100%">
          <ComposedChart data={puntos} margin={{ top: 8, right: cruzaDias ? 44 : 32, bottom: 0, left: 0 }} accessibilityLayer={false}>
            <CartesianGrid stroke="var(--regla)" vertical={false} />
            <XAxis
              dataKey="t"
              type="number"
              scale="time"
              domain={['dataMin', 'dataMax']}
              tickFormatter={cruzaDias ? formatearDiaYHoraCorta : formatearHora}
              tick={{ fill: COLOR_TEXTO, fontSize: 13 }}
              stroke="var(--borde-control)"
              minTickGap={cruzaDias ? 72 : 48}
            />
            <YAxis
              domain={[0, 'auto']}
              tick={{ fill: COLOR_TEXTO, fontSize: 13 }}
              stroke="var(--borde-control)"
              width={40}
              tickFormatter={(valor: number) => String(Math.round(valor * 10) / 10)}
            />
            <Tooltip
              isAnimationActive={false}
              cursor={{ stroke: 'var(--borde-control)' }}
              content={(props) => <TooltipPotencia {...props} />}
            />
            <Line
              dataKey="p"
              name="Potencia"
              type="linear"
              stroke={COLOR_LINEA}
              strokeWidth={2}
              strokeLinecap="round"
              strokeLinejoin="round"
              dot={false}
              activeDot={{ r: 4, fill: COLOR_LINEA, stroke: 'var(--hoja)' }}
              connectNulls={false}
              isAnimationActive={false}
            />
            {/* Lecturas aisladas (sin vecinas cercanas): una línea no las dibujaría. */}
            <Line
              dataKey="suelta"
              stroke="none"
              dot={{ r: 3, fill: COLOR_LINEA, stroke: COLOR_LINEA }}
              activeDot={false}
              isAnimationActive={false}
              tooltipType="none"
              legendType="none"
            />
          </ComposedChart>
        </ResponsiveContainer>
      </div>
      <p id="resumen-grafica" className="grafica__resumen">
        {resumen}
        {omitidas > 0 &&
          ultimaOmitida !== null &&
          ` Solo se dibujan los últimos ${VENTANA_MS / 60_000} min: las ${omitidas} lecturas anteriores (hasta la del ${formatearFechaYHora(ultimaOmitida.t)}) están solo en la tabla.`}
      </p>
    </>
  )
}
