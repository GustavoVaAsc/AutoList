// Pruebas de las funciones y de la estructura de la pestaña "Asistencia" de nodered/flows/flows.json:
//   GET /api/sesiones, GET /api/sesiones/:id y GET /api/sesiones/:id/csv.
// Cada función se extrae del propio flows.json y se ejecuta en un contexto vm. Los nombres y cuentas
// de aquí son entradas sintéticas de prueba, nunca datos de alumnos reales.
// Sin dependencias: node --test nodered/pruebas/*.test.js
"use strict";
const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const flujos = JSON.parse(fs.readFileSync(path.join(__dirname, "..", "flows", "flows.json"), "utf8"));
const ID = {
    tab: "a1f0c0de000000c0",
    httpSesiones: "a1f0c0de000000c2",
    validarLimite: "a1f0c0de000000c3",
    selectSesiones: "a1f0c0de000000c4",
    formatearSesiones: "a1f0c0de000000c5",
    httpDetalle: "a1f0c0de000000c7",
    validarId: "a1f0c0de000000c8",
    selectDetalle: "a1f0c0de000000c9",
    formatearDetalle: "a1f0c0de000000ca",
    httpCsv: "a1f0c0de000000cc",
    validarIdCsv: "a1f0c0de000000cd",
    leerCsv: "a1f0c0de000000ce",
    responderCsv: "a1f0c0de000000cf",
    respuestaJson: "a1f0c0de000000d0",
    respuestaCsv: "a1f0c0de000000d1",
    catch: "a1f0c0de000000d2",
    responderError: "a1f0c0de000000d3"
};

function nodo(id) {
    const n = flujos.find((x) => x.id === id);
    assert.ok(n, "no existe el nodo " + id);
    return n;
}

function ejecutar(id, msg) {
    const n = nodo(id);
    assert.equal(n.type, "function");
    const avisos = [];
    const contexto = vm.createContext({ Buffer, Date, Math, Number, String, JSON, Array, console });
    const funcion = new vm.Script("(function (msg, node) {" + n.func + "\n})").runInContext(contexto);
    const nodoFalso = { warn: (t) => avisos.push(String(t)), log() {}, error() {} };
    const salida = funcion(msg, nodoFalso);
    if (salida === undefined || salida === null) {
        return { salida, avisos };
    }
    if (Buffer.isBuffer(salida.payload)) {
        return {
            salida: { ...JSON.parse(JSON.stringify({ ...salida, payload: undefined })), payload: Buffer.from(salida.payload) },
            avisos
        };
    }
    return { salida: JSON.parse(JSON.stringify(salida)), avisos };
}

// ---------------------------------------------------------------- GET /api/sesiones

test("sesiones: sin limite usa 50 por defecto y fija la ruta plantilla", () => {
    const { salida } = ejecutar(ID.validarLimite, { req: { query: {} } });
    assert.equal(salida[1], null);
    assert.deepEqual(salida[0].params, [50]);
    assert.equal(salida[0].rutaPlantilla, "/api/sesiones");
});

test("sesiones: acepta limite válido y recorta en 200", () => {
    assert.deepEqual(ejecutar(ID.validarLimite, { req: { query: { limite: "1" } } }).salida[0].params, [1]);
    assert.deepEqual(ejecutar(ID.validarLimite, { req: { query: { limite: "25" } } }).salida[0].params, [25]);
    assert.deepEqual(ejecutar(ID.validarLimite, { req: { query: { limite: "999" } } }).salida[0].params, [200]);
});

test("sesiones: limite inválido responde 400", () => {
    for (const malo of ["0", "-5", "10.5", "abc", "", ["10"], "10; DROP TABLE sesiones"]) {
        const { salida } = ejecutar(ID.validarLimite, { req: { query: { limite: malo } } });
        assert.equal(salida[0], null);
        assert.equal(salida[1].statusCode, 400);
        assert.match(salida[1].payload.error, /limite/);
    }
});

test("sesiones: formatea sesión cerrada con faltas y sesión abierta con pendientes", () => {
    const filas = [
        {
            id: 21,
            materia: "FSE",
            grupo: "1",
            inicio: new Date("2026-11-06T22:23:19.000Z"),
            fin: null,
            abierta: true,
            tol_min: 10,
            total_grupo: 10,
            presentes: 3,
            retardos: 1,
            sin_registro: 6,
            intentos_desconocido: 1,
            intentos_duplicado: 0,
            intentos_imagen_insuficiente: 2,
            confianza_min: 0.71,
            confianza_max: 0.99,
            confianza_promedio: 0.85
        },
        {
            id: 20,
            materia: "FSE",
            grupo: "1",
            inicio: "2026-11-06T20:06:30.000Z",
            fin: "2026-11-06T20:33:06.000Z",
            abierta: false,
            tol_min: 10,
            total_grupo: 10,
            presentes: 0,
            retardos: 2,
            sin_registro: 8,
            intentos_desconocido: 0,
            intentos_duplicado: 1,
            intentos_imagen_insuficiente: 0,
            confianza_min: null,
            confianza_max: null,
            confianza_promedio: null
        }
    ];
    const { salida } = ejecutar(ID.formatearSesiones, { payload: filas });
    assert.equal(salida.statusCode, 200);
    assert.equal(salida.payload.length, 2);

    assert.equal(salida.payload[0].abierta, true);
    assert.equal(salida.payload[0].fin, null);
    assert.equal(salida.payload[0].faltas, 0);
    assert.equal(salida.payload[0].pendientes, 6);
    assert.deepEqual(salida.payload[0].intentos, { desconocido: 1, duplicado: 0, imagen_insuficiente: 2, total: 3 });
    assert.deepEqual(salida.payload[0].confianza, { min: 0.71, max: 0.99, promedio: 0.85 });

    assert.equal(salida.payload[1].abierta, false);
    assert.equal(salida.payload[1].fin, "2026-11-06T20:33:06.000Z");
    assert.equal(salida.payload[1].faltas, 8);
    assert.equal(salida.payload[1].pendientes, 0);
    assert.deepEqual(salida.payload[1].confianza, { min: null, max: null, promedio: null });
});

test("sesiones: sin sesiones devuelve arreglo vacío", () => {
    const { salida } = ejecutar(ID.formatearSesiones, { payload: [] });
    assert.equal(salida.statusCode, 200);
    assert.deepEqual(salida.payload, []);
});

// ---------------------------------------------------------------- GET /api/sesiones/:id

test("detalle: valida id entero positivo de 1 a 2147483647", () => {
    const { salida } = ejecutar(ID.validarId, { req: { params: { id: "20" } } });
    assert.equal(salida[1], null);
    assert.deepEqual(salida[0].params, [20]);
    assert.equal(salida[0].sesion_id, 20);
    assert.equal(salida[0].rutaPlantilla, "/api/sesiones/:id");

    for (const malo of ["0", "01", "-1", "2147483648", "20.csv", "../20", "abc", "", undefined]) {
        const r = ejecutar(ID.validarId, { req: { params: { id: malo } } });
        assert.equal(r.salida[0], null);
        assert.equal(r.salida[1].statusCode, 400);
    }
});

test("detalle: cero filas responde 404", () => {
    const { salida } = ejecutar(ID.formatearDetalle, { payload: [] });
    assert.equal(salida.statusCode, 404);
    assert.match(salida.payload.error, /No existe/);
});

test("detalle: arma sesion, lista nominal e intentos sin filtrar campos extra", () => {
    const fila = {
        id: 20,
        materia: "FSE",
        grupo: "1",
        inicio: "2026-11-06T20:06:30.000Z",
        fin: "2026-11-06T20:33:06.000Z",
        abierta: false,
        tol_min: 10,
        alumnos: [
            { cuenta: "999000001", nombre: "Alba, Ana", estado: "presente", hora: "2026-11-06T20:10:00.000Z", confianza: 0.92, metodo: "rostro", extra: "ignorado" },
            { cuenta: "999000002", nombre: "Báez, Bruno", estado: "retardo", hora: "2026-11-06T20:20:00.000Z", confianza: 0.88, metodo: "rostro" },
            { cuenta: "999000003", nombre: "Cruz, Carla", estado: "falta", hora: null, confianza: null, metodo: null }
        ],
        confianza_min: 0.88,
        confianza_max: 0.92,
        confianza_promedio: 0.9,
        intentos: [
            { id: 5, hora: "2026-11-06T20:21:00.000Z", motivo: "desconocido", confianza_max: 0.14, secreto: "no" },
            { id: 4, hora: "2026-11-06T20:19:00.000Z", motivo: "imagen_insuficiente", confianza_max: null }
        ],
        intentos_desconocido: 1,
        intentos_duplicado: 0,
        intentos_imagen_insuficiente: 1
    };
    const { salida } = ejecutar(ID.formatearDetalle, { payload: [fila] });
    assert.equal(salida.statusCode, 200);
    assert.equal(salida.payload.sesion.total_grupo, 3);
    assert.equal(salida.payload.sesion.presentes, 1);
    assert.equal(salida.payload.sesion.retardos, 1);
    assert.equal(salida.payload.sesion.faltas, 1);
    assert.equal(salida.payload.sesion.pendientes, 0);
    assert.equal(salida.payload.alumnos.length, 3);
    assert.equal(salida.payload.alumnos[0].extra, undefined);
    assert.equal(salida.payload.intentos.length, 2);
    assert.equal(salida.payload.intentos[0].secreto, undefined);
});

// ---------------------------------------------------------------- GET /api/sesiones/:id/csv

test("csv: valida id estricto y construye ruta fija sin aceptar barras ni nombres", () => {
    const { salida } = ejecutar(ID.validarIdCsv, { req: { params: { id: "20" } } });
    assert.equal(salida[1], null);
    assert.equal(salida[0].sesion_id, 20);
    assert.equal(salida[0].filename, "/data/reportes/sesion-20.csv");
    assert.equal(salida[0].rutaPlantilla, "/api/sesiones/:id/csv");

    for (const malo of ["0", "-1", "../etc/passwd", "20/../../x", "sesion-20.csv", "020", "2147483648", ""]) {
        const r = ejecutar(ID.validarIdCsv, { req: { params: { id: malo } } });
        assert.equal(r.salida[0], null);
        assert.equal(r.salida[1].statusCode, 400);
    }
});

test("csv: conserva los bytes exactos del Buffer (BOM y CRLF) y fija cabeceras de descarga", () => {
    const crudo = Buffer.from("\ufeffno_cuenta,nombre,estado,hora\r\n999000001,\"Alba, Ana\",presente,2026-11-06 14:10:00\r\n", "utf8");
    const { salida } = ejecutar(ID.responderCsv, { sesion_id: 20, payload: crudo });
    assert.equal(salida.statusCode, 200);
    assert.equal(salida.headers["content-type"], "text/csv; charset=utf-8");
    assert.equal(salida.headers["content-disposition"], 'attachment; filename="sesion-20.csv"');
    assert.ok(Buffer.isBuffer(salida.payload));
    assert.equal(Buffer.compare(salida.payload, crudo), 0);
});

// ---------------------------------------------------------------- Errores

test("errores: ENOENT del CSV responde 404 sin registrar aviso de fallo 500", () => {
    for (const error of [{ code: "ENOENT" }, { message: "Error: ENOENT: no such file or directory, open '/data/reportes/sesion-99.csv'" }]) {
        const { salida, avisos } = ejecutar(ID.responderError, {
            rutaPlantilla: "/api/sesiones/:id/csv",
            headers: { "content-type": "text/csv" },
            error
        });
        assert.equal(salida.statusCode, 404);
        assert.equal(salida.headers, undefined);
        assert.equal(avisos.length, 0);
    }
});

test("errores: fallo de base registra solo SQLSTATE o mensaje con cuentas enmascaradas y responde 500", () => {
    const conCodigo = ejecutar(ID.responderError, {
        rutaPlantilla: "/api/sesiones/:id",
        error: { code: "08006", message: "connection failure 320068234" }
    });
    assert.equal(conCodigo.salida.statusCode, 500);
    assert.deepEqual(conCodigo.avisos, ["GET /api/sesiones/:id falló: SQLSTATE 08006"]);

    const sinCodigo = ejecutar(ID.responderError, {
        rutaPlantilla: "/api/sesiones",
        error: { message: "fallo con cuenta 320068234" }
    });
    assert.equal(sinCodigo.salida.statusCode, 500);
    assert.doesNotMatch(sinCodigo.avisos[0], /320068234/);
    assert.match(sinCodigo.avisos[0], /\*\*\*\*\*\*\*\*\*/);
});

// ---------------------------------------------------------------- Estructura de la pestaña

const enPestana = flujos.filter((n) => n.z === ID.tab);

test("estructura: todos los cables y alcances apuntan a nodos de la pestaña, las consultas no leen embeddings y el catch cubre toda la cadena", () => {
    const tab = nodo(ID.tab);
    assert.equal(tab.type, "tab");
    assert.equal(tab.label, "Asistencia");
    const ids = new Set(enPestana.map((n) => n.id));
    for (const n of enPestana) {
        for (const salida of n.wires || []) {
            for (const destino of salida) {
                assert.ok(ids.has(destino), n.id + " apunta fuera de la pestaña: " + destino);
            }
        }
        for (const objetivo of n.scope || []) {
            assert.ok(ids.has(objetivo), n.id + " incluye fuera de la pestaña: " + objetivo);
        }
    }
    const consultas = enPestana.filter((n) => n.type === "postgresql");
    assert.equal(consultas.length, 2);
    for (const c of consultas) {
        assert.doesNotMatch(c.query, /embedding/i, c.name + " no debe tocar la columna embedding");
        assert.doesNotMatch(c.query, /\{\{/);
        assert.equal(c.postgreSQLConfig, "a1f0c0de00000003");
    }
    const lector = nodo(ID.leerCsv);
    assert.equal(lector.type, "file in");
    assert.equal(lector.sendError, false, "un archivo ausente debe ir solo al catch y no duplicar respuesta");
    const catchNodo = nodo(ID.catch);
    const esperados = enPestana
        .filter((n) => ["function", "postgresql", "file in"].includes(n.type) && n.id !== ID.responderError)
        .map((n) => n.id)
        .sort();
    assert.deepEqual([...catchNodo.scope].sort(), esperados);
});
