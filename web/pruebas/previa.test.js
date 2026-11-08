// Pruebas de la lógica pura de la vista previa de la cámara (src/api/previa.ts):
// interpretación de la respuesta del borde (200, 401, 404, 503 y el resto), los
// textos de cada estado, el formateo de la antigüedad del cuadro, la conversión
// del cuadro a URL de datos, el plazo de espera de la consulta y el aviso de
// privacidad (que tiene que ser el mismo en el código y en DESIGN.md).
// Sin dependencias, desde la raíz del repo: node --test web/pruebas/*.test.js
// (Node 22.18 o posterior carga el .ts quitando los tipos).
import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import {
  AVISO_PRIVACIDAD,
  ErrorPrevia,
  base64DeBytes,
  formatearEdadCuadro,
  interpretarPrevia,
  pedirPrevia,
  textoEstadoPrevia,
  tipoMedio,
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

test('formatearEdadCuadro: una antigüedad que no es un número va a cero', () => {
  assert.equal(formatearEdadCuadro(Number.NaN), '0 s')
  assert.equal(formatearEdadCuadro(Number.POSITIVE_INFINITY), '0 s')
  assert.equal(formatearEdadCuadro(-1), '0 s')
})

// ------------------------------------------------------------ base64DeBytes

test('base64DeBytes: los bytes de "hola" se codifican en base64', () => {
  const bytes = new Uint8Array([104, 111, 108, 97])
  assert.equal(base64DeBytes(bytes), 'aG9sYQ==')
})

test('base64DeBytes: un arreglo vacío no da nada', () => {
  assert.equal(base64DeBytes(new Uint8Array(0)), '')
})

test('base64DeBytes: un cuadro grande no revienta la pila y sale completo', () => {
  // El cuadro de la vista previa sale pequeño (`res=HVGA`: 480×320, calidad 20),
  // pero la codificación va troceada: `String.fromCharCode(...bytes)` sobre el
  // arreglo entero revienta la pila de llamadas si el cuadro fuera mayor.
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

test('urlDeImagen: el tipo de medio se acepta en mayúsculas y con espacios', () => {
  const bytes = new Uint8Array([104, 111, 108, 97])
  assert.equal(tipoMedio(' IMAGE/JPEG '), 'image/jpeg')
  assert.equal(urlDeImagen(bytes, ' IMAGE/JPEG '), 'data:image/jpeg;base64,aG9sYQ==')
})

test('urlDeImagen: solo se acepta image/jpeg, que es lo del contrato', () => {
  const bytes = new Uint8Array([104, 111, 108, 97])
  assert.equal(urlDeImagen(bytes, 'image/png'), null)
  assert.equal(urlDeImagen(bytes, 'image/svg+xml'), null)
})

test('urlDeImagen: un cuerpo vacío no arma ninguna URL', () => {
  assert.equal(urlDeImagen(new Uint8Array(0), 'image/jpeg'), null)
})

// ------------------------------------------------------------ pedirPrevia

/**
 * Ejecuta `accion` con un `fetch` simulado y deja el original en su lugar.
 * No hay dependencias: el `fetch` global de Node se sustituye por el que
 * devuelve la respuesta que cada prueba necesita.
 */
async function conFetch(simulado, accion) {
  const original = globalThis.fetch
  globalThis.fetch = simulado
  try {
    return await accion()
  } finally {
    globalThis.fetch = original
  }
}

test('pedirPrevia: un 200 con un JPEG da el cuadro y su antigüedad', async () => {
  const cuerpo = new Uint8Array([0xff, 0xd8, 0xff, 0xe0])
  const consulta = await conFetch(
    async () => new Response(cuerpo, { status: 200, headers: { 'Content-Type': 'image/jpeg', 'X-Edad-Cuadro': '250' } }),
    () => pedirPrevia(new AbortController().signal),
  )
  assert.equal(consulta.resultado.tipo, 'cuadro')
  assert.equal(consulta.resultado.edadMs, 250)
  assert.match(consulta.imagenUrl, /^data:image\/jpeg;base64,/)
})

test('pedirPrevia: un 200 que no es imagen no produce un cuadro', async () => {
  await conFetch(
    async () => new Response('{"error":"no"}', { status: 200, headers: { 'Content-Type': 'application/json' } }),
    async () => {
      await assert.rejects(
        () => pedirPrevia(new AbortController().signal),
        (error) => error instanceof ErrorPrevia && /no es una imagen/.test(error.message),
      )
    },
  )
})

test('pedirPrevia: un 200 con el cuerpo vacío no produce un cuadro', async () => {
  await conFetch(
    async () => new Response(null, { status: 200, headers: { 'Content-Type': 'image/jpeg' } }),
    async () => {
      await assert.rejects(
        () => pedirPrevia(new AbortController().signal),
        (error) => error instanceof ErrorPrevia && /cuadro vacío/.test(error.message),
      )
    },
  )
})

test('pedirPrevia: si el plazo se vence leyendo el cuerpo, dice que la API tardó', async () => {
  // El plazo de espera también cubre la lectura del cuerpo: si se vence
  // mientras se lee, el error tiene que ser el de tiempo agotado y no el
  // AbortError crudo (que el panel mostraría como error inesperado).
  const respuesta = {
    status: 200,
    headers: new Headers({ 'Content-Type': 'image/jpeg' }),
    arrayBuffer: () =>
      new Promise((_resolver, rechazar) => {
        setTimeout(() => rechazar(new DOMException('La operación fue abortada.', 'AbortError')), 30)
      }),
  }
  await conFetch(async () => respuesta, async () => {
    await assert.rejects(
      () => pedirPrevia(new AbortController().signal, 5),
      (error) => error instanceof ErrorPrevia && /tardó demasiado/.test(error.message),
    )
  })
})

// ------------------------------------------------------------ aviso de privacidad

test('el aviso de privacidad va literal', () => {
  assert.equal(
    AVISO_PRIVACIDAD,
    'Vista en vivo: los cuadros solo se muestran aquí, en memoria. No se guarda ninguna imagen del rostro. El sistema almacena únicamente el vector matemático del rostro y el registro de asistencia.',
  )
})

test('el aviso de privacidad de DESIGN.md es el mismo que el del panel', () => {
  // Si el texto cambia en el código, esta prueba avisa: el documento puede
  // quedarse desfasado sin que nada lo note.
  const diseno = readFileSync(new URL('../DESIGN.md', import.meta.url), 'utf8')
  assert.ok(diseno.includes(AVISO_PRIVACIDAD), 'DESIGN.md no trae el aviso de privacidad del panel')
})
