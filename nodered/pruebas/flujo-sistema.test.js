// Pruebas de la pestaña "Sistema" de nodered/flows/flows.json:
//   1. Ingesta de estados: aula/vision/estado, aula/camara/estado y aula/actuador/estado (retenidos: el
//      retenido ES el estado actual). Se valida y se guarda el último estado válido de cada uno en el
//      contexto del flujo (memoria) con la hora en que Node-RED lo recibió.
//   2. Métricas de la placa: cada 10 s se leen /proc/stat, /proc/meminfo y las zonas térmicas, se publica
//      {"ts","temp","cpu","ram"} en aula/sistema/metricas (QoS 0, sin retain) y se guarda la temperatura en
//      metricas_energia con origen 'sistema'.
//   3. API: GET /api/sistema/estado devuelve el último estado de cada dispositivo y las últimas métricas.
// Cada función se extrae del propio flows.json y se ejecuta en un contexto vm, con un `flow` de mentira.
// No prueba el nodo de Postgres, el nodo `file in` ni el broker: que /proc se lea dentro del contenedor y
// que la sentencia sea válida con la base real es de la placa. Los payloads de aquí son de prueba.
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
    tab: "a1f0c0de000000f0",
    mqttVision: "a1f0c0de000000f2",
    mqttCamara: "a1f0c0de000000f3",
    mqttActuador: "a1f0c0de000000f4",
    guardar: "a1f0c0de000000f5",
    avisos: "a1f0c0de000000f6",
    catchEstado: "a1f0c0de000000f7",
    errorEstado: "a1f0c0de000000f8",
    inyectar: "a1f0c0de000000fa",
    siguiente: "a1f0c0de000000fb",
    leer: "a1f0c0de000000fc",
    catchLectura: "a1f0c0de000000fd",
    calcular: "a1f0c0de000000fe",
    publicar: "a1f0c0de000000ff",
    limite: "a1f0c0de00000100",
    insertar: "a1f0c0de00000101",
    catchMetricas: "a1f0c0de00000102",
    errorMetricas: "a1f0c0de00000103",
    entradaApi: "a1f0c0de00000106",
    estadoApi: "a1f0c0de00000107",
    respuestaApi: "a1f0c0de00000108",
    limpieza: "a1f0c0de00000033"
};
const CONFIG_POSTGRES = "a1f0c0de00000003";
const BROKER = "a1f0c0de00000002";
const ID_LIMPIEZA = "a1f0c0de00000033";
const AHORA = Date.parse("2026-11-16T15:30:12.000Z");

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

// Un `flow` de mentira: el contexto del flujo en memoria.
function nuevoFlow() {
    const datos = new Map();
    return {
        get: (k) => datos.get(k),
        set: (k, v) => void datos.set(k, v),
        datos
    };
}

// Ejecuta la función del nodo `id`. Devuelve { salida, avisos, logs, flow }; la salida se copia por JSON
// (los objetos del contexto vm son de otro realm). Una excepción se propaga.
function ejecutar(id, msg, { ahora = AHORA, flow = nuevoFlow() } = {}) {
    const n = nodo(id);
    assert.equal(n.type, "function");
    const avisos = [];
    const logs = [];
    const contexto = vm.createContext({ Buffer, Date: relojFijo(ahora), Math, Number, Object, console });
    const funcion = new vm.Script("(function (msg, node, flow) {" + n.func + "\n})").runInContext(contexto);
    const nodoFalso = { warn: (t) => avisos.push(String(t)), log: (t) => logs.push(String(t)), error() {} };
    const salida = funcion(msg, nodoFalso, flow);
    return { salida: salida === undefined || salida === null ? salida : JSON.parse(JSON.stringify(salida)), avisos, logs, flow };
}
const copia = (x) => (x === undefined ? undefined : JSON.parse(JSON.stringify(x)));

const TOPICO_VISION = "aula/vision/estado";
const TOPICO_CAMARA = "aula/camara/estado";
const TOPICO_ACTUADOR = "aula/actuador/estado";
function mensaje(topic, objeto, extra = {}) {
    return { topic, payload: typeof objeto === "string" ? objeto : JSON.stringify(objeto), retain: true, ...extra };
}
const guardar = (m, opciones) => ejecutar(ID.guardar, m, opciones);

const VISION = { ts: "2026-11-16T09:30:00.000-06:00", en_linea: true, modo: "inactivo", galeria: 38, modelo: "w600k_mbf-uint8-v1" };
// Lo que publica el firmware en marcha (firmware/xiao/logica_proteccion.cpp): además de los campos del
// contrato lleva camara, corte y nvs.
const CAMARA = { en_linea: true, rssi: -58, heap: 120000, temp: 48.5, camara: false, corte: false, nvs: true };
const ACTUADOR = { servo: "cerrado", led: "azul", luz: 0 };

// ---------------------------------------------------------------------------------------------
// Ingesta de estados
// ---------------------------------------------------------------------------------------------

test("un estado válido de cada tópico se guarda con la hora en que Node-RED lo recibió y no deja avisos", () => {
    const flow = nuevoFlow();
    const casos = [
        [TOPICO_VISION, VISION, "estado_vision"],
        [TOPICO_CAMARA, CAMARA, "estado_camara"],
        [TOPICO_ACTUADOR, ACTUADOR, "estado_actuadores"]
    ];
    for (const [topico, payload, clave] of casos) {
        const r = guardar(mensaje(topico, payload), { flow });
        assert.equal(r.salida, null, topico);
        assert.deepEqual(r.avisos, [], topico);
        assert.deepEqual(copia(flow.get(clave)), { datos: payload, recibido_ms: AHORA }, topico);
    }
    assert.equal(flow.datos.size, 3);
});

test("el retenido es el estado vigente: un mensaje con retain true se guarda, no se descarta", () => {
    for (const [topico, payload, clave] of [
        [TOPICO_VISION, VISION, "estado_vision"],
        [TOPICO_CAMARA, CAMARA, "estado_camara"],
        [TOPICO_ACTUADOR, ACTUADOR, "estado_actuadores"]
    ]) {
        const r = guardar(mensaje(topico, payload, { retain: true }));
        assert.deepEqual(r.avisos, [], topico);
        assert.notEqual(r.flow.get(clave), undefined, topico);
    }
});

test("el Will de la visión y el de la cámara ({\"en_linea\":false}) reemplazan al estado en línea", () => {
    const flow = nuevoFlow();
    guardar(mensaje(TOPICO_VISION, VISION), { flow });
    guardar(mensaje(TOPICO_CAMARA, CAMARA), { flow });
    const r1 = guardar(mensaje(TOPICO_VISION, { en_linea: false }), { flow, ahora: AHORA + 5000 });
    const r2 = guardar(mensaje(TOPICO_CAMARA, { en_linea: false }), { flow, ahora: AHORA + 5000 });
    assert.deepEqual([r1.avisos, r2.avisos], [[], []]);
    assert.deepEqual(copia(flow.get("estado_vision")), { datos: { en_linea: false }, recibido_ms: AHORA + 5000 });
    assert.deepEqual(copia(flow.get("estado_camara")), { datos: { en_linea: false }, recibido_ms: AHORA + 5000 });
});

test("el estado parcial de los actuadores (la versión vieja del firmware solo mandaba luz) es válido", () => {
    const r = guardar(mensaje(TOPICO_ACTUADOR, { luz: 1 }));
    assert.deepEqual(r.avisos, []);
    assert.deepEqual(copia(r.flow.get("estado_actuadores")).datos, { luz: 1 });
});

test("vacío (texto o Buffer) se ignora en silencio y no borra el último estado", () => {
    const flow = nuevoFlow();
    guardar(mensaje(TOPICO_CAMARA, CAMARA), { flow });
    for (const vacio of ["", Buffer.alloc(0)]) {
        const r = guardar({ topic: TOPICO_CAMARA, payload: vacio, retain: false }, { flow, ahora: AHORA + 1000 });
        assert.equal(r.salida, null);
        assert.deepEqual(r.avisos, []);
    }
    assert.deepEqual(copia(flow.get("estado_camara")), { datos: CAMARA, recibido_ms: AHORA });
});

test("lo no válido deja un aviso sin el contenido y conserva el último estado válido", () => {
    const flow = nuevoFlow();
    guardar(mensaje(TOPICO_VISION, VISION), { flow });
    const MARCA = "SECRETO-12345";
    const malos = [
        { payload: Buffer.from("{}") },
        { payload: 12 },
        { payload: "no es json " + MARCA },
        { payload: JSON.stringify([MARCA]) },
        { payload: JSON.stringify(MARCA) },
        { payload: "null" },
        { payload: JSON.stringify({ en_linea: true, modelo: MARCA.repeat(200) }) },
        { payload: " ".repeat(2000) + "{}" },
        { payload: JSON.stringify({ en_linea: MARCA }) }
    ];
    for (const malo of malos) {
        const r = guardar({ topic: TOPICO_VISION, retain: false, ...malo }, { flow, ahora: AHORA + 1000 });
        assert.equal(r.salida[0].payload.startsWith("Descartado, "), true, JSON.stringify(malo.payload).slice(0, 40));
        assert.equal(r.avisos.every((a) => !a.includes(MARCA)), true);
        assert.equal(r.salida[0].payload.includes(MARCA), false);
    }
    assert.deepEqual(copia(flow.get("estado_vision")), { datos: VISION, recibido_ms: AHORA });
});

test("campos fuera del contrato (tipo, vocabulario o rango) descartan todo el mensaje y el aviso nombra el campo, no el valor", () => {
    const casos = [
        [TOPICO_VISION, { en_linea: "si" }, "en_linea"],
        [TOPICO_VISION, { modo: "inactivo" }, "en_linea"], // falta el obligatorio
        [TOPICO_VISION, { en_linea: true, modo: "dormido" }, "modo"],
        [TOPICO_VISION, { en_linea: true, modo: 1 }, "modo"],
        [TOPICO_VISION, { en_linea: true, galeria: -1 }, "galeria"],
        [TOPICO_VISION, { en_linea: true, galeria: 1.5 }, "galeria"],
        [TOPICO_VISION, { en_linea: true, galeria: "38" }, "galeria"],
        [TOPICO_VISION, { en_linea: true, modelo: 7 }, "modelo"],
        [TOPICO_VISION, { en_linea: true, modelo: "a\nb" }, "modelo"],
        [TOPICO_VISION, { en_linea: true, ts: "ayer" }, "ts"],
        [TOPICO_VISION, { en_linea: true, ts: 12 }, "ts"],
        [TOPICO_CAMARA, { en_linea: 1 }, "en_linea"],
        [TOPICO_CAMARA, { rssi: -58 }, "en_linea"],
        [TOPICO_CAMARA, { en_linea: true, rssi: "-58" }, "rssi"],
        [TOPICO_CAMARA, { en_linea: true, rssi: 20.5 }, "rssi"],
        [TOPICO_CAMARA, { en_linea: true, heap: -1 }, "heap"],
        [TOPICO_CAMARA, { en_linea: true, heap: 1e12 }, "heap"],
        [TOPICO_CAMARA, { en_linea: true, temp: 500 }, "temp"],
        [TOPICO_CAMARA, { en_linea: true, temp: null }, "temp"],
        [TOPICO_CAMARA, { en_linea: true, camara: "no" }, "camara"],
        [TOPICO_CAMARA, { en_linea: true, corte: 0 }, "corte"],
        [TOPICO_CAMARA, { en_linea: true, nvs: "true" }, "nvs"],
        [TOPICO_ACTUADOR, { servo: "arriba" }, "servo"],
        [TOPICO_ACTUADOR, { servo: "abrir" }, "servo"],
        [TOPICO_ACTUADOR, { led: "morado" }, "led"],
        [TOPICO_ACTUADOR, { luz: 2 }, "luz"],
        [TOPICO_ACTUADOR, { luz: true }, "luz"],
        [TOPICO_ACTUADOR, { servo: "cerrado", led: 3, luz: 0 }, "led"]
    ];
    for (const [topico, payload, campo] of casos) {
        const flow = nuevoFlow();
        const r = guardar(mensaje(topico, payload), { flow });
        assert.equal(flow.datos.size, 0, JSON.stringify(payload));
        assert.match(r.salida[0].payload, /^Descartado, /);
        assert.ok(r.salida[0].payload.includes(campo), JSON.stringify(payload) + " -> " + r.salida[0].payload);
        assert.ok(r.salida[0].payload.includes(topico));
    }
});

test("el modelo de la visión es ASCII imprimible: caracteres de control, de formato (bidi, ancho cero) y acentuados se descartan", () => {
    for (const modelo of ["w600k\u202ebmf", "w600k\u200bmbf", "w600k\u2066mbf", "w600k\ufeffmbf", "w600k\u00e9", "w600k\ud83d\ude00", "a\tb", "a\nb", "a\u007fb", "x".repeat(101)]) {
        const flow = nuevoFlow();
        const r = guardar(mensaje(TOPICO_VISION, { en_linea: true, modelo }), { flow });
        assert.equal(flow.datos.size, 0, JSON.stringify(modelo));
        assert.match(r.salida[0].payload, /^Descartado, aula\/vision\/estado: modelo fuera de la tabla del contrato$/);
        assert.doesNotMatch(r.salida[0].payload, /[^\x20-\x7e]/);
    }
    for (const modelo of ["", "w600k_mbf-uint8-v1", " ~ ", "x".repeat(100)]) {
        const flow = nuevoFlow();
        guardar(mensaje(TOPICO_VISION, { en_linea: true, modelo }), { flow });
        assert.equal(flow.get("estado_vision").datos.modelo, modelo);
    }
});

test("un estado de actuadores sin ningún campo del contrato no se guarda", () => {
    for (const payload of [{}, { otro: 1 }]) {
        const flow = nuevoFlow();
        const r = guardar(mensaje(TOPICO_ACTUADOR, payload), { flow });
        assert.equal(flow.datos.size, 0);
        assert.match(r.salida[0].payload, /^Descartado, /);
    }
});

test("los campos que el contrato no define se ignoran y no se guardan; __proto__ no contamina", () => {
    const flow = nuevoFlow();
    guardar(mensaje(TOPICO_VISION, '{"en_linea":true,"clave":"x","galeria":3,"__proto__":{"polluted":1}}'), { flow });
    assert.deepEqual(copia(flow.get("estado_vision")).datos, { en_linea: true, galeria: 3 });
    assert.equal({}.polluted, undefined);
    guardar(mensaje(TOPICO_CAMARA, { en_linea: true, otro: [1], rssi: -60 }), { flow });
    assert.deepEqual(copia(flow.get("estado_camara")).datos, { en_linea: true, rssi: -60 });
});

test("la visión puede mandar ts vacío (así lo escribe cuando no hay hora) y no se guarda ese ts", () => {
    const flow = nuevoFlow();
    const r = guardar(mensaje(TOPICO_VISION, { ts: "", en_linea: true, modo: "pase", galeria: 2, modelo: "" }), { flow });
    assert.deepEqual(r.avisos, []);
    assert.deepEqual(copia(flow.get("estado_vision")).datos, { en_linea: true, modo: "pase", galeria: 2, modelo: "" });
});

test("ts con zona (Z o desfase) es válido y no se compara con el reloj: un retenido es viejo por naturaleza", () => {
    const flow = nuevoFlow();
    for (const ts of ["2020-01-01T00:00:00Z", "1970-01-01T00:00:00.000+00:00", "2026-11-16T09:30:00.120-06:00"]) {
        const r = guardar(mensaje(TOPICO_VISION, { ts, en_linea: true }), { flow });
        assert.deepEqual(r.avisos, [], ts);
        assert.equal(copia(flow.get("estado_vision")).datos.ts, ts);
    }
});

test("un estado válido más nuevo reemplaza al anterior y renueva la hora de recepción", () => {
    const flow = nuevoFlow();
    guardar(mensaje(TOPICO_ACTUADOR, ACTUADOR), { flow });
    guardar(mensaje(TOPICO_ACTUADOR, { servo: "abierto", led: "verde", luz: 1 }), { flow, ahora: AHORA + 7000 });
    assert.deepEqual(copia(flow.get("estado_actuadores")), {
        datos: { servo: "abierto", led: "verde", luz: 1 },
        recibido_ms: AHORA + 7000
    });
});

test("un tópico que no es de los tres no se guarda (defensa: el mqtt in solo debería entregar los suyos)", () => {
    const flow = nuevoFlow();
    const r = guardar(mensaje("aula/vision/cmd", { en_linea: true }), { flow });
    assert.equal(flow.datos.size, 0);
    assert.match(r.salida[0].payload, /^Descartado, /);
});

// ---------------------------------------------------------------------------------------------
// Estructura de la ingesta
// ---------------------------------------------------------------------------------------------

test("tres mqtt in (QoS 1, broker Mosquitto) alimentan una sola función de guardado, con avisos a la consola", () => {
    const entradas = flujos.filter((n) => n.z === ID.tab && n.type === "mqtt in");
    assert.deepEqual(entradas.map((n) => n.topic).sort(), [TOPICO_CAMARA, TOPICO_ACTUADOR, TOPICO_VISION].sort());
    for (const n of entradas) {
        assert.equal(n.qos, "1", n.topic);
        assert.equal(n.broker, BROKER, n.topic);
        assert.equal(n.datatype, "utf8", n.topic);
        assert.deepEqual(n.wires, [[ID.guardar]], n.topic);
    }
    const avisos = nodo(ID.avisos);
    assert.equal(avisos.type, "debug");
    assert.equal(avisos.console, true);
    assert.deepEqual(nodo(ID.guardar).wires, [[ID.avisos]]);
    assert.equal(nodo(ID.guardar).outputs, 1);
});

test("la limpieza de retenidos no borra los tres estados (ni aula/sistema/metricas)", () => {
    const r = ejecutar(ID.limpieza, { status: { fill: "green", shape: "dot" } });
    const topicos = r.salida[0].map((m) => m.topic);
    assert.ok(topicos.length > 0);
    for (const prohibido of [TOPICO_VISION, TOPICO_CAMARA, TOPICO_ACTUADOR, "aula/sistema/metricas"]) {
        assert.ok(!topicos.includes(prohibido), prohibido);
    }
});

test("la pestaña solo publica en aula/sistema/metricas: ningún mqtt out toca los tópicos de estado ni de órdenes", () => {
    const salidas = flujos.filter((n) => n.z === ID.tab && n.type === "mqtt out");
    assert.deepEqual(salidas.map((n) => n.id), [ID.publicar]);
    assert.equal(salidas[0].topic, "aula/sistema/metricas");
});

test("un catch propio cubre el guardado y lo manda a un registro de error sin contenido", () => {
    const c = nodo(ID.catchEstado);
    assert.equal(c.type, "catch");
    assert.deepEqual(c.scope, [ID.guardar]);
    assert.deepEqual(c.wires, [[ID.errorEstado]]);
    const r = ejecutar(ID.errorEstado, { payload: "SECRETO-1", error: { message: "boom SECRETO-1 320068234", code: "XX000" } });
    assert.equal(r.salida, null);
    assert.equal(r.avisos.length, 1);
    assert.equal(r.avisos.join("").includes("SECRETO-1"), false);
    assert.equal(r.avisos.join("").includes("320068234"), false);
});

// ---------------------------------------------------------------------------------------------
// Métricas de la placa
// ---------------------------------------------------------------------------------------------

const STAT_1 = "cpu  100 0 100 800 0 0 0 0 0 0\ncpu0 50 0 50 400 0 0 0 0 0 0\nintr 1 2 3\n";
const STAT_2 = "cpu  200 0 200 1600 0 0 0 0 0 0\ncpu0 100 0 100 800 0 0 0 0 0 0\nintr 4 5 6\n"; // 1000 más, 800 inactivos → 20 %
const MEMINFO = "MemTotal:        4000000 kB\nMemFree:          100000 kB\nMemAvailable:    1000000 kB\nBuffers: 1 kB\n";
const ZONAS = [0, 1, 2, 3, 4].map((i) => "/sys/class/thermal/thermal_zone" + i + "/temp");
const TEMPS = { [ZONAS[0]]: "48360\n", [ZONAS[1]]: "46686\n", [ZONAS[2]]: "45880\n", [ZONAS[3]]: "48608\n", [ZONAS[4]]: "47368\n" };

// Recorre el bucle "Siguiente archivo" ↔ file in con un sistema de archivos de mentira. `archivos[ruta]`
// es el texto, null/undefined para un error de lectura ENOENT (lo que haría el catch) o un objeto que se usa
// como msg.error (otro error de lectura). `flow` es el contexto del flujo: pásalo para que dure entre ciclos; `ahora`
// (ms) fija el reloj del ciclo.
function recorrer(archivos, flow = nuevoFlow(), ahora) {
    let m = { payload: "2026-11-16T15:30:12.000Z", topic: "" };
    const leidos = [];
    const avisos = [];
    for (let vuelta = 0; vuelta < 30; vuelta++) {
        const e = ejecutar(ID.siguiente, m, { flow, ahora });
        const r = e.salida;
        avisos.push(...e.avisos);
        assert.equal(r.length, 2);
        if (r[1] !== null) {
            return { fin: r[1], leidos, avisos };
        }
        m = r[0];
        const ruta = m.filename;
        leidos.push(ruta);
        const contenido = archivos[ruta];
        if (typeof contenido === "string") {
            m.payload = contenido;
        } else {
            delete m.payload;
            m.error = contenido !== null && typeof contenido === "object"
                ? contenido
                : { message: "ENOENT: no such file or directory, open '" + ruta + "'" };
        }
    }
    assert.fail("el bucle no termina");
}
const sistemaDeArchivos = (stat, extra = {}) => ({ "/proc/stat": stat, "/proc/meminfo": MEMINFO, ...TEMPS, ...extra });
const calcular = (m, opciones) => ejecutar(ID.calcular, m, opciones);

test("el bucle lee /proc/stat, /proc/meminfo y las cinco zonas térmicas, una por vuelta, y termina", () => {
    const { fin, leidos } = recorrer(sistemaDeArchivos(STAT_1));
    assert.deepEqual(leidos, ["/proc/stat", "/proc/meminfo", ...ZONAS]);
    assert.deepEqual(fin.leidos, sistemaDeArchivos(STAT_1));
});

test("un archivo que no se pudo leer queda en null y el bucle sigue con los demás", () => {
    const { fin, leidos } = recorrer(sistemaDeArchivos(STAT_1, { [ZONAS[2]]: null }));
    assert.equal(leidos.length, 7);
    assert.equal(fin.leidos[ZONAS[2]], null);
    assert.equal(fin.leidos[ZONAS[3]], TEMPS[ZONAS[3]]);
    assert.equal(fin.error, undefined);
});

test("un Buffer (si el nodo no decodifica) también se lee como texto", () => {
    let m = { payload: "x" };
    const r1 = ejecutar(ID.siguiente, m).salida[0];
    r1.payload = Buffer.from(STAT_1);
    const r2 = ejecutar(ID.siguiente, r1).salida[0];
    assert.equal(r2.leidos["/proc/stat"], STAT_1);
});

test("el primer ciclo solo guarda la base de la CPU: no publica ni guarda nada y no deja aviso (es lo esperado al arrancar)", () => {
    const { fin } = recorrer(sistemaDeArchivos(STAT_1));
    const r = calcular(fin);
    assert.deepEqual(r.salida, [null, null, null]);
    assert.equal(r.flow.get("metricas_sistema"), undefined);
    assert.notEqual(r.flow.get("cpu_previo"), undefined);
});

test("el segundo ciclo publica {ts,temp,cpu,ram} y guarda la temperatura y las métricas", () => {
    const flow = nuevoFlow();
    calcular(recorrer(sistemaDeArchivos(STAT_1)).fin, { flow });
    const r = calcular(recorrer(sistemaDeArchivos(STAT_2)).fin, { flow, ahora: AHORA + 10000 });
    const esperado = { ts: new Date(AHORA + 10000).toISOString(), temp: 48.6, cpu: 20, ram: 75 };
    assert.deepEqual(JSON.parse(r.salida[0].payload), esperado);
    assert.deepEqual(Object.keys(JSON.parse(r.salida[0].payload)), ["ts", "temp", "cpu", "ram"]);
    assert.deepEqual(r.salida[1].params, [48.6]);
    assert.equal(r.salida[2], null);
    assert.deepEqual(copia(flow.get("metricas_sistema")), { datos: esperado, recibido_ms: AHORA + 10000 });
});

test("las salidas de publicar y de la base son mensajes distintos (no comparten objeto)", () => {
    const flow = nuevoFlow();
    calcular(recorrer(sistemaDeArchivos(STAT_1)).fin, { flow });
    const n = nodo(ID.calcular);
    const contexto = vm.createContext({ Buffer, Date: relojFijo(AHORA), Math, Number, Object, console });
    const funcion = new vm.Script("(function (msg, node, flow) {" + n.func + "\n})").runInContext(contexto);
    const cruda = funcion(recorrer(sistemaDeArchivos(STAT_2)).fin, { warn() {}, log() {}, error() {} }, flow);
    assert.notStrictEqual(cruda[0], cruda[1]);
});

test("la temperatura es la de la zona más caliente de las que se pudieron leer (milésimas de grado → °C, un decimal)", () => {
    const flow = nuevoFlow();
    calcular(recorrer(sistemaDeArchivos(STAT_1)).fin, { flow });
    const r = calcular(
        recorrer(sistemaDeArchivos(STAT_2, { [ZONAS[3]]: null, [ZONAS[0]]: "51234\n", [ZONAS[4]]: "no es un número\n" })).fin,
        { flow }
    );
    assert.equal(JSON.parse(r.salida[0].payload).temp, 51.2);
});

test("una lectura de temperatura absurda se ignora; sin ninguna temperatura legible no se publica y queda un aviso", () => {
    const flow = nuevoFlow();
    calcular(recorrer(sistemaDeArchivos(STAT_1)).fin, { flow });
    const absurdas = Object.fromEntries(ZONAS.map((z) => [z, "999999\n"]));
    const r = calcular(recorrer(sistemaDeArchivos(STAT_2, absurdas)).fin, { flow });
    assert.equal(r.salida[0], null);
    assert.equal(r.salida[1], null);
    assert.match(r.salida[2].payload, /temperatura/);
    assert.equal(flow.get("metricas_sistema"), undefined);
    const sinZonas = Object.fromEntries(ZONAS.map((z) => [z, null]));
    const flow2 = nuevoFlow();
    calcular(recorrer(sistemaDeArchivos(STAT_1)).fin, { flow: flow2 });
    const r2 = calcular(recorrer(sistemaDeArchivos(STAT_2, sinZonas)).fin, { flow: flow2 });
    assert.equal(r2.salida[0], null);
    assert.match(r2.salida[2].payload, /temperatura/);
});

test("sin /proc/meminfo o sin /proc/stat legibles no se publica nada y el aviso lo dice", () => {
    for (const faltante of ["/proc/meminfo", "/proc/stat"]) {
        const flow = nuevoFlow();
        calcular(recorrer(sistemaDeArchivos(STAT_1)).fin, { flow });
        const r = calcular(recorrer(sistemaDeArchivos(STAT_2, { [faltante]: null })).fin, { flow });
        assert.equal(r.salida[0], null, faltante);
        assert.equal(r.salida[1], null, faltante);
        assert.match(r.salida[2].payload, /^Métricas del sistema no publicadas: /, faltante);
    }
});

test("archivos con basura no producen NaN ni valores fuera de rango: no se publica", () => {
    const basura = [
        { "/proc/stat": "cpu  a b c d\n" },
        { "/proc/stat": "" },
        { "/proc/meminfo": "MemTotal: 0 kB\nMemAvailable: 0 kB\n" },
        { "/proc/meminfo": "MemTotal: 100 kB\nMemAvailable: 200 kB\n" },
        { "/proc/meminfo": "nada\n" }
    ];
    for (const extra of basura) {
        const flow = nuevoFlow();
        calcular(recorrer(sistemaDeArchivos(STAT_1)).fin, { flow });
        const r = calcular(recorrer(sistemaDeArchivos(STAT_2, extra)).fin, { flow });
        assert.equal(r.salida[0], null, JSON.stringify(extra));
        assert.equal(r.salida[1], null, JSON.stringify(extra));
        assert.equal(typeof r.salida[2].payload, "string");
    }
});

test("si los contadores de la CPU no avanzan (o retroceden por un reinicio) no hay porcentaje: se toma nueva base", () => {
    const flow = nuevoFlow();
    calcular(recorrer(sistemaDeArchivos(STAT_2)).fin, { flow });
    const igual = calcular(recorrer(sistemaDeArchivos(STAT_2)).fin, { flow });
    assert.equal(igual.salida[0], null);
    assert.match(igual.salida[2].payload, /contadores de la CPU/);
    const atras = calcular(recorrer(sistemaDeArchivos(STAT_1)).fin, { flow });
    assert.equal(atras.salida[0], null);
    assert.match(atras.salida[2].payload, /contadores de la CPU/);
    // Con STAT_1 como base nueva, el siguiente ciclo ya publica.
    const sigue = calcular(recorrer(sistemaDeArchivos(STAT_2)).fin, { flow });
    assert.equal(JSON.parse(sigue.salida[0].payload).cpu, 20);
});

test("cpu y ram se redondean a enteros y se acotan a 0 a 100; la CPU cuenta iowait como inactivo y steal como ocupado", () => {
    const flow = nuevoFlow();
    // base: user 0, nice 0, system 0, idle 0, iowait 0, irq 0, softirq 0, steal 0
    calcular(recorrer(sistemaDeArchivos("cpu  0 0 0 0 0 0 0 0 0 0\n")).fin, { flow });
    // 100 user + 100 system + 50 irq + 50 softirq + 100 steal = 400 ocupados; 300 idle + 300 iowait = 600 inactivos → 40 %
    const r = calcular(
        recorrer(sistemaDeArchivos("cpu  100 0 100 300 300 50 50 100 0 0\n", { "/proc/meminfo": "MemTotal: 3 kB\nMemAvailable: 1 kB\n" })).fin,
        { flow }
    );
    const m = JSON.parse(r.salida[0].payload);
    assert.equal(m.cpu, 40);
    assert.equal(m.ram, 67); // 2/3
    assert.ok(Number.isInteger(m.cpu) && Number.isInteger(m.ram));
});

test("el guest ya está contado en user: no se suma dos veces", () => {
    const flow = nuevoFlow();
    calcular(recorrer(sistemaDeArchivos("cpu  0 0 0 0 0 0 0 0 0 0\n")).fin, { flow });
    const r = calcular(recorrer(sistemaDeArchivos("cpu  100 0 0 100 0 0 0 0 100 100\n")).fin, { flow });
    assert.equal(JSON.parse(r.salida[0].payload).cpu, 50);
});

// ---------------------------------------------------------------------------------------------
// Estructura de las métricas
// ---------------------------------------------------------------------------------------------

// ---- Lo que falta de forma permanente no llena el log -------------------------------------------------

const ENOENT_ZONAS = { [ZONAS[3]]: null, [ZONAS[4]]: null };
const EIO = (ruta) => ({ code: "EIO", message: "EIO: i/o error, read '" + ruta + "'" });

test("una zona térmica que no existe (ENOENT) se lee una vez, deja un solo aviso y desde el ciclo siguiente ya no se pide", () => {
    const flow = nuevoFlow();
    const archivos = sistemaDeArchivos(STAT_1, ENOENT_ZONAS);
    const primero = recorrer(archivos, flow);
    assert.deepEqual(primero.leidos, ["/proc/stat", "/proc/meminfo", ...ZONAS]);
    assert.equal(primero.avisos.length, 2);
    for (const a of primero.avisos) {
        assert.match(a, /^Zona térmica omitida: .*thermal_zone[34]\/temp/);
    }
    for (let ciclo = 2; ciclo <= 4; ciclo++) {
        const otro = recorrer(archivos, flow);
        assert.deepEqual(otro.leidos, ["/proc/stat", "/proc/meminfo", ZONAS[0], ZONAS[1], ZONAS[2]], "ciclo " + ciclo);
        assert.deepEqual(otro.avisos, [], "ciclo " + ciclo);
        assert.ok(!(ZONAS[3] in otro.fin.leidos));
    }
    // La temperatura sigue saliendo de las zonas que sí existen.
    const m = nuevoFlow();
    calcular(recorrer(archivos, flow).fin, { flow: m });
    const r = calcular(recorrer(sistemaDeArchivos(STAT_2, ENOENT_ZONAS), flow).fin, { flow: m });
    assert.equal(JSON.parse(r.salida[0].payload).temp, 48.4);
});

test("una zona que falla por otra causa se omite tras 3 ciclos seguidos (y 10 minutos); un acierto en medio reinicia la cuenta", () => {
    const flow = nuevoFlow();
    const malo = sistemaDeArchivos(STAT_1, { [ZONAS[1]]: EIO(ZONAS[1]) });
    const bueno = sistemaDeArchivos(STAT_1);
    const lee = (archivos) => recorrer(archivos, flow);
    assert.ok(lee(malo).leidos.includes(ZONAS[1]));
    assert.ok(lee(malo).leidos.includes(ZONAS[1]));
    assert.ok(lee(bueno).leidos.includes(ZONAS[1])); // se leyó bien: la cuenta vuelve a cero
    assert.equal(lee(malo).avisos.length, 0);
    assert.equal(lee(malo).avisos.length, 0);
    const tercera = lee(malo);
    assert.equal(tercera.avisos.length, 1);
    assert.match(tercera.avisos[0], /thermal_zone1\/temp/);
    const despues = lee(bueno);
    assert.ok(!despues.leidos.includes(ZONAS[1]));
    assert.deepEqual(despues.avisos, []);
});

const DIEZ_MIN = 10 * 60 * 1000;
const malaZona1 = () => sistemaDeArchivos(STAT_1, { [ZONAS[1]]: EIO(ZONAS[1]) });

// Deja la zona 1 omitida por tres errores de lectura (EIO) en AHORA.
function omitirZona1(flow) {
    for (let ciclo = 0; ciclo < 3; ciclo++) {
        recorrer(malaZona1(), flow, AHORA);
    }
}

test("una zona omitida por errores (no ENOENT) se vuelve a probar a los 10 minutos, no antes", () => {
    const flow = nuevoFlow();
    omitirZona1(flow);
    for (const despues of [1000, 5 * 60 * 1000, DIEZ_MIN - 1]) {
        assert.ok(!recorrer(malaZona1(), flow, AHORA + despues).leidos.includes(ZONAS[1]), despues + " ms");
    }
    const sondeo = recorrer(malaZona1(), flow, AHORA + DIEZ_MIN);
    assert.ok(sondeo.leidos.includes(ZONAS[1]), "a los 10 minutos se lee una vez");
    assert.deepEqual(sondeo.avisos, [], "el reintento fallido no repite el aviso");
    // Falló otra vez: la ventana de 10 minutos cuenta desde ese reintento.
    assert.ok(!recorrer(malaZona1(), flow, AHORA + DIEZ_MIN + 60000).leidos.includes(ZONAS[1]));
    assert.ok(!recorrer(malaZona1(), flow, AHORA + 2 * DIEZ_MIN - 1).leidos.includes(ZONAS[1]));
    assert.ok(recorrer(malaZona1(), flow, AHORA + 2 * DIEZ_MIN).leidos.includes(ZONAS[1]));
});

test("si al volver a probar la zona se lee bien, vuelve a leerse en cada ciclo", () => {
    const flow = nuevoFlow();
    omitirZona1(flow);
    const bien = sistemaDeArchivos(STAT_1);
    assert.ok(recorrer(bien, flow, AHORA + DIEZ_MIN).leidos.includes(ZONAS[1]));
    for (const despues of [DIEZ_MIN + 10000, DIEZ_MIN + 20000]) {
        assert.ok(recorrer(bien, flow, AHORA + despues).leidos.includes(ZONAS[1]), despues + " ms");
    }
});

test("una zona omitida por errores que al volver a probar da ENOENT queda omitida para siempre", () => {
    const flow = nuevoFlow();
    omitirZona1(flow);
    const ausente = sistemaDeArchivos(STAT_1, { [ZONAS[1]]: null });
    assert.ok(recorrer(ausente, flow, AHORA + DIEZ_MIN).leidos.includes(ZONAS[1]));
    for (const despues of [DIEZ_MIN * 2, DIEZ_MIN * 100, 24 * 3600 * 1000]) {
        assert.ok(!recorrer(ausente, flow, AHORA + DIEZ_MIN + despues).leidos.includes(ZONAS[1]), despues + " ms");
    }
});

test("un ENOENT nunca se vuelve a probar, pasen los minutos que pasen (solo reiniciar Node-RED lo hace)", () => {
    const flow = nuevoFlow();
    const ausente = sistemaDeArchivos(STAT_1, ENOENT_ZONAS);
    recorrer(ausente, flow, AHORA);
    for (const despues of [DIEZ_MIN, 3 * DIEZ_MIN, 7 * 24 * 3600 * 1000]) {
        const r = recorrer(ausente, flow, AHORA + despues);
        assert.ok(!r.leidos.includes(ZONAS[3]) && !r.leidos.includes(ZONAS[4]), despues + " ms");
        assert.deepEqual(r.avisos, []);
    }
});

test("un salto del reloj hacia atrás no deja la zona omitida para siempre: se vuelve a probar", () => {
    const flow = nuevoFlow();
    omitirZona1(flow);
    assert.ok(recorrer(malaZona1(), flow, AHORA - 3600 * 1000).leidos.includes(ZONAS[1]));
});

test("el aviso de una zona omitida por errores dice que se vuelve a probar; el de ENOENT, que solo reiniciar Node-RED la lee", () => {
    const flow = nuevoFlow();
    let avisos = [];
    for (let ciclo = 0; ciclo < 3; ciclo++) {
        avisos = recorrer(malaZona1(), flow, AHORA).avisos;
    }
    assert.equal(avisos.length, 1);
    assert.match(avisos[0], /^Zona térmica omitida: no se puede leer \(.*thermal_zone1\/temp\); se vuelve a probar cada 10 minutos$/);
    const enoent = recorrer(sistemaDeArchivos(STAT_1, ENOENT_ZONAS), nuevoFlow(), AHORA).avisos;
    assert.equal(enoent.length, 2);
    assert.match(enoent[0], /^Zona térmica omitida: no existe \(.*\); no se vuelve a leer hasta reiniciar Node-RED$/);
});

test("/proc/stat y /proc/meminfo no se omiten nunca, por muchos ciclos que fallen", () => {
    const flow = nuevoFlow();
    for (let ciclo = 0; ciclo < 6; ciclo++) {
        const r = recorrer(sistemaDeArchivos(STAT_1, { "/proc/stat": null, "/proc/meminfo": EIO("/proc/meminfo") }), flow);
        assert.ok(r.leidos.includes("/proc/stat") && r.leidos.includes("/proc/meminfo"), "ciclo " + ciclo);
        assert.deepEqual(r.avisos, []);
    }
});

test("si ninguna zona se puede leer, el aviso de la temperatura sale una vez y se rearma cuando vuelve a leerse", () => {
    const flow = nuevoFlow();
    const base = recorrer(sistemaDeArchivos(STAT_1), nuevoFlow()).fin;
    calcular(base, { flow });
    const sinZonas = Object.fromEntries(ZONAS.map((z) => [z, null]));
    const sin = () => calcular(recorrer(sistemaDeArchivos(STAT_2, sinZonas)).fin, { flow });
    assert.match(sin().salida[2].payload, /^Métricas del sistema no publicadas: no se pudo leer la temperatura$/);
    assert.equal(sin().salida[2], null, "el segundo ciclo igual no repite el aviso");
    assert.equal(sin().salida[2], null, "ni el tercero");
    // Cambia lo que falta: avisa de nuevo, con el conjunto nuevo.
    const tambienRam = calcular(recorrer(sistemaDeArchivos(STAT_2, { ...sinZonas, "/proc/meminfo": null })).fin, { flow });
    assert.match(tambienRam.salida[2].payload, /la temperatura, la RAM$/);
    // Todo se lee otra vez: publica, y un fallo posterior vuelve a avisar.
    const bien = calcular(recorrer(sistemaDeArchivos(STAT_2)).fin, { flow });
    assert.ok(bien.salida[0]);
    assert.match(sin().salida[2].payload, /la temperatura$/);
});

test("el aviso de los contadores de la CPU que no avanzan sigue saliendo en cada ciclo (no es un archivo que falte)", () => {
    const flow = nuevoFlow();
    calcular(recorrer(sistemaDeArchivos(STAT_2)).fin, { flow });
    for (let ciclo = 0; ciclo < 3; ciclo++) {
        const r = calcular(recorrer(sistemaDeArchivos(STAT_2)).fin, { flow });
        assert.match(r.salida[2].payload, /contadores de la CPU/);
    }
});

test("el inject dispara cada 10 s, también al arrancar, y arranca el bucle de lectura", () => {
    const i = nodo(ID.inyectar);
    assert.equal(i.type, "inject");
    assert.equal(i.repeat, "10");
    assert.equal(i.once, true);
    assert.deepEqual(i.wires, [[ID.siguiente]]);
});

test("el bucle: función → file in → función, con un catch propio de la lectura que vuelve a la función", () => {
    const siguiente = nodo(ID.siguiente);
    assert.equal(siguiente.outputs, 2);
    assert.deepEqual(siguiente.wires, [[ID.leer], [ID.calcular]]);
    const leer = nodo(ID.leer);
    assert.equal(leer.type, "file in");
    assert.equal(leer.filenameType, "msg");
    assert.equal(leer.filename, "filename");
    assert.equal(leer.format, "utf8");
    assert.equal(leer.sendError, false); // el error va al catch (con sendError true saldría dos veces)
    assert.deepEqual(leer.wires, [[ID.siguiente]]);
    const c = nodo(ID.catchLectura);
    assert.equal(c.type, "catch");
    assert.deepEqual(c.scope, [ID.leer]);
    assert.deepEqual(c.wires, [[ID.siguiente]]);
});

test("solo se leen archivos fijos de /proc y de las zonas térmicas: ningún dato de fuera elige la ruta", () => {
    const { leidos } = recorrer(sistemaDeArchivos(STAT_1));
    for (const ruta of leidos) {
        assert.match(ruta, /^(\/proc\/(stat|meminfo)|\/sys\/class\/thermal\/thermal_zone[0-9]\/temp)$/);
    }
    // Una entrada con una ruta ajena en msg.filename o msg.pendientes no se usa para empezar el ciclo.
    const r = ejecutar(ID.siguiente, { payload: "x", filename: "/etc/passwd", pendientes: undefined });
    assert.equal(r.salida[0].filename, "/proc/stat");
    assert.ok(!JSON.stringify(r.salida[0].pendientes).includes("passwd"));
});

test("aula/sistema/metricas: mqtt out con QoS 0 y sin retain fijados en el nodo (contrato, sección 2)", () => {
    const p = nodo(ID.publicar);
    assert.equal(p.type, "mqtt out");
    assert.equal(p.topic, "aula/sistema/metricas");
    assert.equal(p.qos, "0");
    assert.equal(p.retain, "false");
    assert.equal(p.broker, BROKER);
    assert.deepEqual(nodo(ID.calcular).wires, [[ID.publicar], [ID.limite], [ID.avisos]]);
});

test("la inserción pasa por un delay de límite propio que descarta, y usa el mismo Postgres y una sentencia fija", () => {
    const d = nodo(ID.limite);
    assert.equal(d.type, "delay");
    assert.equal(d.pauseType, "rate");
    assert.equal(d.drop, true);
    assert.equal(d.allowrate, false);
    // Una muestra cada 10 s: el límite deja pasar a lo más una cada 5 s (CWE-770).
    assert.equal(Number(d.rate), 1);
    assert.equal(Number(d.nbRateUnits), 5);
    assert.equal(d.rateUnits, "second");
    assert.deepEqual(d.wires, [[ID.insertar]]);
    const p = nodo(ID.insertar);
    assert.equal(p.type, "postgresql");
    assert.equal(p.postgreSQLConfig, CONFIG_POSTGRES);
    assert.equal(p.query, "INSERT INTO metricas_energia (origen, temp_soc) VALUES ('sistema', $1)");
    assert.deepEqual(p.wires, [[]]);
});

test("la columna temp_soc existe en schema.sql, el origen 'sistema' está documentado y el rol puede insertar", () => {
    const schema = fs.readFileSync(path.join(raiz, "orangepi", "sql", "schema.sql"), "utf8");
    assert.match(schema, /CREATE TABLE metricas_energia \([\s\S]*?temp_soc\s+REAL[\s\S]*?\);/);
    assert.match(schema, /origen\s+TEXT\s+NOT NULL,\s+--[^\n]*\bsistema\b/);
    const roles = fs.readFileSync(path.join(raiz, "orangepi", "sql", "roles.sh"), "utf8");
    assert.match(roles, /GRANT SELECT, INSERT ON metricas_energia TO :"rol";/);
});

test("la rama de las métricas no toca las lecturas de energía ni de la NPU: la API de energía filtra por origen", () => {
    const energia = nodo("a1f0c0de00000023");
    const npu = nodo("a1f0c0de0000002a");
    assert.match(energia.query, /origen = 'ina219'/);
    assert.match(npu.query, /origen = 'npu'/);
});

test("un catch cubre el bucle, el cálculo y la base, y el error registra solo el SQLSTATE (o un mensaje truncado y sin cuentas)", () => {
    const c = nodo(ID.catchMetricas);
    assert.equal(c.type, "catch");
    assert.deepEqual([...c.scope].sort(), [ID.siguiente, ID.calcular, ID.limite, ID.insertar].sort());
    assert.deepEqual(c.wires, [[ID.errorMetricas]]);
    const conCodigo = ejecutar(ID.errorMetricas, { params: [48.6], error: { code: "42501", message: "permission denied for table x" } });
    assert.equal(conCodigo.salida, null);
    assert.deepEqual(conCodigo.avisos, ["Métricas del sistema: falló: SQLSTATE 42501"]);
    const sinCodigo = ejecutar(ID.errorMetricas, { error: { message: "falló con 320068234 " + "x".repeat(300) } });
    assert.equal(sinCodigo.avisos[0].includes("320068234"), false);
    assert.ok(sinCodigo.avisos[0].length < 200);
    assert.deepEqual(ejecutar(ID.errorMetricas, {}).avisos, ["Métricas del sistema: falló: error desconocido"]);
});

// ---------------------------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------------------------

const estadoApi = (flow) => ejecutar(ID.estadoApi, { payload: null }, { flow });

test("sin nada recibido, cada entrada dice explícitamente que nunca llegó: recibido y datos en null", () => {
    const r = estadoApi(nuevoFlow());
    const nunca = { recibido: null, datos: null };
    assert.deepEqual(r.salida.payload, { vision: nunca, camara: nunca, actuadores: nunca, metricas: nunca });
});

test("con estados y métricas recibidos, la respuesta lleva los datos y la hora de recepción en ISO 8601 (UTC)", () => {
    const flow = nuevoFlow();
    guardar(mensaje(TOPICO_VISION, VISION), { flow });
    guardar(mensaje(TOPICO_CAMARA, { en_linea: false }), { flow, ahora: AHORA + 1000 });
    guardar(mensaje(TOPICO_ACTUADOR, ACTUADOR), { flow, ahora: AHORA + 2000 });
    flow.set("metricas_sistema", { datos: { ts: "2026-11-16T15:31:00.000Z", temp: 48.6, cpu: 20, ram: 75 }, recibido_ms: AHORA + 3000 });
    const r = estadoApi(flow);
    assert.deepEqual(r.salida.payload, {
        vision: { recibido: new Date(AHORA).toISOString(), datos: VISION },
        camara: { recibido: new Date(AHORA + 1000).toISOString(), datos: { en_linea: false } },
        actuadores: { recibido: new Date(AHORA + 2000).toISOString(), datos: ACTUADOR },
        metricas: { recibido: new Date(AHORA + 3000).toISOString(), datos: { ts: "2026-11-16T15:31:00.000Z", temp: 48.6, cpu: 20, ram: 75 } }
    });
});

test("la respuesta copia solo los campos del contrato: un campo de más en el contexto no sale", () => {
    const flow = nuevoFlow();
    flow.set("estado_vision", { datos: { ...VISION, secreto: "x", embedding: [1, 2] }, recibido_ms: AHORA });
    flow.set("estado_camara", { datos: { ...CAMARA, token: "x" }, recibido_ms: AHORA });
    flow.set("estado_actuadores", { datos: { ...ACTUADOR, cuenta: "320068234" }, recibido_ms: AHORA });
    flow.set("metricas_sistema", { datos: { ts: "t", temp: 1, cpu: 2, ram: 3, host: "x" }, recibido_ms: AHORA, otro: 1 });
    const texto = JSON.stringify(estadoApi(flow).salida.payload);
    for (const prohibido of ["secreto", "embedding", "token", "320068234", "host", "otro"]) {
        assert.equal(texto.includes(prohibido), false, prohibido);
    }
});

test("los campos que guarda la ingesta son los mismos que copia la API (el último campo nuevo no se queda a medias)", () => {
    const flow = nuevoFlow();
    const completos = [
        [TOPICO_VISION, VISION, "vision"],
        [TOPICO_CAMARA, CAMARA, "camara"],
        [TOPICO_ACTUADOR, ACTUADOR, "actuadores"]
    ];
    for (const [topico, payload] of completos) {
        guardar(mensaje(topico, payload), { flow });
    }
    const r = estadoApi(flow).salida.payload;
    for (const [, payload, clave] of completos) {
        assert.deepEqual(r[clave].datos, payload, clave);
    }
});

test("la ruta GET /api/sistema/estado existe una vez, solo lee del contexto y responde JSON sin caché", () => {
    const entradas = flujos.filter((n) => n.type === "http in" && n.url === "/api/sistema/estado");
    assert.equal(entradas.length, 1);
    assert.equal(entradas[0].id, ID.entradaApi);
    assert.equal(entradas[0].method, "get");
    assert.equal(entradas[0].z, ID.tab);
    assert.deepEqual(entradas[0].wires, [[ID.estadoApi]]);
    const f = nodo(ID.estadoApi);
    assert.deepEqual(f.wires, [[ID.respuestaApi]]);
    assert.doesNotMatch(f.func, /flow\.set|global\.set|context\.set/);
    const resp = nodo(ID.respuestaApi);
    assert.equal(resp.type, "http response");
    assert.equal(resp.headers["content-type"], "application/json");
    assert.equal(resp.headers["cache-control"], "no-store");
});

test("la ruta de la API no llega a ningún mqtt out ni a la base (la ingesta y las métricas van en otras ramas)", () => {
    const porId = new Map(flujos.map((n) => [n.id, n]));
    const vistos = new Set();
    const pendientes = [ID.entradaApi];
    while (pendientes.length > 0) {
        const id = pendientes.pop();
        if (vistos.has(id)) continue;
        vistos.add(id);
        for (const salida of porId.get(id).wires || []) pendientes.push(...salida);
    }
    for (const id of vistos) {
        const tipo = porId.get(id).type;
        assert.ok(!["mqtt out", "postgresql", "exec", "file"].includes(tipo), tipo);
    }
});

test("ningún nodo de la pestaña lleva credenciales ni usuarios de MQTT propios", () => {
    for (const n of flujos.filter((x) => x.z === ID.tab)) {
        if (n.type === "mqtt in" || n.type === "mqtt out") {
            assert.equal(n.broker, BROKER, n.id);
        }
        assert.equal("credentials" in n, false, n.id);
    }
});
