// Pruebas de la matemática de las cifras que cuentan (src/animacion.ts).
// Sin dependencias, desde la raíz del repo: node --test web/pruebas/*.test.js
import test from 'node:test'
import assert from 'node:assert/strict'
import { CUENTA_CAMBIO_MS, CUENTA_INICIAL_MS, suavizar, valorIntermedio } from '../src/animacion.ts'

test('suavizar: empieza en 0, termina en 1 y nunca baja', () => {
  assert.equal(suavizar(0), 0)
  assert.equal(suavizar(1), 1)
  let previo = 0
  for (let t = 0.05; t <= 1; t += 0.05) {
    const v = suavizar(t)
    assert.ok(v >= previo, `no decrece en ${t}`)
    previo = v
  }
})

test('suavizar: es una salida (ease-out): a la mitad del tiempo ya recorrió más de la mitad', () => {
  assert.ok(suavizar(0.5) > 0.9)
})

test('suavizar: fuera de rango se recorta, para que la cifra nunca pase de su valor', () => {
  assert.equal(suavizar(-1), 0)
  assert.equal(suavizar(2), 1)
})

test('valorIntermedio: interpola hacia arriba y hacia abajo y respeta los extremos', () => {
  assert.equal(valorIntermedio(0, 40, 0), 0)
  assert.equal(valorIntermedio(0, 40, 1), 40)
  assert.equal(valorIntermedio(0, 40, 0.5), 20)
  assert.equal(valorIntermedio(40, 10, 0.5), 25)
  assert.equal(valorIntermedio(7, 7, 0.3), 7)
})

test('las duraciones: la primera cuenta se nota y la de un cambio es más corta', () => {
  assert.ok(CUENTA_INICIAL_MS >= 600 && CUENTA_INICIAL_MS <= 1000)
  assert.ok(CUENTA_CAMBIO_MS < CUENTA_INICIAL_MS)
})
