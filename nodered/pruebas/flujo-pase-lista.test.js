// Pruebas de las funciones de la pestaña "Pase de lista" de nodered/flows/flows.json:
//   aula/vision/resultado → validación → una sentencia de Postgres → retroalimentación en
//   aula/actuador/cmd.
// Cada función se extrae del propio flows.json y se ejecuta en un contexto vm. No prueba el nodo de
// Postgres: que la sentencia (CTE con INSERT ... ON CONFLICT) sea válida y haga lo que dice con el
// reloj de la base es de la placa (orangepi/AGENTS.md, Verificación, paso 38), ni el broker. Aquí
// solo se comprueba su forma, sus parámetros y que los privilegios y la ACL alcancen. Las cuentas
// de aquí son entradas de prueba de las funciones, no datos de la lista real.
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
    tab: "a1f0c0de000000b0",
    entrada: "a1f0c0de000000b2",
    validar: "a1f0c0de000000b3",
    limite: "a1f0c0de000000ba",
    consulta: "a1f0c0de000000b4",
    retro: "a1f0c0de000000b5",
    publicar: "a1f0c0de000000b6",
    publicarAsistencia: "a1f0c0de000000bb",
    avisos: "a1f0c0de000000b7",
    catch: "a1f0c0de000000b8",
    registrarError: "a1f0c0de000000b9",
    estadoTrasIniciar: "a1f0c0de00000095",
    estadoTrasTerminar: "a1f0c0de00000097",
    estadoBase: "a1f0c0de0000009e",
    alArrancar: "a1f0c0de00000140",
    borrarMiniaturas: "a1f0c0de00000141"
};
const CUENTA = "320068234";

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

// Contexto `global` de mentira (una copia por JSON, como un contexto que sí persiste).
function nuevoGlobal() {
    const datos = new Map();
    return {
        get: (k) => (datos.has(k) ? JSON.parse(datos.get(k)) : undefined),
        set: (k, v) => {
            if (v === undefined) {
                datos.delete(k);
            } else {
                datos.set(k, JSON.stringify(v));
            }
        },
        claves: () => [...datos.keys()]
    };
}

// Ejecuta la función del nodo `id`. Devuelve { salida, avisos, logs, global }; la salida se copia por JSON
// (los objetos del contexto vm son de otro realm). Una excepción se propaga. `ahora` (ms) fija
// Date.now() para probar la ventana de `ts`. `global` es el contexto global que ve la función y `context`, el del propio nodo.
function ejecutar(id, msg, { ahora, global = nuevoGlobal(), context = nuevoGlobal(), flow = nuevoGlobal() } = {}) {
    const n = nodo(id);
    assert.equal(n.type, "function");
    const avisos = [];
    const logs = [];
    const contexto = vm.createContext({ Buffer, Date: ahora === undefined ? Date : relojFijo(ahora), Math, Number, Object, console });
    const funcion = new vm.Script("(function (msg, node, global, context, flow) {" + n.func + "\n})").runInContext(contexto);
    const nodoFalso = { warn: (t) => avisos.push(String(t)), log: (t) => logs.push(String(t)), error() {} };
    const salida = funcion(msg, nodoFalso, global, context, flow);
    return { salida: salida === undefined || salida === null ? salida : JSON.parse(JSON.stringify(salida)), avisos, logs, global, context };
}

const AHORA = Date.parse("2026-11-05T15:30:12.000Z");
const LAT = { cap: 180, det: 12, emb: 6, total: 240 };

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

function assertDescarte(r, fragmento) {
    assert.equal(r.salida[0], null, "no debe llegar a la base");
    assert.ok(r.salida[1], "debe salir por los avisos");
    assert.match(r.salida[1].payload, /^Descartado, /);
    if (fragmento) {
        assert.match(r.salida[1].payload, fragmento);
    }
}

// ---------------------------------------------------------------------------------------------
// Validar resultado de visión
// ---------------------------------------------------------------------------------------------

test("un reconocido válido sale por la salida 1 con [decision, cuenta, conf] y sin el mensaje original", () => {
    const r = validar(mensaje(resultado()));
    assert.deepEqual(r.salida[0].params, ["reconocido", CUENTA, 0.78]);
    assert.equal(r.salida[0].fuente, "pase");
    assert.equal(r.salida[0].decision, "reconocido");
    assert.equal(r.salida[0].topic, undefined);
    assert.equal(r.salida[0].retain, undefined);
    assert.equal(r.salida[1], null);
});

test("dudoso, desconocido e imagen_insuficiente van a la base, con o sin candidato", () => {
    for (const decision of ["dudoso", "desconocido"]) {
        const r = validar(mensaje(resultado({ decision })));
        assert.deepEqual(r.salida[0].params, [decision, CUENTA, 0.78]);
    }
    const sinCandidato = validar(mensaje(resultado({ decision: "desconocido", cuenta: null, conf: null, margen: null })));
    assert.deepEqual(sinCandidato.salida[0].params, ["desconocido", null, null]);
    const insuficiente = validar(mensaje(resultado({ decision: "imagen_insuficiente", cuenta: null, conf: null, motivo: "imagen_insuficiente" })));
    assert.deepEqual(insuficiente.salida[0].params, ["imagen_insuficiente", null, null]);
});

test("margen puede faltar y los campos de más se ignoran", () => {
    const m = resultado({ campo_nuevo: { a: 1 } });
    delete m.margen;
    assert.deepEqual(validar(mensaje(m)).salida[0].params, ["reconocido", CUENTA, 0.78]);
});

test("conf acepta el rango de la similitud coseno, de -1 a 1", () => {
    for (const conf of [-1, -0.05, 0, 1]) {
        assert.deepEqual(validar(mensaje(resultado({ decision: "desconocido", conf }))).salida[0].params, ["desconocido", CUENTA, conf]);
    }
});

test("sin_rostro y error no van a la base ni mandan retroalimentación: solo dejan un aviso sin contenido", () => {
    for (const decision of ["sin_rostro", "error"]) {
        const r = validar(mensaje(resultado({ decision, cuenta: null, conf: null })));
        assert.equal(r.salida[0], null);
        assert.equal(r.salida[1].payload, "Sin retroalimentación: la visión informó " + decision);
    }
});

test("modo prueba no llega a la base y deja un aviso neutro (no es un descarte: va a la prueba de reconocimiento), sea cual sea la decisión", () => {
    for (const decision of ["reconocido", "dudoso", "desconocido", "sin_rostro"]) {
        const r = validar(mensaje(resultado({ modo: "prueba", decision })));
        assert.equal(r.salida[0], null, "no debe llegar a la base");
        assert.equal(r.salida[1].payload, "Resultado de prueba: no marca asistencia (va a la prueba de reconocimiento)");
    }
});

test("modo desconocido, ausente o de otro tipo se descarta", () => {
    for (const modo of ["Pase", "registro", "", null, 1, undefined]) {
        assertDescarte(validar(mensaje(resultado({ modo }))), /modo debe ser pase o prueba/);
    }
});

test("un retenido se descarta con aviso, aunque sea válido", () => {
    const r = validar(mensaje(resultado(), { retain: true }));
    assertDescarte(r, /mensaje retenido en aula\/vision\/resultado/);
    assert.equal(r.avisos.length, 1);
    assert.match(r.avisos[0], /Descartado mensaje retenido en aula\/vision\/resultado/);
    assert.doesNotMatch(r.avisos[0], new RegExp(CUENTA));
});

test("un payload vacío se descarta en silencio", () => {
    for (const payload of ["", Buffer.alloc(0)]) {
        const r = validar({ topic: "aula/vision/resultado", payload, retain: false });
        assert.equal(r.salida, null);
        assert.deepEqual(r.avisos, []);
    }
});

test("lo que no es texto, es demasiado largo o no es un objeto JSON se descarta", () => {
    assertDescarte(validar({ topic: "t", payload: 12, retain: false }), /no es texto/);
    assertDescarte(validar({ topic: "t", payload: Buffer.from("{}"), retain: false }), /no es texto/);
    assertDescarte(validar(mensaje("x".repeat(16385))), /payload de 16385 caracteres \(máximo 16384\)/);
    assertDescarte(validar(mensaje("no es json")), /no es JSON/);
    assertDescarte(validar(mensaje('{"modo":"pase"')), /no es JSON/);
    for (const texto of ["null", "12", '"texto"', "[]", "[1]", "true"]) {
        assertDescarte(validar(mensaje(texto)), /no es un objeto JSON/);
    }
});

test("decision desconocida o de otro tipo se descarta", () => {
    for (const decision of ["Reconocido", "ok", "", null, 1, undefined, ["reconocido"]]) {
        assertDescarte(validar(mensaje(resultado({ decision }))), /decision desconocida/);
    }
});

test("cuenta debe ser null o 9 dígitos, y siempre debe venir", () => {
    for (const cuenta of ["32006823", "3200682345", "32006823a", " 320068234", 320068234, "", {}, []]) {
        assertDescarte(validar(mensaje(resultado({ cuenta }))), /cuenta debe ser null o un texto de 9 dígitos/);
    }
    const sin = resultado();
    delete sin.cuenta;
    assertDescarte(validar(mensaje(sin)), /cuenta debe ser null/);
});

test("conf debe ser null o un número finito de -1 a 1, y siempre debe venir", () => {
    for (const conf of ["0.8", 1.01, -1.01, [0.8], {}, true]) {
        assertDescarte(validar(mensaje(resultado({ decision: "desconocido", conf }))), /conf debe ser null o un número de -1 a 1/);
    }
    // JSON.stringify escribe Infinity como null: el desbordamiento se manda como texto JSON.
    const desbordado = JSON.stringify(resultado({ decision: "desconocido", conf: 0 })).replace('"conf":0', '"conf":1e400');
    assertDescarte(validar(mensaje(desbordado)), /conf debe ser null o un número de -1 a 1/);
    const sin = resultado({ decision: "desconocido" });
    delete sin.conf;
    assertDescarte(validar(mensaje(sin)), /conf debe ser null/);
});

test("margen, si viene, debe ser null o un número", () => {
    for (const margen of ["0.2", [0.2], {}, true]) {
        assertDescarte(validar(mensaje(resultado({ margen }))), /margen debe ser null o un número/);
    }
    assert.ok(validar(mensaje(resultado({ margen: null }))).salida[0]);
});

test("lat_ms debe ser un objeto", () => {
    for (const lat_ms of [null, 240, "240", [], undefined]) {
        assertDescarte(validar(mensaje(resultado({ lat_ms }))), /lat_ms debe ser un objeto/);
    }
});

test("un reconocido sin cuenta o sin conf se descarta", () => {
    assertDescarte(validar(mensaje(resultado({ cuenta: null }))), /un reconocido exige cuenta y conf/);
    assertDescarte(validar(mensaje(resultado({ conf: null }))), /un reconocido exige cuenta y conf/);
});

test("ts es obligatorio y debe ser ISO 8601 con zona", () => {
    const sin = resultado();
    delete sin.ts;
    assertDescarte(validar(mensaje(sin)), /falta ts o no es ISO 8601 con zona/);
    for (const ts of [null, 1793892612000, "", "ayer", "2026-11-05T15:30:12", "2026-11-05 15:30:12Z", "2026-11-05T15:30:12+0000", "2026-11-05"]) {
        assertDescarte(validar(mensaje(resultado({ ts }))), /falta ts o no es ISO 8601 con zona/);
    }
    assertDescarte(validar(mensaje(resultado({ ts: "2026-13-45T25:61:61Z" }))), /ts no se puede leer/);
});

test("ts acepta Z, desfase y fracciones de segundo", () => {
    for (const ts of ["2026-11-05T15:30:12Z", "2026-11-05T15:30:12.5Z", "2026-11-05T09:30:12.123456-06:00", "2026-11-05T16:30:12+01:00"]) {
        assert.ok(validar(mensaje(resultado({ ts }))).salida[0], ts);
    }
});

test("un ts a más de 60 s del reloj de Node-RED se descarta, hacia atrás o hacia adelante", () => {
    const en = (ms) => new Date(AHORA + ms).toISOString();
    for (const ms of [-60000, -30000, 0, 30000, 60000]) {
        assert.ok(validar(mensaje(resultado({ ts: en(ms) }))).salida[0], "a " + ms + " ms debe pasar");
    }
    for (const ms of [-60001, -3600000, 60001, 3600000]) {
        assertDescarte(validar(mensaje(resultado({ ts: en(ms) }))), /ts a más de 60 s del reloj de Node-RED/);
    }
});

test("el 1970 de reserva de la visión se descarta", () => {
    assertDescarte(validar(mensaje(resultado({ ts: "1970-01-01T00:00:00.000+00:00" }))), /ts a más de 60 s/);
});

test("un descarte nunca repite el contenido del mensaje (ni la cuenta)", () => {
    const secreto = "SECRETO-" + CUENTA;
    const casos = [
        mensaje(resultado({ ts: "1970-01-01T00:00:00.000+00:00" })),
        mensaje(resultado({ modo: "prueba" })),
        mensaje(resultado({ cuenta: secreto })),
        mensaje(resultado({ conf: secreto })),
        mensaje(resultado({ decision: secreto })),
        mensaje(resultado({ modo: secreto })),
        mensaje(resultado({ lat_ms: secreto })),
        mensaje(resultado({ ts: secreto })),
        mensaje(resultado({ margen: secreto })),
        mensaje(resultado(), { retain: true }),
        mensaje("x".repeat(3000) + CUENTA),
        mensaje('{"cuenta":"' + CUENTA + '"'),
        mensaje("[" + CUENTA + "]")
    ];
    for (const m of casos) {
        const r = validar(m);
        assert.equal(r.salida[0], null);
        // La salida 4 (solo modo prueba) lleva la cuenta a propósito, hacia la pestaña Registro: flujo-prueba.test.js.
        const todo = JSON.stringify(r.salida.slice(0, 3)) + r.avisos.join("|") + r.logs.join("|");
        assert.doesNotMatch(todo, new RegExp(CUENTA), "el descarte no debe llevar la cuenta");
        assert.doesNotMatch(todo, /SECRETO/);
    }
});

test("sin_rostro y error con la cuenta o la confianza que sea no filtran nada en el aviso", () => {
    const r = validar(mensaje(resultado({ decision: "error", cuenta: CUENTA })));
    assert.doesNotMatch(JSON.stringify(r.salida) + r.avisos.join("") + r.logs.join(""), new RegExp(CUENTA));
});

// ---------------------------------------------------------------------------------------------
// Retroalimentación del pase
// ---------------------------------------------------------------------------------------------

const RESUMEN = { sesion_id: 23, presentes: 1, retardos: 0, faltantes: 9, ultimo_nombre: "Ana López" };
const filaResultado = (resultado, extra = {}) => (
    resultado === "presente" || resultado === "retardo"
        ? { resultado, ...RESUMEN, ...extra }
        : { resultado, ...extra }
);
const retro = (payload, extra = {}) => ejecutar(ID.retro, { payload, decision: "reconocido", params: ["reconocido", CUENTA, 0.8], ...extra });

test("la retroalimentación sigue la tabla del contrato para cada resultado de la base y solo presente/retardo publican asistencia", () => {
    const esperado = {
        presente: { servo: "abrir", led: "verde", buzzer: 1 },
        retardo: { servo: "abrir", led: "verde", buzzer: 1 },
        duplicado: { led: "rojo", buzzer: 2 },
        fuera_de_grupo: { led: "rojo", buzzer: 2 },
        desconocido: { led: "rojo", buzzer: 2 },
        imagen_insuficiente: { led: "ambar" },
        dudoso: { led: "ambar" }
    };
    for (const [resultado, orden] of Object.entries(esperado)) {
        const r = retro([filaResultado(resultado)]);
        assert.deepEqual(JSON.parse(r.salida[0].payload), orden, resultado);
        assert.equal(r.salida[1], null);
        if (resultado === "presente" || resultado === "retardo") {
            assert.deepEqual(JSON.parse(r.salida[2].payload), {
                activa: true, id: 23, presentes: 1, retardos: 0, faltantes: 9,
                ultimo: { nombre: "Ana López", estado: resultado }
            });
        } else {
            assert.equal(r.salida[2], null, resultado);
        }
    }
});

test("las salidas salen limpias: solo el payload, sin la cuenta ni los parámetros del mensaje original", () => {
    const r = retro([filaResultado("presente")], { topic: "aula/vision/resultado", retain: true });
    assert.deepEqual(Object.keys(r.salida[0]), ["payload"]);
    assert.deepEqual(Object.keys(r.salida[2]), ["payload"]);
    assert.doesNotMatch(r.salida[0].payload + r.salida[2].payload, new RegExp(CUENTA));
});

test("sin sesión abierta (cero filas) no se manda retroalimentación ni asistencia y queda un aviso sin contenido", () => {
    for (const decision of ["reconocido", "dudoso", "desconocido", "imagen_insuficiente"]) {
        const r = retro([], { decision });
        assert.equal(r.salida[0], null);
        assert.equal(r.salida[2], null);
        assert.equal(r.salida[1].payload, "Sin sesión abierta: no se registra ni se manda retroalimentación (" + decision + ")");
        assert.doesNotMatch(r.salida[1].payload, new RegExp(CUENTA));
    }
});

test("el log de la retroalimentación lleva el resultado y nunca la cuenta ni el nombre", () => {
    const r = retro([filaResultado("presente")]);
    assert.deepEqual(r.logs, ["Pase de lista: presente"]);
});

test("una respuesta imposible de la base lanza y no publica nada", () => {
    for (const payload of [
        undefined, null, "x", {}, [filaResultado("presente"), filaResultado("presente")], [{}], [{ resultado: "otro" }],
        [{ resultado: 1 }], [{ resultado: "toString" }], [{ resultado: "__proto__" }], [{ resultado: "constructor" }],
        [{ resultado: "presente" }], [filaResultado("presente", { presentes: -1 })], [filaResultado("retardo", { ultimo_nombre: "" })]
    ]) {
        assert.throws(() => retro(payload), undefined, JSON.stringify(payload));
    }
});

// ---------------------------------------------------------------------------------------------
// Registrar error del pase
// ---------------------------------------------------------------------------------------------

test("el error del pase registra solo el SQLSTATE y nunca la cuenta ni los parámetros", () => {
    const r = ejecutar(ID.registrarError, {
        decision: "reconocido",
        params: ["reconocido", CUENTA, 0.8],
        payload: null,
        error: { message: "duplicate key value (" + CUENTA + ")", code: "23505", detail: "Key (no_cuenta)=(" + CUENTA + ")" }
    });
    assert.equal(r.salida, null);
    assert.deepEqual(r.avisos, ["Pase de lista: falló (reconocido): SQLSTATE 23505"]);
});

test("sin SQLSTATE, el mensaje se trunca y se enmascaran las secuencias de 9 dígitos", () => {
    const r = ejecutar(ID.registrarError, { decision: "desconocido", error: { message: "falló con " + CUENTA + " " + "x".repeat(300) } });
    assert.equal(r.avisos.length, 1);
    assert.doesNotMatch(r.avisos[0], new RegExp(CUENTA));
    assert.match(r.avisos[0], /\*\*\*\*\*\*\*\*\*/);
    assert.ok(r.avisos[0].length < 200);
    assert.deepEqual(ejecutar(ID.registrarError, {}).avisos, ["Pase de lista: falló: error desconocido"]);
});

// ---------------------------------------------------------------------------------------------
// Estructura de la pestaña
// ---------------------------------------------------------------------------------------------

const enPestana = flujos.filter((n) => n.z === ID.tab);

test("la pestaña existe y todos los cables y alcances apuntan a nodos de la pestaña", () => {
    const tab = nodo(ID.tab);
    assert.equal(tab.type, "tab");
    assert.equal(tab.label, "Pase de lista");
    const ids = new Set(enPestana.map((n) => n.id));
    for (const n of enPestana) {
        for (const salida of n.wires || []) {
            for (const destino of salida) {
                assert.ok(ids.has(destino), n.id + " cablea a " + destino);
            }
        }
        for (const objetivo of n.scope || []) {
            assert.ok(ids.has(objetivo), n.id + " tiene en su alcance a " + objetivo);
        }
    }
});

test("el único mqtt in es aula/vision/resultado con QoS 1, del broker Mosquitto, y alimenta solo a la validación", () => {
    const entradas = enPestana.filter((n) => n.type === "mqtt in");
    assert.equal(entradas.length, 1);
    const m = entradas[0];
    assert.equal(m.id, ID.entrada);
    assert.equal(m.topic, "aula/vision/resultado");
    assert.equal(m.qos, "1");
    assert.equal(m.broker, "a1f0c0de00000002");
    assert.deepEqual(m.wires, [[ID.validar]]);
});

test("los mqtt out publican solo en aula/actuador/cmd (sin retain) y aula/sesion/asistencia (con retain), con QoS 1, y solo los alimenta la retroalimentación", () => {
    const salidas = enPestana.filter((n) => n.type === "mqtt out");
    assert.equal(salidas.length, 2);
    const o = nodo(ID.publicar);
    assert.equal(o.topic, "aula/actuador/cmd");
    assert.equal(o.qos, "1");
    assert.equal(o.retain, "false");
    assert.equal(o.broker, "a1f0c0de00000002");
    const a = nodo(ID.publicarAsistencia);
    assert.equal(a.topic, "aula/sesion/asistencia");
    assert.equal(a.qos, "1");
    assert.equal(a.retain, "true");
    assert.equal(a.broker, "a1f0c0de00000002");
    for (const id of [ID.publicar, ID.publicarAsistencia]) {
        const alimentan = flujos.filter((n) => (n.wires || []).some((salida) => salida.includes(id)));
        assert.deepEqual(alimentan.map((n) => n.id), [ID.retro]);
    }
    assert.deepEqual(nodo(ID.retro).wires, [[ID.publicar], [ID.avisos], [ID.publicarAsistencia]]);
});

test("la cadena es entrada → validar → límite → sentencia → retroalimentación, y los avisos van al debug de consola", () => {
    // La tercera salida de la validación (la latencia de la NPU) tiene su propia cadena: flujo-latencia.test.js.
    assert.deepEqual(nodo(ID.validar).wires.slice(0, 2), [[ID.limite], [ID.avisos]]);
    assert.deepEqual(nodo(ID.limite).wires, [[ID.consulta]]);
    assert.deepEqual(nodo(ID.consulta).wires, [[ID.retro]]);
    const d = nodo(ID.avisos);
    assert.equal(d.type, "debug");
    assert.equal(d.console, true);
    assert.equal(d.complete, "payload");
});

test("el límite de entrada es un delay de 5 mensajes por segundo (200 ms entre mensajes) que descarta los intermedios, y solo él alimenta la sentencia", () => {
    const d = nodo(ID.limite);
    assert.equal(d.type, "delay");
    assert.equal(d.z, ID.tab);
    assert.equal(d.pauseType, "rate");
    assert.equal(d.rate, "5", "200 ms entre mensajes: un reconocido legítimo tras un dudoso (unos 240 ms por ciclo) no se pierde");
    assert.match(d.name, /5 por segundo, 200 ms/);
    assert.equal(d.nbRateUnits, "1");
    assert.equal(d.rateUnits, "second");
    assert.equal(d.drop, true, "descarta los intermedios: no encola (CWE-770)");
    assert.equal(d.allowrate, false, "un mensaje no puede cambiar el límite");
    const alimentan = flujos.filter((n) => (n.wires || []).some((salida) => salida.includes(ID.consulta)));
    assert.deepEqual(alimentan.map((n) => n.id), [ID.limite]);
    const alimentanLimite = flujos.filter((n) => (n.wires || []).some((salida) => salida.includes(ID.limite)));
    assert.deepEqual(alimentanLimite.map((n) => n.id), [ID.validar]);
});

test("el catch cubre las funciones y la consulta de la cadena (menos el propio registro)", () => {
    const c = nodo(ID.catch);
    assert.equal(c.type, "catch");
    assert.equal(c.uncaught, false);
    assert.deepEqual([...c.scope].sort(), [ID.validar, ID.consulta, ID.retro].sort());
    assert.deepEqual(c.wires, [[ID.registrarError]]);
});

// ---------------------------------------------------------------------------------------------
// La sentencia de Postgres, los privilegios y la ACL
// ---------------------------------------------------------------------------------------------

const consulta = nodo(ID.consulta).query;

test("la sentencia es fija, parametrizada ($1 a $3), con el rol de Node-RED y sin DELETE ni DDL", () => {
    const n = nodo(ID.consulta);
    assert.equal(n.type, "postgresql");
    assert.equal(n.postgreSQLConfig, "a1f0c0de00000003");
    assert.doesNotMatch(consulta, /\{\{/);
    assert.doesNotMatch(consulta, /\bDELETE\b|\bTRUNCATE\b|\bDROP\b|\bALTER\b|\bUPDATE\b/i);
    assert.match(consulta, /\$1::text/);
    assert.match(consulta, /\$2::text/);
    assert.match(consulta, /\$3::real/);
    assert.doesNotMatch(consulta, /\$4/);
    // Solo comprueba que haya rostro: nunca lee el vector (un embedding no sale de la base).
    const usos = consulta.match(/embedding/g) || [];
    assert.deepEqual(usos, ["embedding"], "solo un uso de la columna");
    assert.match(consulta, /a\.embedding IS NOT NULL/);
});

test("la sentencia toma la sesión abierta de la base y valida el grupo", () => {
    assert.match(consulta, /FROM sesiones WHERE fin IS NULL/);
    assert.match(consulta, /FROM alumnos a JOIN s ON a\.grupo = s\.grupo/);
    assert.match(consulta, /a\.no_cuenta = \$2::text/);
    // Rostro y consentimiento vigentes: sin ellos (retirados, o nunca registrados) cae en fuera_de_grupo.
    assert.match(consulta, /AND a\.consentimiento AND a\.embedding IS NOT NULL/);
    assert.match(consulta, /\$1::text = 'reconocido'/);
});

test("la asistencia se inserta con ON CONFLICT (no_cuenta, sesion_id) DO NOTHING, metodo rostro y el reloj de la base", () => {
    assert.match(consulta, /INSERT INTO asistencias \(no_cuenta, sesion_id, estado, confianza, metodo\)/);
    assert.match(consulta, /ON CONFLICT \(no_cuenta, sesion_id\) DO NOTHING/);
    assert.match(consulta, /\$3::real, 'rostro'/);
    assert.match(consulta, /CASE WHEN now\(\) <= inicio \+ tolerancia_min \* interval '1 minute' THEN 'presente' ELSE 'retardo' END/);
    assert.doesNotMatch(consulta, /'manual'/);
});

test("los motivos de intentos son solo los que admite el CHECK de schema.sql", () => {
    const schema = fs.readFileSync(path.join(raiz, "orangepi", "sql", "schema.sql"), "utf8");
    const check = /motivo\s+TEXT CHECK \(motivo IN \(([^)]*)\)\)/.exec(schema);
    assert.ok(check, "no se encontró el CHECK de intentos.motivo");
    const admitidos = [...check[1].matchAll(/'([a-z_]+)'/g)].map((x) => x[1]);
    assert.deepEqual(admitidos.sort(), ["desconocido", "duplicado", "imagen_insuficiente"]);
    // Lo que la sentencia escribe en `motivo`: el CASE de `intento` y el filtro WHERE sobre `resultado`.
    const insercion = /INSERT INTO intentos[\s\S]*?RETURNING 1/.exec(consulta);
    assert.ok(insercion, "no se encontró el INSERT de intentos");
    const caso = /CASE resultado WHEN 'fuera_de_grupo' THEN '([a-z_]+)' ELSE resultado END/.exec(insercion[0]);
    assert.ok(caso, "el CASE de motivo cambió");
    assert.ok(admitidos.includes(caso[1]));
    const filtro = /WHERE resultado IN \(([^)]*)\)/.exec(insercion[0]);
    assert.ok(filtro);
    const resultados = [...filtro[1].matchAll(/'([a-z_]+)'/g)].map((x) => x[1]);
    for (const r of resultados) {
        assert.ok(admitidos.includes(r) || r === "fuera_de_grupo", r + " no es un motivo admitido");
    }
    assert.ok(!resultados.includes("dudoso"), "dudoso no se guarda");
});

test("cada resultado que la sentencia puede devolver tiene una retroalimentación", () => {
    // La sentencia devuelve la decisión (si no es reconocido) o uno de estos del CASE de `res`.
    const delCase = /CASE WHEN \$1::text <> 'reconocido'[\s\S]*?END AS resultado/.exec(consulta);
    assert.ok(delCase, "el CASE de `res` cambió");
    const literales = [...delCase[0].matchAll(/THEN '([a-z_]+)'|ELSE '([a-z_]+)'/g)].map((m) => m[1] || m[2]);
    const resultados = new Set([...literales, "presente", "retardo", "desconocido", "imagen_insuficiente", "dudoso"]);
    assert.deepEqual([...resultados].sort(), ["dudoso", "duplicado", "desconocido", "fuera_de_grupo", "imagen_insuficiente", "presente", "retardo"].sort());
    for (const resultado of resultados) {
        assert.ok(ejecutar(ID.retro, { payload: [filaResultado(resultado)], decision: "x" }).salida[0], resultado);
    }
});

test("el rol de Node-RED tiene los privilegios que la sentencia usa y ninguno de más", () => {
    const roles = fs.readFileSync(path.join(raiz, "orangepi", "sql", "roles.sh"), "utf8");
    for (const tabla of ["asistencias", "intentos", "alumnos", "sesiones"]) {
        assert.match(roles, new RegExp("GRANT SELECT, INSERT ON " + tabla + " TO :\"rol\";"), tabla);
    }
    assert.match(roles, /GRANT USAGE ON SEQUENCE asistencias_id_seq TO :"rol";/);
    assert.match(roles, /GRANT USAGE ON SEQUENCE intentos_id_seq TO :"rol";/);
    assert.doesNotMatch(roles, /GRANT[^;]*\b(UPDATE|DELETE)\b[^;]*ON (asistencias|intentos)/);
    assert.doesNotMatch(roles, /GRANT UPDATE[^;]*ON (asistencias|intentos)/);
});

test("la ACL deja a vision publicar el resultado, a nodered publicar todo y a pantalla leer aula/sesion/asistencia", () => {
    const acl = fs.readFileSync(path.join(raiz, "orangepi", "mosquitto", "acl"), "utf8");
    const bloque = (usuario) => {
        const inicio = acl.indexOf("\nuser " + usuario + "\n");
        assert.ok(inicio >= 0, usuario);
        const resto = acl.slice(inicio + 1);
        const siguiente = resto.indexOf("\nuser ", 1);
        return siguiente < 0 ? resto : resto.slice(0, siguiente);
    };
    assert.match(bloque("vision"), /^topic write aula\/vision\/resultado$/m);
    assert.match(bloque("nodered"), /^topic readwrite aula\/#$/m);
    assert.match(bloque("pantalla"), /^topic read aula\/sesion\/asistencia$/m);
});

test("ningún otro nodo de la hoja de flujos se suscribe a aula/vision/resultado", () => {
    const suscritos = flujos.filter((n) => n.type === "mqtt in" && n.topic === "aula/vision/resultado");
    assert.deepEqual(suscritos.map((n) => n.id), [ID.entrada]);
});

test("la limpieza de retenidos borra también aula/vision/resultado, y su mensaje vacío en vivo se descarta en silencio", () => {
    const r = ejecutar("a1f0c0de00000033", { status: { fill: "green", shape: "dot" } });
    const topicos = r.salida[0].map((m) => m.topic);
    assert.deepEqual(topicos, ["aula/actuador/cmd", "aula/sesion/cmd", "aula/vision/cmd", "aula/camara/pir", "aula/camara/energia", "aula/vision/resultado"]);
    for (const m of r.salida[0]) {
        assert.equal(m.payload, "");
        assert.equal(m.retain, true);
    }
    // Esa limpieza provoca un mensaje vacío en vivo, sin retain, que la validación descarta sin aviso.
    const vacio = validar({ topic: "aula/vision/resultado", payload: "", retain: false });
    assert.equal(vacio.salida, null);
    assert.deepEqual(vacio.avisos, []);
});

// ---------------------------------------------------------------------------------------------
// Miniatura del rostro del pase de lista (contrato MQTT v0.17): efímera, solo en memoria
// ---------------------------------------------------------------------------------------------

// Un JPEG de mentira en base64 (la función solo comprueba la forma y el tamaño).
const FOTO = "/9j/4AAQSkZJRgABAQEASABIAAD/" + "QUJD".repeat(500) + "RA==";
const OTRA_FOTO = "/9j/4AAQSkZJRgAA" + "WFZH".repeat(300) + "AA==";
const sinFoto = (r) => !JSON.stringify(r).includes("QUJD");

test("la miniatura de un reconocido de modo pase viaja en el mensaje de la salida 1 y en nada más", () => {
    const r = validar(mensaje(resultado({ miniatura: FOTO })));
    assert.equal(r.salida[0].miniatura, FOTO);
    assert.deepEqual(r.salida[0].params, ["reconocido", CUENTA, 0.78], "los parámetros de la sentencia no cambian");
    assert.equal(r.salida[1], null);
    assert.deepEqual(r.avisos, []);
    // Ni la latencia ni los avisos la llevan.
    assert.ok(sinFoto(r.salida.slice(1)), "solo la salida 1 lleva la miniatura");
});

test("un mensaje real, de unos 11 KB con la miniatura, y uno con la miniatura en su tope pasan el límite del payload", () => {
    for (const largo of [10924, 12288]) {
        const foto = "/9j/" + "A".repeat(largo - 4);
        const r = validar(mensaje(resultado({ miniatura: foto })));
        assert.equal(r.salida[0].miniatura, foto, String(largo));
        assert.deepEqual(r.salida[0].params, ["reconocido", CUENTA, 0.78]);
        assert.ok(JSON.stringify(resultado({ miniatura: foto })).length < 16384, "el mensaje cabe en MAX_CARACTERES con margen");
    }
});

test("la miniatura debe ser un JPEG en base64 con relleno: lo demás se ignora y el resultado sigue", () => {
    for (const miniatura of ["iVBORw0KGgo=", "/9j/QUJ", "/9j", "QUJD", "AAAA".repeat(10)]) {
        const r = validar(mensaje(resultado({ miniatura })));
        assert.deepEqual(r.salida[0].params, ["reconocido", CUENTA, 0.78], miniatura);
        assert.equal(r.salida[0].miniatura, undefined, miniatura);
        assert.match(r.avisos[0], /no es el base64 de un JPEG/);
    }
    assert.equal(validar(mensaje(resultado({ miniatura: "/9j/QUJD" }))).salida[0].miniatura, "/9j/QUJD");
});

test("los avisos de miniatura inválida salen a lo más uno cada 10 s y cuentan los suprimidos", () => {
    const context = nuevoGlobal();
    const mala = mensaje(resultado({ miniatura: "no es base64!" }));
    const en = (ms) => ejecutar(ID.validar, mala, { ahora: AHORA + ms, context });
    const primero = en(0);
    assert.equal(primero.avisos.length, 1);
    for (const ms of [1000, 2000, 9999]) {
        const r = en(ms);
        assert.deepEqual(r.avisos, [], "dentro de los 10 s no avisa");
        assert.deepEqual(r.salida[0].params, ["reconocido", CUENTA, 0.78], "el resultado sigue aunque no avise");
    }
    // A los 10 s el aviso vuelve, con la cuenta de los suprimidos y sin contenido.
    const tarde = en(10000);
    assert.equal(tarde.avisos.length, 1);
    assert.match(tarde.avisos[0], /\(3 avisos de miniatura suprimidos\)/);
    assert.doesNotMatch(tarde.avisos[0], /no es base64!/);
    // El contador empieza de cero.
    assert.deepEqual(en(10500).avisos, []);
    // Un reloj que salta hacia atrás no bloquea el aviso.
    assert.equal(en(5000).avisos.length, 1);
});

test("la miniatura se ignora, sin copiarla, si el resultado no es un reconocido del pase", () => {
    for (const extra of [{ decision: "dudoso" }, { decision: "desconocido" }, { decision: "imagen_insuficiente", cuenta: null, conf: null }]) {
        const r = validar(mensaje(resultado({ ...extra, miniatura: FOTO })));
        assert.equal(r.salida[0].miniatura, undefined, JSON.stringify(extra));
        assert.ok(sinFoto(r.salida), "no sale por ninguna salida");
        assert.deepEqual(r.avisos, [], "ignorarla aquí no es un aviso");
    }
    for (const decision of ["sin_rostro", "error"]) {
        const r = validar(mensaje(resultado({ decision, cuenta: null, conf: null, miniatura: FOTO })));
        assert.ok(sinFoto(r.salida), decision);
    }
    // El modo prueba va a la pestaña Registro: su resultado nunca lleva la foto.
    const prueba = validar(mensaje(resultado({ modo: "prueba", miniatura: FOTO })));
    assert.equal(prueba.salida[0], null);
    assert.ok(sinFoto(prueba.salida), "la prueba no la lleva");
});

test("una miniatura inválida se ignora con un aviso sin contenido: el resultado sigue su curso", () => {
    const casos = [
        [7, /no es un texto/],
        [["QUJD"], /no es un texto/],
        ["QUJD".repeat(3100), /caracteres \(máximo 12288\)/],
        ["no es base64!", /no es el base64 de un JPEG/],
        ["QUJD\nQUJD", /no es el base64 de un JPEG/],
        ["data:image/jpeg;base64,QUJD", /no es el base64 de un JPEG/],
        ["=QUJD", /no es el base64 de un JPEG/],
        ["", /no es el base64 de un JPEG/],
        ["iVBORw0KGgo=", /no es el base64 de un JPEG/],
        ["/9j/QUJ", /no es el base64 de un JPEG/]
    ];
    for (const [miniatura, aviso] of casos) {
        const r = validar(mensaje(resultado({ miniatura })));
        assert.deepEqual(r.salida[0].params, ["reconocido", CUENTA, 0.78], String(miniatura).slice(0, 20));
        assert.equal(r.salida[0].miniatura, undefined);
        assert.equal(r.avisos.length, 1);
        assert.match(r.avisos[0], aviso);
        assert.doesNotMatch(r.avisos[0] + JSON.stringify(r.salida), /QUJD|data:image/);
    }
    // null equivale a no traerla.
    const nula = validar(mensaje(resultado({ miniatura: null })));
    assert.equal(nula.salida[0].miniatura, undefined);
    assert.deepEqual(nula.avisos, []);
});

const guardadas = (g) => g.get("miniaturas_pase");
const conMiniatura = (extra = {}) => ({ miniatura: FOTO, ...extra });

test("un presente o un retardo guarda la miniatura en memoria, con la sesión y la cuenta, y las salidas salen limpias", () => {
    for (const resultado of ["presente", "retardo"]) {
        const global = nuevoGlobal();
        const r = ejecutar(ID.retro,
            { payload: [filaResultado(resultado)], decision: "reconocido", params: ["reconocido", CUENTA, 0.8], ...conMiniatura() }, { global });
        assert.deepEqual(guardadas(global), { sesion_id: 23, fotos: { [CUENTA]: FOTO } }, resultado);
        assert.deepEqual(global.claves(), ["miniaturas_pase"]);
        assert.ok(sinFoto(r.salida), "ninguna salida lleva la miniatura");
        assert.ok(sinFoto(r.avisos) && sinFoto(r.logs), "ni el aviso ni el log");
        assert.deepEqual(r.logs, ["Pase de lista: " + resultado]);
    }
});

test("la miniatura de otro alumno de la misma sesión se suma; la de otra sesión empieza de cero", () => {
    const global = nuevoGlobal();
    const dar = (cuenta, foto, sesion) => ejecutar(ID.retro, {
        payload: [filaResultado("presente", { sesion_id: sesion })], decision: "reconocido", params: ["reconocido", cuenta, 0.8], miniatura: foto
    }, { global });
    dar(CUENTA, FOTO, 23);
    dar("320068235", OTRA_FOTO, 23);
    assert.deepEqual(guardadas(global), { sesion_id: 23, fotos: { [CUENTA]: FOTO, "320068235": OTRA_FOTO } });
    dar("320068236", FOTO, 24);
    assert.deepEqual(guardadas(global), { sesion_id: 24, fotos: { "320068236": FOTO } });
});

test("un resultado rezagado de una sesión anterior no pisa las fotos de la sesión actual", () => {
    const global = nuevoGlobal();
    global.set("miniaturas_pase", { sesion_id: 24, fotos: { "320068235": OTRA_FOTO } });
    ejecutar(ID.retro, {
        payload: [filaResultado("presente", { sesion_id: 23 })], decision: "reconocido", params: ["reconocido", CUENTA, 0.8], ...conMiniatura()
    }, { global });
    assert.deepEqual(guardadas(global), { sesion_id: 24, fotos: { "320068235": OTRA_FOTO } });
});

test("sin guardar: duplicado, fuera de grupo, desconocido, dudoso, imagen insuficiente y sin sesión abierta", () => {
    for (const resultado of ["duplicado", "fuera_de_grupo", "desconocido", "dudoso", "imagen_insuficiente"]) {
        const global = nuevoGlobal();
        const r = ejecutar(ID.retro, { payload: [filaResultado(resultado)], decision: "reconocido", params: ["reconocido", CUENTA, 0.8], ...conMiniatura() }, { global });
        assert.deepEqual(global.claves(), [], resultado);
        assert.ok(sinFoto(r), resultado + ": no sale por ninguna parte");
        assert.ok(r.salida[0], "la retroalimentación sigue saliendo");
    }
    const global = nuevoGlobal();
    const sinSesion = ejecutar(ID.retro, { payload: [], decision: "reconocido", params: ["reconocido", CUENTA, 0.8], ...conMiniatura() }, { global });
    assert.deepEqual(global.claves(), []);
    assert.ok(sinFoto(sinSesion));
});

test("una miniatura inválida o una cuenta inválida no se guardan y no impiden la retroalimentación ni la asistencia", () => {
    const malos = [
        conMiniatura({ miniatura: 7 }), conMiniatura({ miniatura: "no es base64!" }), conMiniatura({ miniatura: "" }),
        conMiniatura({ miniatura: "A".repeat(12289) }), conMiniatura({ params: ["reconocido", "abc", 0.8] }),
        conMiniatura({ params: undefined }), {}
    ];
    for (const extra of malos) {
        const global = nuevoGlobal();
        const r = ejecutar(ID.retro, { payload: [filaResultado("presente")], decision: "reconocido", params: ["reconocido", CUENTA, 0.8], ...extra }, { global });
        assert.deepEqual(global.claves(), [], JSON.stringify(Object.keys(extra)));
        assert.deepEqual(JSON.parse(r.salida[0].payload), { servo: "abrir", led: "verde", buzzer: 1 });
        assert.equal(JSON.parse(r.salida[2].payload).ultimo.estado, "presente");
    }
});

test("tope de miniaturas: pasadas 100, las nuevas no se guardan (con aviso) y las ya guardadas se pueden reemplazar", () => {
    const global = nuevoGlobal();
    const fotos = {};
    for (let i = 0; i < 100; i++) {
        fotos[String(100000000 + i)] = FOTO;
    }
    global.set("miniaturas_pase", { sesion_id: 23, fotos });
    const r = ejecutar(ID.retro, { payload: [filaResultado("presente")], decision: "reconocido", params: ["reconocido", CUENTA, 0.8], ...conMiniatura() }, { global });
    assert.equal(Object.keys(guardadas(global).fotos).length, 100);
    assert.equal(guardadas(global).fotos[CUENTA], undefined);
    assert.equal(r.avisos.length, 1);
    assert.match(r.avisos[0], /ya hay 100 guardadas/);
    assert.ok(r.salida[0], "la retroalimentación sigue saliendo");
    // Una cuenta que ya tiene foto cabe: reemplaza la suya.
    ejecutar(ID.retro, { payload: [filaResultado("presente")], decision: "reconocido", params: ["reconocido", "100000005", 0.8], miniatura: OTRA_FOTO }, { global });
    assert.equal(guardadas(global).fotos["100000005"], OTRA_FOTO);
    assert.equal(Object.keys(guardadas(global).fotos).length, 100);
});

test("si el contexto falla, la retroalimentación y la asistencia salen igual y el aviso no lleva la foto", () => {
    const global = { get() { throw new Error("contexto roto " + FOTO); }, set() { throw new Error("contexto roto"); } };
    const r = ejecutar(ID.retro, { payload: [filaResultado("presente")], decision: "reconocido", params: ["reconocido", CUENTA, 0.8], ...conMiniatura() }, { global });
    assert.deepEqual(JSON.parse(r.salida[0].payload), { servo: "abrir", led: "verde", buzzer: 1 });
    assert.ok(r.salida[2]);
    assert.deepEqual(r.avisos, ["Miniatura no guardada: falló el contexto"]);
});

test("al arrancar o desplegar se borran las miniaturas: el inject corre una vez y alimenta solo a la función que las borra", () => {
    const inject = nodo(ID.alArrancar);
    assert.equal(inject.type, "inject");
    assert.equal(inject.once, true);
    assert.equal(inject.repeat, "");
    assert.equal(inject.crontab, "");
    assert.deepEqual(inject.wires, [[ID.borrarMiniaturas]]);
    const global = nuevoGlobal();
    global.set("miniaturas_pase", { sesion_id: 23, fotos: { [CUENTA]: FOTO } });
    const r = ejecutar(ID.borrarMiniaturas, { payload: 1 }, { global });
    assert.equal(r.salida, null);
    assert.deepEqual(global.claves(), []);
    assert.ok(sinFoto(r));
    assert.equal(nodo(ID.borrarMiniaturas).outputs, 0);
});

test("la miniatura nunca llega a un log: el error del pase no la lleva, y los nodos debug solo muestran el payload", () => {
    const r = ejecutar(ID.registrarError, {
        error: { message: "falló la consulta" }, decision: "reconocido", params: ["reconocido", CUENTA, 0.8], miniatura: FOTO
    });
    assert.ok(sinFoto(r), "el aviso del error no lleva la foto");
    // El mensaje con la miniatura recorre la pestaña del pase; la respuesta de la API, la de Asistencia.
    for (const n of flujos.filter((x) => x.type === "debug" && (x.z === ID.tab || x.z === "a1f0c0de000000c0"))) {
        assert.ok(n.complete === "payload" || n.complete === undefined, "debug " + n.id + " muestra el mensaje completo");
    }
    // La sentencia solo recibe los tres parámetros: la foto no va a la base.
    assert.doesNotMatch(consulta, /miniatura/);
    assert.equal(validar(mensaje(resultado({ miniatura: FOTO }))).salida[0].params.length, 3);
    // Solo `nodered/flows` toca el contexto de las miniaturas, y siempre el almacén en memoria.
    const usos = flujos.filter((n) => n.type === "function" && /miniaturas_pase/.test(n.func));
    assert.deepEqual(usos.map((n) => n.id).sort(), [ID.retro, ID.borrarMiniaturas, ID.estadoTrasIniciar, ID.estadoTrasTerminar, ID.estadoBase, "a1f0c0de000000ca"].sort());
    for (const n of usos) {
        assert.doesNotMatch(n.func, /persistente/, n.id + " usa el almacén persistente");
    }
});

// ---------------------------------------------------------------------------------------------
// Carrera al cerrar la sesión: un `presente` en vuelo no vuelve a crear el almacén de la sesión cerrada
// ---------------------------------------------------------------------------------------------

const presenteDe = (sesion, cuenta = CUENTA, foto = FOTO) => ({
    payload: [filaResultado("presente", { sesion_id: sesion })], decision: "reconocido", params: ["reconocido", cuenta, 0.8], miniatura: foto
});

test("la foto de una sesión ya cerrada (o anterior) no se guarda; la de una posterior sí", () => {
    const global = nuevoGlobal();
    global.set("miniaturas_pase_cerrada", 23);
    for (const sesion of [22, 23]) {
        const r = ejecutar(ID.retro, presenteDe(sesion), { global });
        assert.equal(guardadas(global), undefined, "sesión " + sesion);
        assert.ok(r.salida[0] && r.salida[2], "la retroalimentación y la asistencia salen igual");
    }
    ejecutar(ID.retro, presenteDe(24), { global });
    assert.deepEqual(guardadas(global), { sesion_id: 24, fotos: { [CUENTA]: FOTO } });
});

test("terminar borra las fotos y un presente en vuelo de esa sesión llega tarde sin crear un almacén", () => {
    const global = nuevoGlobal();
    ejecutar(ID.retro, presenteDe(23), { global });
    assert.ok(guardadas(global));
    ejecutar(ID.estadoTrasTerminar, { payload: [{ id: 23 }], fuente: "terminar" }, { global });
    assert.equal(guardadas(global), undefined);
    assert.equal(global.get("miniaturas_pase_cerrada"), 23);
    // El resultado que iba en vuelo llega después del cierre.
    const tarde = ejecutar(ID.retro, presenteDe(23, "320068235", OTRA_FOTO), { global });
    assert.equal(guardadas(global), undefined, "no queda un almacén de la sesión cerrada en memoria");
    assert.ok(tarde.salida[0], "la asistencia y la retroalimentación no se pierden");
    // La sesión siguiente sí guarda.
    ejecutar(ID.retro, presenteDe(24), { global });
    assert.deepEqual(guardadas(global), { sesion_id: 24, fotos: { [CUENTA]: FOTO } });
});

test("iniciar con una sesión olvidada abierta marca la olvidada como cerrada y borra sus fotos", () => {
    const global = nuevoGlobal();
    ejecutar(ID.retro, presenteDe(23), { global });
    ejecutar(ID.estadoTrasIniciar, {
        payload: [{ id: 24, materia: "FSE", grupo: "1", inicio: "2026-11-05T15:30:12+00:00", tol_min: 10, cerrada_id: 23, faltantes: 10 }]
    }, { global });
    assert.equal(guardadas(global), undefined);
    assert.equal(global.get("miniaturas_pase_cerrada"), 23);
    ejecutar(ID.retro, presenteDe(23, "320068235", OTRA_FOTO), { global });
    assert.equal(guardadas(global), undefined);
    ejecutar(ID.retro, presenteDe(24), { global });
    assert.deepEqual(guardadas(global), { sesion_id: 24, fotos: { [CUENTA]: FOTO } });
});

test("la reconciliación con la base borra las fotos cuando no hay sesión abierta y las conserva con una abierta", () => {
    const global = nuevoGlobal();
    ejecutar(ID.retro, presenteDe(23), { global });
    const abierta = ejecutar(ID.estadoBase, {
        payload: [{ id: 23, materia: "FSE", grupo: "1", inicio: "2026-11-05T15:30:12+00:00", tol_min: 10, abierta: true, presentes: 1, retardos: 0, faltantes: 9 }]
    }, { global });
    assert.ok(abierta.salida[0]);
    assert.deepEqual(guardadas(global), { sesion_id: 23, fotos: { [CUENTA]: FOTO } }, "con la sesión abierta no se toca");
    assert.equal(global.get("miniaturas_pase_cerrada"), undefined);
    // La base dice que la última sesión (23) está cerrada: otro camino la cerró.
    ejecutar(ID.estadoBase, { payload: [{ id: 23, materia: "FSE", grupo: "1", inicio: "2026-11-05T15:30:12+00:00", tol_min: 10, abierta: false }] }, { global });
    assert.equal(guardadas(global), undefined);
    assert.equal(global.get("miniaturas_pase_cerrada"), 23);
    ejecutar(ID.retro, presenteDe(23, "320068235", OTRA_FOTO), { global });
    assert.equal(guardadas(global), undefined);
});

test("una reconciliación que llega tarde no borra las fotos de una sesión posterior a la cerrada que devolvió la base", () => {
    const global = nuevoGlobal();
    // La consulta corrió con la sesión 23 como última (cerrada); después se abrió la 24 y ya tiene fotos.
    global.set("miniaturas_pase", { sesion_id: 24, fotos: { [CUENTA]: FOTO } });
    ejecutar(ID.estadoBase, { payload: [{ id: 23, materia: "FSE", grupo: "1", inicio: "2026-11-05T15:30:12+00:00", tol_min: 10, abierta: false }] }, { global });
    assert.deepEqual(guardadas(global), { sesion_id: 24, fotos: { [CUENTA]: FOTO } }, "la sesión 24 no se toca");
    assert.equal(global.get("miniaturas_pase_cerrada"), 23, "pero sí se recuerda la 23 como cerrada");
    // Las fotos de la misma sesión cerrada o de una anterior sí se borran.
    for (const guardada of [23, 22]) {
        const g = nuevoGlobal();
        g.set("miniaturas_pase", { sesion_id: guardada, fotos: { [CUENTA]: FOTO } });
        ejecutar(ID.estadoBase, { payload: [{ id: 23, abierta: false }] }, { global: g });
        assert.equal(guardadas(g), undefined, "almacén de la sesión " + guardada);
    }
});

test("la reconciliación sin ninguna sesión en la base no borra nada: no hay con qué comparar el almacén", () => {
    const global = nuevoGlobal();
    global.set("miniaturas_pase", { sesion_id: 1, fotos: { [CUENTA]: FOTO } });
    ejecutar(ID.estadoBase, { payload: [] }, { global });
    assert.deepEqual(guardadas(global), { sesion_id: 1, fotos: { [CUENTA]: FOTO } });
    assert.equal(global.get("miniaturas_pase_cerrada"), undefined);
});

test("el id de la última sesión cerrada nunca baja", () => {
    const global = nuevoGlobal();
    global.set("miniaturas_pase_cerrada", 30);
    ejecutar(ID.estadoTrasTerminar, { payload: [{ id: 12 }], fuente: "terminar" }, { global });
    assert.equal(global.get("miniaturas_pase_cerrada"), 30);
    ejecutar(ID.estadoBase, { payload: [{ id: 12, abierta: false }] }, { global });
    assert.equal(global.get("miniaturas_pase_cerrada"), 30);
});

test("la foto que se guarda debe ser un JPEG en base64 con relleno (Node-RED lo comprueba otra vez al guardar)", () => {
    for (const foto of ["iVBORw0KGgo=", "/9j/QUJ", "QUJD", "/9j"]) {
        const global = nuevoGlobal();
        const r = ejecutar(ID.retro, presenteDe(23, CUENTA, foto), { global });
        assert.equal(guardadas(global), undefined, foto);
        assert.ok(r.salida[0]);
    }
});
