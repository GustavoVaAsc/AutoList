// Pruebas de las funciones del flujo "Registro" de nodered/flows/flows.json:
//   POST /api/enrolamiento, POST /api/enrolamiento/cancelar, GET /api/enrolamiento/estado,
//   DELETE /api/alumnos/:cuenta/rostro, y los manejadores de aula/vision/enrolamiento y
//   aula/vision/embedding, el vencimiento y el arranque.
// Cada función se extrae del propio flows.json y se ejecuta en un contexto vm con un
// `flow` de mentira (un Map) y un reloj que la prueba controla. No prueba el nodo de
// Postgres ni el broker: eso es de la placa. Los alumnos, cuentas y vectores de aquí son
// entradas de prueba de las funciones, no datos de la API real.
// Sin dependencias: node --test nodered/pruebas/*.test.js
"use strict";
const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const flujos = JSON.parse(fs.readFileSync(path.join(__dirname, "..", "flows", "flows.json"), "utf8"));
const ID = {
    validarApartar: "a1f0c0de00000063",
    abrir: "a1f0c0de00000065",
    cancelar: "a1f0c0de00000068",
    estado: "a1f0c0de0000006b",
    validarBaja: "a1f0c0de0000006e",
    decidirBaja: "a1f0c0de00000070",
    progreso: "a1f0c0de00000073",
    validarEmbedding: "a1f0c0de00000076",
    trasGuardar: "a1f0c0de00000078",
    vencer: "a1f0c0de0000007b",
    arrancar: "a1f0c0de0000007d",
    baja_tardia: "a1f0c0de00000089",
    limpiezaLista: "a1f0c0de0000008a",
    trasRevertir: "a1f0c0de00000080",
    cerrar500: "a1f0c0de00000084",
    responder500Estado: "a1f0c0de00000086"
};
const POSES = ["frente", "izquierda", "derecha", "arriba", "frente_2"];
const CINCO_MIN = 5 * 60 * 1000;

function nodo(id) {
    const n = flujos.find((x) => x.id === id);
    assert.ok(n, "no existe el nodo " + id);
    return n;
}

// Contexto `flow` de mentira. Guarda una copia por JSON: así una función que mutara el
// objeto sin llamar a flow.set no pasaría por un contexto que sí persiste.
// Los almacenes de contexto van separados, como en Node-RED: sin nombre es el de memoria
// (default) y "persistente" es el localfilesystem de settings.js.
function nuevoFlow() {
    const almacenes = new Map();
    const de = (almacen) => {
        const nombre = almacen === undefined ? "default" : almacen;
        if (!almacenes.has(nombre)) {
            almacenes.set(nombre, new Map());
        }
        return almacenes.get(nombre);
    };
    return {
        get: (k, almacen) => {
            const datos = de(almacen);
            return datos.has(k) ? JSON.parse(datos.get(k)) : undefined;
        },
        set: (k, v, almacen) => {
            const datos = de(almacen);
            if (v === undefined) {
                datos.delete(k);
            } else {
                datos.set(k, JSON.stringify(v));
            }
        }
    };
}

function relojFijo(ms) {
    return class extends Date {
        constructor(...a) {
            if (a.length === 0) {
                super(ms);
            } else {
                super(...a);
            }
        }
        static now() {
            return ms;
        }
    };
}

// Ejecuta la función del nodo `id`. Devuelve { salida, avisos, logs }; la salida se copia
// por JSON (los objetos del contexto vm son de otro realm).
function ejecutar(id, msg, { flow = nuevoFlow(), ahora } = {}) {
    const n = nodo(id);
    assert.equal(n.type, "function");
    const avisos = [];
    const logs = [];
    const contexto = vm.createContext({ Buffer, Date: ahora === undefined ? Date : relojFijo(ahora), Math, console });
    const funcion = new vm.Script("(function (msg, node, flow) {" + n.func + "\n})").runInContext(contexto);
    const nodoFalso = { warn: (t) => avisos.push(String(t)), log: (t) => logs.push(String(t)), error() {} };
    const salida = funcion(msg, nodoFalso, flow);
    return { salida: salida === undefined || salida === null ? salida : JSON.parse(JSON.stringify(salida)), avisos, logs, flow };
}

const CUENTA = "320068234";
const OTRA = "320068235";
const NOMBRE = "Ana López";
// consentimiento_fecha que devuelve el UPDATE del embedding: UTC con microsegundos, como lo da to_char.
const FECHA = "2026-11-05T15:30:12.123456Z";
const OTRA_FECHA = "2026-11-05T15:41:07.654321Z";
const FILA_GUARDADA = [{ no_cuenta: CUENTA, consentimiento_fecha: FECHA }];

// retiros_pendientes vive en el almacén persistente: se lee y se escribe ahí, nunca en memoria.
function retirosPendientes(flow) {
    return flow.get("retiros_pendientes", "persistente");
}

function peticionPost(cuerpo, tipo = "application/json; charset=utf-8") {
    const headers = tipo === null ? {} : { "content-type": tipo };
    return { req: { method: "POST", path: "/api/enrolamiento", headers, query: {} }, payload: cuerpo };
}

// Un vector de n números, normalizado L2 y con 4 decimales como lo define el contrato MQTT
// (aula/vision/embedding); solo para probar la forma, no tiene relación con ninguna cara.
function vector(n = 512, escala = 1) {
    const base = Array.from({ length: n }, (_, i) => (i % 7) - 3);
    const norma = Math.sqrt(base.reduce((suma, x) => suma + x * x, 0));
    return base.map((x) => Math.round((x / norma) * escala * 10000) / 10000);
}

// ts de un mensaje de la visión: ahora, o desplazado `desfase` ms.
function ts(desfase = 0) {
    return new Date(Date.now() + desfase).toISOString();
}

function mensajeEmbedding(extra = {}) {
    return JSON.stringify({ ts: ts(), cuenta: CUENTA, dim: 512, modelo: "w600k_mbf-uint8-v1", embedding: vector(), ...extra });
}

function mensajeProgreso(extra = {}) {
    return JSON.stringify({ ts: ts(), cuenta: CUENTA, nombre: NOMBRE, paso: 1, total: 5, pose: "frente", estado: "capturando", ...extra });
}

// Abre una inscripción como lo hacen "Validar y apartar" y "Abrir": devuelve el flow.
function abrirInscripcion(cuenta = CUENTA, nombre = NOMBRE) {
    const flow = nuevoFlow();
    const r = ejecutar(ID.validarApartar, peticionPost({ cuenta, consentimiento: true }), { flow });
    assert.ok(r.salida[0], "la inscripción debió apartarse");
    const msg = { ...r.salida[0], payload: [{ existe: 1, nombre, tiene_rostro: false, actualizadas: 1 }] };
    const a = ejecutar(ID.abrir, msg, { flow });
    assert.equal(a.salida[0].statusCode, 202);
    return flow;
}

// ------------------------------------------------ POST /api/enrolamiento: validación

test("registro: sin application/json responde 415 y no aparta nada", () => {
    for (const tipo of [null, "text/plain", "text/csv", "application/x-www-form-urlencoded", "multipart/form-data; boundary=x"]) {
        const flow = nuevoFlow();
        const { salida } = ejecutar(ID.validarApartar, peticionPost({ cuenta: CUENTA, consentimiento: true }, tipo), { flow });
        assert.equal(salida[0], null, String(tipo));
        assert.equal(salida[1].statusCode, 415, String(tipo));
        assert.equal(flow.get("inscripcion"), undefined);
    }
});

test("registro: acepta application/json con parámetros y en mayúsculas", () => {
    for (const tipo of ["application/json", "application/json; charset=utf-8", "Application/JSON"]) {
        const { salida } = ejecutar(ID.validarApartar, peticionPost({ cuenta: CUENTA, consentimiento: true }, tipo));
        assert.ok(salida[0], tipo);
    }
});

test("registro: cuerpo que no es un objeto o cuenta inválida responde 400 y no aparta nada", () => {
    const malos = [
        null, "texto", 7, [], [{ cuenta: CUENTA, consentimiento: true }], {},
        { consentimiento: true },
        { cuenta: 320068234, consentimiento: true },
        { cuenta: "32006823", consentimiento: true },
        { cuenta: "3200682345", consentimiento: true },
        { cuenta: "32006823a", consentimiento: true },
        { cuenta: " 320068234", consentimiento: true },
        { cuenta: "320068234\n", consentimiento: true },
        { cuenta: ["320068234"], consentimiento: true }
    ];
    for (const cuerpo of malos) {
        const flow = nuevoFlow();
        const { salida } = ejecutar(ID.validarApartar, peticionPost(cuerpo), { flow });
        assert.equal(salida[0], null, JSON.stringify(cuerpo));
        assert.equal(salida[1].statusCode, 400, JSON.stringify(cuerpo));
        assert.equal(typeof salida[1].payload.error, "string");
        assert.equal(flow.get("inscripcion"), undefined);
    }
});

test("registro: consentimiento distinto del booleano true responde 400", () => {
    for (const consentimiento of [false, "true", 1, "si", null, undefined, [true], {}]) {
        const flow = nuevoFlow();
        const { salida } = ejecutar(ID.validarApartar, peticionPost({ cuenta: CUENTA, consentimiento }), { flow });
        assert.equal(salida[1].statusCode, 400, JSON.stringify(consentimiento));
        assert.match(salida[1].payload.error, /consentimiento/);
        assert.equal(flow.get("inscripcion"), undefined);
    }
});

test("registro: con un registro abierto responde 409, sea de la misma cuenta o de otra, y no lo toca", () => {
    for (const cuenta of [CUENTA, OTRA]) {
        const flow = abrirInscripcion();
        const antes = JSON.stringify(flow.get("inscripcion"));
        const { salida } = ejecutar(ID.validarApartar, peticionPost({ cuenta, consentimiento: true }), { flow });
        assert.equal(salida[0], null);
        assert.equal(salida[1].statusCode, 409);
        assert.equal(salida[1].payload.motivo, "registro_abierto");
        assert.equal(JSON.stringify(flow.get("inscripcion")), antes);
    }
});

test("registro: dos peticiones seguidas (doble clic): la segunda recibe 409 antes de tocar la base", () => {
    const flow = nuevoFlow();
    const primera = ejecutar(ID.validarApartar, peticionPost({ cuenta: CUENTA, consentimiento: true }), { flow });
    assert.ok(primera.salida[0]);
    const segunda = ejecutar(ID.validarApartar, peticionPost({ cuenta: CUENTA, consentimiento: true }), { flow });
    assert.equal(segunda.salida[0], null);
    assert.equal(segunda.salida[1].statusCode, 409);
});

test("registro: un registro ya cerrado no impide iniciar otro", () => {
    const flow = abrirInscripcion();
    ejecutar(ID.cancelar, { req: { method: "POST" } }, { flow });
    const { salida } = ejecutar(ID.validarApartar, peticionPost({ cuenta: OTRA, consentimiento: true }), { flow });
    assert.ok(salida[0]);
    assert.deepEqual(salida[0].params, [OTRA]);
});

test("registro: aparta el lugar con 5 poses pendientes y deja el parámetro enlazado", () => {
    const { salida, flow } = ejecutar(ID.validarApartar, peticionPost({ cuenta: CUENTA, consentimiento: true }, "application/json"), { ahora: 1_000_000 });
    assert.deepEqual(salida[0].params, [CUENTA]);
    assert.equal(typeof salida[0].reserva, "string");
    assert.equal(salida[0].payload, null);
    // El flow de esa llamada es el que devolvió ejecutar: se relee de ahí.
    const insc = flow.get("inscripcion");
    assert.equal(insc.cuenta, CUENTA);
    assert.equal(insc.abierto, true);
    assert.equal(insc.apartada, true);
    assert.equal(insc.nombre, null);
    assert.equal(insc.estado, "esperando_vision");
    assert.equal(insc.inicio_ms, 1_000_000);
    assert.deepEqual(insc.poses, POSES.map((pose) => ({ pose, estado: "pendiente", motivo: null, miniatura: null })));
    assert.equal(insc.id, salida[0].reserva);
});

// ------------------------------------------------ POST /api/enrolamiento: tras la base

function respuestaConsentimiento(flow, fila) {
    const insc = flow.get("inscripcion");
    return { req: { method: "POST" }, reserva: insc ? insc.id : "x", params: [CUENTA], payload: [fila] };
}

test("registro: cuenta inexistente responde 404 y libera el lugar", () => {
    const flow = nuevoFlow();
    ejecutar(ID.validarApartar, peticionPost({ cuenta: CUENTA, consentimiento: true }), { flow });
    const { salida } = ejecutar(ID.abrir, respuestaConsentimiento(flow, { existe: 0, nombre: null, tiene_rostro: false, actualizadas: 0 }), { flow });
    assert.equal(salida[0].statusCode, 404);
    assert.equal(salida[1], null);
    assert.equal(salida[2], null);
    assert.equal(flow.get("inscripcion"), undefined);
});

test("registro: alumno que ya tiene rostro responde 409 sin publicar, libera el lugar y no pisa el embedding", () => {
    const flow = nuevoFlow();
    ejecutar(ID.validarApartar, peticionPost({ cuenta: CUENTA, consentimiento: true }), { flow });
    const { salida } = ejecutar(ID.abrir, respuestaConsentimiento(flow, { existe: 1, nombre: NOMBRE, tiene_rostro: true, actualizadas: 0 }), { flow });
    assert.equal(salida[0].statusCode, 409);
    assert.equal(salida[0].payload.motivo, "tiene_rostro");
    assert.equal(salida[1], null);
    assert.equal(flow.get("inscripcion"), undefined);
});

test("registro: alta correcta: abre la inscripción, publica enrolar con el payload del contrato y responde 202", () => {
    const flow = nuevoFlow();
    ejecutar(ID.validarApartar, peticionPost({ cuenta: CUENTA, consentimiento: true }), { flow });
    const { salida } = ejecutar(ID.abrir, respuestaConsentimiento(flow, { existe: 1, nombre: NOMBRE, tiene_rostro: false, actualizadas: 1 }), { flow });
    assert.equal(salida[0].statusCode, 202);
    assert.deepEqual(salida[0].payload, { cuenta: CUENTA, nombre: NOMBRE, abierto: true });
    assert.equal(salida[1].topic, "aula/vision/cmd");
    assert.equal(salida[1].qos, 1);
    assert.equal(salida[1].retain, false);
    assert.deepEqual(JSON.parse(salida[1].payload), { accion: "enrolar", cuenta: CUENTA, nombre: NOMBRE });
    assert.equal(salida[2], null);
    const insc = flow.get("inscripcion");
    assert.equal(insc.abierto, true);
    assert.equal(insc.apartada, false);
    assert.equal(insc.nombre, NOMBRE);
    assert.equal(insc.poses.length, 5);
});

test("registro: si el lugar se cerró mientras se escribía, responde 409, no publica y revierte el consentimiento", () => {
    const flow = nuevoFlow();
    ejecutar(ID.validarApartar, peticionPost({ cuenta: CUENTA, consentimiento: true }), { flow });
    const msg = respuestaConsentimiento(flow, { existe: 1, nombre: NOMBRE, tiene_rostro: false, actualizadas: 1 });
    ejecutar(ID.cancelar, { req: { method: "POST" } }, { flow }); // cierra el lugar apartado
    const { salida } = ejecutar(ID.abrir, msg, { flow });
    assert.equal(salida[0].statusCode, 409);
    assert.equal(salida[0].payload.motivo, "interrumpido");
    assert.equal(salida[1], null);
    assert.deepEqual(salida[2].params, [CUENTA]);
});

test("registro: un UPDATE que no escribe el consentimiento lanza error y libera el lugar", () => {
    const flow = nuevoFlow();
    ejecutar(ID.validarApartar, peticionPost({ cuenta: CUENTA, consentimiento: true }), { flow });
    assert.throws(() => ejecutar(ID.abrir, respuestaConsentimiento(flow, { existe: 1, nombre: NOMBRE, tiene_rostro: false, actualizadas: 0 }), { flow }), /consentimiento/);
    assert.equal(flow.get("inscripcion"), undefined);
});

// ------------------------------------------------ cancelar

test("cancelar: sin registro abierto responde 200 idempotente, sin publicar ni revertir", () => {
    const { salida } = ejecutar(ID.cancelar, { req: { method: "POST" } });
    assert.equal(salida[0].statusCode, 200);
    assert.deepEqual(salida[0].payload, { cancelado: false, abierto: false });
    assert.equal(salida[1], null);
    assert.equal(salida[2], null);
});

test("cancelar: con registro abierto lo cierra, publica cancelar y manda revertir el consentimiento", () => {
    const flow = abrirInscripcion();
    const { salida } = ejecutar(ID.cancelar, { req: { method: "POST" } }, { flow });
    assert.equal(salida[0], null, "la respuesta sale después de la base");
    assert.equal(salida[1].topic, "aula/vision/cmd");
    assert.equal(salida[1].qos, 1);
    assert.equal(salida[1].retain, false);
    assert.deepEqual(JSON.parse(salida[1].payload), { accion: "cancelar", cuenta: CUENTA });
    assert.deepEqual(salida[2].params, [CUENTA]);
    assert.ok(salida[2].req, "conserva la petición para responder");
    const insc = flow.get("inscripcion");
    assert.equal(insc.abierto, false);
    assert.equal(insc.estado, "cancelado");
});

test("cancelar: dos veces seguidas, la segunda es idempotente y no publica", () => {
    const flow = abrirInscripcion();
    ejecutar(ID.cancelar, { req: { method: "POST" } }, { flow });
    const { salida } = ejecutar(ID.cancelar, { req: { method: "POST" } }, { flow });
    assert.equal(salida[0].statusCode, 200);
    assert.equal(salida[0].payload.cancelado, false);
    assert.equal(salida[1], null);
    assert.equal(salida[2], null);
});

test("cancelar: tras la base, solo la petición HTTP recibe respuesta 200", () => {
    const { salida } = ejecutar(ID.trasRevertir, { req: { method: "POST" }, payload: [] });
    assert.equal(salida.statusCode, 200);
    assert.deepEqual(salida.payload, { cancelado: true, abierto: false });
    // Las reversiones de MQTT, del vencimiento y de los fallos no traen petición.
    assert.equal(ejecutar(ID.trasRevertir, { params: [CUENTA], payload: [] }).salida, null);
});

// ------------------------------------------------ GET /api/enrolamiento/estado

const CLAVES_ESTADO = ["abierto", "cuenta", "nombre", "estado", "motivo", "poses", "advertencia", "inicio", "actualizado"];

test("estado: sin ningún registro responde la forma vacía", () => {
    const { salida } = ejecutar(ID.estado, { req: { method: "GET" } });
    assert.equal(salida.statusCode, 200);
    assert.deepEqual(Object.keys(salida.payload), CLAVES_ESTADO);
    assert.deepEqual(salida.payload, {
        abierto: false, cuenta: null, nombre: null, estado: "sin_registro", motivo: null,
        poses: [], advertencia: null, inicio: null, actualizado: null
    });
});

test("estado: con un registro abierto trae cuenta, nombre, 5 poses y fechas ISO", () => {
    const flow = abrirInscripcion();
    const { salida } = ejecutar(ID.estado, { req: { method: "GET" } }, { flow });
    assert.deepEqual(Object.keys(salida.payload), CLAVES_ESTADO);
    assert.equal(salida.payload.abierto, true);
    assert.equal(salida.payload.cuenta, CUENTA);
    assert.equal(salida.payload.nombre, NOMBRE);
    assert.equal(salida.payload.estado, "esperando_vision");
    assert.equal(salida.payload.motivo, null);
    assert.equal(salida.payload.advertencia, null);
    assert.deepEqual(salida.payload.poses.map((p) => Object.keys(p)), POSES.map(() => ["pose", "estado", "motivo", "miniatura"]));
    assert.deepEqual(salida.payload.poses.map((p) => p.pose), POSES);
    assert.match(salida.payload.inicio, /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z$/);
    assert.match(salida.payload.actualizado, /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z$/);
});

test("estado: nunca salen embeddings; la miniatura sale solo de su pose y mientras el registro está abierto", () => {
    const flow = abrirInscripcion();
    const insc = flow.get("inscripcion");
    insc.embedding = [0.5, 0.25];
    insc.miniatura = "BASE64SECRETO"; // fuera de las poses: no sale
    insc.poses[0].miniatura = "BASE64POSE"; // la de la pose: sí sale
    insc.poses[0].embedding = [1];
    insc.advertencia = { parecido_a: OTRA, embedding: [1, 2] };
    flow.set("inscripcion", insc);
    const { salida } = ejecutar(ID.estado, { req: { method: "GET" } }, { flow });
    const texto = JSON.stringify(salida.payload);
    assert.ok(!/embedding/.test(texto), texto);
    assert.ok(!texto.includes("BASE64SECRETO"), texto);
    assert.equal(salida.payload.poses[0].miniatura, "BASE64POSE");
    assert.equal(salida.payload.poses[1].miniatura, null);
    assert.deepEqual(salida.payload.advertencia, { parecido_a: OTRA });
});

test("estado: al cerrarse el registro, la miniatura se borra del contexto y ya no sale", () => {
    const flow = abrirInscripcion();
    progreso(flow, mensajeProgreso({ pose: "frente", estado: "aceptada", miniatura: "BASE64POSE" }));
    assert.equal(ejecutar(ID.estado, { req: { method: "GET" } }, { flow }).salida.payload.poses[0].miniatura, "BASE64POSE");
    ejecutar(ID.cancelar, { req: { method: "POST" } }, { flow });
    const { salida } = ejecutar(ID.estado, { req: { method: "GET" } }, { flow });
    assert.equal(salida.payload.poses[0].miniatura, null);
    assert.ok(!JSON.stringify(salida.payload).includes("BASE64POSE"));
    assert.ok(!JSON.stringify(flow.get("inscripcion")).includes("BASE64POSE"));
});

test("estado: tras cerrarse, sigue devolviendo cómo terminó, con abierto false", () => {
    const flow = abrirInscripcion();
    ejecutar(ID.cancelar, { req: { method: "POST" } }, { flow });
    const { salida } = ejecutar(ID.estado, { req: { method: "GET" } }, { flow });
    assert.equal(salida.payload.abierto, false);
    assert.equal(salida.payload.estado, "cancelado");
    assert.equal(salida.payload.cuenta, CUENTA);
});

test("estado: el error al leerlo responde 500 sin copiar cuentas de 9 dígitos al aviso", () => {
    const { salida, avisos } = ejecutar(ID.responder500Estado, { error: { message: "falló con 320068234 dentro" } });
    assert.equal(salida.statusCode, 500);
    assert.ok(!avisos.join("\n").includes(CUENTA));
});

// ------------------------------------------------ DELETE /api/alumnos/:cuenta/rostro

function peticionBaja(cuenta) {
    return { req: { method: "DELETE", params: cuenta === undefined ? {} : { cuenta }, path: "/api/alumnos/" + cuenta + "/rostro" } };
}

test("baja: cuenta inválida responde 400", () => {
    for (const cuenta of [undefined, "", "1234", "32006823a", "3200682345", "320068234 ", "../320068234"]) {
        const { salida } = ejecutar(ID.validarBaja, peticionBaja(cuenta));
        assert.equal(salida[0], null, String(cuenta));
        assert.equal(salida[1].statusCode, 400, String(cuenta));
    }
});

test("baja: cuenta válida sigue a la base con el parámetro enlazado", () => {
    const { salida } = ejecutar(ID.validarBaja, peticionBaja(CUENTA));
    assert.deepEqual(salida[0].params, [CUENTA]);
    assert.equal(salida[1], null);
});

test("baja: con un registro abierto de esa cuenta responde 409; con el de otra cuenta, sigue", () => {
    const flow = abrirInscripcion();
    const propia = ejecutar(ID.validarBaja, peticionBaja(CUENTA), { flow });
    assert.equal(propia.salida[1].statusCode, 409);
    assert.equal(propia.salida[1].payload.motivo, "registro_abierto");
    assert.equal(propia.salida[0], null);
    const ajena = ejecutar(ID.validarBaja, peticionBaja(OTRA), { flow });
    assert.deepEqual(ajena.salida[0].params, [OTRA]);
});

test("baja: cuenta inexistente responde 404 y no publica", () => {
    const { salida } = ejecutar(ID.decidirBaja, { params: [CUENTA], payload: [{ existe: 0, tenia_rostro: false, actualizadas: 0 }] });
    assert.equal(salida[0].statusCode, 404);
    assert.equal(salida[1], null);
});

test("baja: con rostro responde 200 con el estado nuevo y publica la baja con el payload del contrato", () => {
    const { salida } = ejecutar(ID.decidirBaja, { params: [CUENTA], payload: [{ existe: 1, tenia_rostro: true, actualizadas: 1 }] });
    assert.equal(salida[0].statusCode, 200);
    assert.deepEqual(salida[0].payload, { cuenta: CUENTA, rostro: "pendiente", retirado: true });
    assert.equal(salida[1].topic, "aula/vision/galeria/baja");
    assert.equal(salida[1].qos, 1);
    assert.equal(salida[1].retain, false);
    assert.equal(salida[1].payload, JSON.stringify({ cuenta: CUENTA }));
});

test("baja: es idempotente, si ya estaba pendiente responde 200 y no publica", () => {
    for (const actualizadas of [0, 1]) {
        const { salida } = ejecutar(ID.decidirBaja, { params: [CUENTA], payload: [{ existe: 1, tenia_rostro: false, actualizadas }] });
        assert.equal(salida[0].statusCode, 200);
        assert.deepEqual(salida[0].payload, { cuenta: CUENTA, rostro: "pendiente", retirado: false });
        assert.equal(salida[1], null);
    }
});

test("baja: la consulta quita embedding, consentimiento y fecha en una sola sentencia con parámetros", () => {
    const { query } = nodo("a1f0c0de0000006f");
    assert.match(query, /UPDATE alumnos SET embedding = NULL, consentimiento = false, consentimiento_fecha = NULL/);
    assert.match(query, /WHERE no_cuenta = \$1/);
    assert.equal(query.includes(";"), false, "una sola sentencia");
    assert.ok(!/DELETE/i.test(query), "el rol de Node-RED no tiene DELETE");
});

// ------------------------------------------------ progreso (aula/vision/enrolamiento)

function progreso(flow, payload, extra = {}) {
    return ejecutar(ID.progreso, { topic: "aula/vision/enrolamiento", payload, retain: false, ...extra }, { flow });
}

test("progreso: descarta lo retenido, ignora lo vacío y descarta lo que no es un objeto JSON", () => {
    const flow = abrirInscripcion();
    const antes = JSON.stringify(flow.get("inscripcion"));
    const retenido = progreso(flow, mensajeProgreso(), { retain: true });
    assert.equal(retenido.salida[0], null);
    assert.match(retenido.salida[1].payload, /retenido/);
    assert.equal(retenido.avisos.length, 1);
    assert.equal(progreso(flow, "").salida, null);
    assert.equal(progreso(flow, Buffer.alloc(0)).salida, null);
    for (const malo of ["no es json", "[]", "7", "null", Buffer.from("{}"), "x".repeat(40000)]) {
        const r = progreso(flow, malo);
        assert.equal(r.salida[0], null);
        assert.match(r.salida[1].payload, /^Descartado, /);
    }
    assert.equal(JSON.stringify(flow.get("inscripcion")), antes);
});

test("progreso: el descarte nunca copia el contenido del mensaje (puede llevar una miniatura)", () => {
    const flow = abrirInscripcion();
    const r = progreso(flow, mensajeProgreso({ cuenta: OTRA, miniatura: "BASE64SECRETO" }));
    assert.equal(r.salida[0], null);
    assert.ok(!r.salida[1].payload.includes("BASE64SECRETO"));
    assert.ok(!r.salida[1].payload.includes(OTRA));
    const noJson = progreso(flow, "BASE64SECRETO no es json");
    assert.ok(!noJson.salida[1].payload.includes("BASE64SECRETO"));
});

test("progreso: solo cuenta la inscripción abierta, y solo si la cuenta coincide", () => {
    const sin = progreso(nuevoFlow(), mensajeProgreso());
    assert.match(sin.salida[1].payload, /no hay un registro abierto/);
    const flow = abrirInscripcion();
    const antes = JSON.stringify(flow.get("inscripcion"));
    for (const cuenta of [OTRA, "", undefined, 320068234]) {
        const r = progreso(flow, mensajeProgreso({ cuenta }));
        assert.equal(r.salida[0], null);
        assert.match(r.salida[1].payload, /cuenta no coincide/);
    }
    assert.equal(JSON.stringify(flow.get("inscripcion")), antes, "el progreso de otra cuenta no toca la inscripción");
    // Un registro ya cerrado tampoco se actualiza.
    ejecutar(ID.cancelar, { req: { method: "POST" } }, { flow });
    const cerrado = progreso(flow, mensajeProgreso());
    assert.match(cerrado.salida[1].payload, /no hay un registro abierto/);
});

test("progreso: actualiza la pose del mensaje y pasa a en_curso", () => {
    const flow = abrirInscripcion();
    progreso(flow, mensajeProgreso({ pose: "frente", estado: "aceptada" }));
    progreso(flow, mensajeProgreso({ pose: "izquierda", estado: "rechazada", motivo: "borrosa", paso: 2 }));
    progreso(flow, mensajeProgreso({ pose: "derecha", estado: "capturando", paso: 3 }));
    const insc = flow.get("inscripcion");
    assert.equal(insc.estado, "en_curso");
    assert.deepEqual(insc.poses.map((p) => [p.estado, p.motivo]), [
        ["aceptada", null], ["rechazada", "borrosa"], ["capturando", null], ["pendiente", null], ["pendiente", null]
    ]);
});

test("progreso: reintentar una pose rechazada borra el motivo anterior", () => {
    const flow = abrirInscripcion();
    progreso(flow, mensajeProgreso({ pose: "frente", estado: "rechazada", motivo: "rostro_chico" }));
    progreso(flow, mensajeProgreso({ pose: "frente", estado: "capturando" }));
    assert.deepEqual(flow.get("inscripcion").poses[0], { pose: "frente", estado: "capturando", motivo: null, miniatura: null });
});

test("progreso: el motivo solo se acepta como código en minúsculas; el texto libre no llega al frontend", () => {
    for (const motivo of ["<script>", "Borrosa", "borrosa y más", "x".repeat(41), 5, ""]) {
        const flow = abrirInscripcion();
        progreso(flow, mensajeProgreso({ pose: "frente", estado: "rechazada", motivo }));
        assert.equal(flow.get("inscripcion").poses[0].motivo, null, JSON.stringify(motivo));
    }
});

test("progreso: una pose o un estado desconocidos se descartan sin tocar la inscripción", () => {
    const flow = abrirInscripcion();
    const antes = JSON.stringify(flow.get("inscripcion"));
    assert.match(progreso(flow, mensajeProgreso({ pose: "saltar" })).salida[1].payload, /pose desconocida/);
    assert.match(progreso(flow, mensajeProgreso({ pose: "" })).salida[1].payload, /pose desconocida/);
    assert.match(progreso(flow, mensajeProgreso({ estado: "bailando" })).salida[1].payload, /estado desconocido/);
    assert.match(progreso(flow, mensajeProgreso({ estado: 7 })).salida[1].payload, /falta estado/);
    assert.equal(JSON.stringify(flow.get("inscripcion")), antes);
});

test("progreso: la miniatura se guarda en la pose, en el contexto del flujo, y no sale por ninguna salida", () => {
    const flow = abrirInscripcion();
    const r = progreso(flow, mensajeProgreso({ pose: "frente", estado: "aceptada", miniatura: "BASE64POSE" }));
    assert.equal(r.salida[0], null, "no revierte el consentimiento");
    assert.equal(r.salida[1], null, "no va al descarte");
    assert.equal(flow.get("inscripcion").poses[0].miniatura, "BASE64POSE");
});

test("progreso: una miniatura que no es base64 o que excede el tope se ignora con aviso, sin romper el mensaje", () => {
    const flow = abrirInscripcion();
    for (const mala of [12345, "BASE64_SECRETA", "", "A".repeat(12289)]) {
        const r = progreso(flow, mensajeProgreso({ pose: "frente", estado: "aceptada", miniatura: mala }));
        assert.equal(r.salida[0], null, "el mensaje sigue su curso");
        assert.equal(r.salida[1], null);
        assert.equal(flow.get("inscripcion").poses[0].estado, "aceptada");
        assert.equal(flow.get("inscripcion").poses[0].miniatura, null, JSON.stringify(mala));
        assert.ok(r.avisos.length >= 1, "avisa: " + JSON.stringify(mala));
        assert.ok(!r.avisos.join(" ").includes("BASE64_SECRETA"));
    }
});

test("progreso: un reintento sin miniatura borra la de la pose anterior (sería la foto de otra toma)", () => {
    const flow = abrirInscripcion();
    progreso(flow, mensajeProgreso({ pose: "frente", estado: "rechazada", motivo: "borrosa", miniatura: "BASE64POSE" }));
    assert.equal(flow.get("inscripcion").poses[0].miniatura, "BASE64POSE");
    progreso(flow, mensajeProgreso({ pose: "frente", estado: "capturando" }));
    assert.equal(flow.get("inscripcion").poses[0].miniatura, null);
});

test("progreso: una miniatura sin registro abierto se descarta y no se guarda en ninguna parte", () => {
    const flow = nuevoFlow();
    const r = progreso(flow, mensajeProgreso({ pose: "frente", estado: "aceptada", miniatura: "BASE64POSE" }));
    assert.equal(r.salida[0], null);
    assert.match(r.salida[1].payload, /no hay un registro abierto/);
    assert.ok(!r.salida[1].payload.includes("BASE64POSE"));
    assert.equal(flow.get("inscripcion"), undefined);
});

test("progreso: cada mensaje aceptado renueva el plazo de 5 minutos", () => {
    const flow = abrirInscripcion();
    const antes = flow.get("inscripcion").actualizado_ms;
    ejecutar(ID.progreso, { topic: "aula/vision/enrolamiento", payload: mensajeProgreso(), retain: false }, { flow, ahora: antes + 120000 });
    assert.equal(flow.get("inscripcion").actualizado_ms, antes + 120000);
    // Un mensaje de otra cuenta no lo renueva.
    ejecutar(ID.progreso, { topic: "aula/vision/enrolamiento", payload: mensajeProgreso({ cuenta: OTRA }), retain: false }, { flow, ahora: antes + 240000 });
    assert.equal(flow.get("inscripcion").actualizado_ms, antes + 120000);
});

test("progreso: la advertencia guarda la cuenta parecida (o null si no es una cuenta) y no detiene el registro", () => {
    const flow = abrirInscripcion();
    progreso(flow, mensajeProgreso({ estado: "advertencia", parecido_a: OTRA }));
    assert.deepEqual(flow.get("inscripcion").advertencia, { parecido_a: OTRA });
    assert.equal(flow.get("inscripcion").abierto, true);
    progreso(flow, mensajeProgreso({ estado: "advertencia", parecido_a: "no-cuenta" }));
    assert.deepEqual(flow.get("inscripcion").advertencia, { parecido_a: null });
});

test("progreso: completado deja la inscripción ABIERTA en espera del embedding (la visión lo publica después, por otro tópico)", () => {
    const flow = abrirInscripcion();
    const r = progreso(flow, mensajeProgreso({ pose: "frente_2", estado: "completado", paso: 5 }));
    assert.equal(r.salida[0], null, "completado no revierte el consentimiento");
    const insc = flow.get("inscripcion");
    assert.equal(insc.abierto, true);
    assert.equal(insc.estado, "guardando");
    // Y el embedding que llega a continuación sí se acepta.
    const e = ejecutar(ID.validarEmbedding, { topic: "aula/vision/embedding", payload: mensajeEmbedding(), retain: false }, { flow });
    assert.deepEqual(e.salida[0].params[1], CUENTA);
});

test("progreso: una pose tardía no saca a la inscripción de 'guardando'", () => {
    const flow = abrirInscripcion();
    progreso(flow, mensajeProgreso({ estado: "completado", pose: "frente_2" }));
    progreso(flow, mensajeProgreso({ estado: "aceptada", pose: "frente_2", paso: 5 }));
    assert.equal(flow.get("inscripcion").estado, "guardando");
    assert.equal(flow.get("inscripcion").poses[4].estado, "aceptada");
});

test("progreso: cancelado cierra la inscripción y manda revertir el consentimiento", () => {
    const flow = abrirInscripcion();
    const r = progreso(flow, mensajeProgreso({ estado: "cancelado", pose: "derecha", paso: 3 }));
    assert.deepEqual(r.salida[0], { params: [CUENTA] });
    const insc = flow.get("inscripcion");
    assert.equal(insc.abierto, false);
    assert.equal(insc.estado, "cancelado");
    assert.equal(insc.motivo, null);
});

test("progreso: error cierra con su motivo y revierte; un motivo raro queda como 'desconocido'", () => {
    const a = abrirInscripcion();
    const r = progreso(a, mensajeProgreso({ estado: "error", motivo: "inconsistente" }));
    assert.deepEqual(r.salida[0], { params: [CUENTA] });
    assert.equal(a.get("inscripcion").abierto, false);
    assert.equal(a.get("inscripcion").estado, "error");
    assert.equal(a.get("inscripcion").motivo, "inconsistente");
    const b = abrirInscripcion();
    progreso(b, mensajeProgreso({ estado: "error", motivo: "Texto libre" }));
    assert.equal(b.get("inscripcion").motivo, "desconocido");
    const c = abrirInscripcion();
    progreso(c, mensajeProgreso({ estado: "error" }));
    assert.equal(c.get("inscripcion").motivo, "desconocido");
});

test("progreso: el rechazo 'ocupado' de la visión (paso 0, pose vacía) cierra el registro de esa cuenta", () => {
    const flow = abrirInscripcion();
    const r = progreso(flow, mensajeProgreso({ estado: "error", motivo: "ocupado", paso: 0, pose: "" }));
    assert.deepEqual(r.salida[0], { params: [CUENTA] });
    assert.equal(flow.get("inscripcion").motivo, "ocupado");
});

test("progreso: el rechazo de otra cuenta, o con cuenta vacía, no cierra el registro abierto", () => {
    const flow = abrirInscripcion();
    for (const cuenta of ["", OTRA]) {
        progreso(flow, mensajeProgreso({ estado: "error", motivo: "ocupado", paso: 0, pose: "", cuenta }));
    }
    assert.equal(flow.get("inscripcion").abierto, true);
});

// ------------------------------------------------ embedding ligado a su inscripción

test("embedding de R1 que se confirma tarde con R2 abierto para la misma cuenta: se retira, no se da de alta y R2 no se toca", () => {
    const flow = abrirInscripcion();
    const r1 = flow.get("inscripcion").id;
    const e = embedding(flow, mensajeEmbedding());
    assert.equal(e.salida[0].inscripcion_id, r1, "la validación liga el embedding a su inscripción");
    ejecutar(ID.cancelar, { req: { method: "POST" } }, { flow }); // R1 cancelado
    // R2: otro registro de la misma cuenta, ya abierto.
    const apartado = ejecutar(ID.validarApartar, peticionPost({ cuenta: CUENTA, consentimiento: true }), { flow, ahora: Date.now() + 10 });
    ejecutar(ID.abrir, { ...apartado.salida[0], payload: [{ existe: 1, nombre: NOMBRE, tiene_rostro: false, actualizadas: 1 }] }, { flow });
    const r2 = flow.get("inscripcion");
    assert.notEqual(r2.id, r1);
    assert.equal(r2.abierto, true);
    const g = trasGuardar(flow, FILA_GUARDADA, r1); // el UPDATE de R1 se confirma ahora
    assert.equal(g.salida[0], null, "sin alta: ese embedding no es de R2");
    assert.deepEqual(g.salida[2], { params: [CUENTA, FECHA], fuente: "retiro_tardio" });
    assert.equal(flow.get("inscripcion").abierto, true, "R2 sigue abierto");
    // R2 se quedó sin consentimiento: cuando llegue su embedding, la base no lo guarda y R2 falla cerrado.
    const f = trasGuardar(flow, [], r2.id);
    assert.deepEqual(f.salida[1], { params: [CUENTA] });
    assert.equal(flow.get("inscripcion").motivo, "no_guardado");
});

test("embedding: un error al guardar el de R1 no cierra el registro R2 de la misma cuenta", () => {
    const flow = abrirInscripcion();
    const r1 = flow.get("inscripcion").id;
    ejecutar(ID.cancelar, { req: { method: "POST" } }, { flow });
    const apartado = ejecutar(ID.validarApartar, peticionPost({ cuenta: CUENTA, consentimiento: true }), { flow, ahora: Date.now() + 10 });
    ejecutar(ID.abrir, { ...apartado.salida[0], payload: [{ existe: 1, nombre: NOMBRE, tiene_rostro: false, actualizadas: 1 }] }, { flow });
    const r = ejecutar(ID.cerrar500, { fuente: "embedding", inscripcion_id: r1, params: ["[0.1]", CUENTA], error: { code: "57P01" } }, { flow });
    assert.equal(r.salida[1], null);
    assert.equal(flow.get("inscripcion").abierto, true);
});

// ------------------------------------------------ retiro tardío que falla

function retiroFallido(cuenta = CUENTA, fecha = FECHA, extra = {}) {
    return { fuente: "retiro_tardio", params: [cuenta, fecha], error: { code: "57P01" }, ...extra };
}

test("retiro tardío: si el UPDATE falla, el par cuenta/fecha queda pendiente (sin duplicarse) y no se responde ni se revierte nada", () => {
    const flow = nuevoFlow();
    const a = ejecutar(ID.cerrar500, retiroFallido(), { flow });
    ejecutar(ID.cerrar500, retiroFallido(), { flow });
    assert.equal(a.salida[0], null);
    assert.equal(a.salida[1], null);
    assert.deepEqual(retirosPendientes(flow), [{ cuenta: CUENTA, fecha: FECHA }]);
    assert.ok(!a.avisos.join("\n").includes(CUENTA));
    assert.ok(!a.avisos.join("\n").includes(FECHA));
    // La misma cuenta con otro consentimiento es otro par; y una fecha o cuenta mal formada no entra.
    ejecutar(ID.cerrar500, retiroFallido(CUENTA, OTRA_FECHA), { flow });
    ejecutar(ID.cerrar500, retiroFallido("abc"), { flow });
    ejecutar(ID.cerrar500, retiroFallido(CUENTA, "ayer"), { flow });
    ejecutar(ID.cerrar500, retiroFallido(CUENTA, undefined), { flow });
    ejecutar(ID.cerrar500, { fuente: "retiro_tardio", params: [CUENTA], error: { code: "57P01" } }, { flow });
    assert.deepEqual(retirosPendientes(flow), [{ cuenta: CUENTA, fecha: FECHA }, { cuenta: CUENTA, fecha: OTRA_FECHA }]);
});

test("retiro tardío: retiros_pendientes va al almacén persistente y nunca a memoria", () => {
    const flow = nuevoFlow();
    ejecutar(ID.cerrar500, retiroFallido(), { flow });
    assert.equal(flow.get("retiros_pendientes"), undefined, "no está en el almacén de memoria");
    assert.equal(retirosPendientes(flow).length, 1);
    ejecutar(ID.baja_tardia, { params: [CUENTA, FECHA], fuente: "retiro_tardio", payload: [{ no_cuenta: CUENTA }] }, { flow });
    assert.equal(retirosPendientes(flow), undefined);
    assert.equal(flow.get("retiros_pendientes"), undefined);
    // El resto del contexto de la pestaña sigue en memoria (settings.js no cambia el almacén por defecto).
    const abierto = abrirInscripcion();
    assert.equal(abierto.get("inscripcion", "persistente"), undefined);
    assert.ok(abierto.get("inscripcion"));
});

// Solo comprueba el texto de settings.js. Que Node-RED cargue el almacén y escriba en /data/context
// se verifica en la placa (orangepi/AGENTS.md, Verificación).
test("settings.js declara el almacén persistente (localfilesystem) que usa el flujo y deja memoria por defecto", () => {
    const texto = fs.readFileSync(path.join(__dirname, "..", "settings.js"), "utf8");
    assert.match(texto, /contextStorage:\s*\{/);
    assert.match(texto, /default:\s*\{\s*module:\s*"memory"\s*\}/);
    assert.match(texto, /persistente:\s*\{\s*module:\s*"localfilesystem"/);
    // En /data (volumen nodered_data), con el directorio de contexto por defecto: /data/context.
    assert.match(texto, /dir:\s*"\/data"/);
    assert.match(texto, /flushInterval:\s*1\b/);
});

test("retiro tardío: cada tick de 15 s lo reintenta con cuenta y fecha hasta que sale; entonces se publica la baja y deja de reintentarse", () => {
    const flow = nuevoFlow();
    ejecutar(ID.cerrar500, retiroFallido(), { flow });
    for (let i = 0; i < 3; i++) {
        const r = ejecutar(ID.vencer, {}, { flow });
        assert.deepEqual(r.salida[3], [{ params: [CUENTA, FECHA], fuente: "retiro_tardio" }], "reintento " + i);
        assert.equal(r.salida[0], null);
        assert.equal(r.salida[1], null);
    }
    const ok = ejecutar(ID.baja_tardia, { params: [CUENTA, FECHA], fuente: "retiro_tardio", payload: [{ no_cuenta: CUENTA }] }, { flow });
    assert.equal(ok.salida.topic, "aula/vision/galeria/baja");
    assert.equal(ok.salida.payload, JSON.stringify({ cuenta: CUENTA }));
    assert.equal(retirosPendientes(flow), undefined);
    assert.equal(ejecutar(ID.vencer, {}, { flow }).salida, null);
});

test("retiro tardío: lo que quedó en el almacén con otra forma (cuentas sueltas, fechas ilegibles) no se reintenta ni rompe el tick", () => {
    const flow = nuevoFlow();
    flow.set("retiros_pendientes", [CUENTA, null, 7, { cuenta: CUENTA }, { cuenta: "abc", fecha: FECHA }, { cuenta: CUENTA, fecha: "ayer" }, { cuenta: OTRA, fecha: OTRA_FECHA }], "persistente");
    const r = ejecutar(ID.vencer, {}, { flow });
    assert.deepEqual(r.salida[3], [{ params: [OTRA, OTRA_FECHA], fuente: "retiro_tardio" }]);
});

test("retiro tardío: no se reintenta una cuenta con un registro abierto, y abrir un registro nuevo de esa cuenta lo cancela", () => {
    const flow = abrirInscripcion();
    flow.set("retiros_pendientes", [{ cuenta: CUENTA, fecha: FECHA }, { cuenta: OTRA, fecha: OTRA_FECHA }], "persistente");
    const r = ejecutar(ID.vencer, {}, { flow });
    assert.deepEqual(r.salida[3], [{ params: [OTRA, OTRA_FECHA], fuente: "retiro_tardio" }]);
    // Si el consentimiento se escribió es porque la cuenta no tenía rostro: el retiro pendiente ya no hace falta.
    const flow2 = nuevoFlow();
    flow2.set("retiros_pendientes", [{ cuenta: CUENTA, fecha: FECHA }, { cuenta: CUENTA, fecha: OTRA_FECHA }, { cuenta: OTRA, fecha: OTRA_FECHA }], "persistente");
    const apartado = ejecutar(ID.validarApartar, peticionPost({ cuenta: CUENTA, consentimiento: true }), { flow: flow2 });
    ejecutar(ID.abrir, { ...apartado.salida[0], payload: [{ existe: 1, nombre: NOMBRE, tiene_rostro: false, actualizadas: 1 }] }, { flow: flow2 });
    assert.deepEqual(retirosPendientes(flow2), [{ cuenta: OTRA, fecha: OTRA_FECHA }]);
    // Sin otros pendientes, la clave desaparece.
    const flow3 = nuevoFlow();
    flow3.set("retiros_pendientes", [{ cuenta: CUENTA, fecha: FECHA }], "persistente");
    const apartado3 = ejecutar(ID.validarApartar, peticionPost({ cuenta: CUENTA, consentimiento: true }), { flow: flow3 });
    ejecutar(ID.abrir, { ...apartado3.salida[0], payload: [{ existe: 1, nombre: NOMBRE, tiene_rostro: false, actualizadas: 1 }] }, { flow: flow3 });
    assert.equal(retirosPendientes(flow3), undefined);
});

test("retiro tardío: el reintento sale junto con el vencimiento de un registro", () => {
    const flow = abrirInscripcion();
    flow.set("retiros_pendientes", [{ cuenta: OTRA, fecha: OTRA_FECHA }], "persistente");
    let r;
    for (let i = 0; i < 20; i++) {
        r = ejecutar(ID.vencer, {}, { flow });
    }
    assert.ok(r.salida[0], "vence");
    assert.deepEqual(r.salida[3], [{ params: [OTRA, OTRA_FECHA], fuente: "retiro_tardio" }]);
});

test("retiro tardío: tras un reinicio (el almacén persistente conserva el par) el primer tick lo reintenta", () => {
    const antes = nuevoFlow();
    ejecutar(ID.cerrar500, retiroFallido(), { flow: antes });
    // Node-RED reinicia: la memoria se pierde, el almacén persistente no.
    const despues = nuevoFlow();
    despues.set("retiros_pendientes", antes.get("retiros_pendientes", "persistente"), "persistente");
    const arranque = ejecutar(ID.arrancar, { payload: null }, { flow: despues });
    assert.equal(arranque.flow.get("inscripcion"), undefined);
    assert.deepEqual(retirosPendientes(despues), [{ cuenta: CUENTA, fecha: FECHA }], "el arranque no borra los retiros pendientes");
    const r = ejecutar(ID.vencer, {}, { flow: despues });
    assert.deepEqual(r.salida[3], [{ params: [CUENTA, FECHA], fuente: "retiro_tardio" }]);
});

// El retiro tardío solo actúa sobre el consentimiento que escribió el embedding: la consulta lo
// condiciona a consentimiento_fecha y "Publicar baja tras retiro tardío" distingue si actuó.
test("retiro tardío: el UPDATE del embedding devuelve la fecha del consentimiento y el retiro la exige igual", () => {
    const guardar = nodo("a1f0c0de00000077").query;
    assert.match(guardar, /RETURNING no_cuenta,\s*to_char\(consentimiento_fecha AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS\.US"Z"'\) AS consentimiento_fecha/);
    const retirar = nodo("a1f0c0de00000088").query;
    assert.match(retirar, /WHERE no_cuenta = \$1 AND consentimiento_fecha = \$2::timestamptz/);
    assert.match(retirar, /RETURNING no_cuenta\s*$/);
});

test("retiro tardío: con 1 fila se publica la baja; con 0 filas, un alta de reconciliación; en ambos casos el par sale de pendientes", () => {
    const flow = nuevoFlow();
    flow.set("retiros_pendientes", [{ cuenta: CUENTA, fecha: FECHA }, { cuenta: CUENTA, fecha: OTRA_FECHA }, { cuenta: OTRA, fecha: FECHA }], "persistente");
    // 0 filas: el retiro ya había actuado (Node-RED murió antes de publicar) o la fila tiene otro consentimiento.
    // La alta hace que la visión recargue la galería completa y quede igual que la base, en cualquiera de los dos casos.
    const sin = ejecutar(ID.baja_tardia, { params: [CUENTA, FECHA], fuente: "retiro_tardio", payload: [] }, { flow });
    assert.equal(sin.salida.topic, "aula/vision/galeria/alta");
    assert.equal(sin.salida.payload, JSON.stringify({ cuenta: CUENTA }));
    assert.equal(sin.salida.qos, 1);
    assert.equal(sin.salida.retain, false);
    assert.equal(sin.avisos.length, 1);
    assert.ok(!sin.avisos[0].includes(CUENTA));
    assert.deepEqual(retirosPendientes(flow), [{ cuenta: CUENTA, fecha: OTRA_FECHA }, { cuenta: OTRA, fecha: FECHA }], "solo sale el par que se resolvió");
    const con = ejecutar(ID.baja_tardia, { params: [CUENTA, OTRA_FECHA], fuente: "retiro_tardio", payload: [{ no_cuenta: CUENTA }] }, { flow });
    assert.equal(con.salida.topic, "aula/vision/galeria/baja");
    assert.equal(con.salida.payload, JSON.stringify({ cuenta: CUENTA }));
    assert.equal(con.salida.qos, 1);
    assert.equal(con.salida.retain, false);
    assert.deepEqual(retirosPendientes(flow), [{ cuenta: OTRA, fecha: FECHA }]);
});

test("retiro tardío: una ejecución duplicada del mismo retiro publica baja una sola vez; la segunda solo reconcilia con un alta", () => {
    const flow = nuevoFlow();
    ejecutar(ID.cerrar500, retiroFallido(), { flow });
    const primero = ejecutar(ID.baja_tardia, { params: [CUENTA, FECHA], fuente: "retiro_tardio", payload: [{ no_cuenta: CUENTA }] }, { flow });
    assert.equal(primero.salida.topic, "aula/vision/galeria/baja");
    // La segunda ejecución ya no encuentra el consentimiento (fecha NULL tras el primer retiro): 0 filas.
    const segundo = ejecutar(ID.baja_tardia, { params: [CUENTA, FECHA], fuente: "retiro_tardio", payload: [] }, { flow });
    assert.equal(segundo.salida.topic, "aula/vision/galeria/alta", "no hay segunda baja");
    assert.equal(retirosPendientes(flow), undefined);
});

// ------------------------------------------------ ts inválido o del futuro

test("embedding: un ts ilegible, vacío o de antes de 2000 en la cuenta abierta termina el registro en embedding_invalido y revierte el consentimiento", () => {
    for (const extra of [{ ts: undefined }, { ts: "" }, { ts: "ayer" }, { ts: 7 }, { ts: null }, { ts: "1970-01-01T00:00:00Z" }, { ts: "1999-12-31T23:59:59Z" }]) {
        const flow = abrirInscripcion();
        const r = embedding(flow, mensajeEmbedding(extra));
        assert.equal(r.salida[0], null, JSON.stringify(extra));
        assert.deepEqual(r.salida[2], { params: [CUENTA] }, JSON.stringify(extra));
        assert.match(r.salida[1].payload, /ts ilegible/, JSON.stringify(extra));
        assert.equal(flow.get("inscripcion").abierto, false);
        assert.equal(flow.get("inscripcion").motivo, "embedding_invalido");
    }
});

test("embedding: de otra cuenta, el ts inválido no cierra nada (la cuenta se comprueba antes)", () => {
    const flow = abrirInscripcion();
    const r = embedding(flow, mensajeEmbedding({ cuenta: OTRA, ts: "ayer" }));
    assert.equal(r.salida[2], null);
    assert.equal(flow.get("inscripcion").abierto, true);
});

test("ts posterior a ahora + 5 min: se descarta en progreso y en embedding, sin cerrar nada; con 4 min se acepta", () => {
    const flow = abrirInscripcion();
    const antes = JSON.stringify(flow.get("inscripcion"));
    const p = progreso(flow, mensajeProgreso({ ts: ts(10 * 60 * 1000) }));
    assert.equal(p.salida[0], null);
    assert.match(p.salida[1].payload, /posterior/);
    const e = embedding(flow, mensajeEmbedding({ ts: ts(10 * 60 * 1000) }));
    assert.equal(e.salida[0], null);
    assert.equal(e.salida[2], null);
    assert.match(e.salida[1].payload, /posterior/);
    assert.equal(JSON.stringify(flow.get("inscripcion")), antes);
    assert.equal(progreso(flow, mensajeProgreso({ ts: ts(4 * 60 * 1000), pose: "frente", estado: "aceptada" })).salida[1], null);
    assert.ok(embedding(flow, mensajeEmbedding({ ts: ts(4 * 60 * 1000) })).salida[0]);
});

// ------------------------------------------------ sesión rezagada (ts)

test("progreso: un mensaje con ts anterior al inicio del registro (sesión pasada de la misma cuenta) se descarta sin tocar nada", () => {
    const flow = abrirInscripcion();
    const antes = JSON.stringify(flow.get("inscripcion"));
    for (const estado of ["cancelado", "error", "aceptada", "completado"]) {
        const r = progreso(flow, mensajeProgreso({ estado, ts: ts(-10 * 60 * 1000) }));
        assert.equal(r.salida[0], null, estado);
        assert.match(r.salida[1].payload, /anterior al inicio/, estado);
    }
    assert.equal(JSON.stringify(flow.get("inscripcion")), antes);
    assert.equal(flow.get("inscripcion").abierto, true);
});

test("progreso: sin ts, con ts ilegible o con ts que no es texto, se descarta", () => {
    const flow = abrirInscripcion();
    for (const extra of [{ ts: undefined }, { ts: "ayer" }, { ts: 12345 }, { ts: null }, { ts: "" }]) {
        const r = progreso(flow, mensajeProgreso(extra));
        assert.equal(r.salida[0], null, JSON.stringify(extra));
        assert.match(r.salida[1].payload, /falta ts/, JSON.stringify(extra));
    }
    assert.equal(flow.get("inscripcion").poses[0].estado, "pendiente");
});

test("progreso: 2 s de margen al inicio: un ts 1 s antes se acepta y 10 s antes no", () => {
    const flow = abrirInscripcion();
    const inicio = flow.get("inscripcion").inicio_ms;
    const con = (desfase) => JSON.parse(mensajeProgreso({ pose: "frente", estado: "aceptada" }).replace(/"ts":"[^"]+"/, '"ts":"' + new Date(inicio + desfase).toISOString() + '"'));
    assert.equal(progreso(flow, JSON.stringify(con(-1000))).salida[1], null);
    assert.match(progreso(flow, JSON.stringify(con(-10000))).salida[1].payload, /anterior al inicio/);
});

test("embedding: un embedding con ts anterior al inicio no se guarda ni cierra el registro nuevo", () => {
    const flow = abrirInscripcion();
    const antes = JSON.stringify(flow.get("inscripcion"));
    const r = embedding(flow, mensajeEmbedding({ ts: ts(-10 * 60 * 1000) }));
    assert.equal(r.salida[0], null);
    assert.equal(r.salida[2], null, "no revierte el consentimiento del registro nuevo");
    assert.match(r.salida[1].payload, /anterior al inicio/);
    assert.equal(JSON.stringify(flow.get("inscripcion")), antes);
});

test("embedding: el ts de un registro anterior de la misma cuenta no pasa tras cancelar y reabrir", () => {
    const flow = abrirInscripcion();
    const tsViejo = ts(0);
    ejecutar(ID.cancelar, { req: { method: "POST" } }, { flow });
    // Reabre mucho después: el relojFijo mueve inicio_ms a +60 s.
    const r = ejecutar(ID.validarApartar, peticionPost({ cuenta: CUENTA, consentimiento: true }), { flow, ahora: Date.now() + 60000 });
    assert.ok(r.salida[0]);
    const e = embedding(flow, mensajeEmbedding({ ts: tsViejo }));
    assert.equal(e.salida[0], null);
    assert.match(e.salida[1].payload, /anterior al inicio/);
});

// ------------------------------------------------ embedding (aula/vision/embedding)

function embedding(flow, payload, extra = {}) {
    return ejecutar(ID.validarEmbedding, { topic: "aula/vision/embedding", payload, retain: false, ...extra }, { flow });
}

test("embedding: descarta lo retenido, ignora lo vacío y descarta lo que no es un objeto JSON", () => {
    const flow = abrirInscripcion();
    const antes = JSON.stringify(flow.get("inscripcion"));
    const retenido = embedding(flow, mensajeEmbedding(), { retain: true });
    assert.equal(retenido.salida[0], null);
    assert.match(retenido.salida[1].payload, /retenido/);
    assert.equal(embedding(flow, "").salida, null);
    assert.equal(embedding(flow, Buffer.alloc(0)).salida, null);
    for (const malo of ["no es json", "[]", "null", Buffer.from("{}"), "x".repeat(70000)]) {
        const r = embedding(flow, malo);
        assert.equal(r.salida[0], null);
        assert.equal(r.salida[2], null);
        assert.match(r.salida[1].payload, /^Descartado, /);
    }
    assert.equal(JSON.stringify(flow.get("inscripcion")), antes);
});

test("embedding: sin inscripción abierta, o de otra cuenta, no se guarda y no toca nada", () => {
    const sin = embedding(nuevoFlow(), mensajeEmbedding());
    assert.equal(sin.salida[0], null);
    assert.match(sin.salida[1].payload, /no hay un registro abierto/);
    const flow = abrirInscripcion();
    const antes = JSON.stringify(flow.get("inscripcion"));
    for (const cuenta of [OTRA, "", undefined]) {
        const r = embedding(flow, mensajeEmbedding({ cuenta }));
        assert.equal(r.salida[0], null, "nunca se guarda para otra cuenta");
        assert.equal(r.salida[2], null, "ni revierte el consentimiento de la cuenta abierta");
        assert.match(r.salida[1].payload, /cuenta no coincide/);
    }
    assert.equal(JSON.stringify(flow.get("inscripcion")), antes);
    ejecutar(ID.cancelar, { req: { method: "POST" } }, { flow });
    assert.match(embedding(flow, mensajeEmbedding()).salida[1].payload, /no hay un registro abierto/, "tras cancelar, un embedding tardío no se guarda");
});

test("embedding: el descarte no copia ni la cuenta ni los números del vector", () => {
    const flow = abrirInscripcion();
    const r = embedding(flow, mensajeEmbedding({ cuenta: OTRA, embedding: [0.123456, 0.654321] }));
    assert.ok(!r.salida[1].payload.includes(OTRA));
    assert.ok(!r.salida[1].payload.includes("0.123456"));
    assert.equal(r.avisos.length, 0);
});

test("embedding: válido, sale el vector como texto enlazado y la cuenta de la inscripción", () => {
    const flow = abrirInscripcion();
    const r = embedding(flow, mensajeEmbedding());
    assert.equal(r.salida[1], null);
    assert.equal(r.salida[2], null);
    const msg = r.salida[0];
    assert.equal(msg.params.length, 2);
    assert.match(msg.params[0], /^\[[-0-9.e,]+\]$/);
    assert.equal(JSON.parse(msg.params[0]).length, 512);
    assert.deepEqual(JSON.parse(msg.params[0]), vector());
    assert.equal(msg.params[1], CUENTA);
    assert.equal(msg.fuente, "embedding");
    assert.equal(msg.payload, null, "el vector no viaja en el payload del mensaje");
    assert.equal(flow.get("inscripcion").abierto, true, "se cierra al guardar, no antes");
});

test("embedding: dim distinta de 512, arreglo de otro tamaño o con valores no numéricos terminan el registro en error", () => {
    const casos = [
        mensajeEmbedding({ dim: 128, embedding: vector(128) }),
        mensajeEmbedding({ dim: 511, embedding: vector(511) }),
        mensajeEmbedding({ dim: 513, embedding: vector(513) }),
        mensajeEmbedding({ dim: "512" }),
        mensajeEmbedding({ dim: undefined }),
        mensajeEmbedding({ dim: 512, embedding: vector(511) }),
        mensajeEmbedding({ embedding: "no es un arreglo" }),
        mensajeEmbedding({ embedding: undefined }),
        mensajeEmbedding({ embedding: [...vector(511), null] }),
        mensajeEmbedding({ embedding: [...vector(511), "0.5"] }),
        mensajeEmbedding({ modelo: undefined }),
        mensajeEmbedding({ modelo: "  " }),
        mensajeEmbedding({ modelo: 4 })
    ];
    for (const payload of casos) {
        const flow = abrirInscripcion();
        const r = embedding(flow, payload);
        assert.equal(r.salida[0], null, "no se guarda");
        assert.deepEqual(r.salida[2], { params: [CUENTA] }, "se revierte el consentimiento");
        assert.match(r.salida[1].payload, /^Descartado, /);
        assert.ok(!/0\.00|-0\.00/.test(r.salida[1].payload), "no copia números del vector");
        const insc = flow.get("inscripcion");
        assert.equal(insc.abierto, false);
        assert.equal(insc.estado, "error");
        assert.equal(insc.motivo, "embedding_invalido");
    }
});

test("embedding: norma cero o lejos de 1 termina el registro en error; la norma del contrato (L2 = 1, 4 decimales) pasa", () => {
    for (const embeddingMalo of [Array(512).fill(0), vector(512, 2), vector(512, 0.5), vector(512, 1.02)]) {
        const flow = abrirInscripcion();
        const r = embedding(flow, mensajeEmbedding({ embedding: embeddingMalo }));
        assert.equal(r.salida[0], null);
        assert.deepEqual(r.salida[2], { params: [CUENTA] });
        assert.match(r.salida[1].payload, /norma/);
        assert.equal(flow.get("inscripcion").motivo, "embedding_invalido");
    }
    for (const bueno of [vector(512), vector(512, 1.005), vector(512, 0.995)]) {
        assert.ok(embedding(abrirInscripcion(), mensajeEmbedding({ embedding: bueno })).salida[0]);
    }
});

test("embedding: la consulta enlaza el vector con $1::vector y exige consentimiento y rostro vacío", () => {
    const { query } = nodo("a1f0c0de00000077");
    assert.match(query, /SET embedding = \$1::vector/);
    assert.match(query, /WHERE no_cuenta = \$2 AND consentimiento AND embedding IS NULL/);
    assert.match(query, /RETURNING no_cuenta/);
    assert.ok(!/consentimiento\s*=\s*true/i.test(query), "el manejador del embedding nunca escribe el consentimiento");
});

// ------------------------------------------------ tras guardar el embedding

// `inscripcionId` es el que "Validar embedding" dejó en el mensaje; por omisión, el de la inscripción actual.
function trasGuardar(flow, filas, inscripcionId = (flow.get("inscripcion") || {}).id) {
    return ejecutar(ID.trasGuardar, { params: ["[0.1]", CUENTA], payload: filas, inscripcion_id: inscripcionId }, { flow });
}

test("guardado: cierra como completado y publica galeria/alta con el payload del contrato", () => {
    const flow = abrirInscripcion();
    const r = trasGuardar(flow, FILA_GUARDADA);
    assert.equal(r.salida[1], null);
    assert.equal(r.salida[0].topic, "aula/vision/galeria/alta");
    assert.equal(r.salida[0].qos, 1);
    assert.equal(r.salida[0].retain, false);
    assert.equal(r.salida[0].payload, JSON.stringify({ cuenta: CUENTA }));
    const insc = flow.get("inscripcion");
    assert.equal(insc.abierto, false);
    assert.equal(insc.estado, "completado");
    assert.equal(insc.motivo, null);
});

test("guardado: si la base no escribió nada, error no_guardado, sin alta y con reversión", () => {
    const flow = abrirInscripcion();
    const r = trasGuardar(flow, []);
    assert.equal(r.salida[0], null);
    assert.deepEqual(r.salida[1], { params: [CUENTA] });
    assert.equal(flow.get("inscripcion").estado, "error");
    assert.equal(flow.get("inscripcion").motivo, "no_guardado");
    assert.equal(r.avisos.length, 1);
    assert.ok(!r.avisos[0].includes(CUENTA));
});

// Cancelación contra el embedding en vuelo: el UPDATE ya guardó el rostro cuando el registro
// quedó cerrado. No se da de alta: se retira el rostro y el consentimiento y se publica la baja.
function cerrarComo(flow, como) {
    if (como === "cancelado") {
        ejecutar(ID.cancelar, { req: { method: "POST" } }, { flow });
    } else if (como === "error") {
        progreso(flow, mensajeProgreso({ estado: "error", motivo: "npu" }));
    } else {
        for (let i = 0; i < 20; i++) {
            ejecutar(ID.vencer, {}, { flow });
        }
    }
    assert.equal(flow.get("inscripcion").abierto, false);
    return flow.get("inscripcion").estado;
}

for (const como of ["cancelado", "error", "vencido"]) {
    test("cancelar entre Validar embedding y Tras guardar embedding (" + como + "): no hay alta, se retira el rostro y se publica la baja", () => {
        const flow = abrirInscripcion();
        const e = embedding(flow, mensajeEmbedding()); // el embedding pasa la validación con el registro abierto
        assert.ok(e.salida[0]);
        const estadoCerrado = cerrarComo(flow, como); // y el registro se cierra mientras el UPDATE está en vuelo
        const g = trasGuardar(flow, FILA_GUARDADA); // el UPDATE sí guardó el rostro
        assert.equal(g.salida[0], null, "sin alta en la galería");
        assert.equal(g.salida[1], null);
        assert.deepEqual(g.salida[2], { params: [CUENTA, FECHA], fuente: "retiro_tardio" }, "se retira el rostro tardío");
        assert.equal(g.avisos.length, 1);
        assert.ok(!g.avisos[0].includes(CUENTA));
        assert.equal(flow.get("inscripcion").estado, estadoCerrado, "el estado de la API sigue diciendo la verdad");
        // Y la baja se publica DESPUÉS del UPDATE que retira el rostro.
        const b = ejecutar(ID.baja_tardia, { params: [CUENTA, FECHA], payload: [{ no_cuenta: CUENTA }] });
        assert.equal(b.salida.topic, "aula/vision/galeria/baja");
        assert.equal(b.salida.qos, 1);
        assert.equal(b.salida.retain, false);
        assert.equal(b.salida.payload, JSON.stringify({ cuenta: CUENTA }));
    });
}

test("guardado tardío: sin consentimiento_fecha en la fila no hay con qué condicionar el retiro: falla en lugar de retirar a ciegas", () => {
    for (const fila of [{ no_cuenta: CUENTA }, { no_cuenta: CUENTA, consentimiento_fecha: null }, { no_cuenta: CUENTA, consentimiento_fecha: "ayer" }, { no_cuenta: CUENTA, consentimiento_fecha: new Date().toISOString() }]) {
        const flow = abrirInscripcion();
        cerrarComo(flow, "cancelado");
        assert.throws(() => trasGuardar(flow, [fila]), /consentimiento_fecha/, JSON.stringify(fila));
    }
});

test("guardado con el registro abierto: no necesita la fecha para dar de alta (solo el retiro tardío la usa)", () => {
    const flow = abrirInscripcion();
    assert.ok(trasGuardar(flow, [{ no_cuenta: CUENTA }]).salida[0]);
});

test("guardado tardío: el embedding de una cuenta cuando hay otro registro abierto tampoco se da de alta", () => {
    const flow = abrirInscripcion(OTRA, "Otra Persona");
    const g = trasGuardar(flow, FILA_GUARDADA);
    assert.equal(g.salida[0], null);
    assert.deepEqual(g.salida[2], { params: [CUENTA, FECHA], fuente: "retiro_tardio" });
    assert.equal(flow.get("inscripcion").abierto, true, "no toca el registro abierto de la otra cuenta");
});

test("retiro tardío: la consulta deja embedding, consentimiento y fecha en NULL, enlazada y sin DELETE", () => {
    const { query } = nodo("a1f0c0de00000088");
    assert.match(query, /UPDATE alumnos SET embedding = NULL, consentimiento = false, consentimiento_fecha = NULL\s+WHERE no_cuenta = \$1/);
    assert.equal(query.includes(";"), false);
    assert.ok(!/DELETE/i.test(query));
});

// ------------------------------------------------ recorridos completos

test("recorrido: completado antes del embedding y alta final", () => {
    const flow = abrirInscripcion();
    POSES.forEach((pose, i) => {
        progreso(flow, mensajeProgreso({ pose, paso: i + 1, estado: "capturando" }));
        progreso(flow, mensajeProgreso({ pose, paso: i + 1, estado: "aceptada" }));
    });
    progreso(flow, mensajeProgreso({ pose: "frente_2", paso: 5, estado: "completado" }));
    assert.equal(ejecutar(ID.estado, { req: { method: "GET" } }, { flow }).salida.payload.estado, "guardando");
    const e = embedding(flow, mensajeEmbedding());
    assert.ok(e.salida[0]);
    const g = trasGuardar(flow, FILA_GUARDADA);
    assert.equal(g.salida[0].topic, "aula/vision/galeria/alta");
    const estado = ejecutar(ID.estado, { req: { method: "GET" } }, { flow }).salida.payload;
    assert.equal(estado.abierto, false);
    assert.equal(estado.estado, "completado");
    assert.deepEqual(estado.poses.map((p) => p.estado), ["aceptada", "aceptada", "aceptada", "aceptada", "aceptada"]);
});

test("recorrido: el embedding llega antes que 'completado': se guarda y el 'completado' tardío se descarta sin cambiar nada", () => {
    const flow = abrirInscripcion();
    progreso(flow, mensajeProgreso({ pose: "frente_2", paso: 5, estado: "aceptada" }));
    const e = embedding(flow, mensajeEmbedding());
    assert.ok(e.salida[0]);
    trasGuardar(flow, FILA_GUARDADA);
    const tardio = progreso(flow, mensajeProgreso({ pose: "frente_2", paso: 5, estado: "completado" }));
    assert.equal(tardio.salida[0], null);
    assert.equal(flow.get("inscripcion").estado, "completado");
});

test("recorrido: error de la visión a mitad del registro revierte el consentimiento y un embedding posterior no se guarda", () => {
    const flow = abrirInscripcion();
    progreso(flow, mensajeProgreso({ pose: "derecha", paso: 3, estado: "rechazada", motivo: "giro_excesivo" }));
    const r = progreso(flow, mensajeProgreso({ pose: "derecha", paso: 3, estado: "error", motivo: "npu" }));
    assert.deepEqual(r.salida[0], { params: [CUENTA] });
    assert.equal(embedding(flow, mensajeEmbedding()).salida[0], null);
});

test("recorrido: un error 'duplicado' con parecido_a cierra como cualquier error, revierte el consentimiento y no guarda el embedding", () => {
    const flow = abrirInscripcion();
    const r = progreso(flow, mensajeProgreso({ pose: "frente_2", paso: 5, estado: "error", motivo: "duplicado", parecido_a: "320068999" }));
    assert.deepEqual(r.salida[0], { params: [CUENTA] });
    assert.equal(r.salida[1], null);
    const estado = ejecutar(ID.estado, { req: { method: "GET" } }, { flow }).salida.payload;
    assert.equal(estado.abierto, false);
    assert.equal(estado.estado, "error");
    assert.equal(estado.motivo, "duplicado");
    // La cuenta parecida no sale por la API (solo la advertencia informativa la lleva).
    assert.equal(estado.advertencia, null);
    assert.equal(embedding(flow, mensajeEmbedding()).salida[0], null);
});

test("recorrido: 'galeria_no_disponible' (rechazo de la orden, paso 0, sin pose) cierra el registro como cualquier error y revierte el consentimiento", () => {
    const flow = abrirInscripcion();
    const r = progreso(flow, mensajeProgreso({ paso: 0, pose: "", estado: "error", motivo: "galeria_no_disponible" }));
    assert.deepEqual(r.salida[0], { params: [CUENTA] });
    const estado = ejecutar(ID.estado, { req: { method: "GET" } }, { flow }).salida.payload;
    assert.equal(estado.abierto, false);
    assert.equal(estado.estado, "error");
    assert.equal(estado.motivo, "galeria_no_disponible");
    assert.equal(embedding(flow, mensajeEmbedding()).salida[0], null);
});

test("recorrido: al agotarse los intentos el error trae el motivo del ultimo rechazo (no 'npu') y se muestra tal cual", () => {
    for (const motivo of ["pose_incorrecta", "giro_excesivo", "varios_rostros", "inconsistente", "sin_rostro", "fallo_captura"]) {
        const flow = abrirInscripcion();
        progreso(flow, mensajeProgreso({ pose: "izquierda", paso: 2, estado: "rechazada", motivo }));
        const r = progreso(flow, mensajeProgreso({ pose: "izquierda", paso: 2, estado: "error", motivo }));
        assert.deepEqual(r.salida[0], { params: [CUENTA] }, motivo);
        const estado = ejecutar(ID.estado, { req: { method: "GET" } }, { flow }).salida.payload;
        assert.equal(estado.abierto, false, motivo);
        assert.equal(estado.motivo, motivo);
    }
});

// ------------------------------------------------ vencimiento y arranque

test("vencer: sin registro, cerrado o dentro del plazo no hace nada", () => {
    assert.equal(ejecutar(ID.vencer, {}).salida, null);
    const flow = abrirInscripcion();
    for (let i = 0; i < 19; i++) {
        assert.equal(ejecutar(ID.vencer, {}, { flow }).salida, null);
    }
    assert.equal(flow.get("inscripcion").abierto, true);
    ejecutar(ID.cancelar, { req: { method: "POST" } }, { flow });
    assert.equal(ejecutar(ID.vencer, {}, { flow }).salida, null);
});

test("vencer: a los 20 ticks de 15 s (5 minutos) sin progreso cierra como 'vencido', ordena cancelar y revierte el consentimiento", () => {
    const flow = abrirInscripcion();
    let r;
    for (let i = 0; i < 20; i++) {
        r = ejecutar(ID.vencer, {}, { flow });
    }
    assert.equal(r.salida[0].topic, "aula/vision/cmd");
    assert.equal(r.salida[0].qos, 1);
    assert.equal(r.salida[0].retain, false);
    assert.deepEqual(JSON.parse(r.salida[0].payload), { accion: "cancelar", cuenta: CUENTA });
    assert.deepEqual(r.salida[1], { params: [CUENTA] });
    assert.equal(r.salida[2], null);
    const insc = flow.get("inscripcion");
    assert.equal(insc.abierto, false);
    assert.equal(insc.estado, "error");
    assert.equal(insc.motivo, "vencido");
    assert.equal(r.avisos.length, 1);
    // Un embedding tardío ya no se acepta.
    assert.equal(embedding(flow, mensajeEmbedding()).salida[0], null);
});

test("vencer: no depende del reloj de pared; un salto del reloj no vence ni salva un registro", () => {
    const flow = abrirInscripcion();
    const t0 = flow.get("inscripcion").actualizado_ms;
    // El reloj salta una hora hacia adelante: con un solo tick no vence.
    assert.equal(ejecutar(ID.vencer, {}, { flow, ahora: t0 + 3600 * 1000 }).salida, null);
    assert.equal(flow.get("inscripcion").abierto, true);
    // Y el reloj parado (o hacia atrás) no impide vencer tras los 20 ticks.
    let r;
    for (let i = 0; i < 19; i++) { // con el tick de arriba son 20
        r = ejecutar(ID.vencer, {}, { flow, ahora: t0 - 3600 * 1000 });
    }
    assert.equal(flow.get("inscripcion").motivo, "vencido");
    assert.ok(r.salida);
});

test("vencer: un mensaje de progreso de este registro renueva el plazo", () => {
    const flow = abrirInscripcion();
    for (let i = 0; i < 16; i++) {
        ejecutar(ID.vencer, {}, { flow });
    }
    progreso(flow, mensajeProgreso());
    for (let i = 0; i < 19; i++) {
        assert.equal(ejecutar(ID.vencer, {}, { flow }).salida, null);
    }
    assert.ok(ejecutar(ID.vencer, {}, { flow }).salida);
});

test("vencer: reintenta la limpieza de arranque si quedó pendiente, y solo sin un registro abierto", () => {
    const flow = nuevoFlow();
    ejecutar(ID.arrancar, { payload: 1 }, { flow }); // la marca queda puesta hasta que el UPDATE tenga éxito
    const r = ejecutar(ID.vencer, {}, { flow });
    assert.equal(r.salida[0], null);
    assert.equal(r.salida[1], null);
    assert.ok(r.salida[2], "se reintenta");
    ejecutar(ID.limpiezaLista, { payload: [] }, { flow }); // el reintento tuvo éxito
    assert.equal(ejecutar(ID.vencer, {}, { flow }).salida, null);
    // Con un registro abierto, el UPDATE quitaría su consentimiento: no se reintenta.
    const abierta = abrirInscripcion();
    abierta.set("limpieza_pendiente", true);
    assert.equal(ejecutar(ID.vencer, {}, { flow: abierta }).salida, null);
    assert.equal(abierta.get("inscripcion").abierto, true);
});

test("arranque: borra la inscripción del contexto y deja pasar el mensaje al UPDATE de limpieza", () => {
    const flow = abrirInscripcion();
    const r = ejecutar(ID.arrancar, { payload: 123 }, { flow });
    assert.equal(flow.get("inscripcion"), undefined);
    assert.equal(flow.get("limpieza_pendiente"), true);
    assert.ok(r.salida);
    assert.equal(r.logs.length, 1);
    const { query } = nodo("a1f0c0de0000007e");
    assert.match(query, /UPDATE alumnos SET consentimiento = false, consentimiento_fecha = NULL\s+WHERE consentimiento AND embedding IS NULL/);
});

test("revertir: la consulta solo toca alumnos sin rostro, con la cuenta enlazada", () => {
    const { query } = nodo("a1f0c0de0000007f");
    assert.match(query, /UPDATE alumnos SET consentimiento = false, consentimiento_fecha = NULL\s+WHERE no_cuenta = \$1 AND embedding IS NULL/);
});

test("consentimiento: se escribe solo si el alumno no tiene rostro, con fecha y en una sola sentencia", () => {
    const { query } = nodo("a1f0c0de00000064");
    assert.match(query, /SET consentimiento = true, consentimiento_fecha = now\(\)/);
    assert.match(query, /WHERE no_cuenta = \$1 AND embedding IS NULL/);
    assert.equal(query.includes(";"), false);
});

// ------------------------------------------------ errores

test("500: libera el lugar apartado y responde 500 en las peticiones HTTP, sin cuentas en el aviso", () => {
    const flow = nuevoFlow();
    const apartado = ejecutar(ID.validarApartar, peticionPost({ cuenta: CUENTA, consentimiento: true }), { flow });
    const msg = {
        ...apartado.salida[0],
        req: { method: "POST", path: "/api/enrolamiento", route: { path: "/api/enrolamiento" } },
        error: { message: "duplicate key (no_cuenta)=(320068234)", code: undefined }
    };
    const r = ejecutar(ID.cerrar500, msg, { flow });
    assert.equal(r.salida[0].statusCode, 500);
    assert.equal(r.salida[1], null);
    assert.equal(flow.get("inscripcion"), undefined);
    assert.ok(!r.avisos.join("\n").includes(CUENTA));
});

test("500: registra el SQLSTATE, no la ruta con la cuenta del DELETE", () => {
    const r = ejecutar(ID.cerrar500, {
        req: { method: "DELETE", path: "/api/alumnos/" + CUENTA + "/rostro", route: { path: "/api/alumnos/:cuenta/rostro" } },
        params: [CUENTA],
        error: { code: "23514", message: "check constraint con detalle (" + CUENTA + ")" }
    });
    const aviso = r.avisos.join("\n");
    assert.match(aviso, /SQLSTATE 23514/);
    assert.match(aviso, /DELETE \/api\/alumnos\/:cuenta\/rostro/);
    assert.ok(!aviso.includes(CUENTA));
    assert.equal(r.salida[0].statusCode, 500);
});

test("500: sin petición HTTP (MQTT, vencimiento) solo registra y no responde", () => {
    const r = ejecutar(ID.cerrar500, { params: [CUENTA], error: { code: "08006" } });
    assert.equal(r.salida[0], null);
    assert.equal(r.salida[1], null);
    assert.equal(r.avisos.length, 1);
});

test("500: si falla el UPDATE del embedding, el registro termina en error y se revierte el consentimiento", () => {
    const flow = abrirInscripcion();
    const r = ejecutar(ID.cerrar500, { fuente: "embedding", inscripcion_id: flow.get("inscripcion").id, params: ["[0.1]", CUENTA], error: { code: "57P01" } }, { flow });
    assert.equal(r.salida[0], null);
    assert.deepEqual(r.salida[1], { params: [CUENTA] });
    assert.equal(flow.get("inscripcion").estado, "error");
    assert.equal(flow.get("inscripcion").motivo, "no_guardado");
    assert.ok(!r.avisos.join("\n").includes("0.1"));
});

test("500: un error en otro mensaje no cierra una inscripción de otra cuenta", () => {
    const flow = abrirInscripcion();
    const r = ejecutar(ID.cerrar500, { fuente: "embedding", params: ["[0.1]", OTRA], error: { code: "57P01" } }, { flow });
    assert.equal(r.salida[1], null);
    assert.equal(flow.get("inscripcion").abierto, true);
});

// ------------------------------------------------ forma del flujo

test("flows.json: rutas, tópicos y cables del flujo Registro", () => {
    const tab = flujos.find((n) => n.type === "tab" && n.label === "Registro");
    assert.ok(tab);
    const nodos = flujos.filter((n) => n.z === tab.id);
    const rutas = nodos.filter((n) => n.type === "http in").map((n) => n.method + " " + n.url).sort();
    assert.deepEqual(rutas, [
        "delete /api/alumnos/:cuenta/rostro",
        "get /api/enrolamiento/estado",
        "post /api/enrolamiento",
        "post /api/enrolamiento/cancelar"
    ]);
    const entradas = nodos.filter((n) => n.type === "mqtt in").map((n) => n.topic).sort();
    assert.deepEqual(entradas, ["aula/vision/embedding", "aula/vision/enrolamiento"]);
    // Cada cable apunta a un nodo de la misma pestaña.
    const ids = new Set(nodos.map((n) => n.id));
    for (const n of nodos) {
        for (const salida of n.wires || []) {
            for (const destino of salida) {
                assert.ok(ids.has(destino), n.id + " apunta a " + destino);
            }
        }
        for (const id of n.scope || []) {
            assert.ok(ids.has(id), "el alcance de " + n.id + " incluye " + id);
        }
    }
    // El único mqtt out publica con QoS 1 y sin retención por omisión.
    const salidas = nodos.filter((n) => n.type === "mqtt out");
    assert.equal(salidas.length, 1);
    assert.equal(salidas[0].qos, "1");
    assert.equal(salidas[0].retain, "false");
    assert.equal(salidas[0].broker, "a1f0c0de00000002");
});

test("flows.json: toda consulta del flujo Registro va parametrizada y sin credenciales", () => {
    const tab = flujos.find((n) => n.type === "tab" && n.label === "Registro");
    const consultas = flujos.filter((n) => n.z === tab.id && n.type === "postgresql");
    assert.equal(consultas.length, 6);
    for (const n of consultas) {
        assert.equal(n.postgreSQLConfig, "a1f0c0de00000003", n.name);
        assert.ok(!/\$\{|\+\s*msg|'\s*\+|`/.test(n.query), "consulta concatenada: " + n.name);
        assert.ok(!/DELETE|DROP|TRUNCATE|ALTER|INSERT/i.test(n.query), "el rol no puede, ni debe: " + n.name);
    }
    const texto = JSON.stringify(flujos.filter((n) => n.z === tab.id));
    assert.ok(!/password|contraseña|secret/i.test(texto.replace(/NODERED_DB/g, "")), "sin credenciales en el flujo");
});

test("flows.json: el nodo de depuración del flujo Registro registra solo el payload de descarte, nunca el mensaje", () => {
    const tab = flujos.find((n) => n.type === "tab" && n.label === "Registro");
    const debug = flujos.filter((n) => n.z === tab.id && n.type === "debug");
    assert.equal(debug.length, 1);
    assert.equal(debug[0].complete, "payload");
    assert.equal(debug[0].console, true);
});
