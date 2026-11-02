// Pruebas de las funciones del flujo "Alumnos" de nodered/flows/flows.json
// (GET /api/galeria, GET /api/alumnos e importación del CSV). Cada función se
// extrae del propio flows.json y se ejecuta en un contexto vm sin más globales que
// los que da un nodo function (Buffer, Date, console), como en Node-RED.
// Sin dependencias: node --test nodered/pruebas/*.test.js
"use strict";
const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const flujos = JSON.parse(fs.readFileSync(path.join(__dirname, "..", "flows", "flows.json"), "utf8"));
const ID = {
    formatearGaleria: "a1f0c0de00000044",
    validarGrupo: "a1f0c0de00000047",
    validarCsv: "a1f0c0de0000004b",
    decidirImportacion: "a1f0c0de0000004d",
    responderImportacion: "a1f0c0de0000004f",
    responder500: "a1f0c0de00000052"
};

function nodo(id) {
    const n = flujos.find((x) => x.id === id);
    assert.ok(n, "no existe el nodo " + id);
    return n;
}

// Ejecuta la función del nodo `id` con `msg`; devuelve { salida, avisos }. La salida
// se copia por JSON: los objetos del contexto vm son de otro "realm" y
// assert.deepStrictEqual los distingue por su prototipo.
function ejecutar(id, msg) {
    const n = nodo(id);
    assert.equal(n.type, "function");
    const avisos = [];
    const contexto = vm.createContext({ Buffer, Date, console });
    const funcion = new vm.Script("(function (msg, node) {" + n.func + "\n})").runInContext(contexto);
    const nodoFalso = { warn: (t) => avisos.push(String(t)), log() {}, error() {} };
    const salida = funcion(msg, nodoFalso);
    return { salida: salida === undefined ? salida : JSON.parse(JSON.stringify(salida)), avisos };
}

function peticion(query = {}, extra = {}, tipo = "text/csv; charset=utf-8") {
    const headers = tipo === null ? {} : { "content-type": tipo }; // null: sin Content-Type
    return { req: { query, method: "POST", path: "/api/alumnos/importar", headers }, ...extra };
}

const ENC = "no_cuenta,nombre,grupo\r\n";
function validar(cuerpo, query = {}, tipo = "text/csv; charset=utf-8") {
    const { salida } = ejecutar(ID.validarCsv, { ...peticion(query, {}, tipo), payload: cuerpo });
    return { ok: salida[0], error: salida[1] };
}

// ---------------------------------------------------------------- galería

test("galería: arma {embeddings:[{cuenta,embedding}]} con el embedding como arreglo numérico", () => {
    const { salida } = ejecutar(ID.formatearGaleria, {
        payload: [
            { cuenta: "320068234", embedding: "[0.1,-0.25,3]" },
            { cuenta: "320068235", embedding: "[1e-05,2,3]" }
        ]
    });
    assert.equal(salida.statusCode, 200);
    assert.deepEqual(salida.payload, {
        embeddings: [
            { cuenta: "320068234", embedding: [0.1, -0.25, 3] },
            { cuenta: "320068235", embedding: [0.00001, 2, 3] }
        ]
    });
});

test("galería: sin alumnos con rostro devuelve {embeddings: []}", () => {
    const { salida } = ejecutar(ID.formatearGaleria, { payload: [] });
    assert.deepEqual(salida.payload, { embeddings: [] });
});

test("galería: un embedding ilegible falla sin copiar su contenido ni la cuenta en el error", () => {
    for (const malo of ["[0.1,SECRETO]", "[]", "no es json", '["a"]', null, "[1,null]"]) {
        assert.throws(
            () => ejecutar(ID.formatearGaleria, { payload: [{ cuenta: "320068234", embedding: malo }] }),
            (e) => /embedding/.test(e.message) && !/320068234|SECRETO/.test(e.message)
        );
    }
});

test("galería: el payload sin filas falla", () => {
    assert.throws(() => ejecutar(ID.formatearGaleria, { payload: undefined }), /no devolvió filas/);
});

// ---------------------------------------------------------------- GET /api/alumnos

test("alumnos: sin grupo lista todos (parámetro null)", () => {
    const { salida } = ejecutar(ID.validarGrupo, peticion({}));
    assert.equal(salida[1], null);
    assert.deepEqual(salida[0].params, [null]);
});

test("alumnos: el grupo se recorta y se normaliza a NFC", () => {
    const { salida } = ejecutar(ID.validarGrupo, peticion({ grupo: "  Cálculo 1  " }));
    assert.deepEqual(salida[0].params, ["Cálculo 1"]);
});

test("alumnos: un grupo de 40 caracteres pasa y uno de 41 se rechaza", () => {
    assert.deepEqual(ejecutar(ID.validarGrupo, peticion({ grupo: "g".repeat(40) })).salida[0].params, ["g".repeat(40)]);
    const { salida } = ejecutar(ID.validarGrupo, peticion({ grupo: "g".repeat(41) }));
    assert.equal(salida[0], null);
    assert.equal(salida[1].statusCode, 400);
});

for (const [nombre, grupo] of [
    ["vacío", ""],
    ["solo espacios", "   "],
    ["repetido", ["a", "b"]],
    ["objeto", { a: "1" }],
    ["con salto de línea", "a\nb"],
    ["con NUL", "a\u0000b"],
    ["con carácter invisible (ancho cero)", "3CM\u200B1"],
    ["con marca de orden de bytes", "3CM\uFEFF1"]
]) {
    test("alumnos: grupo inválido (" + nombre + ") responde 400", () => {
        const { salida } = ejecutar(ID.validarGrupo, peticion({ grupo }));
        assert.equal(salida[0], null);
        assert.equal(salida[1].statusCode, 400);
        assert.equal(typeof salida[1].payload.error, "string");
    });
}

test("alumnos: las consultas SQL del flujo nunca devuelven la columna embedding", () => {
    const consulta = nodo("a1f0c0de00000048").query;
    assert.doesNotMatch(consulta.replace(/embedding IS (NOT )?NULL/g, ""), /embedding/);
});

// ---------------------------------------------------------------- importar: el CSV

test("csv: archivo válido con CRLF, acentos y línea final", () => {
    const { ok, error } = validar(ENC + "320068234,Ñandú Pérez,3CM1\r\n320068235,Ana López,3CM1\r\n");
    assert.equal(error, null);
    assert.deepEqual(ok.csv, [
        { cuenta: "320068234", nombre: "Ñandú Pérez", grupo: "3CM1" },
        { cuenta: "320068235", nombre: "Ana López", grupo: "3CM1" }
    ]);
    assert.deepEqual(ok.params, [["320068234", "320068235"]]);
    assert.equal(ok.modo, null);
});

test("csv: BOM de UTF-8, LF, sin línea final y encabezado en otro orden de mayúsculas", () => {
    const { ok } = validar("﻿No_Cuenta,Nombre,GRUPO\n320068234,Ana,3CM1");
    assert.equal(ok.csv.length, 1);
});

test("csv: el mismo archivo como Buffer con BOM en bytes", () => {
    const cuerpo = Buffer.concat([Buffer.from([0xEF, 0xBB, 0xBF]), Buffer.from(ENC + "320068234,Pérez,3CM1\n", "utf8")]);
    const { ok } = validar(cuerpo);
    assert.equal(ok.csv[0].nombre, "Pérez");
});

test("csv: campos entre comillas con comas y comillas dobles", () => {
    const { ok, error } = validar(ENC + '320068234,"Pérez López, Juan ""Juanito""","3CM1"\n');
    assert.equal(error, null);
    assert.equal(ok.csv[0].nombre, 'Pérez López, Juan "Juanito"');
    assert.equal(ok.csv[0].grupo, "3CM1");
});

test("csv: acento descompuesto (e + marca) queda en NFC y se recortan espacios", () => {
    const { ok } = validar(ENC + " 320068234 , José  , 3CM1 \n");
    assert.equal(ok.csv[0].nombre, "José");
    assert.equal(ok.csv[0].cuenta, "320068234");
});

test("csv: las líneas en blanco se ignoran", () => {
    const { ok } = validar(ENC + "\n320068234,Ana,3CM1\n\n   \n320068235,Luis,3CM1\n\n");
    assert.equal(ok.csv.length, 2);
});

test("csv: filas inválidas listadas con su línea y no se importa nada", () => {
    const { ok, error } = validar(
        ENC +
        "320068234,Ana,3CM1\n" +            // 2: válida
        "12345678,Luis,3CM1\n" +            // 3: 8 dígitos
        "320068236,,3CM1\n" +               // 4: nombre vacío
        "320068237,Eva,\n" +                // 5: grupo vacío
        "32006823a,Eva,3CM1\n" +            // 6: letra en la cuenta
        "320068234,Otra,3CM1\n" +           // 7: repetida de la 2
        "320068238,Eva,3CM1,extra\n" +      // 8: cuatro columnas
        "320068239,Eva\n" +                 // 9: dos columnas
        "3200682310,Eva,3CM1\n"             // 10: 10 dígitos
    );
    assert.equal(ok, null);
    assert.equal(error.statusCode, 400);
    const porFila = Object.fromEntries(error.payload.filas.map((f) => [f.fila, f.error]));
    assert.deepEqual(Object.keys(porFila).map(Number), [3, 4, 5, 6, 7, 8, 9, 10]);
    assert.match(porFila[3], /9 dígitos/);
    assert.match(porFila[4], /nombre vacío/);
    assert.match(porFila[5], /grupo vacío/);
    assert.match(porFila[6], /9 dígitos/);
    assert.match(porFila[7], /repetida.*fila 2/);
    assert.match(porFila[8], /3 columnas y hay 4/);
    assert.match(porFila[9], /3 columnas y hay 2/);
    assert.match(porFila[10], /9 dígitos/);
    assert.equal(error.payload.total_errores, 8);
});

test("csv: varios errores de una misma fila se juntan", () => {
    const { error } = validar(ENC + "1,,\n");
    assert.equal(error.payload.filas.length, 1);
    assert.match(error.payload.filas[0].error, /9 dígitos.*nombre vacío.*grupo vacío/);
});

test("csv: los números de fila cuentan los saltos dentro de comillas", () => {
    const { error } = validar(ENC + '320068234,"Ana\nLópez",3CM1\n12,Luis,3CM1\n');
    const filas = error.payload.filas;
    assert.deepEqual(filas.map((f) => f.fila), [2, 4]);
    assert.match(filas[0].error, /saltos de línea/);
});

test("csv: un \r suelto dentro de comillas cuenta como salto de línea en el número de fila", () => {
    const { error } = validar(ENC + '320068234,"Ana\rLópez",3CM1\n12,Luis,3CM1\n');
    assert.deepEqual(error.payload.filas.map((f) => f.fila), [2, 4]);
    const crlf = validar(ENC + '320068234,"Ana\r\nLópez",3CM1\n12,Luis,3CM1\n');
    assert.deepEqual(crlf.error.payload.filas.map((f) => f.fila), [2, 4]);
});

test("csv: caracteres de formato invisibles (ancho cero, BOM intermedio) se rechazan en nombre y grupo", () => {
    const { error } = validar(ENC + "320068234,Ana\u200BLópez,3CM1\n320068235,Luis,3CM\uFEFF1\n320068236,Eva\u202Eoz,3CM1\n");
    assert.deepEqual(error.payload.filas.map((f) => f.fila), [2, 3, 4]);
    for (const f of error.payload.filas) {
        assert.match(f.error, /control o invisibles/);
    }
});

test("csv: longitudes acotadas del nombre y del grupo", () => {
    assert.equal(validar(ENC + "320068234," + "ñ".repeat(120) + "," + "g".repeat(40) + "\n").error, null);
    const { error } = validar(ENC + "320068234," + "ñ".repeat(121) + "," + "g".repeat(41) + "\n");
    assert.match(error.payload.filas[0].error, /nombre de más de 120.*grupo de más de 40/);
});

test("csv: comillas mal formadas se reportan por fila", () => {
    const { error } = validar(ENC + '320068234,Ana "la" López,3CM1\n320068235,"Luis"x,3CM1\n320068236,"sin cerrar,3CM1\n');
    assert.deepEqual(error.payload.filas.map((f) => f.fila), [2, 3, 4]);
    assert.match(error.payload.filas[0].error, /comilla inesperada/);
    assert.match(error.payload.filas[1].error, /después de cerrar/);
    assert.match(error.payload.filas[2].error, /comillas sin cerrar/);
});

test("csv: encabezado ausente, mal escrito o con otro separador", () => {
    for (const cuerpo of [
        "320068234,Ana,3CM1\n",
        "cuenta,nombre,grupo\n320068234,Ana,3CM1\n",
        "no_cuenta;nombre;grupo\n320068234;Ana;3CM1\n",
        "no_cuenta,nombre\n320068234,Ana\n",
        "no_cuenta,nombre,grupo,extra\n320068234,Ana,3CM1,x\n"
    ]) {
        const { ok, error } = validar(cuerpo);
        assert.equal(ok, null, cuerpo);
        assert.equal(error.statusCode, 400);
        assert.match(error.payload.error, /encabezado no_cuenta,nombre,grupo/);
        assert.match(error.payload.error, /CSV UTF-8 \(delimitado por comas\)/);
    }
});

test("csv: el tipo de contenido debe ser text/csv (CSRF): sin tipo, text/plain y otros dan 415", () => {
    const cuerpo = ENC + "320068234,Ana,3CM1\n";
    for (const tipo of [null, "", "text/plain", "text/plain; charset=utf-8", "application/json", "application/x-www-form-urlencoded", "multipart/form-data; boundary=x", "application/vnd.ms-excel", "text/csvx", "text/csv/x", "x-text/csv"]) {
        const { ok, error } = validar(cuerpo, {}, tipo);
        assert.equal(ok, null, String(tipo));
        assert.equal(error.statusCode, 415, String(tipo));
        assert.match(error.payload.error, /text\/csv/);
    }
});

test("csv: text/csv se acepta con parámetros y sin distinguir mayúsculas", () => {
    const cuerpo = ENC + "320068234,Ana,3CM1\n";
    for (const tipo of ["text/csv", "text/csv; charset=utf-8", "TEXT/CSV;charset=UTF-8", "text/csv ; charset=utf-8; header=present", " text/csv"]) {
        assert.equal(validar(cuerpo, {}, tipo).error, null, tipo);
    }
});

test("csv: el 415 se decide antes de leer el cuerpo o existentes", () => {
    const { error } = validar("basura", { existentes: "x" }, "text/plain");
    assert.equal(error.statusCode, 415);
});

test("csv: vacío, solo encabezado y solo espacios responden 400", () => {
    for (const cuerpo of ["", "   \n\n", ENC, "﻿"]) {
        const { ok, error } = validar(cuerpo);
        assert.equal(ok, null, JSON.stringify(cuerpo));
        assert.equal(error.statusCode, 400);
    }
});

test("csv: 500 filas pasan y 501 se rechazan", () => {
    const fila = (n) => String(300000000 + n) + ",Alumno " + n + ",3CM1\n";
    const cuerpo = (n) => ENC + Array.from({ length: n }, (_, i) => fila(i)).join("");
    assert.equal(validar(cuerpo(500)).error, null);
    const { error } = validar(cuerpo(501));
    assert.equal(error.statusCode, 400);
    assert.match(error.payload.error, /501 filas \(máximo 500\)/);
});

test("csv: más de 256 KiB responde 413, como texto o como Buffer", () => {
    const grande = ENC + "320068234,Ana,3CM1\n" + " ".repeat(256 * 1024);
    for (const cuerpo of [grande, Buffer.from(grande)]) {
        const { error } = validar(cuerpo);
        assert.equal(error.statusCode, 413);
    }
    assert.equal(validar(ENC + "320068234,Ana,3CM1\n" + " ".repeat(1000)).error, null);
});

test("csv: se listan a lo más 100 filas inválidas, pero se cuentan todas", () => {
    const cuerpo = ENC + Array.from({ length: 150 }, () => "x,,\n").join("");
    const { error } = validar(cuerpo);
    assert.equal(error.payload.filas.length, 100);
    assert.equal(error.payload.total_errores, 150);
});

test("csv: un archivo que no es UTF-8 válido responde 400", () => {
    const { error } = validar(Buffer.concat([Buffer.from(ENC + "320068234,"), Buffer.from([0xD1, 0x61]), Buffer.from(",3CM1\n")]));
    assert.equal(error.statusCode, 400);
    assert.match(error.payload.error, /UTF-8/);
});

test("csv: un cuerpo que no es texto (JSON ya interpretado) responde 400", () => {
    for (const cuerpo of [{ a: 1 }, [1], undefined, null, 5]) {
        const { error } = validar(cuerpo);
        assert.equal(error.statusCode, 400);
        assert.match(error.payload.error, /text\/csv/);
    }
});

test("csv: existentes solo acepta actualizar u omitir", () => {
    const cuerpo = ENC + "320068234,Ana,3CM1\n";
    assert.equal(validar(cuerpo, { existentes: "actualizar" }).ok.modo, "actualizar");
    assert.equal(validar(cuerpo, { existentes: "omitir" }).ok.modo, "omitir");
    for (const valor of ["", "borrar", "Actualizar", ["omitir", "omitir"], { a: "omitir" }]) {
        const { ok, error } = validar(cuerpo, { existentes: valor });
        assert.equal(ok, null, JSON.stringify(valor));
        assert.equal(error.statusCode, 400);
        assert.match(error.payload.error, /existentes/);
    }
});

test("csv: un existentes inválido se rechaza aunque el archivo también lo sea (sin leerlo)", () => {
    const { error } = validar("basura", { existentes: "x" });
    assert.match(error.payload.error, /existentes/);
});

// ---------------------------------------------------------------- importar: decisión

function conCsv(modo, payload) {
    return {
        ...peticion(),
        csv: [
            { cuenta: "320068234", nombre: "Ana López", grupo: "3CM1" },
            { cuenta: "320068235", nombre: "Luis Pérez", grupo: "3CM2" }
        ],
        modo,
        payload
    };
}
const EXISTE = { cuenta: "320068235", nombre_actual: "Luis P.", grupo_actual: "3CM1", tiene_rostro: true };

test("decidir: sin existentes en la base se escribe (modo omitir por omisión)", () => {
    const { salida } = ejecutar(ID.decidirImportacion, conCsv(null, []));
    assert.equal(salida[1], null);
    assert.deepEqual(salida[0].params, [
        ["320068234", "320068235"],
        ["Ana López", "Luis Pérez"],
        ["3CM1", "3CM2"],
        "omitir"
    ]);
});

test("decidir: existentes sin decisión del profesor responde 409 con los dos lados y no escribe", () => {
    const { salida } = ejecutar(ID.decidirImportacion, conCsv(null, [EXISTE]));
    assert.equal(salida[0], null);
    assert.equal(salida[1].statusCode, 409);
    assert.equal(salida[1].payload.escrito, false);
    assert.deepEqual(salida[1].payload.existentes, [{
        cuenta: "320068235",
        actual: { nombre: "Luis P.", grupo: "3CM1" },
        csv: { nombre: "Luis Pérez", grupo: "3CM2" },
        cambia: true,
        tiene_rostro: true
    }]);
    assert.deepEqual(salida[1].payload.opciones, ["actualizar", "omitir"]);
    assert.equal(salida[1].params, undefined);
});

test("decidir: una existente idéntica y sin rostro marca cambia:false y tiene_rostro:false", () => {
    const igual = { cuenta: "320068235", nombre_actual: "Luis Pérez", grupo_actual: "3CM2", tiene_rostro: false };
    const { salida } = ejecutar(ID.decidirImportacion, conCsv(null, [igual]));
    assert.equal(salida[1].statusCode, 409);
    assert.equal(salida[1].payload.existentes[0].cambia, false);
    assert.equal(salida[1].payload.existentes[0].tiene_rostro, false);
});

test("decidir: con actualizar u omitir se escribe y el modo llega a la sentencia", () => {
    assert.equal(ejecutar(ID.decidirImportacion, conCsv("actualizar", [EXISTE])).salida[0].params[3], "actualizar");
    assert.equal(ejecutar(ID.decidirImportacion, conCsv("omitir", [EXISTE])).salida[0].params[3], "omitir");
});

test("decidir: el payload sin filas falla", () => {
    assert.throws(() => ejecutar(ID.decidirImportacion, conCsv(null, undefined)), /no devolvió filas/);
});

// ---------------------------------------------------------------- importar: respuestas

test("responder importación: devuelve los conteos y el total del archivo", () => {
    const { salida } = ejecutar(ID.responderImportacion, {
        ...conCsv("omitir", [{ insertadas: 1, actualizadas: 0, omitidas: 1 }])
    });
    assert.equal(salida.statusCode, 200);
    assert.deepEqual(salida.payload, { insertadas: 1, actualizadas: 0, omitidas: 1, total: 2 });
});

test("responder importación: sin decisión del profesor y con omitidas, 409 (otra importación ganó la carrera)", () => {
    const { salida } = ejecutar(ID.responderImportacion, {
        ...conCsv(null, [{ insertadas: 1, actualizadas: 0, omitidas: 1 }])
    });
    assert.equal(salida.statusCode, 409);
    assert.equal(salida.payload.escrito, true);
    assert.equal(salida.payload.insertadas, 1);
    assert.equal(salida.payload.omitidas, 1);
    assert.match(salida.payload.error, /existentes=/);
});

test("responder importación: sin decisión y sin omitidas, 200", () => {
    const { salida } = ejecutar(ID.responderImportacion, {
        ...conCsv(null, [{ insertadas: 2, actualizadas: 0, omitidas: 0 }])
    });
    assert.equal(salida.statusCode, 200);
    assert.equal(salida.payload.escrito, undefined);
});

test("responder importación: el 409 de la carrera no lleva la lista de existentes (esa es la forma con escrito:false)", () => {
    const { salida } = ejecutar(ID.responderImportacion, conCsv(null, [{ insertadas: 0, actualizadas: 0, omitidas: 2 }]));
    assert.equal(salida.statusCode, 409);
    assert.equal(salida.payload.existentes, undefined);
});

test("responder importación: sin conteos falla", () => {
    assert.throws(() => ejecutar(ID.responderImportacion, conCsv("omitir", [])), /no devolvió conteos/);
});

test("responder 500: registra solo el SQLSTATE, nunca parámetros, payload, detalle ni cuentas", () => {
    const { salida, avisos } = ejecutar(ID.responder500, {
        ...peticion(),
        params: [["320068234"], ["NOMBRE-SECRETO"]],
        payload: "[0.1,0.2]",
        error: { message: "duplicate key value violates unique constraint (Key (no_cuenta)=(320068234))", code: "23505", detail: "Key (no_cuenta)=(320068234) already exists." }
    });
    assert.equal(salida.statusCode, 500);
    assert.deepEqual(salida.payload, { error: "No se pudo completar la operación en la base de datos" });
    assert.equal(avisos.length, 1);
    assert.match(avisos[0], /POST \/api\/alumnos\/importar falló: SQLSTATE 23505$/);
    assert.doesNotMatch(avisos.join(), /SECRETO|0\.1|320068234|Key/);
});

test("responder 500: sin SQLSTATE, el mensaje va truncado y sin números de cuenta", () => {
    const largo = "falla con cuenta 320068234 " + "x".repeat(500);
    const { avisos } = ejecutar(ID.responder500, { ...peticion(), error: { message: largo } });
    assert.equal(avisos.length, 1);
    assert.doesNotMatch(avisos[0], /320068234/);
    assert.ok(avisos[0].length < 250, String(avisos[0].length));
    const { avisos: otros } = ejecutar(ID.responder500, { ...peticion(), error: {} });
    assert.match(otros[0], /falló: error desconocido$/);
});

test("responder 500: un SQLSTATE con forma rara no se copia tal cual", () => {
    const { avisos } = ejecutar(ID.responder500, { ...peticion(), error: { message: "m", code: "320068234-no-es-sqlstate" } });
    assert.doesNotMatch(avisos[0], /320068234/);
});

// ---------------------------------------------------------------- estructura del flujo

test("flujo: toda consulta de la pestaña Alumnos va parametrizada, sin plantillas", () => {
    const consultas = flujos.filter((n) => n.type === "postgresql" && n.z === "a1f0c0de00000040");
    assert.ok(consultas.length >= 4);
    for (const n of consultas) {
        assert.doesNotMatch(n.query, /\{\{|\$\{|msg\./, n.name);
    }
});

test("flujo: las rutas http in de la API están definidas una sola vez", () => {
    const rutas = flujos.filter((n) => n.type === "http in").map((n) => n.method + " " + n.url);
    assert.equal(new Set(rutas).size, rutas.length);
    for (const ruta of ["get /api/galeria", "get /api/alumnos", "post /api/alumnos/importar", "get /api/metricas/energia"]) {
        assert.ok(rutas.includes(ruta), ruta);
    }
});

test("flujo: el http in de la importación no sube archivos (el cuerpo llega crudo)", () => {
    const entrada = flujos.find((n) => n.type === "http in" && n.url === "/api/alumnos/importar");
    assert.equal(entrada.upload, false);
});
