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
    avisos: "a1f0c0de000000b7",
    catch: "a1f0c0de000000b8",
    registrarError: "a1f0c0de000000b9"
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

// Ejecuta la función del nodo `id`. Devuelve { salida, avisos, logs }; la salida se copia por JSON
// (los objetos del contexto vm son de otro realm). Una excepción se propaga. `ahora` (ms) fija
// Date.now() para probar la ventana de `ts`.
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

test("modo prueba se ignora con un aviso, sea cual sea la decisión", () => {
    for (const decision of ["reconocido", "dudoso", "desconocido", "sin_rostro"]) {
        assertDescarte(validar(mensaje(resultado({ modo: "prueba", decision }))), /modo prueba ignorado/);
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
    assertDescarte(validar(mensaje("x".repeat(2049))), /payload de 2049 caracteres \(máximo 2048\)/);
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
        const todo = JSON.stringify(r.salida) + r.avisos.join("|") + r.logs.join("|");
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

const retro = (payload, extra = {}) => ejecutar(ID.retro, { payload, decision: "reconocido", params: ["reconocido", CUENTA, 0.8], ...extra });

test("la retroalimentación sigue la tabla del contrato para cada resultado de la base", () => {
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
        const r = retro([{ resultado }]);
        assert.deepEqual(JSON.parse(r.salida[0].payload), orden, resultado);
        assert.equal(r.salida[1], null);
    }
});

test("la orden sale limpia: solo el payload, sin la cuenta ni los parámetros del mensaje original", () => {
    const r = retro([{ resultado: "presente" }], { topic: "aula/vision/resultado", retain: true });
    assert.deepEqual(Object.keys(r.salida[0]), ["payload"]);
    assert.doesNotMatch(r.salida[0].payload, new RegExp(CUENTA));
});

test("sin sesión abierta (cero filas) no se manda retroalimentación y queda un aviso sin contenido", () => {
    for (const decision of ["reconocido", "dudoso", "desconocido", "imagen_insuficiente"]) {
        const r = retro([], { decision });
        assert.equal(r.salida[0], null);
        assert.equal(r.salida[1].payload, "Sin sesión abierta: no se registra ni se manda retroalimentación (" + decision + ")");
        assert.doesNotMatch(r.salida[1].payload, new RegExp(CUENTA));
    }
});

test("el log de la retroalimentación lleva el resultado y nunca la cuenta", () => {
    const r = retro([{ resultado: "presente" }]);
    assert.deepEqual(r.logs, ["Pase de lista: presente"]);
});

test("una respuesta imposible de la base lanza y no publica nada", () => {
    for (const payload of [undefined, null, "x", {}, [{ resultado: "presente" }, { resultado: "presente" }], [{}], [{ resultado: "otro" }],
        [{ resultado: 1 }], [{ resultado: "toString" }], [{ resultado: "__proto__" }], [{ resultado: "constructor" }]]) {
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

test("el único mqtt out publica en aula/actuador/cmd con QoS 1, sin retain, y solo lo alimenta la retroalimentación", () => {
    const salidas = enPestana.filter((n) => n.type === "mqtt out");
    assert.equal(salidas.length, 1);
    const o = salidas[0];
    assert.equal(o.id, ID.publicar);
    assert.equal(o.topic, "aula/actuador/cmd");
    assert.equal(o.qos, "1");
    assert.equal(o.retain, "false");
    assert.equal(o.broker, "a1f0c0de00000002");
    const alimentan = flujos.filter((n) => (n.wires || []).some((salida) => salida.includes(ID.publicar)));
    assert.deepEqual(alimentan.map((n) => n.id), [ID.retro]);
    assert.deepEqual(nodo(ID.retro).wires, [[ID.publicar], [ID.avisos]]);
});

test("la cadena es entrada → validar → límite → sentencia → retroalimentación, y los avisos van al debug de consola", () => {
    assert.deepEqual(nodo(ID.validar).wires, [[ID.limite], [ID.avisos]]);
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
        assert.ok(ejecutar(ID.retro, { payload: [{ resultado }], decision: "x" }).salida[0], resultado);
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

test("la ACL deja a vision publicar el resultado y a nodered publicar el actuador", () => {
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
