// Qué error muestra una vista con varios sondeos (Métricas: asistencia, energía y
// latencia). Lógica pura: `node --test web/pruebas/*.test.js` carga este archivo
// directo en Node 22.18+ quitando los tipos.

export interface FuenteError {
  /** Mensaje del último fallo del sondeo; null si va bien. */
  error: string | null
  /** El sondeo recibió 401 y está detenido hasta que la persona reintente. */
  sinSesion: boolean
}

export interface ErrorElegido {
  error: string | null
  /** Algún sondeo está detenido por un 401: la vista ofrece "Reintentar". */
  sinSesion: boolean
}

/**
 * Elige el error que se muestra, en el orden de `fuentes`. Con un sondeo detenido
 * por 401 se muestra el error de uno que de verdad lo esté: si no, el aviso diría
 * "La vista no insistirá sola" junto al error de otro sondeo que sí insiste.
 */
export function elegirError(fuentes: readonly FuenteError[]): ErrorElegido {
  const sinSesion = fuentes.some((f) => f.sinSesion)
  const elegida =
    (sinSesion ? fuentes.find((f) => f.sinSesion && f.error !== null) : undefined) ??
    fuentes.find((f) => f.error !== null)
  return { error: elegida?.error ?? null, sinSesion }
}
