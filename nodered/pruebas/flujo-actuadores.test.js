// Pruebas de la pestaña "Actuadores" de nodered/flows/flows.json: POST /api/actuadores, el control manual
// de la XIAO desde el frontend (contrato MQTT, sección 3, aula/actuador/cmd; plan, sección 6.7).
//   - Validación estricta del cuerpo: solo servo, led, buzzer y luz, con el vocabulario que acepta el
//     firmware de la XIAO (firmware/xiao/logica_actuadores.cpp, que esta prueba lee).
//   - 415 si el Content-Type no es application/json; 400 con {"error"} por cualquier otra cosa.
//   - Límite de 1 orden por segundo que descarta (429), sin encolar.
//   - Lo que se publica se reconstruye campo por campo: nunca el cuerpo recibido.
// La función se extrae del propio flows.json y se ejecuta en un contexto vm con un contexto de flujo falso.
// No prueba el broker ni Express: que Node-RED responda 500 a un JSON mal formado antes de llegar al flujo
// es de http in, y la autenticación y la protección CSRF de la ruta están en autenticacion-api.test.js.
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
    tab: "a1f0c0de00000120",
    entrada: "a1f0c0de00000122",
    validar: "a1f0c0de00000123",
    publicar: "a1f0c0de00000124",
    respuesta: "a1f0c0de00000125",
    catchApi: "a1f0c0de00000126",
    responder500: "a1f0c0de00000127"
};
const BROKER = "a1f0c0de00000002";
const AHORA = Date.parse("2026-11-16T15:30:12.000Z");
const MARCA = "SECRETO-no-debe-salir";
const JSON_CT = { "content-type": "application/json" };

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

function nuevoFlow() {
    const datos = new Map();
    return { get: (k) => datos.get(k), set: (k, v) => void datos.set(k, v), datos };
}

// Ejecuta la función del nodo `id`. La salida se copia por JSON (los objetos del contexto vm son de otro realm).
function ejecutar(id, msg, { ahora = AHORA, flow = nuevoFlow() } = {}) {
    const n = nodo(id);
    assert.equal(n.type, "function");
    const avisos = [];
    const logs = [];
    const contexto = vm.createContext({ Buffer, Date: relojFijo(ahora), Math, Number, Object, JSON, console });
    const funcion = new vm.Script("(function (msg, node, flow) {" + n.func + "\n})").runInContext(contexto);
    const nodoFalso = { warn: (t) => avisos.push(String(t)), log: (t) => logs.push(String(t)), error() {} };
    const salida = funcion(msg, nodoFalso, flow);
    return { salida: salida === undefined || salida === null ? salida : JSON.parse(JSON.stringify(salida)), avisos, logs, flow };
}

const peticion = (cuerpo, cabeceras = JSON_CT) => ({ req: { headers: cabeceras }, payload: cuerpo });
const orden = (cuerpo, opciones, cabeceras) => ejecutar(ID.validar, peticion(cuerpo, cabeceras), opciones);

function assertPublica(r, esperado) {
    assert.ok(r.salida[0], "no publicó");
    assert.deepEqual(Object.keys(r.salida[0]), ["payload"], "el mensaje al broker lleva solo payload");
    assert.equal(r.salida[0].payload, JSON.stringify(esperado));
    assert.equal(r.salida[1].statusCode, 200);
    assert.deepEqual(r.salida[1].payload, { publicado: esperado });
}

function assertRechaza(r, codigo) {
    assert.equal(r.salida[0], null, "no debía publicar");
    assert.equal(r.salida[1].statusCode, codigo);
    assert.equal(typeof r.salida[1].payload.error, "string");
    assert.ok(r.salida[1].payload.error.length > 0);
}

// ---- Órdenes válidas --------------------------------------------------------------------------------

test("cada orden del frontend (puerta, LED, buzzer y luz) se publica tal cual y se responde 200 con lo publicado", () => {
    const ordenes = [
        { servo: "abrir" },
        { led: "azul" },
        { led: "verde" },
        { led: "rojo" },
        { led: "ambar" },
        { led: "apagado" },
        { buzzer: 0 },
        { buzzer: 1 },
        { buzzer: 2 },
        { luz: 0 },
        { luz: 1 }
    ];
    for (const o of ordenes) {
        const r = orden(o);
        assertPublica(r, o);
        assert.deepEqual(r.avisos, []);
    }
});

test("varios campos juntos se publican en un orden fijo (servo, led, buzzer, luz) aunque lleguen en otro", () => {
    const r = orden({ luz: 1, buzzer: 2, led: "rojo", servo: "abrir" });
    assertPublica(r, { servo: "abrir", led: "rojo", buzzer: 2, luz: 1 });
    assert.equal(r.salida[0].payload, '{"servo":"abrir","led":"rojo","buzzer":2,"luz":1}');
});

test("el payload más grande cabe de sobra en el búfer de 256 bytes de la XIAO (contrato, sección 1)", () => {
    const r = orden({ servo: "abrir", led: "ambar", buzzer: 2, luz: 1 });
    assert.ok(Buffer.byteLength(r.salida[0].payload) < 100);
    const h = fs.readFileSync(path.join(raiz, "firmware", "xiao", "logica_luz.h"), "utf8");
    assert.match(h, /kMaxCmdBytes\s*=\s*256/);
});

test("el Content-Type puede llevar parámetros y mayúsculas", () => {
    for (const ct of ["application/json; charset=utf-8", "Application/JSON", " application/json "]) {
        assertPublica(orden({ luz: 1 }, {}, { "content-type": ct }), { luz: 1 });
    }
});

test("el 1.0 de un JSON es el entero 1: se acepta, y lo publicado sale como 1", () => {
    const r = ejecutar(ID.validar, peticion(JSON.parse('{"luz":1.0,"buzzer":2.0}')));
    assert.equal(r.salida[0].payload, '{"buzzer":2,"luz":1}');
});

// ---- Rechazos ---------------------------------------------------------------------------------------

test("un Content-Type que no es application/json responde 415 y no publica", () => {
    for (const cabeceras of [{}, { "content-type": "text/plain" }, { "content-type": "application/x-www-form-urlencoded" }, { "content-type": "text/csv" }, { "content-type": "application/jsonx" }]) {
        assertRechaza(orden({ luz: 1 }, {}, cabeceras), 415);
    }
});

test("un cuerpo que no es un objeto JSON, o está vacío, responde 400", () => {
    for (const cuerpo of [undefined, null, "luz", 1, true, [], [{ luz: 1 }], {}]) {
        assertRechaza(orden(cuerpo), 400);
    }
});

test("un campo que no es de los cuatro responde 400, aunque lo demás sea válido, y no se repite su nombre", () => {
    for (const cuerpo of [{ luz: 1, otro: 1 }, { cerrar: true }, { servo: "abrir", ["x" + MARCA]: 1 }, JSON.parse('{"luz":1,"__proto__":{"a":1}}'), { Luz: 1 }]) {
        const r = orden(cuerpo);
        assertRechaza(r, 400);
        assert.ok(!JSON.stringify(r).includes(MARCA));
    }
});

test("el vocabulario es el del firmware: servo solo abrir; led de cinco colores; buzzer 0, 1 o 2; luz 0 o 1", () => {
    const malos = [
        { servo: "cerrar" },
        { servo: "ABRIR" },
        { servo: "Abrir " },
        { servo: 1 },
        { servo: true },
        { servo: null },
        { servo: ["abrir"] },
        { led: "morado" },
        { led: "Verde" },
        { led: "ámbar" },
        { led: "" },
        { led: 1 },
        { led: null },
        { buzzer: 3 },
        { buzzer: -1 },
        { buzzer: 1.5 },
        { buzzer: "1" },
        { buzzer: true },
        { buzzer: null },
        { buzzer: [1] },
        { luz: 2 },
        { luz: "1" },
        { luz: true },
        { luz: false },
        { luz: null },
        { luz: 1e21 },
        { servo: "abrir", luz: 7 }
    ];
    for (const cuerpo of malos) {
        assertRechaza(orden(cuerpo), 400);
    }
});

test("el mensaje de un 400 nombra el campo del contrato y los valores admitidos, nunca lo recibido", () => {
    const casos = [
        [{ led: "morado " + MARCA }, /led/],
        [{ servo: MARCA }, /servo/],
        [{ buzzer: MARCA }, /buzzer/],
        [{ luz: MARCA }, /luz/]
    ];
    for (const [cuerpo, patron] of casos) {
        const r = orden(cuerpo);
        assertRechaza(r, 400);
        assert.match(r.salida[1].payload.error, patron);
        assert.ok(!JSON.stringify(r).includes(MARCA));
    }
});

test("ni un rechazo ni una orden válida dejan el cuerpo en el log", () => {
    const todo = [
        orden({ led: "morado " + MARCA }),
        orden({ ["x" + MARCA]: 1 }),
        orden({ luz: 1 }),
        orden(MARCA),
        orden({ servo: "abrir" }, {}, { "content-type": "text/plain" })
    ];
    for (const r of todo) {
        assert.ok(!JSON.stringify([r.avisos, r.logs]).includes(MARCA));
    }
});

// ---- Límite -----------------------------------------------------------------------------------------

test("una segunda orden a menos de 1 s de la anterior responde 429 con Retry-After y no publica", () => {
    const flow = nuevoFlow();
    assertPublica(orden({ luz: 1 }, { flow, ahora: AHORA }), { luz: 1 });
    for (const dt of [0, 1, 500, 999]) {
        const r = orden({ luz: 0 }, { flow, ahora: AHORA + dt });
        assertRechaza(r, 429);
        assert.equal(r.salida[1].headers["retry-after"], "1");
    }
});

test("a partir de 1 s se acepta otra, y las rechazadas no renuevan la ventana (el goteo no se bloquea a sí mismo)", () => {
    const flow = nuevoFlow();
    orden({ luz: 1 }, { flow, ahora: AHORA });
    assertRechaza(orden({ luz: 0 }, { flow, ahora: AHORA + 900 }), 429);
    assertPublica(orden({ luz: 0 }, { flow, ahora: AHORA + 1000 }), { luz: 0 });
    assertRechaza(orden({ luz: 1 }, { flow, ahora: AHORA + 1500 }), 429);
});

test("un cuerpo inválido no gasta la ventana: lo que no llega a la XIAO no la limita", () => {
    const flow = nuevoFlow();
    assertRechaza(orden({ led: "morado" }, { flow, ahora: AHORA }), 400);
    assertRechaza(orden({ luz: 1 }, { flow, ahora: AHORA + 10 }, {}), 415);
    assertPublica(orden({ luz: 1 }, { flow, ahora: AHORA + 20 }), { luz: 1 });
});

test("un reloj que retrocede acepta en vez de bloquear (Date.now() puede saltar)", () => {
    const flow = nuevoFlow();
    orden({ luz: 1 }, { flow, ahora: AHORA });
    assertPublica(orden({ luz: 0 }, { flow, ahora: AHORA - 60000 }), { luz: 0 });
});

// ---- Errores ----------------------------------------------------------------------------------------

test("el 500 no repite el error ni el cuerpo", () => {
    const r = ejecutar(ID.responder500, { error: { message: "fallo " + MARCA }, payload: { luz: MARCA } });
    assert.equal(r.salida.statusCode, 500);
    assert.equal(typeof r.salida.payload.error, "string");
    assert.ok(!JSON.stringify([r.salida, r.avisos, r.logs]).includes(MARCA));
});

// ---- Estructura -------------------------------------------------------------------------------------

test("la pestaña tiene un solo POST /api/actuadores, que va a la validación, y un solo mqtt out", () => {
    const entradas = flujos.filter((n) => n.type === "http in" && n.url === "/api/actuadores");
    assert.equal(entradas.length, 1);
    assert.equal(entradas[0].id, ID.entrada);
    assert.equal(entradas[0].method, "post");
    assert.equal(entradas[0].z, ID.tab);
    assert.deepEqual(entradas[0].wires, [[ID.validar]]);
    const propios = flujos.filter((n) => n.z === ID.tab);
    assert.deepEqual(propios.filter((n) => n.type === "mqtt out").map((n) => n.id), [ID.publicar]);
    assert.deepEqual(propios.filter((n) => n.type === "http in").map((n) => n.id), [ID.entrada]);
    assert.equal(propios.filter((n) => n.type === "mqtt in").length, 0);
    assert.equal(flujos.filter((n) => n.type === "http in" && n.url === "/api/actuadores").length, 1);
});

test("publica en aula/actuador/cmd con QoS 1 y sin retain, fijados en el nodo y con el broker Mosquitto", () => {
    const p = nodo(ID.publicar);
    assert.equal(p.type, "mqtt out");
    assert.equal(p.topic, "aula/actuador/cmd");
    assert.equal(p.qos, "1");
    assert.equal(p.retain, "false");
    assert.equal(p.broker, BROKER);
    // Mismos ajustes que la retroalimentación del pase de lista (pestaña Pase de lista).
    const pase = nodo("a1f0c0de000000b6");
    for (const campo of ["topic", "qos", "retain", "broker"]) {
        assert.equal(p[campo], pase[campo], campo);
    }
});

test("la validación publica por su salida 1 y responde por la 2; el catch de la pestaña responde 500", () => {
    const v = nodo(ID.validar);
    assert.equal(v.outputs, 2);
    assert.deepEqual(v.wires, [[ID.publicar], [ID.respuesta]]);
    const r = nodo(ID.respuesta);
    assert.equal(r.type, "http response");
    assert.equal(r.headers["content-type"], "application/json");
    assert.deepEqual(nodo(ID.catchApi).scope, [ID.validar]);
    assert.deepEqual(nodo(ID.catchApi).wires, [[ID.responder500]]);
    assert.deepEqual(nodo(ID.responder500).wires, [[ID.respuesta]]);
});

test("la ruta no toca la base: ningún nodo postgresql, exec ni file en la pestaña", () => {
    for (const n of flujos.filter((x) => x.z === ID.tab)) {
        assert.ok(!["postgresql", "exec", "file", "file in"].includes(n.type), n.type);
    }
});

test("el vocabulario de la validación sigue siendo el del firmware (logica_actuadores.cpp)", () => {
    const cpp = fs.readFileSync(path.join(raiz, "firmware", "xiao", "logica_actuadores.cpp"), "utf8");
    const cuerpo = (nombre) => {
        const inicio = cpp.indexOf(nombre);
        assert.ok(inicio >= 0, nombre);
        return cpp.slice(inicio, cpp.indexOf("\n}\n", inicio));
    };
    const cadenas = (texto) => [...texto.matchAll(/strcmp\(valor, "([a-z]+)"\)/g)].map((m) => m[1]).sort();
    const numeros = (texto) => [...texto.matchAll(/valor == ([0-9]+)/g)].map((m) => Number(m[1])).sort();
    assert.deepEqual(cadenas(cuerpo("PedidoServo interpretarServo")), ["abrir"]);
    assert.deepEqual(cadenas(cuerpo("PedidoLed interpretarLed")), ["ambar", "apagado", "azul", "rojo", "verde"]);
    assert.deepEqual(numeros(cuerpo("PedidoBuzzer interpretarBuzzer")), [0, 1, 2]);
    const luz = fs.readFileSync(path.join(raiz, "firmware", "xiao", "logica_luz.cpp"), "utf8");
    assert.deepEqual([...luz.matchAll(/valor == ([0-9]+)/g)].map((m) => Number(m[1])).sort(), [0, 1]);
    // Y lo que acepta la función es exactamente eso.
    for (const o of [{ servo: "abrir" }, { led: "azul" }, { led: "verde" }, { led: "rojo" }, { led: "ambar" }, { led: "apagado" }, { buzzer: 0 }, { buzzer: 1 }, { buzzer: 2 }, { luz: 0 }, { luz: 1 }]) {
        assert.ok(orden(o).salida[0], JSON.stringify(o));
    }
});
