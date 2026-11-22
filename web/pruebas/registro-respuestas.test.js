// Pruebas de la lógica pura de Registro y de la búsqueda de Alumnos
// (src/api/registroRespuestas.ts): lectura del estado del registro, traducción de
// poses y motivos, interpretación de las respuestas de iniciar, cancelar y retirar
// el rostro, búsqueda de alumnos y la máquina de la vista. Los cuerpos de ejemplo
// siguen el contrato de docs/HIGH-LEVEL-PLAN.md (sección 6.7); son entradas de
// prueba del intérprete, no datos de la API real.
// Sin dependencias, desde la raíz del repo: node --test web/pruebas/*.test.js
// (Node 22.18 o posterior carga el .ts quitando los tipos).
import test from 'node:test'
import assert from 'node:assert/strict'
import { assertTextoLegible, assertTextosDistintos } from './texto.js'
import {
  AVISO_REGISTRO_PERDIDO,
  ETIQUETA_POSE,
  POSES,
  avisoDeCancelacion,
  avisoDeInicio,
  capturaVisible,
  filtrarAlumnos,
  formatearTranscurrido,
  interpretarCancelacion,
  interpretarInicio,
  interpretarRetiro,
  leerRegistro,
  marcaDePose,
  normalizarBusqueda,
  ordenarParaRegistro,
  poseEnCurso,
  SEGUIMIENTO_INICIAL,
  seguirCaptura,
  resumirRegistro,
  textoCaptura,
  textoTiempo,
  siguienteVista,
  textoPose,
  traducirMotivo,
  VISTA_INICIAL,
} from '../src/api/registroRespuestas.ts'

const INICIO = '2026-11-04T15:00:00.000Z'
const ACTUALIZADO = '2026-11-04T15:00:07.000Z'

function cuerpo(extra = {}) {
  return {
    abierto: true,
    cuenta: '320068234',
    nombre: 'Ana López',
    estado: 'en_curso',
    motivo: null,
    poses: POSES.map((pose) => ({ pose, estado: 'pendiente', motivo: null })),
    advertencia: null,
    inicio: INICIO,
    actualizado: ACTUALIZADO,
    ...extra,
  }
}

function registro(extra = {}) {
  const r = leerRegistro(cuerpo(extra))
  assert.equal(r.ok, true, JSON.stringify(r))
  return r.registro
}

// ------------------------------------------------------------ leerRegistro

test('leerRegistro: copia los campos conocidos y convierte las fechas a milisegundos', () => {
  const r = registro()
  assert.equal(r.abierto, true)
  assert.equal(r.cuenta, '320068234')
  assert.equal(r.nombre, 'Ana López')
  assert.equal(r.estado, 'en_curso')
  assert.equal(r.motivo, null)
  assert.equal(r.inicio, Date.parse(INICIO))
  assert.equal(r.actualizado, Date.parse(ACTUALIZADO))
  assert.deepEqual(r.poses.map((p) => p.pose), [...POSES])
  assert.equal(r.advertencia, null)
})

test('leerRegistro: sin ningún registro (sin_registro) es válido, con las 5 poses pendientes', () => {
  const r = registro({ abierto: false, cuenta: null, nombre: null, estado: 'sin_registro', poses: [], inicio: null, actualizado: null })
  assert.equal(r.abierto, false)
  assert.equal(r.cuenta, null)
  assert.equal(r.inicio, null)
  assert.equal(r.poses.length, 5)
  assert.ok(r.poses.every((p) => p.estado === 'pendiente'))
})

test('leerRegistro: completa las poses que falten y conserva su orden', () => {
  const r = registro({ poses: [{ pose: 'arriba', estado: 'aceptada', motivo: null }, { pose: 'frente', estado: 'rechazada', motivo: 'borrosa' }] })
  assert.deepEqual(r.poses.map((p) => [p.pose, p.estado, p.motivo]), [
    ['frente', 'rechazada', 'borrosa'],
    ['izquierda', 'pendiente', null],
    ['derecha', 'pendiente', null],
    ['arriba', 'aceptada', null],
    ['frente_2', 'pendiente', null],
  ])
})

test('leerRegistro: la advertencia guarda solo la cuenta parecida', () => {
  const r = registro({ advertencia: { parecido_a: '320068235', embedding: [1, 2, 3], miniatura: 'x' } })
  assert.deepEqual(r.advertencia, { parecidoA: '320068235' })
  assert.deepEqual(registro({ advertencia: { parecido_a: null } }).advertencia, { parecidoA: null })
})

test('leerRegistro: nunca conserva embeddings ni campos de más, aunque la API los mandara', () => {
  const r = registro({ embedding: [0.1], miniatura: 'BASE64', poses: POSES.map((pose) => ({ pose, estado: 'aceptada', motivo: null })) })
  assert.ok(!('embedding' in r))
  assert.equal(r.miniatura, undefined)
  assert.ok(!JSON.stringify(r.poses[0]).includes('BASE64'))
})

test('leerRegistro: la miniatura de la pose se conserva tal cual (es lo que se muestra)', () => {
  const r = registro({ poses: POSES.map((pose) => ({ pose, estado: 'aceptada', motivo: null, miniatura: 'BASE64POSE' })) })
  assert.deepEqual(r.poses.map((p) => p.miniatura), POSES.map(() => 'BASE64POSE'))
})

test('leerRegistro: sin miniatura la pose queda sin ella (no es un error)', () => {
  const r = registro()
  assert.ok(r.poses.every((p) => p.miniatura === null))
  const sinClave = registro({ poses: POSES.map((pose) => ({ pose, estado: 'aceptada', motivo: null })) })
  assert.ok(sinClave.poses.every((p) => p.miniatura === null))
})

test('leerRegistro: una miniatura mal formada o fuera de tamaño se ignora, sin rechazar el estado', () => {
  const malas = [5, {}, [], 'no es base64!', '', 'A'.repeat(12289), 'con_guion_bajo']
  for (const mala of malas) {
    const res = leerRegistro(cuerpo({ poses: POSES.map((pose) => ({ pose, estado: 'aceptada', motivo: null, miniatura: mala })) }))
    assert.equal(res.ok, true, JSON.stringify(mala))
    assert.ok(res.registro.poses.every((p) => p.miniatura === null), JSON.stringify(mala))
  }
})

test('leerRegistro: rechaza formas que no son las del contrato', () => {
  const malos = [
    undefined, null, [], 'texto', 7,
    cuerpo({ abierto: 'si' }),
    cuerpo({ estado: 'bailando' }),
    cuerpo({ estado: undefined }),
    cuerpo({ cuenta: 320068234 }),
    cuerpo({ nombre: 5 }),
    cuerpo({ motivo: 5 }),
    cuerpo({ poses: 'no' }),
    cuerpo({ poses: [{ pose: 'saltar', estado: 'aceptada', motivo: null }] }),
    cuerpo({ poses: [{ pose: 'frente', estado: 'bailando', motivo: null }] }),
    cuerpo({ poses: [{ pose: 'frente', estado: 'aceptada', motivo: 3 }] }),
    cuerpo({ poses: [null] }),
    cuerpo({ advertencia: 'x' }),
    cuerpo({ advertencia: { parecido_a: 5 } }),
    cuerpo({ inicio: 'ayer' }),
    cuerpo({ inicio: 5 }),
    cuerpo({ actualizado: undefined }),
  ]
  for (const malo of malos) {
    const r = leerRegistro(malo)
    assert.equal(r.ok, false, JSON.stringify(malo))
    assert.equal(typeof r.motivo, 'string')
  }
})

test('leerRegistro: un motivo desconocido pasa tal cual (se traduce con un texto de reserva)', () => {
  assert.equal(registro({ estado: 'error', motivo: 'algo_nuevo' }).motivo, 'algo_nuevo')
})

// ------------------------------------------------------------ textos

test('traducirMotivo: traduce cada código del contrato a español claro, sin código ni rayas largas', () => {
  const codigos = [
    'sin_rostro', 'varios_rostros', 'rostro_chico', 'borrosa', 'giro_excesivo', 'pose_incorrecta',
    'fallo_captura', 'ocupado', 'inconsistente', 'npu', 'duplicado', 'galeria_no_disponible', 'cuenta_invalida',
    'vencido', 'no_guardado', 'embedding_invalido', 'desconocido',
  ]
  const vistos = new Set()
  for (const codigo of codigos) {
    const texto = traducirMotivo(codigo)
    assert.ok(texto.length > 10, codigo)
    if (codigo.includes('_')) {
      assert.ok(!texto.includes(codigo), 'no debe mostrar el código: ' + codigo)
    }
    assertTextoLegible(texto, codigo)
    assert.ok(/[.]$/.test(texto), 'termina en punto: ' + codigo)
    vistos.add(texto)
  }
  assert.equal(vistos.size, codigos.length, 'cada código tiene su propio texto')
})

test('traducirMotivo: sin motivo da texto vacío; un código nuevo da el texto de reserva, sin mostrar el código', () => {
  assert.equal(traducirMotivo(null), '')
  const reserva = traducirMotivo('algo_nuevo')
  assertTextoLegible(reserva)
  assert.ok(!reserva.includes('algo_nuevo'))
  assert.equal(reserva, traducirMotivo('desconocido'))
})

test('textoPose: cada estado de pose se dice con su propio texto; solo la foto rechazada lleva el motivo traducido', () => {
  const estados = ['pendiente', 'esperando_pose', 'capturando', 'aceptada', 'rechazada']
  const textos = estados.map((estado) => {
    const t = textoPose({ pose: 'frente', estado, motivo: null })
    assert.equal(t.detalle, null, estado)
    return t.estado
  })
  assertTextosDistintos(textos)
  assert.equal(textoPose({ pose: 'frente', estado: 'rechazada', motivo: 'borrosa' }).detalle, traducirMotivo('borrosa'))
  assert.equal(textoPose({ pose: 'frente', estado: 'aceptada', motivo: 'borrosa' }).detalle, null)
  assert.equal(textoPose({ pose: 'frente', estado: 'rechazada', motivo: null }).detalle, null)
})

test('marcaDePose: la palomita solo para la foto aceptada, la tachita para la rechazada', () => {
  assert.equal(marcaDePose('aceptada'), 'presente')
  assert.equal(marcaDePose('rechazada'), 'falta')
  for (const e of ['pendiente', 'esperando_pose', 'capturando']) {
    assert.equal(marcaDePose(e), 'pendiente')
  }
})

test('formatearTranscurrido: segundos, minutos y límites', () => {
  assert.equal(formatearTranscurrido(0), '0 s')
  assert.equal(formatearTranscurrido(999), '0 s')
  assert.equal(formatearTranscurrido(12_400), '12 s')
  assert.equal(formatearTranscurrido(59_999), '59 s')
  assert.equal(formatearTranscurrido(60_000), '1 min 0 s')
  assert.equal(formatearTranscurrido(125_000), '2 min 5 s')
  assert.equal(formatearTranscurrido(-5000), '0 s')
  assert.equal(formatearTranscurrido(NaN), '0 s')
})

test('poseEnCurso: la primera pose sin aceptar, o null si ya están las cinco', () => {
  assert.equal(poseEnCurso(registro()), 0)
  const dos = POSES.map((pose, i) => ({ pose, estado: i < 2 ? 'aceptada' : 'pendiente', motivo: null }))
  assert.equal(poseEnCurso(registro({ poses: dos })), 2)
  assert.equal(poseEnCurso(registro({ poses: POSES.map((pose) => ({ pose, estado: 'aceptada', motivo: null })) })), null)
})

test('resumirRegistro: esperando a la visión dice la verdad y el tiempo transcurrido, sin simular progreso', () => {
  const r = resumirRegistro(registro({ estado: 'esperando_vision' }))
  assertTextoLegible(r.titulo)
  assertTextoLegible(r.detalle)
  assert.equal(r.tono, 'advertencia')
  assert.ok(!/\d+ s\b/.test(r.detalle), 'el tiempo no va en el texto de la región viva')
  assert.ok(textoTiempo(registro({ estado: 'esperando_vision' }), 12_000).includes(formatearTranscurrido(12_000)))
  assert.equal(textoTiempo(registro({ estado: 'esperando_vision' }), null), null)
})

test('textoTiempo: solo con un registro abierto en espera, en curso o guardando', () => {
  assert.ok(textoTiempo(registro(), 65_000).includes(formatearTranscurrido(65_000)))
  assert.ok(textoTiempo(registro({ estado: 'guardando' }), 3000).includes(formatearTranscurrido(3000)))
  for (const estado of ['completado', 'cancelado', 'error']) {
    assert.equal(textoTiempo(registro({ abierto: false, estado }), 3000), null, estado)
  }
})

test('resumirRegistro: en curso nombra la pose y su número', () => {
  const poses = POSES.map((pose, i) => ({ pose, estado: i === 0 ? 'aceptada' : i === 1 ? 'capturando' : 'pendiente', motivo: null }))
  const r = resumirRegistro(registro({ poses }))
  assertTextoLegible(r.titulo)
  assert.equal(r.tono, 'neutro')
  assert.match(r.detalle, /\b2 de 5\b/, 'dice el número de la pose')
  assert.ok(r.detalle.includes(ETIQUETA_POSE.izquierda.toLowerCase()), 'nombra la pose como la lista')
})

test('resumirRegistro: guardando, completado, cancelado y error', () => {
  const guardando = resumirRegistro(registro({ estado: 'guardando' }))
  assertTextoLegible(guardando.titulo)
  assert.equal(guardando.tono, 'neutro')
  const completado = resumirRegistro(registro({ abierto: false, estado: 'completado' }))
  assertTextoLegible(completado.titulo)
  assert.equal(completado.tono, 'exito')
  assert.match(completado.detalle, /Ana López/)
  const cancelado = resumirRegistro(registro({ abierto: false, estado: 'cancelado' }))
  assertTextoLegible(cancelado.titulo)
  assert.equal(cancelado.tono, 'neutro')
  const error = resumirRegistro(registro({ abierto: false, estado: 'error', motivo: 'inconsistente' }))
  assertTextoLegible(error.titulo)
  assert.equal(error.tono, 'error')
  assert.ok(error.detalle.includes(traducirMotivo('inconsistente')), 'el detalle trae el motivo traducido')
  assertTextosDistintos([guardando.titulo, completado.titulo, cancelado.titulo, error.titulo])
})

// ------------------------------------------------------------ interpretar respuestas

test('interpretarInicio: 202 inicia; 404, 409 y los demás tienen su resultado', () => {
  assert.deepEqual(interpretarInicio(202, { cuenta: '320068234', nombre: 'Ana', abierto: true }), { tipo: 'iniciado' })
  assert.deepEqual(interpretarInicio(404, { error: 'x' }), { tipo: 'no-existe' })
  assert.deepEqual(interpretarInicio(409, { error: 'x', motivo: 'tiene_rostro' }), { tipo: 'tiene-rostro' })
  assert.deepEqual(interpretarInicio(409, { error: 'x', motivo: 'registro_abierto' }), { tipo: 'ocupado' })
  assert.deepEqual(interpretarInicio(409, { error: 'x', motivo: 'interrumpido' }), { tipo: 'interrumpido' })
  assert.deepEqual(interpretarInicio(409, { error: 'x', motivo: 'prueba_pendiente' }), { tipo: 'prueba-en-curso' })
})

test('interpretarInicio: 409 sin motivo conocido, y 400, 401, 403, 415, 500 son fallos con texto en español', () => {
  for (const [status, cuerpoRespuesta] of [[409, {}], [409, undefined], [400, { error: 'x' }], [401, undefined], [403, undefined], [415, undefined], [500, undefined], [502, 'html']]) {
    const r = interpretarInicio(status, cuerpoRespuesta)
    assert.equal(r.tipo, 'fallo', String(status))
    assert.equal(r.status, status)
    assertTextoLegible(r.mensaje, String(status))
  }
  assertTextosDistintos([401, 403, 500].map((status) => interpretarInicio(status, undefined).mensaje))
})

test('interpretarCancelacion: 200 dice si había un registro; lo demás es un fallo', () => {
  assert.deepEqual(interpretarCancelacion(200, { cancelado: true, abierto: false }), { tipo: 'cancelado', habia: true })
  assert.deepEqual(interpretarCancelacion(200, { cancelado: false, abierto: false }), { tipo: 'cancelado', habia: false })
  assert.equal(interpretarCancelacion(200, undefined).tipo, 'fallo')
  assert.equal(interpretarCancelacion(500, undefined).tipo, 'fallo')
  assert.equal(interpretarCancelacion(401, undefined).tipo, 'fallo')
})

test('interpretarRetiro: 200 con o sin rostro previo, 404, 409 y fallos', () => {
  assert.deepEqual(interpretarRetiro(200, { cuenta: '320068234', rostro: 'pendiente', retirado: true }), { tipo: 'retirado', habiaRostro: true })
  assert.deepEqual(interpretarRetiro(200, { cuenta: '320068234', rostro: 'pendiente', retirado: false }), { tipo: 'retirado', habiaRostro: false })
  assert.equal(interpretarRetiro(200, {}).tipo, 'fallo')
  assert.deepEqual(interpretarRetiro(404, { error: 'x' }), { tipo: 'no-existe' })
  assert.deepEqual(interpretarRetiro(409, { error: 'x', motivo: 'registro_abierto' }), { tipo: 'registro-abierto' })
  for (const status of [400, 401, 403, 500]) {
    const r = interpretarRetiro(status, undefined)
    assert.equal(r.tipo, 'fallo')
    assert.equal(r.status, status)
  }
})

test('avisoDeInicio: iniciado no avisa; cada resultado de error tiene título y texto, y ninguno usa rayas largas', () => {
  assert.equal(avisoDeInicio({ tipo: 'iniciado' }), null)
  const resultados = [
    { tipo: 'no-existe' },
    { tipo: 'tiene-rostro' },
    { tipo: 'ocupado' },
    { tipo: 'interrumpido' },
    { tipo: 'prueba-en-curso' },
    { tipo: 'fallo', status: 500, mensaje: 'La placa no pudo atender la petición.' },
  ]
  for (const r of resultados) {
    const aviso = avisoDeInicio(r)
    assert.ok(aviso, r.tipo)
    assertTextoLegible(aviso.titulo, r.tipo)
    assertTextoLegible(aviso.texto, r.tipo)
    assert.ok(aviso.tono === 'error' || aviso.tono === 'advertencia')
  }
  assert.ok(avisoDeInicio({ tipo: 'tiene-rostro' }).texto.includes('Alumnos'), 'dice dónde retirar el rostro')
  assert.equal(avisoDeInicio({ tipo: 'ocupado' }).tono, 'advertencia')
  assert.equal(avisoDeInicio({ tipo: 'prueba-en-curso' }).tono, 'advertencia')
  assert.equal(avisoDeInicio({ tipo: 'fallo', status: null, mensaje: 'sin red' }).texto, 'sin red')
})

test('avisoDeCancelacion: solo el fallo avisa', () => {
  assert.equal(avisoDeCancelacion({ tipo: 'cancelado', habia: true }), null)
  assert.equal(avisoDeCancelacion({ tipo: 'cancelado', habia: false }), null)
  assert.equal(avisoDeCancelacion({ tipo: 'fallo', status: 500, mensaje: 'x' }).tono, 'error')
})

// ------------------------------------------------------------ búsqueda de alumnos

const ALUMNOS = [
  { cuenta: '320068234', nombre: 'Ana María López', grupo: '3CM1', rostro: 'registrado' },
  { cuenta: '320068235', nombre: 'Luis Ñandú', grupo: '3CM1', rostro: 'pendiente' },
  { cuenta: '419000111', nombre: 'Zoé Pérez', grupo: '3CM2', rostro: 'pendiente' },
  { cuenta: '419000112', nombre: 'Álvaro Núñez', grupo: '3CM2', rostro: 'registrado' },
]

test('normalizarBusqueda: sin acentos, en minúsculas y sin espacios en los extremos', () => {
  assert.equal(normalizarBusqueda('  ÁLVARO  Núñez '), 'alvaro nunez')
  assert.equal(normalizarBusqueda('Ñandú'), 'nandu')
  assert.equal(normalizarBusqueda(''), '')
})

test('filtrarAlumnos: por nombre, sin importar acentos ni mayúsculas', () => {
  assert.deepEqual(filtrarAlumnos(ALUMNOS, 'alvaro').map((a) => a.cuenta), ['419000112'])
  assert.deepEqual(filtrarAlumnos(ALUMNOS, 'ÑANDU').map((a) => a.cuenta), ['320068235'])
  assert.deepEqual(filtrarAlumnos(ALUMNOS, 'maria').map((a) => a.cuenta), ['320068234'])
  assert.deepEqual(filtrarAlumnos(ALUMNOS, 'zoe perez').map((a) => a.cuenta), ['419000111'])
})

test('filtrarAlumnos: por cuenta completa o parcial', () => {
  assert.deepEqual(filtrarAlumnos(ALUMNOS, '320068235').map((a) => a.cuenta), ['320068235'])
  assert.deepEqual(filtrarAlumnos(ALUMNOS, '4190001').map((a) => a.cuenta), ['419000111', '419000112'])
})

test('filtrarAlumnos: varias palabras deben coincidir todas, en cualquier orden', () => {
  assert.deepEqual(filtrarAlumnos(ALUMNOS, 'lopez ana').map((a) => a.cuenta), ['320068234'])
  assert.deepEqual(filtrarAlumnos(ALUMNOS, 'ana nunez'), [])
})

test('filtrarAlumnos: búsqueda vacía o de espacios devuelve la lista tal cual; sin coincidencias, una lista vacía', () => {
  assert.equal(filtrarAlumnos(ALUMNOS, ''), ALUMNOS)
  assert.equal(filtrarAlumnos(ALUMNOS, '   '), ALUMNOS)
  assert.deepEqual(filtrarAlumnos(ALUMNOS, 'xyz'), [])
})

test('filtrarAlumnos: el texto se toma literal (sin expresiones regulares)', () => {
  assert.deepEqual(filtrarAlumnos(ALUMNOS, '.*'), [])
  assert.deepEqual(filtrarAlumnos(ALUMNOS, '('), [])
})

test('ordenarParaRegistro: los pendientes primero, luego grupo, nombre y cuenta; no modifica la entrada', () => {
  const copia = ALUMNOS.map((a) => ({ ...a }))
  const orden = ordenarParaRegistro(copia)
  assert.deepEqual(orden.map((a) => a.cuenta), ['320068235', '419000111', '320068234', '419000112'])
  assert.deepEqual(copia, ALUMNOS)
})

// ------------------------------------------------------------ máquina de la vista

test('siguienteVista: la primera consulta sin registro abierto deja la vista libre', () => {
  const v = siguienteVista(VISTA_INICIAL, { tipo: 'estado', registro: registro({ abierto: false, estado: 'sin_registro', cuenta: null, nombre: null, poses: [], inicio: null, actualizado: null }) })
  assert.equal(v.fase, 'libre')
  assert.equal(v.registro, null)
})

test('siguienteVista: un registro abierto al cargar se retoma', () => {
  const abierto = registro()
  const v = siguienteVista(VISTA_INICIAL, { tipo: 'estado', registro: abierto })
  assert.equal(v.fase, 'abierto')
  assert.equal(v.registro, abierto)
})

test('siguienteVista: un registro ya cerrado al cargar no se muestra (puede ser de hace horas)', () => {
  const v = siguienteVista(VISTA_INICIAL, { tipo: 'estado', registro: registro({ abierto: false, estado: 'completado' }) })
  assert.equal(v.fase, 'libre')
  assert.equal(v.registro, null)
})

test('siguienteVista: iniciar pasa a iniciando y el registro abierto de la consulta siguiente lo abre', () => {
  const iniciando = siguienteVista({ fase: 'libre', registro: null }, { tipo: 'inicio-enviado' })
  assert.equal(iniciando.fase, 'iniciando')
  const abierto = siguienteVista(iniciando, { tipo: 'estado', registro: registro({ estado: 'esperando_vision' }) })
  assert.equal(abierto.fase, 'abierto')
})

test('siguienteVista: mientras se inicia, una consulta que aún trae el registro anterior (cerrado) no cambia la fase', () => {
  const iniciando = { fase: 'iniciando', registro: null }
  const v = siguienteVista(iniciando, { tipo: 'estado', registro: registro({ abierto: false, estado: 'completado' }) })
  assert.equal(v.fase, 'iniciando')
})

test('siguienteVista: si iniciar falla, vuelve a libre', () => {
  assert.equal(siguienteVista({ fase: 'iniciando', registro: null }, { tipo: 'inicio-fallido' }).fase, 'libre')
})

test('siguienteVista: el registro abierto que se cierra pasa a terminado y conserva cómo terminó', () => {
  const cerrado = registro({ abierto: false, estado: 'completado' })
  const v = siguienteVista({ fase: 'abierto', registro: registro() }, { tipo: 'estado', registro: cerrado })
  assert.equal(v.fase, 'terminado')
  assert.equal(v.registro, cerrado)
  // Y una consulta posterior no lo borra.
  const otra = siguienteVista(v, { tipo: 'estado', registro: cerrado })
  assert.equal(otra.fase, 'terminado')
  assert.equal(otra.registro, cerrado)
})

test('siguienteVista: mientras está abierto, cada consulta actualiza el registro', () => {
  const primero = registro()
  const segundo = registro({ estado: 'guardando' })
  const v = siguienteVista({ fase: 'abierto', registro: primero }, { tipo: 'estado', registro: segundo })
  assert.equal(v.fase, 'abierto')
  assert.equal(v.registro, segundo)
})

test('siguienteVista: si el registro abierto desaparece (sin_registro, Node-RED se reinició), la vista vuelve a libre y no muestra un resultado', () => {
  const vacio = registro({ abierto: false, estado: 'sin_registro', cuenta: null, nombre: null, poses: [], inicio: null, actualizado: null })
  const v = siguienteVista({ fase: 'abierto', registro: registro() }, { tipo: 'estado', registro: vacio })
  assert.equal(v.fase, 'libre')
  assert.equal(v.registro, null)
  assert.equal(AVISO_REGISTRO_PERDIDO.tono, 'advertencia')
  assertTextoLegible(AVISO_REGISTRO_PERDIDO.titulo)
  assertTextoLegible(AVISO_REGISTRO_PERDIDO.texto)
  assert.ok(AVISO_REGISTRO_PERDIDO.texto.includes('Alumnos'), 'dice dónde revisar')
})

test('siguienteVista: "Registrar a otro alumno" limpia el resultado y deja la vista libre', () => {
  const terminado = { fase: 'terminado', registro: registro({ abierto: false, estado: 'completado' }) }
  const v = siguienteVista(terminado, { tipo: 'descartar-resultado' })
  assert.equal(v.fase, 'libre')
  assert.equal(v.registro, null)
  // Descartar donde no hay resultado no cambia nada.
  assert.equal(siguienteVista({ fase: 'abierto', registro: registro() }, { tipo: 'descartar-resultado' }).fase, 'abierto')
})

test('siguienteVista: un registro abierto que aparece estando libre (iniciado en otra pestaña) se retoma', () => {
  const v = siguienteVista({ fase: 'libre', registro: null }, { tipo: 'estado', registro: registro() })
  assert.equal(v.fase, 'abierto')
})

test('siguienteVista: no modifica el estado de entrada', () => {
  const entrada = Object.freeze({ fase: 'libre', registro: null })
  assert.doesNotThrow(() => siguienteVista(entrada, { tipo: 'inicio-enviado' }))
})

function poses(estados) {
  return POSES.map((pose, i) => ({ pose, estado: estados[i] ?? 'pendiente', motivo: null }))
}

// ------------------------------------------------------------ seguirCaptura

function conFotos(estados, minis, motivos = []) {
  return registro({ poses: POSES.map((pose, i) => ({ pose, estado: estados[i] ?? 'pendiente', motivo: motivos[i] ?? null, miniatura: minis[i] ?? null })) })
}

test('seguirCaptura: sin fotos no hay captura', () => {
  const s = seguirCaptura(SEGUIMIENTO_INICIAL, conFotos(['esperando_pose'], []))
  assert.equal(s.captura, null)
})

test('seguirCaptura: la foto nueva (aceptada) es la última captura', () => {
  let s = seguirCaptura(SEGUIMIENTO_INICIAL, conFotos(['esperando_pose'], []))
  s = seguirCaptura(s, conFotos(['aceptada', 'esperando_pose'], ['AAA']))
  assert.deepEqual(s.captura, { pose: 'frente', estado: 'aceptada', motivo: null, miniatura: 'AAA', intento: 1 })
  s = seguirCaptura(s, conFotos(['aceptada', 'aceptada'], ['AAA', 'BBB']))
  assert.equal(s.captura.pose, 'izquierda')
  assert.equal(s.captura.miniatura, 'BBB')
})

test('seguirCaptura: al retomar la página, la última es la pose más avanzada con foto', () => {
  const s = seguirCaptura(SEGUIMIENTO_INICIAL, conFotos(['aceptada', 'aceptada', 'rechazada'], ['AAA', 'BBB', 'CCC'], [null, null, 'borrosa']))
  assert.deepEqual(s.captura, { pose: 'derecha', estado: 'rechazada', motivo: 'borrosa', miniatura: 'CCC', intento: 1 })
})

test('seguirCaptura: tras un rechazo la visión pasa a capturando (no a esperando_pose); la foto rechazada se conserva y el reintento la reemplaza', () => {
  let s = seguirCaptura(SEGUIMIENTO_INICIAL, conFotos(['esperando_pose'], []))
  s = seguirCaptura(s, conFotos(['rechazada'], ['AAA'], ['rostro_chico']))
  assert.equal(s.captura.estado, 'rechazada')
  // El flujo borra la miniatura de la pose al pasar a capturando: la grande sigue.
  const toma = seguirCaptura(s, conFotos(['capturando'], []))
  assert.equal(toma.captura, s.captura)
  const reintento = seguirCaptura(toma, conFotos(['aceptada'], ['ZZZ']))
  assert.deepEqual(reintento.captura, { pose: 'frente', estado: 'aceptada', motivo: null, miniatura: 'ZZZ', intento: 2 })
})

test('seguirCaptura: rechazada, capturando y otra vez rechazada reemplaza la captura', () => {
  let s = seguirCaptura(SEGUIMIENTO_INICIAL, conFotos(['rechazada'], ['AAA'], ['borrosa']))
  s = seguirCaptura(s, conFotos(['capturando'], []))
  const otra = seguirCaptura(s, conFotos(['rechazada'], ['BBB'], ['rostro_chico']))
  assert.deepEqual(otra.captura, { pose: 'frente', estado: 'rechazada', motivo: 'rostro_chico', miniatura: 'BBB', intento: 2 })
})

test('seguirCaptura: un rechazo idéntico y sin miniatura, con otro `actualizado`, cuenta como captura nueva', () => {
  const rech = (actualizado) => registro({ actualizado, poses: [{ pose: 'frente', estado: 'rechazada', motivo: 'sin_rostro' }] })
  const primera = seguirCaptura(SEGUIMIENTO_INICIAL, rech('2026-11-04T15:00:07.000Z'))
  // Misma consulta repetida: nada nuevo.
  assert.equal(seguirCaptura(primera, rech('2026-11-04T15:00:07.000Z')), primera)
  // El reintento (sin que la consulta viera capturando) se rechazó igual: es otra captura.
  const segunda = seguirCaptura(primera, rech('2026-11-04T15:00:11.000Z'))
  assert.notEqual(segunda, primera)
  assert.equal(primera.captura.intento, 1)
  assert.equal(segunda.captura.intento, 2)
})

test('seguirCaptura: con varias fotos nuevas a la vez gana la pose de mayor índice', () => {
  let s = seguirCaptura(SEGUIMIENTO_INICIAL, conFotos(['esperando_pose'], []))
  s = seguirCaptura(s, conFotos(['aceptada', 'aceptada', 'aceptada'], ['A', 'B', 'C']))
  assert.equal(s.captura.pose, 'derecha')
})

test('seguirCaptura: al retomar con una pose rechazada sin miniatura, esa es la última', () => {
  const s = seguirCaptura(SEGUIMIENTO_INICIAL, conFotos(['aceptada', 'rechazada'], ['AAA'], [null, 'sin_rostro']))
  assert.deepEqual(s.captura, { pose: 'izquierda', estado: 'rechazada', motivo: 'sin_rostro', miniatura: null, intento: 1 })
})

function cerrado(motivo, estados, motivos = []) {
  return registro({
    abierto: false,
    estado: 'error',
    motivo,
    poses: POSES.map((pose, i) => ({ pose, estado: estados[i] ?? 'pendiente', motivo: motivos[i] ?? null })),
  })
}

test('seguirCaptura: borrar las miniaturas al cerrar el registro no es una foto nueva', () => {
  const s = seguirCaptura(SEGUIMIENTO_INICIAL, conFotos(['aceptada', 'aceptada'], ['AAA', 'BBB']))
  const despues = seguirCaptura(s, cerrado('vencido', ['aceptada', 'aceptada']))
  assert.equal(despues.captura, s.captura)
})

test('capturaVisible: con el registro abierto es la última captura; sin captura, null (estado vacío honesto)', () => {
  const s = seguirCaptura(SEGUIMIENTO_INICIAL, conFotos(['aceptada'], ['AAA']))
  assert.equal(capturaVisible(conFotos(['aceptada'], ['AAA']), s.captura), s.captura)
  assert.equal(capturaVisible(conFotos(['esperando_pose'], []), null), null)
})

test('capturaVisible: cerrado no queda nada, ni el rechazo final (npu) ni una foto aceptada', () => {
  const rechazada = seguirCaptura(SEGUIMIENTO_INICIAL, conFotos(['rechazada'], ['AAA'], ['borrosa']))
  assert.equal(capturaVisible(cerrado('npu', ['rechazada'], ['borrosa']), rechazada.captura), null)
  const aceptada = seguirCaptura(SEGUIMIENTO_INICIAL, conFotos(['aceptada'], ['AAA']))
  assert.equal(capturaVisible(registro({ abierto: false, estado: 'completado', poses: poses(['aceptada']) }), aceptada.captura), null)
})

test('textoCaptura: "Pose · Estado" y, si se rechazó, el motivo traducido', () => {
  const ok = textoCaptura({ pose: 'izquierda', estado: 'aceptada', motivo: null, miniatura: 'AAA', intento: 1 })
  assert.ok(ok.leyenda.startsWith(ETIQUETA_POSE.izquierda), 'la leyenda nombra la pose como la lista')
  assert.equal(ok.detalle, null)
  assert.ok(ok.alt.toLowerCase().includes(ETIQUETA_POSE.izquierda.toLowerCase()), 'el texto alternativo nombra la pose')
  const mala = textoCaptura({ pose: 'frente_2', estado: 'rechazada', motivo: 'borrosa', miniatura: 'AAA', intento: 2 })
  assert.ok(mala.leyenda.startsWith(ETIQUETA_POSE.frente_2))
  assert.notEqual(mala.leyenda.replace(ETIQUETA_POSE.frente_2, ''), ok.leyenda.replace(ETIQUETA_POSE.izquierda, ''), 'aceptada y rechazada se leen distinto')
  assert.equal(mala.detalle, traducirMotivo('borrosa'))
  assert.ok(mala.alt.includes(traducirMotivo('borrosa')), 'el texto alternativo trae el motivo')
})

test('seguirCaptura: el intento cuenta las fotos de la misma pose (anuncio distinto en cada rechazo seguido)', () => {
  let s = seguirCaptura(SEGUIMIENTO_INICIAL, conFotos(['rechazada'], ['AAA'], ['borrosa']))
  s = seguirCaptura(s, conFotos(['capturando'], []))
  s = seguirCaptura(s, conFotos(['rechazada'], ['BBB'], ['borrosa']))
  assert.equal(s.captura.intento, 2)
  s = seguirCaptura(s, conFotos(['aceptada', 'rechazada'], ['BBB', 'CCC'], [null, 'borrosa']))
  assert.equal(s.captura.pose, 'izquierda')
  assert.equal(s.captura.intento, 1)
})

test('seguirCaptura: una consulta sin cambios devuelve el mismo objeto', () => {
  const r = conFotos(['aceptada', 'esperando_pose'], ['AAA'])
  const s = seguirCaptura(SEGUIMIENTO_INICIAL, r)
  assert.equal(seguirCaptura(s, conFotos(['aceptada', 'esperando_pose'], ['AAA'])), s)
})

test('seguirCaptura: otro registro (otra cuenta o inicio) empieza de cero', () => {
  const s = seguirCaptura(SEGUIMIENTO_INICIAL, conFotos(['aceptada'], ['AAA']))
  const otro = seguirCaptura(s, registro({ cuenta: '320068235', poses: poses(['esperando_pose']) }))
  assert.equal(otro.captura, null)
})
