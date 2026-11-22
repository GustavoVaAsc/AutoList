// Pruebas de la lógica pura del estado del sistema (src/api/sistemaRespuestas.ts): validar la
// respuesta de GET /api/sistema/estado, los renglones de la hoja Sistema, el detalle de la cabecera y
// la antigüedad de lo recibido. Los cuerpos de ejemplo son sintéticos.
// Sin dependencias: node --test web/pruebas/*.test.js
import test from 'node:test'
import assert from 'node:assert/strict'
import { assertTextoLegible, assertTextosDistintos } from './texto.js'
import { MEMORIA_MINIMA_BYTES, SENAL_DEBIL_DBM, detalleCabecera, filasSistema, leerSistema } from '../src/api/sistemaRespuestas.ts'

const NUNCA = { recibido: null, datos: null }
const T1 = '2026-11-16T15:30:00.000Z'
const T2 = '2026-11-16T15:30:10.500Z'
const VISION = { ts: '2026-11-16T09:30:00.000-06:00', en_linea: true, modo: 'pase', galeria: 38, modelo: 'w600k_mbf-uint8-v1' }
const CAMARA = { en_linea: true, rssi: -58, heap: 122880, temp: 48.5, camara: false, corte: false, nvs: true }
const ACTUADORES = { servo: 'cerrado', led: 'azul', luz: 0 }
const METRICAS = { ts: '2026-11-16T15:30:09.000Z', temp: 48.6, cpu: 20, ram: 75 }
const cuerpo = (extra = {}) => ({
  vision: { recibido: T1, datos: VISION },
  camara: { recibido: T2, datos: CAMARA },
  actuadores: { recibido: T1, datos: ACTUADORES },
  metricas: { recibido: T2, datos: METRICAS },
  ...extra,
})
const sistema = (extra = {}) => {
  const r = leerSistema(cuerpo(extra))
  assert.equal(r.ok, true, JSON.stringify(r))
  return r.sistema
}

test('leerSistema convierte las cuatro entradas y la hora de recepción a milisegundos', () => {
  const s = sistema()
  assert.deepEqual(s.vision, {
    t: Date.parse(T1),
    datos: { enLinea: true, modo: 'pase', galeria: 38, modelo: 'w600k_mbf-uint8-v1' },
  })
  assert.deepEqual(s.camara, {
    t: Date.parse(T2),
    datos: { enLinea: true, rssi: -58, heap: 122880, temp: 48.5, camara: false, corte: false, nvs: true },
  })
  assert.deepEqual(s.actuadores, { t: Date.parse(T1), datos: { servo: 'cerrado', led: 'azul', luz: false } })
  assert.deepEqual(s.metricas, { t: Date.parse(T2), datos: { temp: 48.6, cpu: 20, ram: 75 } })
})

test('recibido y datos en null dicen que nunca llegó nada: la entrada queda en null', () => {
  const s = sistema({ vision: NUNCA, camara: NUNCA, actuadores: NUNCA, metricas: NUNCA })
  assert.deepEqual(s, { vision: null, camara: null, actuadores: null, metricas: null })
  const parcial = sistema({ camara: NUNCA })
  assert.equal(parcial.camara, null)
  assert.notEqual(parcial.vision, null)
})

test('el Will ({"en_linea":false}) es válido en la visión y en la cámara, con el resto en null', () => {
  const s = sistema({ vision: { recibido: T1, datos: { en_linea: false } }, camara: { recibido: T1, datos: { en_linea: false } } })
  assert.deepEqual(s.vision.datos, { enLinea: false, modo: null, galeria: null, modelo: null })
  assert.deepEqual(s.camara.datos, { enLinea: false, rssi: null, heap: null, temp: null, camara: null, corte: null, nvs: null })
})

test('un estado parcial de los actuadores (solo luz) es válido, y luz 1 es encendida', () => {
  const s = sistema({ actuadores: { recibido: T1, datos: { luz: 1 } } })
  assert.deepEqual(s.actuadores.datos, { servo: null, led: null, luz: true })
})

test('los campos que no son del contrato se ignoran y no pasan al resultado', () => {
  const s = sistema({
    vision: { recibido: T1, datos: { ...VISION, embedding: [1], secreto: 'x' }, extra: 1 },
    otra: { recibido: T1, datos: {} },
  })
  assert.deepEqual(Object.keys(s.vision.datos).sort(), ['enLinea', 'galeria', 'modelo', 'modo'])
  assert.deepEqual(Object.keys(s).sort(), ['actuadores', 'camara', 'metricas', 'vision'])
})

test('lo que no es un objeto se rechaza con un motivo', () => {
  for (const c of [null, undefined, [], 'x', 12]) {
    const r = leerSistema(c)
    assert.equal(r.ok, false, JSON.stringify(c))
    assert.match(r.motivo, /objeto/)
  }
})

test('una entrada que falta o no es un objeto se rechaza y el motivo dice cuál', () => {
  for (const clave of ['vision', 'camara', 'actuadores', 'metricas']) {
    for (const malo of [undefined, null, 'x', []]) {
      const c = cuerpo({ [clave]: malo })
      const r = leerSistema(c)
      assert.equal(r.ok, false, clave)
      assert.match(r.motivo, new RegExp(clave))
    }
  }
})

test('con recibido o datos a medias (uno null y el otro no) se rechaza', () => {
  assert.equal(leerSistema(cuerpo({ vision: { recibido: T1, datos: null } })).ok, false)
  assert.equal(leerSistema(cuerpo({ vision: { recibido: null, datos: VISION } })).ok, false)
  assert.equal(leerSistema(cuerpo({ vision: {} })).ok, false)
})

test('una hora de recepción que no es una fecha se rechaza', () => {
  for (const recibido of ['', 'ayer', 12, {}]) {
    const r = leerSistema(cuerpo({ camara: { recibido, datos: CAMARA } }))
    assert.equal(r.ok, false, JSON.stringify(recibido))
    assert.match(r.motivo, /camara.*hora/)
  }
})

test('un campo fuera del contrato se rechaza y el motivo dice entrada y campo, nunca el valor', () => {
  const casos = [
    ['vision', { ...VISION, en_linea: 'si SECRETO' }, 'en_linea'],
    ['vision', { modo: 'pase' }, 'en_linea'],
    ['vision', { ...VISION, modo: 'dormido SECRETO' }, 'modo'],
    ['vision', { ...VISION, galeria: -1 }, 'galeria'],
    ['vision', { ...VISION, galeria: 1.5 }, 'galeria'],
    ['vision', { ...VISION, modelo: 'x'.repeat(101) }, 'modelo'],
    ['camara', { ...CAMARA, en_linea: 1 }, 'en_linea'],
    ['camara', { ...CAMARA, rssi: '-58' }, 'rssi'],
    ['camara', { ...CAMARA, heap: -1 }, 'heap'],
    ['camara', { ...CAMARA, temp: 900 }, 'temp'],
    ['camara', { ...CAMARA, temp: null }, 'temp'],
    ['camara', { ...CAMARA, camara: 'no' }, 'camara'],
    ['camara', { ...CAMARA, corte: 0 }, 'corte'],
    ['camara', { ...CAMARA, nvs: 'true' }, 'nvs'],
    ['actuadores', { ...ACTUADORES, servo: 'arriba' }, 'servo'],
    ['actuadores', { ...ACTUADORES, led: 'morado' }, 'led'],
    ['actuadores', { ...ACTUADORES, luz: 2 }, 'luz'],
    ['actuadores', { ...ACTUADORES, luz: true }, 'luz'],
    ['metricas', { ...METRICAS, temp: '48' }, 'temp'],
    ['metricas', { ...METRICAS, cpu: 101 }, 'cpu'],
    ['metricas', { ...METRICAS, cpu: -1 }, 'cpu'],
    ['metricas', { ...METRICAS, ram: null }, 'ram'],
    ['metricas', { temp: 1, cpu: 2 }, 'ram'],
  ]
  for (const [clave, datos, campo] of casos) {
    const r = leerSistema(cuerpo({ [clave]: { recibido: T1, datos } }))
    assert.equal(r.ok, false, `${clave} ${JSON.stringify(datos)}`)
    assert.match(r.motivo, new RegExp(clave))
    assert.match(r.motivo, new RegExp(campo))
    assert.doesNotMatch(r.motivo, /SECRETO/)
  }
})

// ---------------------------------------------------------------------------------------------
// Renglones de la hoja
// ---------------------------------------------------------------------------------------------

const filas = (extra) => filasSistema(sistema(extra))
const porClave = (fs, clave) => fs.find((f) => f.clave === clave)
const camaraCon = (datos) => ({ camara: { recibido: T2, datos: { en_linea: true, ...datos } } })

test('hay tres renglones, en este orden: reconocimiento, nodo del aula y actuadores; cada uno con su nombre', () => {
  const fs = filas()
  assert.deepEqual(fs.map((f) => f.clave), ['vision', 'camara', 'actuadores'])
  assertTextosDistintos(fs.map((f) => f.nombre))
})

test('con todo en línea, cada renglón lo dice, trae su detalle en palabras y la hora en que se recibió', () => {
  const fs = filas()
  const vision = porClave(fs, 'vision')
  assert.equal(vision.conexion, 'en-linea')
  assertTextoLegible(vision.estado)
  assert.equal(vision.detalle.length, 2, 'el modo y los rostros registrados')
  assertTextosDistintos(vision.detalle)
  assert.equal(vision.t, Date.parse(T1))
  const camara = porClave(fs, 'camara')
  assert.equal(camara.conexion, 'en-linea')
  assert.equal(camara.detalle.length, 1, 'solo si la cámara está encendida: la señal, la memoria y el chip van solo si fallan')
  assertTextosDistintos(camara.detalle)
  assert.equal(camara.t, Date.parse(T2))
  const actuadores = porClave(fs, 'actuadores')
  assert.equal(actuadores.conexion, 'en-linea')
  assert.equal(actuadores.detalle.length, 3, 'puerta, LED y luz')
  assertTextosDistintos(actuadores.detalle)
  assertTextosDistintos([vision.estado, porClave(filas({ vision: NUNCA }), 'vision').estado, porClave(filas({ vision: { recibido: T1, datos: { en_linea: false } } }), 'vision').estado])
})

test('el Will deja el reconocimiento y el nodo del aula fuera de línea y sin detalle de un estado que ya no vale', () => {
  const fs = filas({ vision: { recibido: T1, datos: { en_linea: false } }, camara: { recibido: T2, datos: { en_linea: false } } })
  for (const clave of ['vision', 'camara']) {
    const f = porClave(fs, clave)
    assert.equal(f.conexion, 'fuera-de-linea', clave)
    assertTextoLegible(f.estado, clave)
    assert.deepEqual(f.detalle, [], clave)
    assert.notEqual(f.t, null, clave)
  }
})

test('lo que nunca llegó es "sin datos", sin hora y sin detalle (no se inventa un estado)', () => {
  const fs = filas({ vision: NUNCA, camara: NUNCA, actuadores: NUNCA })
  for (const f of fs) {
    assert.equal(f.conexion, 'sin-datos', f.clave)
    assertTextoLegible(f.estado, f.clave)
    assert.deepEqual(f.detalle, [], f.clave)
    assert.equal(f.t, null, f.clave)
  }
})

test('los actuadores viven en el nodo del aula: con él fuera de línea también ellos, con su último estado conocido', () => {
  const fs = filas({ camara: { recibido: T2, datos: { en_linea: false } } })
  const a = porClave(fs, 'actuadores')
  assert.equal(a.conexion, 'fuera-de-linea')
  assert.equal(a.detalle.length, 4, 'el aviso de conexión y los tres últimos estados')
  assertTextosDistintos(a.detalle)
  assert.ok(a.detalle[0].toLowerCase().includes(porClave(fs, 'camara').nombre.toLowerCase()), 'el aviso nombra al nodo como la hoja')
})

test('los actuadores sin datos siguen sin datos aunque el nodo esté caído', () => {
  const fs = filas({ camara: { recibido: T2, datos: { en_linea: false } }, actuadores: NUNCA })
  assert.equal(porClave(fs, 'actuadores').conexion, 'sin-datos')
})

test('los actuadores con estado y el nodo sin datos se muestran en línea (no hay nada que diga lo contrario)', () => {
  const fs = filas({ camara: NUNCA })
  assert.equal(porClave(fs, 'actuadores').conexion, 'en-linea')
})

test('el detalle del nodo menciona la cámara y, de lo demás, solo lo que falla', () => {
  const sano = porClave(filas(camaraCon({ camara: true, rssi: SENAL_DEBIL_DBM + 1, heap: MEMORIA_MINIMA_BYTES, nvs: true, corte: false })), 'camara').detalle
  assert.equal(sano.length, 1)
  const encendida = sano[0]
  assert.notEqual(porClave(filas(camaraCon({ camara: false })), 'camara').detalle[0], encendida, 'encendida y apagada se leen distinto')
  const mal = porClave(filas(camaraCon({ camara: true, rssi: SENAL_DEBIL_DBM, heap: MEMORIA_MINIMA_BYTES - 1, nvs: false, corte: true })), 'camara').detalle
  assert.equal(mal.length, 5, 'la cámara más cuatro fallas')
  assert.equal(mal[0], encendida)
  assertTextosDistintos(mal)
  for (const falla of [{ rssi: SENAL_DEBIL_DBM }, { heap: MEMORIA_MINIMA_BYTES - 1 }, { nvs: false }, { corte: true }]) {
    assert.equal(porClave(filas(camaraCon(falla)), 'camara').detalle.length, 1, JSON.stringify(falla))
  }
  const minimo = filas(camaraCon({}))
  assert.deepEqual(porClave(minimo, 'camara').detalle, [])
  assert.equal(porClave(minimo, 'camara').conexion, 'en-linea')
})

test('el detalle del reconocimiento es su modo y los rostros registrados, sin el modelo; sin ellos no hay detalle', () => {
  const modos = ['inactivo', 'pase', 'enrolando'].map((modo) => {
    const d = porClave(filas({ vision: { recibido: T1, datos: { en_linea: true, modo, galeria: 1, modelo: 'm' } } }), 'vision').detalle
    assert.equal(d.length, 2, modo)
    assert.ok(!d.join(' ').includes('m '), 'sin el modelo')
    return d[0]
  })
  assertTextosDistintos(modos)
  assert.deepEqual(porClave(filas({ vision: { recibido: T1, datos: { en_linea: true } } }), 'vision').detalle, [])
})

test('los rostros registrados llevan el número y cambian entre singular y plural', () => {
  const rostros = (galeria) =>
    porClave(filas({ vision: { recibido: T1, datos: { en_linea: true, galeria } } }), 'vision').detalle[0]
  assert.ok(rostros(38).includes('38'))
  assert.ok(rostros(0).includes('0'))
  assert.notEqual(rostros(1).replace('1', 'N'), rostros(2).replace('2', 'N'), 'singular y plural se leen distinto')
})

test('la puerta, el LED y la luz se dicen en español y lo que no llegó no se lista', () => {
  const fs = filas({ actuadores: { recibido: T1, datos: { servo: 'abierto', led: 'ambar', luz: 1 } } })
  const detalle = porClave(fs, 'actuadores').detalle
  assertTextosDistintos(detalle)
  assert.ok(detalle.some((t) => t.includes('ámbar')))
  const cerrada = porClave(filas({ actuadores: { recibido: T1, datos: { servo: 'cerrado' } } }), 'actuadores').detalle
  assert.equal(cerrada.length, 1)
  assert.notEqual(cerrada[0], detalle[0], 'abierta y cerrada se leen distinto')
  const apagado = filas({ actuadores: { recibido: T1, datos: { led: 'apagado' } } })
  assert.equal(porClave(apagado, 'actuadores').detalle.length, 1)
})

// ---------------------------------------------------------------------------------------------
// Cabecera
// ---------------------------------------------------------------------------------------------

const caido = { recibido: T1, datos: { en_linea: false } }
const nombres = () => {
  const fs = filas()
  return { vision: porClave(fs, 'vision').nombre.toLowerCase(), camara: porClave(fs, 'camara').nombre.toLowerCase() }
}

test('sin respuesta de la API, la cabecera no dice nada del sistema', () => {
  assert.equal(detalleCabecera(null), null)
})

test('con el reconocimiento y el nodo en línea no hay detalle: solo se nombra lo que falla', () => {
  assert.equal(detalleCabecera(sistema()), null)
  assert.equal(detalleCabecera(sistema({ actuadores: NUNCA, metricas: NUNCA })), null, 'los actuadores y las métricas no cuentan')
})

test('una caída nombra solo lo que falla, con el mismo nombre que la hoja', () => {
  const n = nombres()
  const vision = detalleCabecera(sistema({ vision: caido })).texto
  assertTextoLegible(vision)
  assert.ok(vision.includes(n.vision) && !vision.includes(n.camara))
  const camara = detalleCabecera(sistema({ camara: caido })).texto
  assertTextoLegible(camara)
  assert.ok(camara.includes(n.camara) && !camara.includes(n.vision))
  const ambas = detalleCabecera(sistema({ vision: caido, camara: caido })).texto
  assertTextoLegible(ambas)
  assert.ok(ambas.includes(n.vision) && ambas.includes(n.camara))
  assertTextosDistintos([vision, camara, ambas])
})

test('lo que nunca llegó también se nombra, y se distingue de lo caído', () => {
  const n = nombres()
  const sinDatos = detalleCabecera(sistema({ camara: NUNCA })).texto
  assert.ok(sinDatos.includes(n.camara) && !sinDatos.includes(n.vision))
  const caida = detalleCabecera(sistema({ camara: caido })).texto
  assert.notEqual(sinDatos, caida)
})

test('con dos fallas distintas, lo caído se dice primero', () => {
  const n = nombres()
  const texto = detalleCabecera(sistema({ vision: NUNCA, camara: caido })).texto
  assert.ok(texto.indexOf(n.camara) < texto.indexOf(n.vision))
  assertTextoLegible(texto)
})
