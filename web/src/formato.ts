// Formato de horas y cifras para la interfaz. Las horas se muestran siempre en
// la zona del aula, sin importar la zona de la laptop que abre el navegador.
const ZONA = 'America/Mexico_City'
const LOCAL = 'es-MX'

const soloHora = new Intl.DateTimeFormat(LOCAL, {
  timeZone: ZONA,
  hour: '2-digit',
  minute: '2-digit',
  second: '2-digit',
  hourCycle: 'h23',
})

const fechaYHora = new Intl.DateTimeFormat(LOCAL, {
  timeZone: ZONA,
  day: 'numeric',
  month: 'short',
  year: 'numeric',
  hour: '2-digit',
  minute: '2-digit',
  second: '2-digit',
  hourCycle: 'h23',
})

// Sin año ni segundos: marcas del eje de la gráfica cuando el rango cruza días.
const diaYHoraCorta = new Intl.DateTimeFormat(LOCAL, {
  timeZone: ZONA,
  day: 'numeric',
  month: 'short',
  hour: '2-digit',
  minute: '2-digit',
  hourCycle: 'h23',
})

const soloDia = new Intl.DateTimeFormat(LOCAL, { timeZone: ZONA, day: 'numeric', month: 'short' })
const claveDia = new Intl.DateTimeFormat('en-CA', { timeZone: ZONA }) // AAAA-MM-DD

const decimales = (cifras: number) =>
  new Intl.NumberFormat(LOCAL, { minimumFractionDigits: cifras, maximumFractionDigits: cifras })

const unDecimal = decimales(1)
const dosDecimales = decimales(2)
const tresDecimales = decimales(3)

/** "19:25:47" */
export const formatearHora = (t: number) => soloHora.format(t)

/** "1 nov 2026, 19:25:47" */
export const formatearFechaYHora = (t: number) => fechaYHora.format(t)

/** "1 nov, 16:26" */
export const formatearDiaYHoraCorta = (t: number) => diaYHoraCorta.format(t)

/** "1 nov" */
export const formatearDia = (t: number) => soloDia.format(t)

/** Identifica el día calendario de la zona del aula, para saber si un rango cruza días. */
export const diaDe = (t: number) => claveDia.format(t)

export const formatearVoltaje = (v: number) => dosDecimales.format(v)
export const formatearCorriente = (i: number) => tresDecimales.format(i)
export const formatearPotencia = (p: number) => dosDecimales.format(p)
export const formatearConfianza = (c: number) => dosDecimales.format(c)
/** Milisegundos como entero: "240". */
export const formatearMilisegundos = (ms: number) => String(Math.round(ms))
/** Temperatura con un decimal: "48,6". */
export const formatearTemperatura = (t: number) => unDecimal.format(t)
/** Porcentaje como entero: "34". */
export const formatearPorcentaje = (p: number) => String(Math.round(p))

/** La hora de `t` y, si cae en otro día que `referencia` (por ejemplo, la hora de la placa), también la fecha. */
export const formatearHoraODia = (t: number, referencia: number) =>
  diaDe(t) === diaDe(referencia) ? formatearHora(t) : formatearFechaYHora(t)

/** "2 min", "45 s": antigüedad aproximada para avisos. */
export function formatearAntiguedad(ms: number): string {
  const segundos = Math.max(0, Math.round(ms / 1000))
  if (segundos < 60) {
    return `${segundos} s`
  }
  const minutos = Math.round(segundos / 60)
  if (minutos < 60) {
    return `${minutos} min`
  }
  const horas = Math.floor(minutos / 60)
  return horas < 48 ? `${horas} h` : `${Math.floor(horas / 24)} días`
}
