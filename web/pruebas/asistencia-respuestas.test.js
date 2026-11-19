// Pruebas de la lógica pura de asistencia por sesión (src/api/asistenciaRespuestas.ts).
// Los cuerpos de ejemplo usan cuentas y nombres sintéticos de prueba.
// Sin dependencias: node --test web/pruebas/*.test.js
import test from 'node:test'
import assert from 'node:assert/strict'
import { assertTextosDistintos } from './texto.js'
import {
  cuentasConEstadoCambiado,
  elegirSesionActiva,
  ETIQUETA_ESTADO_ASISTENCIA,
  ETIQUETA_MOTIVO_INTENTO,
  leerDetalleSesion,
  leerSesiones,
} from '../src/api/asistenciaRespuestas.ts'

const sesionCerrada = {
  id: 21,
  materia: 'FSE',
  grupo: '1',
  inicio: '2026-11-06T22:23:19.000Z',
  fin: '2026-11-06T22:30:05.000Z',
  abierta: false,
  tol_min: 10,
  total_grupo: 10,
  presentes: 1,
  retardos: 0,
  faltas: 9,
  pendientes: 0,
  intentos: { desconocido: 1, duplicado: 1, imagen_insuficiente: 1, total: 3 },
  confianza: { min: 1, max: 1, promedio: 1 },
}

const sesionAbierta = {
  ...sesionCerrada,
  id: 22,
  fin: null,
  abierta: true,
  faltas: 0,
  pendientes: 9,
  confianza: { min: null, max: null, promedio: null },
}

test('leerSesiones: acepta lista vacía y sesiones abiertas o cerradas', () => {
  assert.deepEqual(leerSesiones([]), { ok: true, sesiones: [] })

  const r = leerSesiones([sesionAbierta, sesionCerrada])
  assert.equal(r.ok, true)
  if (!r.ok) return
  assert.equal(r.sesiones.length, 2)
  assert.equal(r.sesiones[0].abierta, true)
  assert.equal(r.sesiones[0].fin, null)
  assert.equal(r.sesiones[0].pendientes, 9)
  assert.equal(r.sesiones[1].abierta, false)
  assert.equal(r.sesiones[1].intentos.imagenInsuficiente, 1)
  assert.equal(r.sesiones[1].tolMin, 10)
  assert.equal(r.sesiones[1].totalGrupo, 10)
})

test('leerSesiones: rechaza formas inválidas o incoherencia entre abierta y fin', () => {
  for (const malo of [
    null,
    {},
    [{ ...sesionCerrada, id: 0 }],
    [{ ...sesionCerrada, inicio: 'no-es-fecha' }],
    [{ ...sesionCerrada, abierta: true, fin: '2026-11-06T22:30:05.000Z' }],
    [{ ...sesionCerrada, abierta: false, fin: null }],
    [{ ...sesionCerrada, presentes: -1 }],
    [{ ...sesionCerrada, confianza: { min: '1', max: null, promedio: null } }],
  ]) {
    assert.equal(leerSesiones(malo).ok, false)
  }
})

test('leerDetalleSesion: traduce sesión, lista nominal con presente/retardo/falta e intentos', () => {
  const r = leerDetalleSesion({
    sesion: sesionCerrada,
    alumnos: [
      {
        cuenta: '999000001',
        nombre: 'Alba, Ana',
        estado: 'presente',
        hora: '2026-11-06T22:24:00.000Z',
        confianza: 0.94,
        metodo: 'rostro',
      },
      {
        cuenta: '999000002',
        nombre: 'Báez, Bruno',
        estado: 'retardo',
        hora: '2026-11-06T22:35:00.000Z',
        confianza: 0.87,
        metodo: 'rostro',
      },
      {
        cuenta: '999000003',
        nombre: 'Cruz, Carla',
        estado: 'falta',
        hora: null,
        confianza: null,
        metodo: null,
      },
    ],
    intentos: [
      {
        id: 8,
        hora: '2026-11-06T22:25:10.000Z',
        motivo: 'desconocido',
        confianza_max: 0.0919,
      },
      {
        id: 7,
        hora: '2026-11-06T22:24:50.000Z',
        motivo: 'imagen_insuficiente',
        confianza_max: null,
      },
    ],
  })

  assert.equal(r.ok, true)
  if (!r.ok) return
  assert.equal(r.detalle.alumnos.length, 3)
  assert.equal(r.detalle.alumnos[1].estado, 'retardo')
  assert.equal(r.detalle.intentos.length, 2)
  assert.equal(r.detalle.intentos[0].confianzaMax, 0.0919)
  assert.equal(r.detalle.intentos[1].confianzaMax, null)
})

test('leerDetalleSesion: rechaza cuenta mal formada, estado desconocido o motivo inválido', () => {
  assert.equal(
    leerDetalleSesion({
      sesion: sesionCerrada,
      alumnos: [{ cuenta: '123', nombre: 'Ana', estado: 'presente', hora: null, confianza: null, metodo: null }],
      intentos: [],
    }).ok,
    false,
  )
  assert.equal(
    leerDetalleSesion({
      sesion: sesionCerrada,
      alumnos: [{ cuenta: '999000001', nombre: 'Ana', estado: 'otro', hora: null, confianza: null, metodo: null }],
      intentos: [],
    }).ok,
    false,
  )
  assert.equal(
    leerDetalleSesion({
      sesion: sesionCerrada,
      alumnos: [],
      intentos: [{ id: 1, hora: '2026-11-06T22:25:10.000Z', motivo: 'otro', confianza_max: null }],
    }).ok,
    false,
  )
})

test('elegirSesionActiva: respeta elección vigente, prioriza abierta y cae a la más reciente', () => {
  const r = leerSesiones([sesionCerrada, { ...sesionCerrada, id: 20 }])
  assert.ok(r.ok)
  assert.equal(elegirSesionActiva([], null), null)
  assert.equal(elegirSesionActiva(r.sesiones, null), 21)
  assert.equal(elegirSesionActiva(r.sesiones, 20), 20)
  assert.equal(elegirSesionActiva(r.sesiones, 999), 21)

  const conAbierta = leerSesiones([{ ...sesionCerrada, id: 23 }, sesionAbierta, sesionCerrada])
  assert.ok(conAbierta.ok)
  assert.equal(elegirSesionActiva(conAbierta.sesiones, null), 22)
  assert.equal(elegirSesionActiva(conAbierta.sesiones, 21), 21)
})

test('cuentasConEstadoCambiado: detecta transiciones solo dentro de la misma sesión', () => {
  const antes = [
    { cuenta: '999000001', nombre: 'Ana', estado: 'pendiente', hora: null, confianza: null, metodo: null },
    { cuenta: '999000002', nombre: 'Bruno', estado: 'pendiente', hora: null, confianza: null, metodo: null },
  ]
  const despues = [
    { cuenta: '999000001', nombre: 'Ana', estado: 'presente', hora: '2026-11-06T22:24:00.000Z', confianza: 0.91, metodo: 'rostro' },
    { cuenta: '999000002', nombre: 'Bruno', estado: 'pendiente', hora: null, confianza: null, metodo: null },
  ]
  assert.deepEqual(cuentasConEstadoCambiado(null, null, despues, 22), [])
  assert.deepEqual(cuentasConEstadoCambiado(antes, 21, despues, 22), [])
  assert.deepEqual(cuentasConEstadoCambiado(antes, 22, despues, 22), ['999000001'])
})

test('las etiquetas de presentación cubren todos los estados y motivos, y cada una dice lo suyo', () => {
  assert.deepEqual(Object.keys(ETIQUETA_ESTADO_ASISTENCIA).sort(), ['falta', 'pendiente', 'presente', 'retardo'])
  assertTextosDistintos(Object.values(ETIQUETA_ESTADO_ASISTENCIA))
  assert.deepEqual(Object.keys(ETIQUETA_MOTIVO_INTENTO).sort(), ['desconocido', 'duplicado', 'imagen_insuficiente'])
  assertTextosDistintos(Object.values(ETIQUETA_MOTIVO_INTENTO))
})
