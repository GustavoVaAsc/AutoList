// Pruebas de la lógica pura de la prueba de reconocimiento (src/api/pruebaRespuestas.ts): validar la respuesta de
// GET /api/enrolamiento/prueba, traducir las de POST /api/enrolamiento/probar y poner el resultado en palabras.
// Los cuerpos de ejemplo son sintéticos (las cuentas y los nombres no son de la lista real).
// Sin dependencias: node --test web/pruebas/*.test.js
import test from 'node:test'
import assert from 'node:assert/strict'
import {
  SIN_RESPUESTA,
  TEXTO_PENDIENTE,
  TEXTO_SIN_RESPUESTA,
  TITULO_DECISION,
  faseDePrueba,
  interpretarSolicitud,
  leerPrueba,
  resumirResultado,
  traducirMotivoPrueba,
} from '../src/api/pruebaRespuestas.ts'

const LAT = { cap: 180, det: 12, emb: 6, total: 240 }
const resultadoApi = (extra = {}) => ({
  decision: 'reconocido',
  cuenta: '320068234',
  nombre: 'Ana López',
  conf: 0.78,
  margen: 0.21,
  motivo: null,
  lat_ms: LAT,
  recibido: '2026-11-16T15:30:12.000Z',
  ...extra,
})
const cuerpoApi = (extra = {}) => ({ pendiente: false, solicitado: '2026-11-16T15:30:10.000Z', resultado: resultadoApi(), ...extra })

// ---- leerPrueba ---------------------------------------------------------------------------------

test('leerPrueba convierte las fechas a milisegundos y conserva cada campo del resultado', () => {
  const r = leerPrueba(cuerpoApi())
  assert.equal(r.ok, true)
  assert.deepEqual(r.prueba, {
    pendiente: false,
    solicitado: Date.parse('2026-11-16T15:30:10.000Z'),
    resultado: {
      decision: 'reconocido',
      cuenta: '320068234',
      nombre: 'Ana López',
      conf: 0.78,
      margen: 0.21,
      motivo: null,
      lat_ms: LAT,
      recibido: Date.parse('2026-11-16T15:30:12.000Z'),
    },
  })
})

test('sin ninguna prueba: pendiente false, solicitado null y resultado null son válidos', () => {
  assert.deepEqual(leerPrueba({ pendiente: false, solicitado: null, resultado: null }), {
    ok: true,
    prueba: { pendiente: false, solicitado: null, resultado: null },
  })
  const pendiente = leerPrueba({ pendiente: true, solicitado: '2026-11-16T15:30:10.000Z', resultado: null })
  assert.equal(pendiente.ok, true)
  assert.equal(pendiente.prueba.pendiente, true)
})

test('un resultado sin cuenta, confianza ni latencia (rechazo "ocupado" o sin rostro) es válido, con null', () => {
  const r = leerPrueba(cuerpoApi({ resultado: resultadoApi({ decision: 'error', cuenta: null, nombre: null, conf: null, margen: null, motivo: 'ocupado', lat_ms: null }) }))
  assert.equal(r.ok, true)
  assert.equal(r.prueba.resultado.lat_ms, null)
  assert.equal(r.prueba.resultado.motivo, 'ocupado')
  const sinEtapa = leerPrueba(cuerpoApi({ resultado: resultadoApi({ decision: 'sin_rostro', cuenta: null, nombre: null, conf: null, margen: null, lat_ms: { cap: 180, det: null, emb: null, total: 200 } }) }))
  assert.deepEqual(sinEtapa.prueba.resultado.lat_ms, { cap: 180, det: null, emb: null, total: 200 })
})

test('una forma que no es la del contrato se rechaza, y el motivo nunca repite el valor recibido', () => {
  const malos = [
    null,
    [],
    'x',
    { solicitado: null, resultado: null },
    { pendiente: 'si', solicitado: null, resultado: null },
    cuerpoApi({ solicitado: 'ayer' }),
    cuerpoApi({ solicitado: 5 }),
    cuerpoApi({ resultado: 'SECRETO' }),
    cuerpoApi({ resultado: resultadoApi({ decision: 'SECRETO' }) }),
    cuerpoApi({ resultado: resultadoApi({ decision: undefined }) }),
    cuerpoApi({ resultado: resultadoApi({ recibido: 'SECRETO' }) }),
    cuerpoApi({ resultado: resultadoApi({ cuenta: 320068234 }) }),
    cuerpoApi({ resultado: resultadoApi({ nombre: 7 }) }),
    cuerpoApi({ resultado: resultadoApi({ conf: 'SECRETO' }) }),
    cuerpoApi({ resultado: resultadoApi({ conf: Number.POSITIVE_INFINITY }) }),
    cuerpoApi({ resultado: resultadoApi({ margen: 'SECRETO' }) }),
    cuerpoApi({ resultado: resultadoApi({ motivo: 3 }) }),
    cuerpoApi({ resultado: resultadoApi({ lat_ms: 'SECRETO' }) }),
    cuerpoApi({ resultado: resultadoApi({ lat_ms: { cap: 1, det: 1, emb: 1 } }) }),
    cuerpoApi({ resultado: resultadoApi({ lat_ms: { cap: 1, det: 1, emb: 1, total: '3' } }) }),
  ]
  for (const cuerpo of malos) {
    const r = leerPrueba(cuerpo)
    assert.equal(r.ok, false, JSON.stringify(cuerpo))
    assert.equal(typeof r.motivo, 'string')
    assert.doesNotMatch(r.motivo, /SECRETO|320068234|Ana/)
  }
})

test('las seis decisiones del contrato se leen, y una desconocida no', () => {
  for (const decision of Object.keys(TITULO_DECISION)) {
    assert.equal(leerPrueba(cuerpoApi({ resultado: resultadoApi({ decision }) })).ok, true, decision)
  }
  assert.equal(Object.keys(TITULO_DECISION).length, 6)
  assert.equal(leerPrueba(cuerpoApi({ resultado: resultadoApi({ decision: 'quizas' }) })).ok, false)
})

// ---- faseDePrueba -------------------------------------------------------------------------------

test('faseDePrueba distingue nada, espera, sin respuesta y resultado', () => {
  const lee = (c) => leerPrueba(c).prueba
  assert.equal(faseDePrueba(null), 'sin-prueba')
  assert.equal(faseDePrueba(lee({ pendiente: false, solicitado: null, resultado: null })), 'sin-prueba')
  assert.equal(faseDePrueba(lee({ pendiente: true, solicitado: '2026-11-16T15:30:10.000Z', resultado: null })), 'pendiente')
  assert.equal(faseDePrueba(lee({ pendiente: false, solicitado: '2026-11-16T15:30:10.000Z', resultado: null })), 'sin-respuesta')
  assert.equal(faseDePrueba(lee(cuerpoApi())), 'resultado')
})

// ---- interpretarSolicitud -----------------------------------------------------------------------

test('un 202 con pendiente true es una solicitud hecha; sin esa forma, un fallo que no se disfraza de éxito', () => {
  assert.deepEqual(interpretarSolicitud(202, { pendiente: true, solicitado: '2026-11-16T15:30:10.000Z' }), { tipo: 'solicitada' })
  const raro = interpretarSolicitud(202, {})
  assert.equal(raro.tipo, 'fallo')
  assert.equal(interpretarSolicitud(202, undefined).tipo, 'fallo')
})

test('los dos 409 se distinguen por su motivo; un 409 sin motivo conocido es un fallo', () => {
  assert.deepEqual(interpretarSolicitud(409, { error: 'x', motivo: 'registro_abierto' }), { tipo: 'registro-abierto' })
  assert.deepEqual(interpretarSolicitud(409, { error: 'x', motivo: 'prueba_pendiente', solicitado: '2026-11-16T15:30:10.000Z' }), { tipo: 'en-curso' })
  assert.equal(interpretarSolicitud(409, { error: 'x' }).tipo, 'fallo')
  assert.equal(interpretarSolicitud(409, undefined).tipo, 'fallo')
})

test('un 429 es demasiado rápido, y los demás códigos traen un mensaje que dice qué hacer', () => {
  assert.deepEqual(interpretarSolicitud(429, { error: 'x' }), { tipo: 'demasiado-rapido' })
  assert.deepEqual(interpretarSolicitud(429, undefined), { tipo: 'demasiado-rapido' })
  for (const status of [401, 403, 500, 502]) {
    const r = interpretarSolicitud(status, { error: 'SECRETO' })
    assert.equal(r.tipo, 'fallo')
    assert.equal(r.status, status)
    assert.ok(r.mensaje.length > 20)
    assert.doesNotMatch(r.mensaje, /SECRETO/)
  }
  assert.match(interpretarSolicitud(403, {}).mensaje, /Recarga la página/)
  assert.match(interpretarSolicitud(401, {}).mensaje, /usuario y contraseña/)
  assert.match(SIN_RESPUESTA, /No se sabe si la prueba salió/)
})

// ---- Palabras -----------------------------------------------------------------------------------

test('cada decisión tiene su título en palabras, en el orden del contrato', () => {
  assert.deepEqual(TITULO_DECISION, {
    reconocido: 'Reconocido',
    dudoso: 'Dudoso',
    desconocido: 'Desconocido',
    sin_rostro: 'Sin rostro',
    imagen_insuficiente: 'Imagen insuficiente',
    error: 'Error',
  })
})

test('los motivos del contrato se traducen; sin motivo, texto vacío; un código nuevo se nombra tal cual', () => {
  for (const motivo of ['captura_fallida', 'imagen_insuficiente', 'sin_rostro', 'desconocido', 'galeria_vacia', 'error_interno', 'ocupado', 'cuenta_invalida']) {
    const t = traducirMotivoPrueba(motivo)
    assert.ok(t.length > 10, motivo)
    assert.doesNotMatch(t, /Motivo sin traducir/, motivo)
    assert.ok(!t.includes('_'), motivo + ' se muestra con palabras, no con el código')
  }
  assert.equal(traducirMotivoPrueba(null), '')
  assert.equal(traducirMotivoPrueba('algo_nuevo'), 'Motivo sin traducir (código algo_nuevo).')
})

test('resumirResultado: el título es la decisión; el detalle, el motivo traducido o, sin motivo, lo que significa', () => {
  const r = (extra) => resumirResultado(leerPrueba(cuerpoApi({ resultado: resultadoApi(extra) })).prueba.resultado)
  assert.deepEqual(r({}), { titulo: 'Reconocido', detalle: 'La visión reconoce a la persona que está frente a la cámara.', tono: 'exito' })
  assert.equal(r({ decision: 'dudoso' }).tono, 'neutro')
  assert.equal(r({ decision: 'dudoso' }).titulo, 'Dudoso')
  assert.match(r({ decision: 'dudoso' }).detalle, /zona gris/)
  const desconocido = r({ decision: 'desconocido', cuenta: null, nombre: null, motivo: 'desconocido' })
  assert.equal(desconocido.titulo, 'Desconocido')
  assert.equal(desconocido.detalle, 'Ningún rostro registrado se parece lo bastante.')
  assert.equal(r({ decision: 'sin_rostro', motivo: 'sin_rostro', cuenta: null }).detalle, 'No se vio ningún rostro.')
  assert.equal(r({ decision: 'imagen_insuficiente', motivo: 'captura_fallida', cuenta: null }).detalle, 'La cámara no pudo tomar la foto.')
  const ocupado = r({ decision: 'error', cuenta: null, motivo: 'ocupado', lat_ms: null })
  assert.equal(ocupado.tono, 'error')
  assert.match(ocupado.detalle, /ocupado/)
  assert.equal(r({ decision: 'error', cuenta: null, motivo: null }).detalle, 'El servicio de visión no pudo terminar la prueba.')
})

test('los textos de espera y de falta de respuesta no inventan avance', () => {
  assert.equal(TEXTO_PENDIENTE.titulo, 'Tomando la foto…')
  assert.doesNotMatch(TEXTO_PENDIENTE.detalle, /%|\d+ de \d+/)
  assert.match(TEXTO_SIN_RESPUESTA.detalle, /servicio de visión/)
})
