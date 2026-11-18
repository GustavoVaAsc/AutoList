// Lógica pura de la latencia de inferencia: validar la respuesta de
// GET /api/metricas/latencia (pestaña Energía de nodered/flows/flows.json) y
// resumir las mediciones.
// Sin DOM, sin fetch y sin importar nada: `node --test web/pruebas/*.test.js`
// carga este archivo directo en Node 22.18+ quitando los tipos.

/** Una medición: una fila que Node-RED guarda por cada resultado de la visión. */
export interface MedicionLatencia {
  /** Instante en que Node-RED guardó la medición, en milisegundos desde 1970 (UTC). */
  t: number
  /** Captura (ms); null si la etapa no corrió. */
  cap: number | null
  /** Detección del rostro (ms); null si la etapa no corrió. */
  det: number | null
  /** Embedding (ms); null si la etapa no corrió. */
  emb: number | null
  /** Ciclo completo (ms); null si la visión no pudo medirlo. */
  total: number | null
}

export type ResultadoLatencias =
  | { ok: true; mediciones: MedicionLatencia[] }
  | { ok: false; motivo: string }

export interface ResumenLatencias {
  /** Mediciones que traen total; las únicas que entran en la mediana y la máxima. */
  conTotal: number
  mediana: number | null
  maxima: number | null
}

const ETAPAS = ['cap', 'det', 'emb', 'total'] as const

function leerEtapa(valor: unknown): { ok: true; valor: number | null } | { ok: false } {
  if (valor === null) return { ok: true, valor: null }
  if (typeof valor === 'number' && Number.isFinite(valor) && valor >= 0) return { ok: true, valor }
  return { ok: false }
}

/**
 * Convierte el JSON de la API en mediciones, de la más nueva a la más vieja.
 * El motivo de un rechazo dice qué fila y qué campo, nunca el valor recibido.
 */
export function leerLatencias(cuerpo: unknown): ResultadoLatencias {
  if (!Array.isArray(cuerpo)) {
    return { ok: false, motivo: 'se esperaba una lista de mediciones' }
  }
  const mediciones: MedicionLatencia[] = []
  for (const [posicion, elemento] of cuerpo.entries()) {
    const fila = typeof elemento === 'object' && elemento !== null && !Array.isArray(elemento)
      ? (elemento as Record<string, unknown>)
      : null
    if (fila === null) {
      return { ok: false, motivo: `la fila ${posicion + 1} no es un objeto` }
    }
    const t = typeof fila.hora === 'string' ? Date.parse(fila.hora) : Number.NaN
    if (Number.isNaN(t)) {
      return { ok: false, motivo: `la fila ${posicion + 1} no tiene una hora válida` }
    }
    const medicion: MedicionLatencia = { t, cap: null, det: null, emb: null, total: null }
    for (const etapa of ETAPAS) {
      const leida = leerEtapa(fila[etapa])
      if (!leida.ok) {
        return { ok: false, motivo: `la fila ${posicion + 1} no tiene ${etapa} en ms (un número o null)` }
      }
      medicion[etapa] = leida.valor
    }
    mediciones.push(medicion)
  }
  return { ok: true, mediciones: mediciones.sort((a, b) => b.t - a.t) }
}

/** Mediana y máxima de los totales de `mediciones`; sin totales no hay cifras (null). */
export function resumirLatencias(mediciones: readonly MedicionLatencia[]): ResumenLatencias {
  const totales = mediciones
    .map((m) => m.total)
    .filter((total): total is number => total !== null)
    .sort((a, b) => a - b)
  if (totales.length === 0) {
    return { conTotal: 0, mediana: null, maxima: null }
  }
  const medio = Math.floor(totales.length / 2)
  const mediana = totales.length % 2 === 1 ? totales[medio]! : (totales[medio - 1]! + totales[medio]!) / 2
  return { conTotal: totales.length, mediana, maxima: totales[totales.length - 1]! }
}
