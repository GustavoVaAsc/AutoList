// Pruebas de la lógica pura del control manual de los actuadores (src/api/actuadoresRespuestas.ts):
// el catálogo de órdenes, la traducción de las respuestas de POST /api/actuadores, el estado de los
// actuadores en palabras y cuándo se pueden mandar órdenes.
// El catálogo se compara con el vocabulario del firmware (firmware/xiao/logica_actuadores.cpp), que es la
// fuente de verdad de los valores. Los cuerpos de ejemplo son sintéticos.
// Sin dependencias: node --test web/pruebas/*.test.js
import test from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import { assertTextoLegible, assertTextosDistintos } from './texto.js'
import { fileURLToPath } from 'node:url'
import {
  ENFRIAMIENTO_MS,
  avisoOrdenEnviada,
  CONFIRMACION_PUERTA,
  ORDENES,
  SIN_RESPUESTA,
  disponibilidadActuadores,
  etiquetaEstadoActuadores,
  estadoEnPalabras,
  interpretarOrden,
  pideConfirmacion,
} from '../src/api/actuadoresRespuestas.ts'

const leer = (ruta) => fs.readFileSync(fileURLToPath(new URL(ruta, import.meta.url)), 'utf8')

// ---- Catálogo -----------------------------------------------------------------------------------

test('el catálogo trae las once órdenes del brief, cada una con su etiqueta y su aviso', () => {
  assert.equal(ORDENES.length, 11)
  assertTextosDistintos(ORDENES.map((o) => o.etiqueta))
  assertTextosDistintos(ORDENES.map((o) => o.hecho))
  assert.deepEqual([...new Set(ORDENES.map((o) => o.grupo))], ['puerta', 'led', 'buzzer', 'luz'])
  assert.equal(new Set(ORDENES.map((o) => o.id)).size, ORDENES.length)
})

test('cada orden manda un solo campo, y sus valores son los que acepta el firmware', () => {
  const cpp = leer('../../firmware/xiao/logica_actuadores.cpp')
  const trozo = (nombre) => {
    const i = cpp.indexOf(nombre)
    assert.ok(i >= 0, nombre)
    return cpp.slice(i, cpp.indexOf('\n}\n', i))
  }
  const cadenas = (t) => [...t.matchAll(/strcmp\(valor, "([a-z]+)"\)/g)].map((m) => m[1]).sort()
  const numeros = (t) => [...t.matchAll(/valor == ([0-9]+)/g)].map((m) => Number(m[1])).sort()
  const servos = cadenas(trozo('PedidoServo interpretarServo'))
  const leds = cadenas(trozo('PedidoLed interpretarLed'))
  const buzzers = numeros(trozo('PedidoBuzzer interpretarBuzzer'))

  const porCampo = { servo: [], led: [], buzzer: [], luz: [] }
  for (const o of ORDENES) {
    const claves = Object.keys(o.cuerpo)
    assert.equal(claves.length, 1, o.id)
    porCampo[claves[0]].push(o.cuerpo[claves[0]])
  }
  assert.deepEqual(porCampo.servo, servos)
  assert.deepEqual([...porCampo.led].sort(), leds)
  assert.deepEqual([...porCampo.buzzer].sort(), buzzers)
  assert.deepEqual([...porCampo.luz].sort(), [0, 1])
})

test('no hay orden "cerrar" ni valores que el firmware ignoraría; el cuerpo cabe en el búfer de la XIAO', () => {
  for (const o of ORDENES) {
    assert.notEqual(o.cuerpo.servo, 'cerrar')
    assert.ok(new TextEncoder().encode(JSON.stringify(o.cuerpo)).length < 100, o.id)
  }
})

test('el aviso de una orden enviada empieza por "Orden enviada" y nombra la orden, sin decir que se ejecutó', () => {
  for (const o of ORDENES) {
    const aviso = avisoOrdenEnviada(o)
    assert.ok(aviso.startsWith('Orden enviada'), o.id)
    assert.ok(aviso.includes(o.hecho), o.id)
  }
})

test('el enfriamiento del frontend cubre el límite de 1 s de la API (Node-RED, "Validar y limitar orden")', () => {
  assert.ok(ENFRIAMIENTO_MS >= 1000)
})

// ---- interpretarOrden ---------------------------------------------------------------------------

test('un 200 con lo publicado es una orden enviada, y trae lo que se publicó', () => {
  assert.deepEqual(interpretarOrden(200, { publicado: { luz: 1 } }), { tipo: 'enviada', publicado: { luz: 1 } })
})

test('un 200 que no trae lo publicado no se da por enviado', () => {
  for (const cuerpo of [undefined, null, {}, { publicado: null }, { publicado: {} }, { publicado: [] }, 'ok']) {
    const r = interpretarOrden(200, cuerpo)
    assert.equal(r.tipo, 'fallo', JSON.stringify(cuerpo))
    assertTextoLegible(r.mensaje)
  }
})

test('un 429 es "demasiado rápido", no un fallo', () => {
  assert.deepEqual(interpretarOrden(429, { error: 'x' }), { tipo: 'demasiado-rapido' })
})

test('400 y 415 dan un fallo legible con su código, sin repetir el texto técnico de la API', () => {
  for (const status of [400, 415]) {
    const r = interpretarOrden(status, { error: 'led debe ser azul, verde, rojo, ambar o apagado' })
    assert.equal(r.tipo, 'fallo')
    assert.equal(r.status, status)
    assertTextoLegible(r.mensaje)
    assert.ok(!r.mensaje.includes('led debe ser'))
    const sin = interpretarOrden(status, undefined)
    assert.equal(sin.tipo, 'fallo')
    assert.equal(sin.mensaje, r.mensaje)
  }
})

test('401, 403, 500 y lo demás son fallos con una causa en palabras', () => {
  assertTextosDistintos([401, 403, 500].map((s) => interpretarOrden(s, undefined).mensaje))
  assert.equal(interpretarOrden(503, undefined).mensaje, interpretarOrden(500, undefined).mensaje)
  for (const s of [401, 403, 500, 503]) {
    assert.equal(interpretarOrden(s, undefined).status, s)
  }
})

test('sin respuesta de la API el mensaje es legible y no es el de un fallo con respuesta', () => {
  assertTextoLegible(SIN_RESPUESTA)
  assert.notEqual(SIN_RESPUESTA, interpretarOrden(500, undefined).mensaje)
})

// ---- estadoEnPalabras ---------------------------------------------------------------------------

test('el estado de los actuadores se dice con palabras, con el cierre y el apagado automáticos', () => {
  assert.deepEqual(estadoEnPalabras({ servo: 'cerrado', led: 'azul', luz: false }), {
    puerta: 'cerrada',
    led: 'azul',
    luz: 'apagada',
  })
  assert.deepEqual(estadoEnPalabras({ servo: 'abierto', led: 'ambar', luz: true }), {
    puerta: 'abierta (se cierra sola a los 3 s)',
    led: 'ámbar',
    luz: 'encendida (se apaga sola a los 6 min)',
  })
  assert.equal(estadoEnPalabras({ servo: 'cerrado', led: 'apagado', luz: false }).led, 'apagado')
})

test('lo que no llegó se dice "sin dato", no se inventa', () => {
  assert.deepEqual(estadoEnPalabras(null), { puerta: 'sin dato', led: 'sin dato', luz: 'sin dato' })
  assert.deepEqual(estadoEnPalabras({ servo: null, led: 'rojo', luz: null }), { puerta: 'sin dato', led: 'rojo', luz: 'sin dato' })
})

// ---- disponibilidadActuadores -------------------------------------------------------------------

test('con la XIAO en línea se puede mandar, sin motivo ni aviso', () => {
  assert.deepEqual(disponibilidadActuadores('en-linea'), { habilitado: true, motivo: null, aviso: null })
})

test('con la XIAO fuera de línea los botones se desactivan y el motivo lo dice', () => {
  const d = disponibilidadActuadores('fuera-de-linea')
  assert.equal(d.habilitado, false)
  assertTextoLegible(d.motivo)
  assert.equal(d.aviso, null)
})

test('sin datos de la XIAO se puede mandar, pero con un aviso de que puede no llegar', () => {
  const d = disponibilidadActuadores('sin-datos')
  assert.equal(d.habilitado, true)
  assert.equal(d.motivo, null)
  assertTextoLegible(d.aviso)
})

test('mientras no hay respuesta del estado del sistema, los botones esperan', () => {
  const d = disponibilidadActuadores(null)
  assert.equal(d.habilitado, false)
  assertTextoLegible(d.motivo)
})

test('con el sondeo del estado fallando los botones se desactivan, aunque la última lectura dijera que la XIAO estaba en línea', () => {
  for (const conexion of ['en-linea', 'fuera-de-linea', 'sin-datos', null]) {
    const d = disponibilidadActuadores(conexion, true)
    assert.equal(d.habilitado, false, String(conexion))
    assertTextoLegible(d.motivo)
    assert.equal(d.aviso, null)
  }
  // Sin fallo del sondeo, nada cambia (el segundo argumento es opcional).
  assert.deepEqual(disponibilidadActuadores('en-linea', false), disponibilidadActuadores('en-linea'))
  assert.equal(disponibilidadActuadores('en-linea', false).habilitado, true)
  // Cada bloqueo dice lo suyo.
  assertTextosDistintos([
    disponibilidadActuadores('fuera-de-linea').motivo,
    disponibilidadActuadores(null).motivo,
    disponibilidadActuadores('en-linea', true).motivo,
  ])
})

test('el estado dice "Ahora" solo con la XIAO en línea y el sondeo bien; en cualquier otro caso, "Último estado conocido"', () => {
  const vigente = etiquetaEstadoActuadores('en-linea', false)
  const viejo = etiquetaEstadoActuadores('en-linea', true)
  assertTextosDistintos([vigente, viejo])
  for (const conexion of ['fuera-de-linea', 'sin-datos', null]) {
    assert.equal(etiquetaEstadoActuadores(conexion, false), viejo)
    assert.equal(etiquetaEstadoActuadores(conexion, true), viejo)
  }
})

// ---- Confirmación de la puerta ------------------------------------------------------------------

test('solo "Abrir puerta" pide confirmación: el LED, el buzzer y la luz se mandan con un clic', () => {
  assert.deepEqual(
    ORDENES.filter(pideConfirmacion).map((o) => o.id),
    ['puerta-abrir'],
  )
  assert.deepEqual(ORDENES.find((o) => o.id === 'puerta-abrir').cuerpo, { servo: 'abrir' })
  for (const o of ORDENES.filter((x) => x.grupo !== 'puerta')) {
    assert.equal(pideConfirmacion(o), false, o.id)
  }
})

test('el diálogo de la puerta tiene todos sus textos, dice cuándo se cierra y no habla de servos', () => {
  for (const texto of Object.values(CONFIRMACION_PUERTA)) {
    assertTextoLegible(texto)
  }
  assert.match(CONFIRMACION_PUERTA.descripcion, /\b3 s\b/, 'dice cuándo se cierra')
  assert.notEqual(CONFIRMACION_PUERTA.confirmar, CONFIRMACION_PUERTA.cancelar)
  // El botón de la hoja y el de confirmar se llaman igual: la persona confirma lo que pidió.
  assert.equal(ORDENES.find((o) => o.id === 'puerta-abrir').etiqueta, CONFIRMACION_PUERTA.confirmar)
})
