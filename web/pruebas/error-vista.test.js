// Pruebas de la elección del error que muestra una vista con varios sondeos
// (src/api/errorVista.ts).
// Sin dependencias: node --test web/pruebas/*.test.js
import test from 'node:test'
import assert from 'node:assert/strict'
import { elegirError } from '../src/api/errorVista.ts'

const bien = { error: null, sinSesion: false }
const falla = (error) => ({ error, sinSesion: false })
const sin401 = (error) => ({ error, sinSesion: true })

test('sin errores no hay nada que mostrar', () => {
  assert.deepEqual(elegirError([bien, bien, bien]), { error: null, sinSesion: false })
  assert.deepEqual(elegirError([]), { error: null, sinSesion: false })
})

test('sin 401 se muestra el primer error, en el orden de las fuentes', () => {
  assert.deepEqual(elegirError([bien, falla('energía'), falla('latencia')]), { error: 'energía', sinSesion: false })
  assert.deepEqual(elegirError([falla('asistencia'), falla('energía'), bien]), { error: 'asistencia', sinSesion: false })
})

test('con un 401 se muestra el error del sondeo detenido aunque otro, anterior en el orden, también falle', () => {
  const r = elegirError([falla('asistencia: red'), bien, sin401('latencia: 401')])
  assert.deepEqual(r, { error: 'latencia: 401', sinSesion: true })
})

test('con varios 401 gana el primero en el orden', () => {
  assert.deepEqual(elegirError([bien, sin401('energía: 401'), sin401('latencia: 401')]), {
    error: 'energía: 401',
    sinSesion: true,
  })
})

test('sinSesion sin mensaje en esa fuente (no debería pasar) cae al primer error que haya', () => {
  const r = elegirError([falla('asistencia'), { error: null, sinSesion: true }])
  assert.deepEqual(r, { error: 'asistencia', sinSesion: true })
})

test('un 401 cuyo mensaje ya se borró no oculta un error de otro sondeo', () => {
  assert.deepEqual(elegirError([bien, falla('latencia')]), { error: 'latencia', sinSesion: false })
})
