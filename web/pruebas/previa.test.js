// Pruebas de la lógica pura de la vista previa de la cámara (src/api/previa.ts):
// interpretación de la respuesta del borde (200, 401, 404, 503 y el resto), los
// textos de cada estado, el formateo de la antigüedad del cuadro, la conversión
// del cuadro a URL de datos y el aviso de privacidad.
// Sin dependencias, desde la raíz del repo: node --test web/pruebas/*.test.js
// (Node 22.18 o posterior carga el .ts quitando los tipos).
import test from 'node:test'
import assert from 'node:assert/strict'
import {
  AVISO_PRIVACIDAD,
  base64DeBytes,
  formatearEdadCuadro,
  interpretarPrevia,
  textoEstadoPrevia,
  urlDeImagen,
} from '../src/api/previa.ts'

// ------------------------------------------------------------ interpretarPrevia

test('interpretarPrevia: un 200 es un cuadro, con la antigüedad de la cabecera', () => {
  const resultado = interpretarPrevia(200, '1234')
  assert.equal(resultado.tipo, 'cuadro')
  assert.equal(resultado.edadMs, 1234)
})

test('interpretarPrevia: sin cabecera de antigüedad, el cuadro no tiene edad', () => {
  const resultado = interpretarPrevia(200, null)
  assert.equal(resultado.tipo, 'cuadro')
  assert.equal(resultado.edadMs, null)
})

test('interpretarPrevia: una antigüedad que no se entiende se descarta', () => {
  assert.equal(interpretarPrevia(200, 'mucho').edadMs, null)
  assert.equal(interpretarPrevia(200, '').edadMs, null)
  assert.equal(interpretarPrevia(200, '-5').edadMs, null)
  assert.equal(interpretarPrevia(200, ' 250 ').edadMs, 250)
})

test('interpretarPrevia: un 404 es que la previsualización no está configurada', () => {
  assert.equal(interpretarPrevia(404, null).tipo, 'no-configurada')
})

test('interpretarPrevia: un 503 es que todavía no hay cuadro', () => {
  assert.equal(interpretarPrevia(503, null).tipo, 'sin-cuadro')
})

test('interpretarPrevia: un 401 es que no hay sesión', () => {
  assert.equal(interpretarPrevia(401, null).tipo, 'sin-sesion')
})

test('interpretarPrevia: cualquier otro estado es un fallo, con su código', () => {
  const resultado = interpretarPrevia(500, null)
  assert.equal(resultado.tipo, 'error')
  assert.match(resultado.mensaje, /500/)
})

// ------------------------------------------------------------ textoEstadoPrevia

test('textoEstadoPrevia: sin respuesta todavía, el panel dice que está consultando', () => {
  const { titulo, detalle } = textoEstadoPrevia(null)
  assert.equal(titulo, 'Consultando la cámara')
  assert.match(detalle, /cuadro/)
})

test('textoEstadoPrevia: con cuadro, el panel dice que es la vista en vivo', () => {
  const { titulo, detalle } = textoEstadoPrevia(interpretarPrevia(200, '1000'))
  assert.equal(titulo, 'Vista en vivo')
  assert.match(detalle, /colocar al alumno/)
})

test('textoEstadoPrevia: sin configurar no se presenta como un fallo', () => {
  const { titulo, detalle } = textoEstadoPrevia(interpretarPrevia(404, null))
  assert.equal(titulo, 'Vista previa no configurada')
  assert.match(detalle, /no está en uso/)
})

test('textoEstadoPrevia: sin cuadro se dice que la vista sigue preguntando', () => {
  const { titulo, detalle } = textoEstadoPrevia(interpretarPrevia(503, null))
  assert.equal(titulo, 'Sin cuadro de la cámara')
  assert.match(detalle, /cada segundo/)
})

test('textoEstadoPrevia: sin sesión se dice que faltan las credenciales', () => {
  const { titulo, detalle } = textoEstadoPrevia(interpretarPrevia(401, null))
  assert.equal(titulo, 'Sin sesión iniciada')
  assert.match(detalle, /usuario y contraseña/)
})

test('textoEstadoPrevia: el fallo muestra el mensaje que llegó de la API', () => {
  const { titulo, detalle } = textoEstadoPrevia(interpretarPrevia(502, null))
  assert.equal(titulo, 'No se pudo consultar la vista previa')
  assert.match(detalle, /502/)
})

// ------------------------------------------------------------ formatearEdadCuadro

test('formatearEdadCuadro: segundos, y minutos con los segundos que falten', () => {
  assert.equal(formatearEdadCuadro(0), '0 s')
  assert.equal(formatearEdadCuadro(999), '0 s')
  assert.equal(formatearEdadCuadro(1500), '1 s')
  assert.equal(formatearEdadCuadro(65_000), '1 min 5 s')
})

test('formatearEdadCuadro: sin antigüedad del borde no hay texto', () => {
  assert.equal(formatearEdadCuadro(null), null)
})

// ------------------------------------------------------------ base64DeBytes

test('base64DeBytes: los bytes de "hola" se codifican en base64', () => {
  const bytes = new Uint8Array([104, 111, 108, 97])
  assert.equal(base64DeBytes(bytes), 'aG9sYQ==')
})

test('base64DeBytes: un cuadro grande no revienta la pila y sale completo', () => {
  // Un cuadro de la cámara pesa cientos de KiB: la codificación va troceada,
  // porque `String.fromCharCode(...bytes)` sobre el arreglo entero revienta la
  // pila de llamadas.
  const bytes = new Uint8Array(300_000)
  for (let i = 0; i < bytes.length; i++) {
    bytes[i] = (i * 31) % 256
  }
  const base64 = base64DeBytes(bytes)
  assert.equal(base64, Buffer.from(bytes).toString('base64'))
  assert.equal(base64.length, 4 * Math.ceil(bytes.length / 3))
})

// ------------------------------------------------------------ urlDeImagen

test('urlDeImagen: arma la URL de datos del cuadro con su tipo de medio', () => {
  const bytes = new Uint8Array([104, 111, 108, 97])
  assert.equal(urlDeImagen(bytes, 'image/jpeg'), 'data:image/jpeg;base64,aG9sYQ==')
})

test('urlDeImagen: el tipo de medio se toma sin sus parámetros', () => {
  const bytes = new Uint8Array([104, 111, 108, 97])
  assert.equal(urlDeImagen(bytes, 'image/jpeg; charset=binary'), 'data:image/jpeg;base64,aG9sYQ==')
})

test('urlDeImagen: una respuesta que no es imagen no arma ninguna URL', () => {
  const bytes = new Uint8Array([104, 111, 108, 97])
  assert.equal(urlDeImagen(bytes, 'text/html; charset=utf-8'), null)
  assert.equal(urlDeImagen(bytes, 'application/json'), null)
  assert.equal(urlDeImagen(bytes, null), null)
  assert.equal(urlDeImagen(bytes, ''), null)
})

// ------------------------------------------------------------ aviso de privacidad

test('el aviso de privacidad va literal', () => {
  assert.equal(
    AVISO_PRIVACIDAD,
    'Vista en vivo: los cuadros solo se muestran aquí, en memoria. No se guarda ninguna imagen del rostro. El sistema almacena únicamente el vector matemático del rostro y el registro de asistencia.',
  )
})
