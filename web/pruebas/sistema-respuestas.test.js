// Pruebas de la lógica pura del estado del sistema (src/api/sistemaRespuestas.ts): validar la
// respuesta de GET /api/sistema/estado, los renglones de la hoja Sistema, el detalle de la cabecera y
// la antigüedad de lo recibido. Los cuerpos de ejemplo son sintéticos.
// Sin dependencias: node --test web/pruebas/*.test.js
import test from 'node:test'
import assert from 'node:assert/strict'
import { antiguedadMs, detalleCabecera, filasSistema, leerSistema } from '../src/api/sistemaRespuestas.ts'

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

test('antiguedadMs parte de la hora de la placa y suma el tiempo desde la respuesta', () => {
  const t = Date.parse('2026-11-16T15:30:00Z')
  const horaServidor = Date.parse('2026-11-16T15:30:12Z')
  assert.equal(antiguedadMs(t, horaServidor, 1000, 1000), 12_000)
  assert.equal(antiguedadMs(t, horaServidor, 1000, 4500), 15_500)
})

test('antiguedadMs nunca es negativa (el reloj de la placa pudo retroceder)', () => {
  assert.equal(antiguedadMs(1000, 500, 0, 0), 0)
})

// ---------------------------------------------------------------------------------------------
// Renglones de la hoja
// ---------------------------------------------------------------------------------------------

const filas = (extra) => filasSistema(sistema(extra))
const porClave = (fs, clave) => fs.find((f) => f.clave === clave)

test('hay tres renglones, en este orden: visión, cámara y actuadores', () => {
  const fs = filas()
  assert.deepEqual(fs.map((f) => f.clave), ['vision', 'camara', 'actuadores'])
  assert.deepEqual(fs.map((f) => f.nombre), ['Servicio de visión', 'Cámara (XIAO)', 'Actuadores'])
})

test('con todo en línea, cada renglón lo dice, trae su detalle en palabras y la hora en que se recibió', () => {
  const fs = filas()
  const vision = porClave(fs, 'vision')
  assert.equal(vision.conexion, 'en-linea')
  assert.equal(vision.estado, 'En línea')
  assert.deepEqual(vision.detalle, ['Modo pase de lista', '38 rostros en la galería', 'Modelo w600k_mbf-uint8-v1'])
  assert.equal(vision.t, Date.parse(T1))
  const camara = porClave(fs, 'camara')
  assert.equal(camara.conexion, 'en-linea')
  assert.deepEqual(camara.detalle, ['Señal -58 dBm', 'Memoria libre 120 KB', 'Chip a 48.5 °C', 'Cámara apagada'])
  assert.equal(camara.t, Date.parse(T2))
  const actuadores = porClave(fs, 'actuadores')
  assert.equal(actuadores.conexion, 'en-linea')
  assert.deepEqual(actuadores.detalle, ['Servo cerrado', 'LED azul', 'Luz apagada'])
})

test('el Will deja la visión y la cámara fuera de línea y sin detalle de un estado que ya no vale', () => {
  const fs = filas({ vision: { recibido: T1, datos: { en_linea: false } }, camara: { recibido: T2, datos: { en_linea: false } } })
  for (const clave of ['vision', 'camara']) {
    const f = porClave(fs, clave)
    assert.equal(f.conexion, 'fuera-de-linea', clave)
    assert.equal(f.estado, 'Fuera de línea', clave)
    assert.deepEqual(f.detalle, [], clave)
    assert.notEqual(f.t, null, clave)
  }
})

test('lo que nunca llegó es "Sin datos", sin hora y sin detalle (no se inventa un estado)', () => {
  const fs = filas({ vision: NUNCA, camara: NUNCA, actuadores: NUNCA })
  for (const f of fs) {
    assert.equal(f.conexion, 'sin-datos', f.clave)
    assert.equal(f.estado, 'Sin datos', f.clave)
    assert.deepEqual(f.detalle, [], f.clave)
    assert.equal(f.t, null, f.clave)
  }
})

test('los actuadores viven en la XIAO: con la cámara fuera de línea también ellos, con su último estado conocido', () => {
  const fs = filas({ camara: { recibido: T2, datos: { en_linea: false } } })
  const a = porClave(fs, 'actuadores')
  assert.equal(a.conexion, 'fuera-de-linea')
  assert.deepEqual(a.detalle, [
    'La XIAO está fuera de línea',
    'Último estado: servo cerrado',
    'Último estado: led azul',
    'Último estado: luz apagada',
  ])
})

test('los actuadores sin datos siguen sin datos aunque la cámara esté caída', () => {
  const fs = filas({ camara: { recibido: T2, datos: { en_linea: false } }, actuadores: NUNCA })
  assert.equal(porClave(fs, 'actuadores').conexion, 'sin-datos')
})

test('los actuadores con estado y la cámara sin datos se muestran en línea (no hay nada que diga lo contrario)', () => {
  const fs = filas({ camara: NUNCA })
  assert.equal(porClave(fs, 'actuadores').conexion, 'en-linea')
})

test('el detalle de la cámara solo menciona lo que pasa: corte y nvs con falla, y sin lo que no llegó', () => {
  const fs = filas({ camara: { recibido: T2, datos: { en_linea: true, camara: true, corte: true, nvs: false } } })
  assert.deepEqual(porClave(fs, 'camara').detalle, [
    'Cámara encendida',
    'Capturas suspendidas por temperatura o enfriamiento',
    'Memoria flash (NVS) con falla',
  ])
  const minimo = filas({ camara: { recibido: T2, datos: { en_linea: true } } })
  assert.deepEqual(porClave(minimo, 'camara').detalle, [])
  assert.equal(porClave(minimo, 'camara').conexion, 'en-linea')
})

test('el detalle de la visión: un solo rostro va en singular, el modo enrolando se dice en palabras y un modelo vacío no se lista', () => {
  const fs = filas({ vision: { recibido: T1, datos: { en_linea: true, modo: 'enrolando', galeria: 1, modelo: '' } } })
  assert.deepEqual(porClave(fs, 'vision').detalle, ['Registrando un rostro', '1 rostro en la galería'])
  const inactivo = filas({ vision: { recibido: T1, datos: { en_linea: true, modo: 'inactivo', galeria: 0 } } })
  assert.deepEqual(porClave(inactivo, 'vision').detalle, ['Modo inactivo', '0 rostros en la galería'])
})

test('los colores del LED y el estado del servo se dicen en español', () => {
  const fs = filas({ actuadores: { recibido: T1, datos: { servo: 'abierto', led: 'ambar', luz: 1 } } })
  assert.deepEqual(porClave(fs, 'actuadores').detalle, ['Servo abierto', 'LED ámbar', 'Luz encendida'])
  const apagado = filas({ actuadores: { recibido: T1, datos: { led: 'apagado' } } })
  assert.deepEqual(porClave(apagado, 'actuadores').detalle, ['LED apagado'])
})

// ---------------------------------------------------------------------------------------------
// Cabecera
// ---------------------------------------------------------------------------------------------

test('sin respuesta de la API, la cabecera no dice nada del sistema', () => {
  assert.equal(detalleCabecera(null), null)
})

test('con visión y cámara en línea, el detalle lo dice y no es un problema', () => {
  assert.deepEqual(detalleCabecera(sistema()), { texto: 'visión y cámara en línea', problema: false })
})

test('una caída se dice con su nombre y es un problema', () => {
  const vision = sistema({ vision: { recibido: T1, datos: { en_linea: false } } })
  assert.deepEqual(detalleCabecera(vision), { texto: 'visión fuera de línea, cámara en línea', problema: true })
  const camara = sistema({ camara: { recibido: T1, datos: { en_linea: false } } })
  assert.deepEqual(detalleCabecera(camara), { texto: 'cámara fuera de línea, visión en línea', problema: true })
  const ambas = sistema({ vision: { recibido: T1, datos: { en_linea: false } }, camara: { recibido: T1, datos: { en_linea: false } } })
  assert.deepEqual(detalleCabecera(ambas), { texto: 'visión y cámara fuera de línea', problema: true })
})

test('lo que nunca llegó se dice "sin datos" y también es un problema', () => {
  assert.deepEqual(detalleCabecera(sistema({ camara: NUNCA })), { texto: 'cámara sin datos, visión en línea', problema: true })
  assert.deepEqual(detalleCabecera(sistema({ vision: NUNCA, camara: NUNCA })), { texto: 'visión y cámara sin datos', problema: true })
  assert.deepEqual(
    detalleCabecera(sistema({ vision: NUNCA, camara: { recibido: T1, datos: { en_linea: false } } })),
    { texto: 'cámara fuera de línea, visión sin datos', problema: true },
  )
})

test('los actuadores y las métricas no cambian el detalle de la cabecera (es de la visión y la cámara)', () => {
  assert.deepEqual(detalleCabecera(sistema({ actuadores: NUNCA, metricas: NUNCA })), { texto: 'visión y cámara en línea', problema: false })
})
