// Pruebas de las horas con fecha (src/formato.ts). Sin dependencias: node --test web/pruebas/*.test.js
import test from 'node:test'
import assert from 'node:assert/strict'
import { formatearFechaYHora, formatearHora, formatearHoraODia } from '../src/formato.ts'

test('formatearHoraODia: solo la hora si cae el mismo día que la referencia; con la fecha si no', () => {
  const ref = Date.parse('2026-11-16T18:00:00Z')
  const mismoDia = Date.parse('2026-11-16T17:00:00Z')
  const ayer = Date.parse('2026-11-15T17:00:00Z')
  assert.equal(formatearHoraODia(mismoDia, ref), formatearHora(mismoDia))
  assert.equal(formatearHoraODia(ayer, ref), formatearFechaYHora(ayer))
  assert.notEqual(formatearHoraODia(ayer, ref), formatearHora(ayer))
})
