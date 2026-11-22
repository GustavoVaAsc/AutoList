// Pruebas de la lógica pura de la vista Alumnos (src/api/alumnosRespuestas.ts):
// interpretación de las respuestas de la API, validación local del archivo y
// resúmenes. Los cuerpos de ejemplo siguen la tabla de respuestas de
// orangepi/README.md (POST /api/alumnos/importar); son entradas de prueba del
// intérprete, no datos de la API real.
// Sin dependencias, desde la raíz del repo: node --test web/pruebas/*.test.js
// (Node 22.18 o posterior carga el .ts quitando los tipos).
import test from 'node:test'
import assert from 'node:assert/strict'
import { assertTextoLegible, assertTextosDistintos } from './texto.js'
import {
  cuentasConRostroCambiado,
  gruposDe,
  interpretarRespuesta,
  leerAlumnos,
  MAX_BYTES,
  resumirConteos,
  validarArchivoLocal,
} from '../src/api/alumnosRespuestas.ts'

const conteos = { insertadas: 3, actualizadas: 1, omitidas: 2, total: 6 }

test('200: éxito con los cuatro conteos', () => {
  assert.deepEqual(interpretarRespuesta(200, { ...conteos }), { tipo: 'exito', conteos })
})

test('200 sin conteos válidos no se toma por éxito', () => {
  for (const cuerpo of [undefined, null, {}, { ...conteos, total: '6' }, { ...conteos, omitidas: -1 }, { ...conteos, insertadas: 1.5 }]) {
    assert.equal(interpretarRespuesta(200, cuerpo).tipo, 'fallo')
  }
})

test('400: filas inválidas con su línea y motivo, y el total aunque se listen menos', () => {
  const r = interpretarRespuesta(400, {
    error: 'El archivo tiene filas inválidas: no se importó nada',
    total_errores: 130,
    filas: [
      { fila: 3, error: 'nombre vacío' },
      { fila: 9, error: 'no_cuenta debe tener exactamente 9 dígitos' },
    ],
  })
  assert.deepEqual(r, {
    tipo: 'filas-invalidas',
    error: 'El archivo tiene filas inválidas: no se importó nada',
    totalErrores: 130,
    filas: [
      { fila: 3, error: 'nombre vacío' },
      { fila: 9, error: 'no_cuenta debe tener exactamente 9 dígitos' },
    ],
  })
})

test('400 sin filas (encabezado, UTF-8, existentes inválido) conserva el mensaje del servidor', () => {
  const r = interpretarRespuesta(400, { error: 'El archivo no es UTF-8 válido: guárdalo como CSV UTF-8' })
  assert.equal(r.tipo, 'filas-invalidas')
  assert.equal(r.error, 'El archivo no es UTF-8 válido: guárdalo como CSV UTF-8')
  assert.deepEqual(r.filas, [])
  assert.equal(r.totalErrores, 0)
})

test('400 ignora filas mal formadas y nunca baja el total por debajo de las listadas', () => {
  const r = interpretarRespuesta(400, { total_errores: 1, filas: [{ fila: 2, error: 'a' }, { fila: 'x', error: 'b' }, null, { fila: 4, error: 'c' }] })
  assert.deepEqual(r.filas, [{ fila: 2, error: 'a' }, { fila: 4, error: 'c' }])
  assert.equal(r.totalErrores, 2)
})

test('400 con un cuerpo ilegible da un mensaje propio y no revienta', () => {
  const r = interpretarRespuesta(400, undefined)
  assert.equal(r.tipo, 'filas-invalidas')
  assertTextoLegible(r.error)
})

const existente = {
  cuenta: '320068234',
  actual: { nombre: 'Ana Pérez', grupo: '3CM1' },
  csv: { nombre: 'Ana Pérez López', grupo: '3CM1' },
  cambia: true,
  tiene_rostro: true,
}

test('409 con escrito:false trae las cuentas existentes y no cuenta como escrito', () => {
  const r = interpretarRespuesta(409, { escrito: false, error: 'Hay cuentas', existentes: [existente], opciones: ['actualizar', 'omitir'] })
  assert.deepEqual(r, {
    tipo: 'existentes',
    error: 'Hay cuentas',
    existentes: [
      {
        cuenta: '320068234',
        actual: { nombre: 'Ana Pérez', grupo: '3CM1' },
        csv: { nombre: 'Ana Pérez López', grupo: '3CM1' },
        cambia: true,
        tieneRostro: true,
      },
    ],
  })
})

test('409 con escrito:true es la carrera: las filas nuevas ya se escribieron', () => {
  const r = interpretarRespuesta(409, { escrito: true, error: 'Otra importación', ...conteos })
  assert.deepEqual(r, { tipo: 'carrera', error: 'Otra importación', conteos })
})

test('409 no se adivina: sin escrito booleano, sin existentes o con una existente rota es un fallo', () => {
  const malos = [
    undefined,
    {},
    { error: 'x', existentes: [existente] },
    { escrito: 'false', existentes: [existente] },
    { escrito: false },
    { escrito: false, existentes: [] },
    { escrito: false, existentes: [{ ...existente, cambia: 'sí' }] },
    { escrito: false, existentes: [{ ...existente, csv: { nombre: 'x' } }] },
    { escrito: true, error: 'x', insertadas: 1 },
  ]
  for (const cuerpo of malos) {
    assert.equal(interpretarRespuesta(409, cuerpo).tipo, 'fallo', JSON.stringify(cuerpo))
  }
})

test('413, 415, 401 y 403 dan un fallo con su código y un mensaje propio, legible y que no repite el texto del servidor', () => {
  const mensajes = []
  for (const status of [401, 403, 413, 415]) {
    const r = interpretarRespuesta(status, { error: 'texto del servidor' })
    assert.equal(r.tipo, 'fallo')
    assert.equal(r.status, status)
    assert.ok(!r.mensaje.includes('texto del servidor'))
    mensajes.push(r.mensaje)
  }
  assertTextosDistintos(mensajes)
  assert.ok(interpretarRespuesta(413, undefined).mensaje.includes('256'), 'dice el tope')
  assert.ok(interpretarRespuesta(415, undefined).mensaje.includes('CSV'), 'dice qué tipo se acepta')
  assert.ok(!interpretarRespuesta(415, undefined).mensaje.includes('256'), '413 y 415 no se intercambian')
})

test('otro código (411, 500) da un fallo legible; el código va en `status` y no en el texto', () => {
  for (const status of [411, 500, 502]) {
    const r = interpretarRespuesta(status, undefined)
    assert.equal(r.tipo, 'fallo')
    assert.equal(r.status, status)
    assertTextoLegible(r.mensaje)
    assert.ok(!r.mensaje.includes(String(status)))
  }
})

test('validarArchivoLocal: pasa un CSV normal, con y sin tipo declarado', () => {
  assert.equal(validarArchivoLocal({ name: 'lista.csv', size: 1200, type: 'text/csv' }), null)
  assert.equal(validarArchivoLocal({ name: 'lista.CSV', size: 1200, type: 'application/vnd.ms-excel' }), null)
  assert.equal(validarArchivoLocal({ name: 'lista.csv', size: 1200, type: '' }), null)
  assert.equal(validarArchivoLocal({ name: 'lista', size: 1200, type: 'text/plain' }), null)
})

test('validarArchivoLocal: con extensión .csv o .txt pasa cualquier tipo declarado, incluido octet-stream', () => {
  for (const type of ['application/octet-stream', 'application/csv', 'application/vnd.ms-excel', 'image/png', 'weird/type']) {
    assert.equal(validarArchivoLocal({ name: 'lista.csv', size: 1200, type }), null, type)
    assert.equal(validarArchivoLocal({ name: 'LISTA.TXT', size: 1200, type }), null, type)
  }
})

test('validarArchivoLocal: el tope es 256 KiB exactos', () => {
  assert.equal(validarArchivoLocal({ name: 'a.csv', size: MAX_BYTES, type: 'text/csv' }), null)
  assert.ok(validarArchivoLocal({ name: 'a.csv', size: MAX_BYTES + 1, type: 'text/csv' }).includes('256'), 'dice el tope')
})

test('validarArchivoLocal: rechaza vacíos y lo que no es texto', () => {
  assertTextoLegible(validarArchivoLocal({ name: 'a.csv', size: 0, type: 'text/csv' }))
  const noTexto = [
    { name: 'foto.png', size: 10, type: 'image/png' },
    { name: 'foto.csv.png', size: 10, type: 'image/png' },
    { name: 'lista.csv.octet', size: 10, type: 'application/octet-stream' },
    { name: 'lista', size: 10, type: 'application/octet-stream' },
    { name: 'lista.xlsx', size: 10, type: 'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet' },
    { name: 'lista.xls', size: 10, type: 'application/vnd.ms-excel' },
    { name: 'lista', size: 10, type: '' },
    { name: 'datos.pdf', size: 10, type: 'application/pdf' },
  ]
  for (const archivo of noTexto) {
    assertTextoLegible(validarArchivoLocal(archivo), archivo.name)
  }
})

test('resumirConteos: solo lo que no es cero, con plural y la "y" final', () => {
  assert.equal(resumirConteos(conteos), '3 nuevas, 1 actualizada y 2 omitidas')
  assert.equal(resumirConteos({ insertadas: 1, actualizadas: 0, omitidas: 0, total: 1 }), '1 nueva')
  assert.equal(resumirConteos({ insertadas: 0, actualizadas: 2, omitidas: 1, total: 3 }), '2 actualizadas y 1 omitida')
  assert.equal(resumirConteos({ insertadas: 0, actualizadas: 0, omitidas: 0, total: 1 }), 'Sin cambios')
})

const lista = [
  { cuenta: '320068234', nombre: 'Ana Pérez', grupo: '3CM1', rostro: 'registrado' },
  { cuenta: '320068235', nombre: 'Luis Ñandú', grupo: '3CM2', rostro: 'pendiente' },
]

test('leerAlumnos: acepta la forma documentada', () => {
  assert.deepEqual(leerAlumnos(lista), { ok: true, alumnos: lista })
  assert.deepEqual(leerAlumnos([]), { ok: true, alumnos: [] })
})

test('leerAlumnos: no conserva campos de más (por ejemplo, un embedding)', () => {
  const r = leerAlumnos([{ ...lista[0], embedding: [0.1, 0.2], consentimiento: true }])
  assert.equal(r.ok, true)
  assert.deepEqual(Object.keys(r.alumnos[0]).sort(), ['cuenta', 'grupo', 'nombre', 'rostro'])
})

test('leerAlumnos: rechaza lo que no es una lista o trae un alumno roto, y dice cuál', () => {
  assert.equal(leerAlumnos({ alumnos: [] }).ok, false)
  assert.equal(leerAlumnos(null).ok, false)
  const roto = leerAlumnos([lista[0], { ...lista[1], rostro: 'raro' }])
  assert.equal(roto.ok, false)
  assert.match(roto.motivo, /\b2\b/, 'dice cuál alumno')
  assert.equal(leerAlumnos([{ ...lista[0], cuenta: 320068234 }]).ok, false)
})

test('leerAlumnos: un nombre con marcas o HTML se conserva como texto, sin interpretarlo', () => {
  const nombre = '<img src=x onerror=alert(1)> & "Ñ"'
  const r = leerAlumnos([{ ...lista[0], nombre }])
  assert.equal(r.alumnos[0].nombre, nombre)
})

test('gruposDe: distintos y en orden alfabético', () => {
  assert.deepEqual(gruposDe([...lista, { ...lista[0], cuenta: '1', grupo: '3CM1' }, { ...lista[0], cuenta: '2', grupo: '10A' }]), ['10A', '3CM1', '3CM2'])
  assert.deepEqual(gruposDe([]), [])
})

test('cuentasConRostroCambiado: solo las que siguen en la lista y cambiaron de estado', () => {
  const despues = [
    { ...lista[0], rostro: 'pendiente' },
    { ...lista[1], rostro: 'pendiente' },
    { cuenta: '320068999', nombre: 'Nueva', grupo: '3CM1', rostro: 'registrado' },
  ]
  assert.deepEqual(cuentasConRostroCambiado(lista, despues), ['320068234'])
  assert.deepEqual(cuentasConRostroCambiado(lista, lista), [])
  assert.deepEqual(cuentasConRostroCambiado([], lista), [])
})
