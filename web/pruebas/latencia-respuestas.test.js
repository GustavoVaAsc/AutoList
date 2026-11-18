// Pruebas de la lógica pura de la latencia de inferencia (src/api/latenciaRespuestas.ts):
// validar la respuesta de GET /api/metricas/latencia y resumir las mediciones.
// Los cuerpos de ejemplo son sintéticos.
// Sin dependencias: node --test web/pruebas/*.test.js
import test from 'node:test'
import assert from 'node:assert/strict'
import { leerLatencias, resumirLatencias } from '../src/api/latenciaRespuestas.ts'

const fila = (hora, extra = {}) => ({ hora, cap: 180, det: 12, emb: 6, total: 240, ...extra })

test('leerLatencias convierte las filas de la API, de la más nueva a la más vieja', () => {
  const r = leerLatencias([
    fila('2026-11-05T15:30:12.000Z'),
    fila('2026-11-05T15:29:00.500Z', { cap: 150, total: 170 }),
  ])
  assert.equal(r.ok, true)
  assert.deepEqual(r.mediciones, [
    { t: Date.parse('2026-11-05T15:30:12.000Z'), cap: 180, det: 12, emb: 6, total: 240 },
    { t: Date.parse('2026-11-05T15:29:00.500Z'), cap: 150, det: 12, emb: 6, total: 170 },
  ])
})

test('leerLatencias ordena de la más nueva a la más vieja aunque la API no lo haga', () => {
  const r = leerLatencias([fila('2026-11-05T15:29:00Z'), fila('2026-11-05T15:31:00Z'), fila('2026-11-05T15:30:00Z')])
  assert.equal(r.ok, true)
  assert.deepEqual(
    r.mediciones.map((m) => m.t),
    [Date.parse('2026-11-05T15:31:00Z'), Date.parse('2026-11-05T15:30:00Z'), Date.parse('2026-11-05T15:29:00Z')],
  )
})

test('una lista vacía es válida: todavía no hay mediciones', () => {
  assert.deepEqual(leerLatencias([]), { ok: true, mediciones: [] })
})

test('una etapa que no corrió llega en null y se conserva en null; 0 es una medición', () => {
  const r = leerLatencias([fila('2026-11-05T15:30:12Z', { det: null, emb: null }), fila('2026-11-05T15:29:12Z', { cap: 0 })])
  assert.equal(r.ok, true)
  assert.equal(r.mediciones[0].det, null)
  assert.equal(r.mediciones[0].emb, null)
  assert.equal(r.mediciones[0].total, 240)
  assert.equal(r.mediciones[1].cap, 0)
})

test('un total en null (fallo del reloj de la visión) es válido', () => {
  const r = leerLatencias([fila('2026-11-05T15:30:12Z', { total: null })])
  assert.equal(r.ok, true)
  assert.equal(r.mediciones[0].total, null)
})

test('lo que no es una lista se rechaza con un motivo', () => {
  for (const cuerpo of [null, undefined, {}, 'x', 12, { mediciones: [] }]) {
    const r = leerLatencias(cuerpo)
    assert.equal(r.ok, false, JSON.stringify(cuerpo))
    assert.match(r.motivo, /lista/)
  }
})

test('una fila sin hora válida se rechaza y el motivo dice cuál', () => {
  for (const hora of [undefined, null, 12, '', 'ayer', {}]) {
    const r = leerLatencias([fila('2026-11-05T15:30:12Z'), fila(hora)])
    assert.equal(r.ok, false, String(hora))
    assert.match(r.motivo, /fila 2/)
    assert.match(r.motivo, /hora/)
  }
})

test('una etapa que no es null ni un número finito mayor o igual a 0 se rechaza, y una que falta también', () => {
  for (const etapa of ['cap', 'det', 'emb', 'total']) {
    for (const malo of ['240', -1, Number.NaN, Number.POSITIVE_INFINITY, true, [], {}, undefined]) {
      const r = leerLatencias([fila('2026-11-05T15:30:12Z', { [etapa]: malo })])
      assert.equal(r.ok, false, etapa + '=' + String(malo))
      assert.match(r.motivo, new RegExp('fila 1'))
      assert.match(r.motivo, new RegExp(etapa))
    }
  }
})

test('una fila que no es un objeto se rechaza', () => {
  for (const elemento of [null, 5, 'x', [], [1, 2]]) {
    assert.equal(leerLatencias([elemento]).ok, false)
  }
})

test('el motivo de un rechazo nunca repite el valor recibido', () => {
  const r = leerLatencias([fila('2026-11-05T15:30:12Z', { cap: 'SECRETO-123' })])
  assert.equal(r.ok, false)
  assert.doesNotMatch(r.motivo, /SECRETO/)
})

test('resumirLatencias da la mediana y la máxima de los totales y cuántos cuenta', () => {
  const r = leerLatencias([100, 300, 200, 250, 150].map((total, i) => fila(`2026-11-05T15:3${i}:00Z`, { total })))
  assert.deepEqual(resumirLatencias(r.mediciones), { conTotal: 5, mediana: 200, maxima: 300 })
})

test('la mediana de una cantidad par es el promedio de los dos centrales', () => {
  const r = leerLatencias([100, 200, 300, 400].map((total, i) => fila(`2026-11-05T15:3${i}:00Z`, { total })))
  assert.deepEqual(resumirLatencias(r.mediciones), { conTotal: 4, mediana: 250, maxima: 400 })
})

test('resumirLatencias ignora las mediciones sin total y no las cuenta', () => {
  const r = leerLatencias([
    fila('2026-11-05T15:30:00Z', { total: null }),
    fila('2026-11-05T15:29:00Z', { total: 90 }),
    fila('2026-11-05T15:28:00Z', { total: 110 }),
  ])
  assert.deepEqual(resumirLatencias(r.mediciones), { conTotal: 2, mediana: 100, maxima: 110 })
})

test('sin mediciones, o sin ningún total, el resumen no inventa cifras', () => {
  assert.deepEqual(resumirLatencias([]), { conTotal: 0, mediana: null, maxima: null })
  const r = leerLatencias([fila('2026-11-05T15:30:00Z', { total: null })])
  assert.deepEqual(resumirLatencias(r.mediciones), { conTotal: 0, mediana: null, maxima: null })
})

test('resumirLatencias no modifica la lista que recibe', () => {
  const r = leerLatencias([300, 100, 200].map((total, i) => fila(`2026-11-05T15:3${i}:00Z`, { total })))
  const antes = JSON.stringify(r.mediciones)
  resumirLatencias(r.mediciones)
  assert.equal(JSON.stringify(r.mediciones), antes)
})
