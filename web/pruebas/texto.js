// Comprobaciones de tono para los textos que ve la profesora. No son pruebas (el
// glob `*.test.js` no las carga): las usan las pruebas de la lógica pura para
// comprobar que un mensaje es un texto legible y sin jerga, sin fijar sus palabras.
// Así reescribir un texto no rompe las pruebas, pero colar una jerga técnica sí.
import assert from 'node:assert/strict'

// Palabras de quien programa la placa; no van en un texto de la interfaz.
const JERGA = /\b(api|node-red|npu|mqtt|xiao|broker|rssi|heap|nvs|soc|json|http|https|coseno|servo)\b|aula\/|docs\//i

/** Un texto para la profesora: no vacío, sin jerga técnica y sin rayas largas. */
export function assertTextoLegible(texto, contexto = '') {
  assert.equal(typeof texto, 'string', contexto)
  assert.ok(texto.trim().length > 0, `vacío ${contexto}`)
  assert.ok(!JERGA.test(texto), `jerga técnica en "${texto}" ${contexto}`)
  assert.ok(!texto.includes('—'), `raya larga en "${texto}" ${contexto}`)
}

/** Cada texto de la lista es legible y ninguno se repite: cada caso dice lo suyo. */
export function assertTextosDistintos(textos, contexto = '') {
  for (const t of textos) {
    assertTextoLegible(t, contexto)
  }
  assert.equal(new Set(textos).size, textos.length, `textos repetidos ${contexto}`)
}
