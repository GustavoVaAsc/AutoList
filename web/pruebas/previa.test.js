// Pruebas de la lógica pura de la vista previa de la cámara (src/api/previa.ts):
// interpretación de la respuesta del borde (200, 401, 404, 503 y el resto), los
// textos de cada estado, el formateo de la antigüedad del cuadro, la conversión
// del cuadro a URL de datos, el estado del panel cuando el navegador no puede
// pintar un cuadro, el tope de bytes del cuerpo de la respuesta, el plazo de
// espera de la consulta y el aviso de privacidad (que tiene que ser el mismo en
// el código y en DESIGN.md).
// Sin dependencias, desde la raíz del repo: node --test web/pruebas/*.test.js
// (Node 22.18 o posterior carga el .ts quitando los tipos).
import test from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import {
  AVISO_PRIVACIDAD,
  ErrorPrevia,
  TAMANO_MAXIMO_CUERPO,
  base64DeBytes,
  estadoDelPanel,
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

test('textoEstadoPrevia: el cuadro que el navegador no pudo pintar tiene su propio texto', () => {
  const { titulo, detalle } = textoEstadoPrevia({ tipo: 'cuadro-no-pintado' })
  assert.equal(titulo, 'No se pudo mostrar el cuadro')
  assert.match(detalle, /recarga la página/i)
  // No es lo que se dice cuando no hay cuadro: el cuadro sí llegó.
  const sinCuadro = textoEstadoPrevia(interpretarPrevia(503, null))
  assert.notEqual(sinCuadro.titulo, titulo)
  assert.notEqual(sinCuadro.detalle, detalle)
})

// ------------------------------------------------------------ estadoDelPanel

test('estadoDelPanel: sin fallo, el panel muestra lo que respondió el borde', () => {
  const resultado = interpretarPrevia(200, '1000')
  assert.deepEqual(estadoDelPanel(resultado, 1, null), resultado)
})

test('estadoDelPanel: un cuadro que el navegador no pudo pintar es un estado propio', () => {
  const estado = estadoDelPanel(interpretarPrevia(200, '1000'), 3, 3)
  assert.equal(estado.tipo, 'cuadro-no-pintado')
})

test('estadoDelPanel: dos cuadros con los mismos bytes no se confunden', () => {
  // La URL de datos es el cuadro en base64: con la escena quieta, el cuadro
  // siguiente tiene los mismos bytes y la misma URL. El fallo se guarda con el
  // número del cuadro, así que el del primero no alcanza al segundo (si se
  // guardara la URL, el panel se quedaría en «Sin cuadro» para siempre).
  const bytes = new Uint8Array([0xff, 0xd8, 0xff, 0xe0])
  const url = urlDeImagen(bytes, 'image/jpeg')
  const resultado = interpretarPrevia(200, '1000')
  assert.equal(estadoDelPanel(resultado, 1, 1).tipo, 'cuadro-no-pintado')
  assert.equal(urlDeImagen(bytes, 'image/jpeg'), url)
  assert.equal(estadoDelPanel(resultado, 2, 1).tipo, 'cuadro')
})

test('estadoDelPanel: un fallo de un cuadro anterior no tapa un error del borde', () => {
  // Si la consulta falla, el fallo de un cuadro anterior ya no dice nada: se
  // muestra el error de la consulta, no el del navegador.
  const error = interpretarPrevia(502, null)
  assert.equal(estadoDelPanel(error, 2, 1).tipo, 'error')
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
  const flujo = new ReadableStream({
    start(controlador) {
      setTimeout(() => controlador.error(new DOMException('La operación fue abortada.', 'AbortError')), 30)
    },
  })
  const respuesta = {
    status: 200,
    headers: new Headers({ 'Content-Type': 'image/jpeg' }),
    body: flujo,
  }
  await conFetch(async () => respuesta, async () => {
    await assert.rejects(
      () => pedirPrevia(new AbortController().signal, 5),
      (error) => error instanceof ErrorPrevia && /tardó demasiado/.test(error.message),
    )
  })
})

test('pedirPrevia: la petición no sigue redirecciones', async () => {
  // Con la política por defecto, un 3xx haría que el navegador del profesor
  // pidiera a un destino elegido por quien lo mande. El borde nunca redirige.
  let opciones = null
  await conFetch(
    async (_ruta, opcionesRecibidas) => {
      opciones = opcionesRecibidas
      return new Response(new Uint8Array([0xff, 0xd8, 0xff, 0xe0]), {
        status: 200,
        headers: { 'Content-Type': 'image/jpeg' },
      })
    },
    () => pedirPrevia(new AbortController().signal),
  )
  assert.equal(opciones.redirect, 'error')
  assert.equal(opciones.cache, 'no-store')
})

test('pedirPrevia: un 3xx se trata como error, no como un cuadro', async () => {
  // Con `redirect: 'error'` el navegador ni siquiera sigue la redirección; si
  // un 3xx llega de todos modos al cliente, es un error del borde, no un cuadro.
  const consulta = await conFetch(
    async () =>
      new Response(new Uint8Array([0xff, 0xd8, 0xff, 0xe0]), {
        status: 302,
        headers: { 'Content-Type': 'image/jpeg' },
      }),
    () => pedirPrevia(new AbortController().signal),
  )
  assert.equal(consulta.resultado.tipo, 'error')
  assert.match(consulta.resultado.mensaje, /302/)
  assert.equal(consulta.imagenUrl, null)
})

test('pedirPrevia: un Content-Length por encima del tope se rechaza sin leer el cuerpo', async () => {
  await conFetch(
    async () =>
      new Response(new Uint8Array([0xff, 0xd8, 0xff, 0xe0]), {
        status: 200,
        headers: {
          'Content-Type': 'image/jpeg',
          'Content-Length': String(TAMANO_MAXIMO_CUERPO + 1),
        },
      }),
    async () => {
      await assert.rejects(
        () => pedirPrevia(new AbortController().signal),
        (error) => error instanceof ErrorPrevia && /demasiado grande/.test(error.message),
      )
    },
  )
})

test('pedirPrevia: sin Content-Length, el cuerpo se lee con tope y se corta al pasarse', async () => {
  // La cabecera no está: el tamaño se comprueba leyendo el flujo, con un tope
  // explícito, para que un cuerpo enorme no quede entero en memoria.
  const trozo = new Uint8Array(600 * 1024)
  const flujo = new ReadableStream({
    start(controlador) {
      controlador.enqueue(trozo)
      controlador.enqueue(trozo)
      controlador.close()
    },
  })
  await conFetch(
    async () => new Response(flujo, { status: 200, headers: { 'Content-Type': 'image/jpeg' } }),
    async () => {
      await assert.rejects(
        () => pedirPrevia(new AbortController().signal),
        (error) => error instanceof ErrorPrevia && /demasiado grande/.test(error.message),
      )
    },
  )
})

test('pedirPrevia: un Content-Length dentro del tope deja pasar el cuadro', async () => {
  const cuerpo = new Uint8Array([0xff, 0xd8, 0xff, 0xe0])
  const consulta = await conFetch(
    async () =>
      new Response(cuerpo, {
        status: 200,
        headers: { 'Content-Type': 'image/jpeg', 'Content-Length': String(cuerpo.length) },
      }),
    () => pedirPrevia(new AbortController().signal),
  )
  assert.equal(consulta.resultado.tipo, 'cuadro')
  assert.match(consulta.imagenUrl, /^data:image\/jpeg;base64,/)
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
