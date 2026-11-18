// Pruebas de la latencia de inferencia de la NPU en nodered/flows/flows.json:
//   1. Ingesta (pestaña "Pase de lista"): cada aula/vision/resultado que pasa la validación de su
//      manejador deja una fila en metricas_energia con origen 'npu' y las cuatro latencias de
//      `lat_ms`, sea cual sea el modo, la decisión o la sesión, y sin tocar la ruta de la asistencia.
//   2. API (pestaña "Energía"): GET /api/metricas/latencia devuelve esas filas, de solo lectura.
// Cada función se extrae del propio flows.json y se ejecuta en un contexto vm. No prueba el nodo de
// Postgres ni el broker: que la sentencia sea válida con la base real es de la placa. Aquí se
// comprueba su forma, sus parámetros y los privilegios. Las cuentas de aquí son entradas de prueba
// de las funciones, no datos de la lista real.
// Sin dependencias: node --test nodered/pruebas/*.test.js
"use strict";
const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const raiz = path.join(__dirname, "..", "..");
const flujos = JSON.parse(fs.readFileSync(path.join(raiz, "nodered", "flows", "flows.json"), "utf8"));
const ID = {
    tabPase: "a1f0c0de000000b0",
    validar: "a1f0c0de000000b3",
    limite: "a1f0c0de000000ba",
    consulta: "a1f0c0de000000b4",
    retro: "a1f0c0de000000b5",
    avisos: "a1f0c0de000000b7",
    limiteLatencia: "a1f0c0de000000e0",
    prepararLatencia: "a1f0c0de000000e1",
    insertarLatencia: "a1f0c0de000000e2",
    catchLatencia: "a1f0c0de000000e3",
    errorLatencia: "a1f0c0de000000e4",
    tabEnergia: "a1f0c0de00000001",
    entradaEnergia: "a1f0c0de00000021",
    validarEnergia: "a1f0c0de00000022",
    seleccionEnergia: "a1f0c0de00000023",
    respuesta: "a1f0c0de00000025",
    entradaApi: "a1f0c0de00000028",
    validarApi: "a1f0c0de00000029",
    seleccionApi: "a1f0c0de0000002a",
    catchApi: "a1f0c0de0000002b",
    responder500Api: "a1f0c0de0000002c"
};
const CONFIG_POSTGRES = "a1f0c0de00000003";
const CUENTA = "320068234";
const LAT = { cap: 180, det: 12, emb: 6, total: 240 };
const AHORA = Date.parse("2026-11-05T15:30:12.000Z");

function nodo(id) {
    const n = flujos.find((x) => x.id === id);
    assert.ok(n, "no existe el nodo " + id);
    return n;
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

// Ejecuta la función del nodo `id`. Devuelve { salida, avisos, logs }; la salida se copia por JSON
// (los objetos del contexto vm son de otro realm). Una excepción se propaga.
function ejecutar(id, msg, { ahora } = {}) {
    const n = nodo(id);
    assert.equal(n.type, "function");
    const avisos = [];
    const logs = [];
    const contexto = vm.createContext({ Buffer, Date: ahora === undefined ? Date : relojFijo(ahora), Math, Number, Object, console });
    const funcion = new vm.Script("(function (msg, node) {" + n.func + "\n})").runInContext(contexto);
    const nodoFalso = { warn: (t) => avisos.push(String(t)), log: (t) => logs.push(String(t)), error() {} };
    const salida = funcion(msg, nodoFalso);
    return { salida: salida === undefined || salida === null ? salida : JSON.parse(JSON.stringify(salida)), avisos, logs };
}

function resultado(extra = {}) {
    return {
        ts: "2026-11-05T09:30:12.000-06:00", // = AHORA
        modo: "pase",
        decision: "reconocido",
        cuenta: CUENTA,
        conf: 0.78,
        margen: 0.21,
        lat_ms: LAT,
        ...extra
    };
}
function mensaje(objeto, extra = {}) {
    return { topic: "aula/vision/resultado", payload: typeof objeto === "string" ? objeto : JSON.stringify(objeto), retain: false, ...extra };
}
const validar = (m) => ejecutar(ID.validar, m, { ahora: AHORA });
const preparar = (lat_ms) => ejecutar(ID.prepararLatencia, { payload: null, lat_ms });

// ---------------------------------------------------------------------------------------------
// Validar resultado de visión: la tercera salida lleva la latencia
// ---------------------------------------------------------------------------------------------

test("un resultado válido saca por la salida 3 las cuatro etapas de lat_ms, además de la ruta de la asistencia", () => {
    const r = validar(mensaje(resultado()));
    assert.deepEqual(r.salida[0], { fuente: "pase", decision: "reconocido", params: ["reconocido", CUENTA, 0.78], payload: null });
    assert.equal(r.salida[1], null);
    assert.deepEqual(r.salida[2], { payload: null, lat_ms: LAT });
    // La rama de la latencia no comparte objeto con las otras salidas (aquí la copia por JSON no lo vería).
    const n = nodo(ID.validar);
    const contexto = vm.createContext({ Buffer, Date: relojFijo(AHORA), Math, Number, Object, console });
    const funcion = new vm.Script("(function (msg, node) {" + n.func + "\n})").runInContext(contexto);
    const cruda = funcion(mensaje(resultado()), { warn() {}, log() {}, error() {} });
    assert.notStrictEqual(cruda[2], cruda[0]);
    assert.notStrictEqual(cruda[2], cruda[1]);
    assert.ok(!Object.values(cruda[0]).includes(cruda[2].lat_ms));
});

test("la latencia sale con cualquier modo y decisión que pase la validación (la inferencia se mide aunque no haya asistencia)", () => {
    for (const modo of ["pase", "prueba"]) {
        for (const decision of ["reconocido", "dudoso", "desconocido", "sin_rostro", "imagen_insuficiente", "error"]) {
            const sinCandidato = ["sin_rostro", "imagen_insuficiente", "error"].includes(decision);
            const r = validar(mensaje(resultado({ modo, decision, cuenta: sinCandidato ? null : CUENTA, conf: sinCandidato ? null : 0.5 })));
            assert.deepEqual(r.salida[2], { payload: null, lat_ms: LAT }, modo + " " + decision);
        }
    }
});

test("la ruta de la asistencia y sus avisos no cambian por la latencia", () => {
    const prueba = validar(mensaje(resultado({ modo: "prueba" })));
    assert.equal(prueba.salida[0], null);
    assert.equal(prueba.salida[1].payload, "Resultado de prueba: no marca asistencia (va a la prueba de reconocimiento)");
    const sinRostro = validar(mensaje(resultado({ decision: "sin_rostro", cuenta: null, conf: null })));
    assert.equal(sinRostro.salida[0], null);
    assert.equal(sinRostro.salida[1].payload, "Sin retroalimentación: la visión informó sin_rostro");
});

test("la latencia solo sale de un mensaje que pasó toda la validación: retain, vacío, JSON, campos, lat_ms y ts la cortan", () => {
    const sin = resultado();
    delete sin.ts;
    const descartados = [
        mensaje(resultado(), { retain: true }),
        mensaje(""),
        mensaje("no es json"),
        mensaje("[1]"),
        mensaje(resultado({ modo: "otro" })),
        mensaje(resultado({ decision: "otra" })),
        mensaje(resultado({ cuenta: "123" })),
        mensaje(resultado({ conf: 7 })),
        mensaje(resultado({ margen: "x" })),
        mensaje(resultado({ lat_ms: null })),
        mensaje(resultado({ lat_ms: [1, 2, 3, 4] })),
        mensaje(resultado({ cuenta: null })),
        mensaje(sin),
        mensaje(resultado({ ts: "1970-01-01T00:00:00.000+00:00" })),
        mensaje(resultado({ ts: new Date(AHORA + 61000).toISOString() })),
        mensaje(resultado({ ts: new Date(AHORA - 61000).toISOString() })),
        { topic: "aula/vision/resultado", payload: Buffer.alloc(0), retain: false },
        { topic: "aula/vision/resultado", payload: 12, retain: false }
    ];
    for (const m of descartados) {
        const r = validar(m);
        const tercera = r.salida === null ? null : r.salida[2];
        assert.ok(tercera === null || tercera === undefined, "no debe salir latencia de " + String(m.payload).slice(0, 60));
    }
});

test("la salida de latencia copia solo las cuatro etapas: ni la cuenta ni campos de más", () => {
    const lat_ms = { cap: 1, det: 2, emb: 3, total: 4, cuenta: CUENTA, extra: { a: 1 } };
    const r = validar(mensaje(resultado({ lat_ms })));
    assert.deepEqual(r.salida[2], { payload: null, lat_ms: { cap: 1, det: 2, emb: 3, total: 4 } });
    assert.doesNotMatch(JSON.stringify(r.salida[2]), new RegExp(CUENTA));
});

test("las etapas en null salen en null y las que faltan siguen ausentes (\"Preparar latencia de la NPU\" las vuelve null)", () => {
    const r = validar(mensaje(resultado({ lat_ms: { cap: 180, total: 190, det: null } })));
    assert.deepEqual(r.salida[2], { payload: null, lat_ms: { cap: 180, det: null, total: 190 } });
});

// ---------------------------------------------------------------------------------------------
// Preparar latencia de la NPU
// ---------------------------------------------------------------------------------------------

test("las cuatro etapas pasan como parámetros [cap, det, emb, total], sin el resto del mensaje", () => {
    const r = preparar(LAT);
    assert.deepEqual(r.salida[0].params, [180, 12, 6, 240]);
    assert.equal(r.salida[1], null);
    assert.deepEqual(Object.keys(r.salida[0]).sort(), ["params", "payload"]);
    assert.deepEqual(r.avisos, []);
});

test("una etapa que no corrió (null o ausente) se guarda como null; 0 es una medición", () => {
    assert.deepEqual(preparar({ cap: 150, det: null, emb: null, total: 160 }).salida[0].params, [150, null, null, 160]);
    assert.deepEqual(preparar({ cap: 150, total: 160 }).salida[0].params, [150, null, null, 160]);
    assert.deepEqual(preparar({ cap: 0, det: 0, emb: 0, total: 0 }).salida[0].params, [0, 0, 0, 0]);
});

test("un decimal se redondea al entero de ms más cercano (la columna es INT)", () => {
    assert.deepEqual(preparar({ cap: 12.4, det: 12.5, emb: 0.4, total: 240.49 }).salida[0].params, [12, 13, 0, 240]);
});

test("con las cuatro etapas en null (o ausentes) no hay nada que guardar y no sale ni un aviso", () => {
    for (const lat_ms of [{ cap: null, det: null, emb: null, total: null }, {}]) {
        const r = preparar(lat_ms);
        assert.equal(r.salida, null);
        assert.deepEqual(r.avisos, []);
    }
    // Un valor malo, en cambio, sí avisa aunque las demás etapas sean null.
    assert.match(preparar({ cap: null, det: null, emb: null, total: -1 }).salida[1].payload, /total debe ser null/);
});

test("un valor que no es un número de 0 a 600000 ms deja un aviso sin el valor y no guarda la fila", () => {
    const SECRETO = "SECRETO-" + CUENTA;
    for (const malo of ["240", SECRETO, -1, 600001, true, [], {}, 1e400, 2147483648]) {
        for (const etapa of ["cap", "det", "emb", "total"]) {
            const r = preparar({ ...LAT, [etapa]: malo });
            assert.equal(r.salida[0], null, etapa + "=" + JSON.stringify(malo));
            assert.equal(r.salida[1].payload, "Latencia no registrada: " + etapa + " debe ser null o un número de 0 a 600000 ms");
            assert.doesNotMatch(JSON.stringify(r.salida) + r.avisos.join(""), /SECRETO/);
        }
    }
    assert.deepEqual(preparar({ ...LAT, total: 600000 }).salida[0].params, [180, 12, 6, 600000]);
});

test("lat_ms que no es un objeto no guarda nada (la cadena nunca lo manda así, pero no debe lanzar)", () => {
    for (const lat_ms of [null, undefined, 240, "240", [180, 12, 6, 240]]) {
        const r = preparar(lat_ms);
        assert.equal(r.salida[0], null);
        assert.equal(r.salida[1].payload, "Latencia no registrada: lat_ms no es un objeto");
    }
});

test("el error de la inserción registra solo el SQLSTATE (o un mensaje truncado y sin cuentas) y no publica nada", () => {
    const r = ejecutar(ID.errorLatencia, { payload: null, params: [1, 2, 3, 4], error: { message: "falló con " + CUENTA, code: "42501" } });
    assert.equal(r.salida, null);
    assert.deepEqual(r.avisos, ["Latencia de la NPU: no se guardó: SQLSTATE 42501"]);
    const sin = ejecutar(ID.errorLatencia, { error: { message: "falló con " + CUENTA + " " + "x".repeat(300) } });
    assert.equal(sin.avisos.length, 1);
    assert.doesNotMatch(sin.avisos[0], new RegExp(CUENTA));
    assert.match(sin.avisos[0], /\*\*\*\*\*\*\*\*\*/);
    assert.ok(sin.avisos[0].length < 200);
    assert.deepEqual(ejecutar(ID.errorLatencia, {}).avisos, ["Latencia de la NPU: no se guardó: error desconocido"]);
});

// ---------------------------------------------------------------------------------------------
// Estructura de la ingesta
// ---------------------------------------------------------------------------------------------

const NODOS_LATENCIA = [ID.limiteLatencia, ID.prepararLatencia, ID.insertarLatencia, ID.catchLatencia, ID.errorLatencia];

// Ids alcanzables siguiendo los cables desde `id` (sin incluirlo).
function alcanzables(id) {
    const visto = new Set();
    const pendientes = [id];
    while (pendientes.length > 0) {
        const n = nodo(pendientes.pop());
        for (const salida of n.wires || []) {
            for (const destino of salida) {
                if (!visto.has(destino)) {
                    visto.add(destino);
                    pendientes.push(destino);
                }
            }
        }
    }
    return visto;
}

test("la validación saca [asistencia, avisos, latencia, prueba]: la asistencia sigue yendo primero y por su propio límite", () => {
    const v = nodo(ID.validar);
    // La cuarta salida (el resultado de modo prueba hacia el enlace a Registro) es de flujo-prueba.test.js.
    assert.equal(v.outputs, 4);
    assert.deepEqual(v.wires.slice(0, 3), [[ID.limite], [ID.avisos], [ID.limiteLatencia]]);
});

test("la ruta de la asistencia no llega a ningún nodo de la latencia y la de la latencia no llega a la asistencia", () => {
    const deAsistencia = alcanzables(ID.limite);
    for (const id of NODOS_LATENCIA) {
        assert.ok(!deAsistencia.has(id), id + " cuelga de la asistencia");
    }
    const deLatencia = alcanzables(ID.limiteLatencia);
    for (const id of [ID.limite, ID.consulta, ID.retro]) {
        assert.ok(!deLatencia.has(id), id + " cuelga de la latencia");
    }
    assert.ok(!deLatencia.has(ID.retro));
});

test("la cadena es validar → límite → preparar → inserción, con los avisos al debug de consola y sin salida después de la inserción", () => {
    assert.deepEqual(nodo(ID.limiteLatencia).wires, [[ID.prepararLatencia]]);
    assert.deepEqual(nodo(ID.prepararLatencia).wires, [[ID.insertarLatencia], [ID.avisos]]);
    assert.deepEqual(nodo(ID.insertarLatencia).wires, [[]]);
    const alimentan = flujos.filter((n) => (n.wires || []).some((salida) => salida.includes(ID.insertarLatencia)));
    assert.deepEqual(alimentan.map((n) => n.id), [ID.prepararLatencia]);
    const alimentanLimite = flujos.filter((n) => (n.wires || []).some((salida) => salida.includes(ID.limiteLatencia)));
    assert.deepEqual(alimentanLimite.map((n) => n.id), [ID.validar]);
});

test("el límite de la latencia es un delay propio de 2 por segundo (500 ms) que descarta (CWE-770), más estricto que el de la asistencia y sin compartir su contador", () => {
    const d = nodo(ID.limiteLatencia);
    assert.equal(d.type, "delay");
    assert.equal(d.z, ID.tabPase);
    assert.equal(d.pauseType, "rate");
    assert.equal(d.rate, "2");
    assert.match(d.name, /2 por segundo, 500 ms/);
    assert.ok(Number(d.rate) < Number(nodo(ID.limite).rate), "debe ser más estricto que el de la asistencia");
    assert.match(d.info, /no retrasa la asistencia/i);
    assert.match(d.info, /base está sana/);
    assert.equal(d.nbRateUnits, "1");
    assert.equal(d.rateUnits, "second");
    assert.equal(d.drop, true);
    assert.equal(d.allowrate, false);
    assert.notEqual(d.id, ID.limite);
});

test("la inserción usa el mismo Postgres que la ingesta de energía y una sentencia fija con origen 'npu' y las cuatro latencias", () => {
    const n = nodo(ID.insertarLatencia);
    assert.equal(n.type, "postgresql");
    assert.equal(n.z, ID.tabPase);
    assert.equal(n.postgreSQLConfig, CONFIG_POSTGRES);
    assert.equal(n.postgreSQLConfig, nodo("a1f0c0de00000013").postgreSQLConfig);
    assert.equal(n.query, "INSERT INTO metricas_energia (origen, lat_cap_ms, lat_det_ms, lat_emb_ms, lat_total_ms) VALUES ('npu', $1, $2, $3, $4)");
    assert.doesNotMatch(n.query, /\{\{|\$\{|msg\./);
    assert.doesNotMatch(n.query, /\bDELETE\b|\bTRUNCATE\b|\bDROP\b|\bALTER\b|\bUPDATE\b|ON CONFLICT/i);
});

test("las columnas de la inserción existen en schema.sql y el rol de Node-RED puede insertar en la tabla y usar su secuencia", () => {
    const schema = fs.readFileSync(path.join(raiz, "orangepi", "sql", "schema.sql"), "utf8");
    const tabla = /CREATE TABLE metricas_energia \(([\s\S]*?)\n\);/.exec(schema);
    assert.ok(tabla, "no se encontró metricas_energia");
    for (const columna of ["origen", "lat_cap_ms", "lat_det_ms", "lat_emb_ms", "lat_total_ms"]) {
        assert.match(tabla[1], new RegExp("^\\s+" + columna + "\\s", "m"), columna);
    }
    for (const columna of ["lat_cap_ms", "lat_det_ms", "lat_emb_ms", "lat_total_ms"]) {
        assert.match(tabla[1], new RegExp(columna + "\\s+INT,"), columna + " debe admitir null");
    }
    const roles = fs.readFileSync(path.join(raiz, "orangepi", "sql", "roles.sh"), "utf8");
    assert.match(roles, /GRANT SELECT, INSERT ON metricas_energia TO :"rol";/);
    assert.match(roles, /GRANT USAGE ON SEQUENCE metricas_energia_id_seq TO :"rol";/);
});

test("un catch propio cubre la preparación y la inserción, y solo lleva a registrar el error (no toca la asistencia)", () => {
    const c = nodo(ID.catchLatencia);
    assert.equal(c.type, "catch");
    assert.equal(c.uncaught, false);
    assert.deepEqual([...c.scope].sort(), [ID.prepararLatencia, ID.insertarLatencia].sort());
    assert.deepEqual(c.wires, [[ID.errorLatencia]]);
    assert.deepEqual(nodo(ID.errorLatencia).wires, [[]]);
    // El catch de la asistencia no cambia: no cubre ningún nodo de la latencia.
    const asistencia = nodo("a1f0c0de000000b8");
    assert.deepEqual([...asistencia.scope].sort(), [ID.validar, ID.consulta, ID.retro].sort());
});

test("la latencia no agrega ningún mqtt out ni mqtt in a la pestaña", () => {
    const enPestana = flujos.filter((n) => n.z === ID.tabPase);
    assert.equal(enPestana.filter((n) => n.type === "mqtt out").length, 2);
    assert.equal(enPestana.filter((n) => n.type === "mqtt in").length, 1);
    for (const id of NODOS_LATENCIA) {
        assert.equal(nodo(id).z, ID.tabPase);
        assert.notEqual(nodo(id).type, "mqtt out");
    }
});

// ---------------------------------------------------------------------------------------------
// API: GET /api/metricas/latencia
// ---------------------------------------------------------------------------------------------

test("la ruta GET /api/metricas/latencia existe una sola vez, junto a la de energía, y la energía conserva su cadena", () => {
    const entradas = flujos.filter((n) => n.type === "http in" && n.url === "/api/metricas/latencia");
    assert.equal(entradas.length, 1);
    assert.equal(entradas[0].id, ID.entradaApi);
    assert.equal(entradas[0].method, "get");
    assert.equal(entradas[0].z, ID.tabEnergia);
    assert.deepEqual(entradas[0].wires, [[ID.validarApi]]);
    assert.deepEqual(nodo(ID.entradaEnergia).wires, [[ID.validarEnergia]]);
    assert.deepEqual(nodo(ID.validarEnergia).wires, [[ID.seleccionEnergia], [ID.respuesta]]);
    assert.deepEqual(nodo(ID.seleccionEnergia).wires, [[ID.respuesta]]);
});

test("la consulta de la API es fija, de solo lectura, parametrizada y filtra origen 'npu' por el índice (origen, hora, id)", () => {
    const n = nodo(ID.seleccionApi);
    assert.equal(n.type, "postgresql");
    assert.equal(n.postgreSQLConfig, CONFIG_POSTGRES);
    assert.doesNotMatch(n.query, /\b(INSERT|UPDATE|DELETE|TRUNCATE|ALTER|DROP)\b/i);
    assert.doesNotMatch(n.query, /\{\{|\$\{|msg\./);
    assert.match(n.query, /FROM metricas_energia/);
    assert.match(n.query, /WHERE origen = 'npu'/);
    assert.match(n.query, /\$1::timestamptz IS NULL OR hora >= \$1::timestamptz/);
    assert.match(n.query, /ORDER BY hora DESC, id DESC\s+LIMIT \$2/);
    assert.doesNotMatch(n.query, /\$3/);
    assert.match(n.query, /lat_cap_ms AS cap/);
    assert.match(n.query, /lat_det_ms AS det/);
    assert.match(n.query, /lat_emb_ms AS emb/);
    assert.match(n.query, /lat_total_ms AS total/);
    // No expone las columnas de energía ni de temperatura: la ruta de latencia solo habla de latencia.
    assert.doesNotMatch(n.query, /\bv,|\bi,|\bp\b|temp_soc|SELECT \*/);
    assert.deepEqual(n.wires, [[ID.respuesta]]);
});

test("la validación de limite y desde es idéntica a la de la ruta de energía", () => {
    const peticiones = [
        {}, { limite: "5" }, { limite: "1" }, { limite: "1000" }, { limite: "5000" }, { limite: "0" }, { limite: "-3" },
        { limite: "abc" }, { limite: "1.5" }, { limite: ["1", "2"] }, { limite: "" },
        { desde: "2026-11-04" }, { desde: "2026-11-04T10:00:00Z" }, { desde: "2026-11-04T10:00:00.5-06:00" },
        { desde: "ayer" }, { desde: "1969-12-31T23:59:59Z" }, { desde: "9999-12-31T23:59:59-06:00" }, { desde: ["2026-11-04"] },
        { limite: "7", desde: "2026-11-04T10:00" }
    ];
    for (const query of peticiones) {
        const de = (id) => ejecutar(id, { req: { query } });
        assert.deepEqual(de(ID.validarApi), de(ID.validarEnergia), JSON.stringify(query));
    }
    assert.deepEqual(ejecutar(ID.validarApi, { req: { query: { limite: "5" } } }).salida[0].params, [null, 5]);
    assert.equal(ejecutar(ID.validarApi, { req: { query: { limite: "0" } } }).salida[1].statusCode, 400);
    assert.deepEqual(ejecutar(ID.validarApi, { req: {} }).salida[0].params, [null, 100]);
});

test("la validación responde por la salida 2 al nodo http response, igual que la de energía", () => {
    assert.deepEqual(nodo(ID.validarApi).wires, [[ID.seleccionApi], [ID.respuesta]]);
    assert.equal(nodo(ID.validarApi).outputs, 2);
    assert.equal(nodo(ID.respuesta).type, "http response");
});

test("un error de la consulta responde 500 sin el detalle y registra solo el SQLSTATE", () => {
    const c = nodo(ID.catchApi);
    assert.equal(c.type, "catch");
    assert.equal(c.uncaught, false);
    assert.deepEqual([...c.scope].sort(), [ID.validarApi, ID.seleccionApi].sort());
    assert.deepEqual(c.wires, [[ID.responder500Api]]);
    assert.deepEqual(nodo(ID.responder500Api).wires, [[ID.respuesta]]);
    const r = ejecutar(ID.responder500Api, { error: { message: "detalle interno de la base " + CUENTA, code: "42P01" } });
    assert.equal(r.salida.statusCode, 500);
    assert.deepEqual(r.salida.payload, { error: "No se pudo consultar la base de datos" });
    assert.deepEqual(r.avisos, ["GET /api/metricas/latencia falló: SQLSTATE 42P01"]);
    const sin = ejecutar(ID.responder500Api, { error: { message: "falló con " + CUENTA } });
    assert.doesNotMatch(sin.avisos.join(""), new RegExp(CUENTA));
});

test("las rutas http in de toda la hoja siguen siendo únicas", () => {
    const rutas = flujos.filter((n) => n.type === "http in").map((n) => n.method + " " + n.url);
    assert.equal(new Set(rutas).size, rutas.length);
    assert.ok(rutas.includes("get /api/metricas/energia"));
    assert.ok(rutas.includes("get /api/metricas/latencia"));
});
