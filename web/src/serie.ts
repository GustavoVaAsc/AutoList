// Prepara las lecturas para la gráfica de potencia: acota la ventana visible y
// corta la línea donde hay huecos largos.
import type { Lectura } from './api/energia'
import { diaDe } from './formato'

/**
 * Ventana visible: los últimos 30 min respecto de la última lectura. Con el
 * ritmo normal de la XIAO (una lectura cada pocos segundos) las 100 lecturas
 * que se piden caben de sobra, así que la ventana solo recorta cuando hubo un
 * paro largo: sin ella, una ráfaga de ayer y otra de hoy quedarían
 * aplastadas en dos picos verticales a los lados de 21 h de nada.
 * Las lecturas recortadas siguen en la tabla.
 */
export const VENTANA_MS = 30 * 60_000

/**
 * Hueco largo: un intervalo mayor que 5 veces el intervalo habitual (la
 * mediana de los intervalos de la ventana), con un mínimo de 30 s. La mediana
 * adapta el criterio al ritmo real de publicación y no la distorsionan los
 * propios huecos; el mínimo evita partir la línea por la variación normal
 * cuando las lecturas llegan casi seguidas.
 */
const FACTOR_HUECO = 5
const HUECO_MINIMO_MS = 30_000

export interface PuntoSerie {
  t: number
  /** Potencia; null en el punto que marca un hueco, para que la línea se corte. */
  p: number | null
  /** Potencia de una lectura sin vecinas cercanas: se dibuja como punto, no como línea. */
  suelta: number | null
}

export interface Serie {
  puntos: PuntoSerie[]
  /** Lecturas dentro de la ventana, de la más vieja a la más nueva. */
  visibles: Lectura[]
  /** Si la ventana visible abarca más de un día calendario. */
  cruzaDias: boolean
}

function mediana(valores: number[]): number {
  if (valores.length === 0) {
    return 0
  }
  const orden = [...valores].sort((a, b) => a - b)
  const medio = Math.floor(orden.length / 2)
  return orden.length % 2 === 1 ? orden[medio]! : (orden[medio - 1]! + orden[medio]!) / 2
}

/** `lecturas` va de la más vieja a la más nueva y no puede estar vacía. */
export function construirSerie(lecturas: Lectura[]): Serie {
  const ultima = lecturas[lecturas.length - 1]!
  const visibles = lecturas.filter((l) => l.t >= ultima.t - VENTANA_MS)
  const intervalos = visibles.slice(1).map((l, i) => l.t - visibles[i]!.t)
  const umbral = Math.max(FACTOR_HUECO * mediana(intervalos), HUECO_MINIMO_MS)

  const puntos: PuntoSerie[] = []
  visibles.forEach((l, i) => {
    const hayHuecoAntes = i === 0 || intervalos[i - 1]! > umbral
    const hayHuecoDespues = i === visibles.length - 1 || intervalos[i]! > umbral
    puntos.push({ t: l.t, p: l.p, suelta: hayHuecoAntes && hayHuecoDespues ? l.p : null })
    if (i < visibles.length - 1 && hayHuecoDespues) {
      puntos.push({ t: l.t + intervalos[i]! / 2, p: null, suelta: null })
    }
  })

  return {
    puntos,
    visibles,
    cruzaDias: cruzaDias(visibles),
  }
}

/** Si el rango de lecturas (de la más vieja a la más nueva) toca más de un día calendario. */
export function cruzaDias(lecturas: Lectura[]): boolean {
  const primera = lecturas[0]
  const ultima = lecturas[lecturas.length - 1]
  return primera !== undefined && ultima !== undefined && diaDe(primera.t) !== diaDe(ultima.t)
}
