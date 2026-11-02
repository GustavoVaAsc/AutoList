import { Area, CartesianGrid, ComposedChart, Line, ResponsiveContainer, Tooltip, XAxis, YAxis } from 'recharts'
import { formatearDiaYHoraCorta, formatearFechaYHora, formatearHora, formatearPotencia } from '../formato'
import { VENTANA_MS, type Serie } from '../serie'

interface Props {
  serie: Serie
}

const COLOR_TEXTO = 'var(--cuerpo)'
const ID_RELLENO = 'relleno-potencia'

interface PropsTooltip {
  active?: boolean
  payload?: ReadonlyArray<{ value?: unknown; payload?: { t?: number } }>
}

/** Tooltip con el estilo de las tarjetas: la hora en mono y la cifra tabular. */
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
 * Área de la potencia (W) de la ventana visible. La línea y el relleno llevan
 * el acento: el relleno da peso visual a la magnitud de la potencia y separa
 * la serie de la cuadrícula. Los huecos largos cortan también el relleno.
 * La tabla de lecturas tiene los mismos datos.
 * Movimiento: la gráfica se revela una sola vez al montar (animación CSS del
 * contenedor, en estilos.css). Los datos que se refrescan cada 5 s no se
 * animan: animarlos sería ruido sin información.
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
            <defs>
              <linearGradient id={ID_RELLENO} x1="0" y1="0" x2="0" y2="1">
                <stop offset="0%" stopColor="var(--acento)" stopOpacity={0.18} />
                <stop offset="100%" stopColor="var(--acento)" stopOpacity={0} />
              </linearGradient>
            </defs>
            <CartesianGrid stroke="var(--linea)" vertical={false} />
            <XAxis
              dataKey="t"
              type="number"
              scale="time"
              domain={['dataMin', 'dataMax']}
              tickFormatter={cruzaDias ? formatearDiaYHoraCorta : formatearHora}
              tick={{ fill: COLOR_TEXTO, fontSize: 12 }}
              stroke="var(--linea-fuerte)"
              minTickGap={cruzaDias ? 72 : 48}
            />
            <YAxis
              domain={[0, 'auto']}
              tick={{ fill: COLOR_TEXTO, fontSize: 12 }}
              stroke="var(--linea-fuerte)"
              width={36}
              tickFormatter={(valor: number) => String(Math.round(valor * 10) / 10)}
            />
            <Tooltip
              isAnimationActive={false}
              cursor={{ stroke: 'var(--linea-fuerte)' }}
              content={(props) => <TooltipPotencia {...props} />}
            />
            <Area
              dataKey="p"
              name="Potencia"
              type="linear"
              stroke="var(--acento)"
              strokeWidth={2}
              fill={`url(#${ID_RELLENO})`}
              dot={false}
              activeDot={{ r: 4, fill: 'var(--acento)', stroke: 'var(--lienzo)' }}
              connectNulls={false}
              isAnimationActive={false}
            />
            {/* Lecturas aisladas (sin vecinas cercanas): una línea no las dibujaría. */}
            <Line
              dataKey="suelta"
              stroke="none"
              dot={{ r: 3, fill: 'var(--acento)', stroke: 'var(--acento)' }}
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
