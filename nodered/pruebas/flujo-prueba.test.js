// Pruebas de la prueba de reconocimiento: lo que el profesor pide con "Probar reconocimiento" del frontend
// (plan, sección 6.7; contrato MQTT, sección 4: aula/vision/cmd {"accion":"probar"} y aula/vision/resultado con
// modo "prueba"). Tres piezas:
//   - Pestaña "Registro": POST /api/enrolamiento/probar (409 con un registro abierto o una prueba pendiente, 429
//     por límite, publica la orden), GET /api/enrolamiento/prueba (solo lectura; el nombre sale de un SELECT) y
//     "Guardar resultado de la prueba" (el siguiente resultado de prueba o el rechazo "ocupado" de la visión).
//   - Pestaña "Pase de lista": la 4.ª salida de "Validar resultado de visión", que reenvía solo los resultados
//     de modo prueba. La asistencia no cambia (el resto de sus pruebas está en flujo-pase-lista.test.js).
//   - Nada de esto escribe en asistencias ni en intentos, ni publica retroalimentación a los actuadores.
// Cada función se extrae del propio flows.json y se ejecuta en un contexto vm con un contexto de flujo falso y un
// reloj que la prueba controla. No prueba el broker, Express ni el nodo de Postgres. Las cuentas y los nombres de
// aquí son entradas de prueba de las funciones, no datos de la lista real.
// Sin dependencias: node --test nodered/pruebas/*.test.js
"use strict";
const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const flujos = JSON.parse(fs.readFileSync(path.join(__dirname, "..", "flows", "flows.json"), "utf8"));
const ID = {
    tabRegistro: "a1f0c0de00000060",
    tabPase: "a1f0c0de000000b0",
    validarApartar: "a1f0c0de00000063",
    inscripcionAbierta: "a1f0c0de00000065",
    publicar: "a1f0c0de00000081",
    respuesta: "a1f0c0de00000082",
    debug: "a1f0c0de00000087",
    mqttEnrolamiento: "a1f0c0de00000072",
    progreso: "a1f0c0de00000073",
    entradaPost: "a1f0c0de00000131",
    pedir: "a1f0c0de00000132",
    entradaGet: "a1f0c0de00000134",
    estado: "a1f0c0de00000135",
    seleccionNombre: "a1f0c0de00000136",
    completar: "a1f0c0de00000137",
    enlaceEntra: "a1f0c0de00000139",
    guardar: "a1f0c0de0000013a",
    catchHttp: "a1f0c0de0000013b",
    responder500: "a1f0c0de0000013c",
    catchGuardar: "a1f0c0de0000013d",
    registrarError: "a1f0c0de0000013e",
    enlaceSale: "a1f0c0de0000013f",
    validar: "a1f0c0de000000b3"
};
const CUENTA = "320068234";
const AHORA = Date.parse("2026-11-16T15:30:12.000Z");
const ISO = (ms) => new Date(ms).toISOString();
const LAT = { cap: 180, det: 12, emb: 6, total: 240 };

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

// Contexto `flow` de mentira. Guarda una copia por JSON: una función que mutara el objeto sin llamar a
// flow.set no pasaría por un contexto que sí persiste.
function nuevoFlow() {
    const datos = new Map();
    return {
        get: (k) => (datos.has(k) ? JSON.parse(datos.get(k)) : undefined),
        set: (k, v) => void (v === undefined ? datos.delete(k) : datos.set(k, JSON.stringify(v)))
    };
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

const pedir = (opciones, msg = { req: { headers: {} }, payload: {} }) => ejecutar(ID.pedir, msg, opciones);
const estadoDe = (opciones) => ejecutar(ID.estado, { req: { headers: {} } }, opciones);

// Un flow con el estado de una prueba ya guardado.
function conPrueba(prueba) {
    const flow = nuevoFlow();
    flow.set("prueba", prueba);
    return flow;
}
const PENDIENTE = (solicitadoMs = AHORA - 1000) => ({ solicitado_ms: solicitadoMs, pendiente: true, resultado: null });
const RESULTADO = (extra = {}) => ({
    decision: "reconocido", cuenta: CUENTA, conf: 0.78, margen: 0.21, motivo: null, lat_ms: LAT, recibido_ms: AHORA - 500, ...extra
});

function inscripcionAbierta(flow) {
    flow.set("inscripcion", { id: "r1", abierto: true, cuenta: CUENTA, estado: "en_curso", poses: [] });
    return flow;
}

// ---------------------------------------------------------------------------------------------
// POST /api/enrolamiento/probar
// ---------------------------------------------------------------------------------------------

test("probar publica solo {accion:probar} en aula/vision/cmd (QoS 1, sin retain), deja la prueba pendiente y responde 202", () => {
    const r = pedir();
    const [orden, http] = r.salida;
    assert.deepEqual(orden, { topic: "aula/vision/cmd", payload: JSON.stringify({ accion: "probar" }), qos: 1, retain: false });
    assert.equal(http.statusCode, 202);
    assert.deepEqual(http.payload, { pendiente: true, solicitado: ISO(AHORA) });
    assert.deepEqual(r.flow.get("prueba"), { solicitado_ms: AHORA, pendiente: true, resultado: null });
});

test("probar no lee el cuerpo: una cuenta enviada no llega a la visión", () => {
    const r = pedir({}, { req: { headers: { "content-type": "application/json" } }, payload: { cuenta: CUENTA, accion: "enrolar" } });
    assert.equal(r.salida[0].payload, JSON.stringify({ accion: "probar" }));
    assert.doesNotMatch(JSON.stringify(r.salida), new RegExp(CUENTA));
});

test("probar con un registro abierto responde 409 registro_abierto y no publica ni toca la prueba anterior", () => {
    const flow = conPrueba({ solicitado_ms: AHORA - 60000, pendiente: false, resultado: RESULTADO() });
    inscripcionAbierta(flow);
    const r = pedir({ flow });
    assert.equal(r.salida[0], null);
    assert.equal(r.salida[1].statusCode, 409);
    assert.equal(r.salida[1].payload.motivo, "registro_abierto");
    assert.match(r.salida[1].payload.error, /registro abierto/i);
    assert.deepEqual(flow.get("prueba").resultado, RESULTADO());
});

test("un registro ya cerrado no impide probar", () => {
    const flow = nuevoFlow();
    flow.set("inscripcion", { id: "r1", abierto: false, cuenta: CUENTA, estado: "completado", poses: [] });
    assert.equal(pedir({ flow }).salida[1].statusCode, 202);
});

test("probar con una prueba pendiente responde 409 prueba_pendiente, sin publicar ni renovar la espera", () => {
    const flow = conPrueba(PENDIENTE(AHORA - 5000));
    const r = pedir({ flow });
    assert.equal(r.salida[0], null);
    assert.equal(r.salida[1].statusCode, 409);
    assert.equal(r.salida[1].payload.motivo, "prueba_pendiente");
    assert.equal(r.salida[1].payload.solicitado, ISO(AHORA - 5000));
    assert.equal(flow.get("prueba").solicitado_ms, AHORA - 5000);
});

test("una prueba que no recibió resultado en 15 s deja de bloquear: se puede pedir otra", () => {
    for (const antes of [15000, 16000, 3600000]) {
        const r = pedir({ flow: conPrueba(PENDIENTE(AHORA - antes)) });
        assert.equal(r.salida[1].statusCode, 202, antes + " ms");
        assert.equal(r.salida[0].payload, JSON.stringify({ accion: "probar" }));
    }
    assert.equal(pedir({ flow: conPrueba(PENDIENTE(AHORA - 14999)) }).salida[1].statusCode, 409);
});

test("un salto del reloj hacia atrás no bloquea: una solicitud 'del futuro' no cuenta como pendiente", () => {
    assert.equal(pedir({ flow: conPrueba(PENDIENTE(AHORA + 60000)) }).salida[1].statusCode, 202);
});

test("límite: otra solicitud a menos de 3 s de la anterior responde 429 con Retry-After, sin publicar ni renovar la ventana", () => {
    const flow = conPrueba({ solicitado_ms: AHORA - 1000, pendiente: false, resultado: RESULTADO() });
    const r = pedir({ flow });
    assert.equal(r.salida[0], null);
    assert.equal(r.salida[1].statusCode, 429);
    assert.deepEqual(r.salida[1].headers, { "retry-after": "2" });
    assert.match(r.salida[1].payload.error, /Demasiado rápido/);
    assert.equal(flow.get("prueba").solicitado_ms, AHORA - 1000);
    assert.deepEqual(flow.get("prueba").resultado, RESULTADO());
    // A los 3 s ya pasa, y la anterior se borra: la nueva espera su propio resultado.
    const despues = conPrueba({ solicitado_ms: AHORA - 3000, pendiente: false, resultado: RESULTADO() });
    const ok = pedir({ flow: despues });
    assert.equal(ok.salida[1].statusCode, 202);
    assert.deepEqual(despues.get("prueba"), { solicitado_ms: AHORA, pendiente: true, resultado: null });
});

test("un 409 o un 429 no se confunden: con la prueba pendiente gana el 409, con un registro abierto gana sobre ambos", () => {
    const pendiente = conPrueba(PENDIENTE(AHORA - 500));
    assert.equal(pedir({ flow: pendiente }).salida[1].statusCode, 409);
    const abierto = inscripcionAbierta(conPrueba(PENDIENTE(AHORA - 500)));
    assert.equal(pedir({ flow: abierto }).salida[1].payload.motivo, "registro_abierto");
});

// ---------------------------------------------------------------------------------------------
// POST /api/enrolamiento con una prueba pendiente
// ---------------------------------------------------------------------------------------------

const iniciar = (opciones) => ejecutar(ID.validarApartar, { req: { headers: { "content-type": "application/json" } }, payload: { cuenta: CUENTA, consentimiento: true } }, opciones);

test("iniciar un registro con una prueba pendiente responde 409 prueba_pendiente, con error, y no aparta el lugar", () => {
    const flow = conPrueba(PENDIENTE(AHORA - 5000));
    const r = iniciar({ flow });
    assert.equal(r.salida[0], null);
    assert.equal(r.salida[1].statusCode, 409);
    assert.equal(r.salida[1].payload.motivo, "prueba_pendiente");
    assert.match(r.salida[1].payload.error, /prueba de reconocimiento en curso/);
    assert.equal(flow.get("inscripcion"), undefined);
    assert.equal(flow.get("prueba").pendiente, true);
});

test("la misma expresión que probar: a los 15 s, con el resultado ya recibido o con un salto del reloj hacia atrás, el registro sí se aparta", () => {
    for (const prueba of [
        PENDIENTE(AHORA - 15000),
        PENDIENTE(AHORA + 60000),
        { solicitado_ms: AHORA - 1000, pendiente: false, resultado: RESULTADO() }
    ]) {
        const flow = conPrueba(prueba);
        const r = iniciar({ flow });
        assert.ok(r.salida[0], JSON.stringify(prueba));
        assert.equal(r.salida[1], null);
        assert.equal(flow.get("inscripcion").abierto, true);
    }
    assert.equal(iniciar({ flow: conPrueba(PENDIENTE(AHORA - 14999)) }).salida[1].statusCode, 409);
});

test("sin ninguna prueba el registro se aparta como siempre, y un registro abierto gana sobre la prueba pendiente", () => {
    assert.ok(iniciar({}).salida[0]);
    const abierto = inscripcionAbierta(conPrueba(PENDIENTE(AHORA - 500)));
    assert.equal(iniciar({ flow: abierto }).salida[1].payload.motivo, "registro_abierto");
});

// ---------------------------------------------------------------------------------------------
// Guardar resultado de la prueba (resultado de modo prueba, 4.ª salida de la validación)
// ---------------------------------------------------------------------------------------------

function resultadoEntrante(extra = {}) {
    return {
        payload: { decision: "reconocido", cuenta: CUENTA, conf: 0.78, margen: 0.21, motivo: null, lat_ms: LAT, ts_ms: AHORA - 800, ...extra }
    };
}
const guardarResultado = (extra, opciones) => ejecutar(ID.guardar, resultadoEntrante(extra), opciones);

test("el resultado de prueba tras la solicitud se guarda con su hora de recepción y cierra la espera", () => {
    const flow = conPrueba(PENDIENTE(AHORA - 1500));
    const r = guardarResultado({}, { flow });
    assert.equal(r.salida, null);
    assert.deepEqual(flow.get("prueba"), {
        solicitado_ms: AHORA - 1500,
        pendiente: false,
        resultado: { decision: "reconocido", cuenta: CUENTA, conf: 0.78, margen: 0.21, motivo: null, lat_ms: LAT, recibido_ms: AHORA }
    });
    assert.deepEqual(r.logs, ["Prueba de reconocimiento: reconocido"]);
});

test("sin una prueba pendiente (o vencida) el resultado se descarta con un aviso sin cuenta y no cambia nada", () => {
    for (const flow of [nuevoFlow(), conPrueba({ solicitado_ms: AHORA - 5000, pendiente: false, resultado: RESULTADO({ decision: "dudoso" }) }), conPrueba(PENDIENTE(AHORA - 16000))]) {
        const antes = JSON.stringify(flow.get("prueba"));
        const r = guardarResultado({}, { flow });
        assert.match(r.salida.payload, /^Descartado, .*prueba pendiente/);
        assert.doesNotMatch(JSON.stringify(r) + r.avisos.join(""), new RegExp(CUENTA));
        assert.equal(JSON.stringify(flow.get("prueba")), antes);
    }
});

test("un resultado anterior a la solicitud (más de 2 s antes) es de una prueba vieja y se descarta", () => {
    const flow = conPrueba(PENDIENTE(AHORA - 1000));
    assert.match(guardarResultado({ ts_ms: AHORA - 1000 - 2001 }, { flow }).salida.payload, /^Descartado, .*anterior a la solicitud/);
    assert.equal(flow.get("prueba").pendiente, true);
    assert.equal(guardarResultado({ ts_ms: AHORA - 1000 - 2000 }, { flow }).salida, null);
    assert.equal(flow.get("prueba").pendiente, false);
});

test("el primer resultado cierra la espera: uno repetido no sobrescribe", () => {
    const flow = conPrueba(PENDIENTE());
    guardarResultado({ decision: "dudoso" }, { flow });
    const r = guardarResultado({ decision: "reconocido" }, { flow });
    assert.match(r.salida.payload, /^Descartado/);
    assert.equal(flow.get("prueba").resultado.decision, "dudoso");
});

test("la cuenta solo se conserva con reconocido o dudoso: un desconocido no la guarda", () => {
    for (const decision of ["reconocido", "dudoso"]) {
        const flow = conPrueba(PENDIENTE());
        guardarResultado({ decision }, { flow });
        assert.equal(flow.get("prueba").resultado.cuenta, CUENTA);
    }
    for (const decision of ["desconocido", "sin_rostro", "imagen_insuficiente", "error"]) {
        const flow = conPrueba(PENDIENTE());
        guardarResultado({ decision, cuenta: CUENTA }, { flow });
        assert.equal(flow.get("prueba").resultado.cuenta, null, decision);
        assert.equal(flow.get("prueba").resultado.decision, decision);
    }
});

test("el motivo, el margen y la latencia se saneran: lo que no cumple queda en null", () => {
    const flow = conPrueba(PENDIENTE());
    guardarResultado({
        decision: "desconocido",
        conf: -0.1,
        margen: "x",
        motivo: "Texto libre con espacios y SECRETO",
        lat_ms: { cap: 180, det: "12", emb: -1, total: Infinity, extra: 5 }
    }, { flow });
    const guardado = flow.get("prueba").resultado;
    assert.equal(guardado.margen, null);
    assert.equal(guardado.motivo, null);
    assert.equal(guardado.conf, -0.1);
    assert.deepEqual(guardado.lat_ms, { cap: 180, det: null, emb: null, total: null });
    assert.doesNotMatch(JSON.stringify(flow.get("prueba")), /SECRETO/);
    const otra = conPrueba(PENDIENTE());
    guardarResultado({ decision: "desconocido", motivo: "galeria_vacia", conf: null, margen: null }, { flow: otra });
    assert.equal(otra.get("prueba").resultado.motivo, "galeria_vacia");
});

// ---------------------------------------------------------------------------------------------
// Guardar resultado de la prueba (rechazo "ocupado" de la visión en aula/vision/enrolamiento)
// ---------------------------------------------------------------------------------------------

function rechazo(extra = {}, msgExtra = {}) {
    const cuerpo = { ts: ISO(AHORA - 100), cuenta: "", paso: 0, total: 5, pose: "", estado: "error", motivo: "ocupado", ...extra };
    return { topic: "aula/vision/enrolamiento", payload: JSON.stringify(cuerpo), retain: false, ...msgExtra };
}

test("el rechazo 'ocupado' de la visión (cuenta vacía, paso 0) cierra la espera con decisión error y motivo ocupado", () => {
    const flow = conPrueba(PENDIENTE());
    const r = ejecutar(ID.guardar, rechazo(), { flow });
    assert.equal(r.salida, null);
    assert.deepEqual(flow.get("prueba"), {
        solicitado_ms: AHORA - 1000,
        pendiente: false,
        resultado: { decision: "error", cuenta: null, conf: null, margen: null, motivo: "ocupado", lat_ms: null, recibido_ms: AHORA }
    });
});

test("el progreso normal del registro y los rechazos de otras órdenes no tocan la prueba, ni dejan aviso", () => {
    const casos = [
        rechazo({ cuenta: CUENTA }), // el "ocupado" de un enrolar (lleva cuenta)
        rechazo({ paso: 1, pose: "frente", estado: "aceptada", motivo: undefined }),
        rechazo({ estado: "capturando" }),
        rechazo({ motivo: "npu" }),
        rechazo({ motivo: "cuenta_invalida" }),
        rechazo({ paso: 2 }),
        rechazo({ ts: ISO(AHORA - 1000 - 2001) }), // un rechazo anterior a esta solicitud
        rechazo({ ts: "no es una fecha" }),
        rechazo({}, { retain: true }),
        { topic: "aula/vision/enrolamiento", payload: "no es JSON" },
        { topic: "aula/vision/enrolamiento", payload: "[1]" },
        { topic: "aula/vision/enrolamiento", payload: Buffer.alloc(0) },
        { topic: "aula/vision/enrolamiento", payload: "x".repeat(40000) }
    ];
    for (const m of casos) {
        const flow = conPrueba(PENDIENTE());
        const r = ejecutar(ID.guardar, m, { flow });
        assert.equal(r.salida, null);
        assert.deepEqual(r.avisos, []);
        assert.deepEqual(flow.get("prueba"), PENDIENTE(), JSON.stringify(m).slice(0, 80));
    }
});

test("un rechazo 'ocupado' sin prueba pendiente se ignora en silencio (es tráfico normal del registro)", () => {
    for (const flow of [nuevoFlow(), conPrueba({ solicitado_ms: AHORA - 5000, pendiente: false, resultado: RESULTADO() })]) {
        const antes = JSON.stringify(flow.get("prueba"));
        const r = ejecutar(ID.guardar, rechazo(), { flow });
        assert.equal(r.salida, null);
        assert.deepEqual(r.avisos, []);
        assert.equal(JSON.stringify(flow.get("prueba")), antes);
    }
});

// ---------------------------------------------------------------------------------------------
// GET /api/enrolamiento/prueba
// ---------------------------------------------------------------------------------------------

test("sin ninguna prueba: pendiente false, solicitado null y resultado null (sin consultar la base)", () => {
    const r = estadoDe();
    assert.equal(r.salida[0], null);
    assert.equal(r.salida[1].statusCode, 200);
    assert.deepEqual(r.salida[1].payload, { pendiente: false, solicitado: null, resultado: null });
});

test("con la prueba pendiente: pendiente true, la hora de la solicitud y sin resultado", () => {
    const r = estadoDe({ flow: conPrueba(PENDIENTE(AHORA - 4000)) });
    assert.deepEqual(r.salida[1].payload, { pendiente: true, solicitado: ISO(AHORA - 4000), resultado: null });
});

test("a los 15 s sin resultado deja de estar pendiente, pero la solicitud sigue a la vista (el frontend dice que no hubo respuesta)", () => {
    const r = estadoDe({ flow: conPrueba(PENDIENTE(AHORA - 15000)) });
    assert.deepEqual(r.salida[1].payload, { pendiente: false, solicitado: ISO(AHORA - 15000), resultado: null });
});

test("un reconocido o dudoso pide el nombre a la base: la cuenta va como parámetro y el cuerpo espera al SELECT", () => {
    for (const decision of ["reconocido", "dudoso"]) {
        const r = estadoDe({ flow: conPrueba({ solicitado_ms: AHORA - 3000, pendiente: false, resultado: RESULTADO({ decision }) }) });
        assert.equal(r.salida[1], null);
        assert.deepEqual(r.salida[0].params, [CUENTA]);
        assert.deepEqual(r.salida[0].cuerpo, {
            pendiente: false,
            solicitado: ISO(AHORA - 3000),
            resultado: { decision, cuenta: CUENTA, nombre: null, conf: 0.78, margen: 0.21, motivo: null, lat_ms: LAT, recibido: ISO(AHORA - 500) }
        });
    }
});

test("cualquier otra decisión responde sin consultar la base, con cuenta y nombre null", () => {
    for (const decision of ["desconocido", "sin_rostro", "imagen_insuficiente", "error"]) {
        const r = estadoDe({ flow: conPrueba({ solicitado_ms: AHORA - 3000, pendiente: false, resultado: RESULTADO({ decision, cuenta: null, conf: null, margen: null, motivo: "desconocido" }) }) });
        assert.equal(r.salida[0], null);
        assert.equal(r.salida[1].statusCode, 200);
        assert.deepEqual(r.salida[1].payload.resultado, { decision, cuenta: null, nombre: null, conf: null, margen: null, motivo: "desconocido", lat_ms: LAT, recibido: ISO(AHORA - 500) });
    }
    // Un rechazo no trae latencia.
    const ocupado = estadoDe({ flow: conPrueba({ solicitado_ms: AHORA - 3000, pendiente: false, resultado: RESULTADO({ decision: "error", cuenta: null, conf: null, margen: null, motivo: "ocupado", lat_ms: null }) }) });
    assert.equal(ocupado.salida[1].payload.resultado.lat_ms, null);
});

test("el cuerpo copia solo los campos conocidos: ni una clave de más del contexto llega al navegador", () => {
    const flow = conPrueba({ solicitado_ms: AHORA - 3000, pendiente: false, extra: "x", resultado: { ...RESULTADO({ decision: "desconocido", cuenta: null }), embedding: [1], ts: "x" } });
    const r = estadoDe({ flow });
    assert.deepEqual(Object.keys(r.salida[1].payload), ["pendiente", "solicitado", "resultado"]);
    assert.deepEqual(Object.keys(r.salida[1].payload.resultado), ["decision", "cuenta", "nombre", "conf", "margen", "motivo", "lat_ms", "recibido"]);
    assert.doesNotMatch(JSON.stringify(r.salida), /embedding/);
});

test("al volver el SELECT, el nombre entra al resultado; sin fila, nombre null; la respuesta es 200 y solo lleva el cuerpo", () => {
    const cuerpo = estadoDe({ flow: conPrueba({ solicitado_ms: AHORA - 3000, pendiente: false, resultado: RESULTADO() }) }).salida[0].cuerpo;
    const con = ejecutar(ID.completar, { payload: [{ nombre: "Ana López" }], params: [CUENTA], cuerpo });
    assert.equal(con.salida.statusCode, 200);
    assert.equal(con.salida.payload.resultado.nombre, "Ana López");
    assert.equal(con.salida.payload.resultado.cuenta, CUENTA);
    assert.deepEqual(Object.keys(con.salida.payload), ["pendiente", "solicitado", "resultado"]);
    const sin = ejecutar(ID.completar, { payload: [], params: [CUENTA], cuerpo });
    assert.equal(sin.salida.statusCode, 200);
    assert.equal(sin.salida.payload.resultado.nombre, null);
});

test("un fallo de la base responde 500 sin cuentas ni nombres en el aviso ni en la respuesta", () => {
    const r = ejecutar(ID.responder500, { error: { message: "falló la cuenta " + CUENTA + " de Ana López" }, payload: { cuenta: CUENTA } });
    assert.equal(r.salida.statusCode, 500);
    assert.deepEqual(Object.keys(r.salida.payload), ["error"]);
    // Lo que sale por HTTP es solo el payload; el aviso del registro lleva la causa con las cuentas enmascaradas.
    assert.doesNotMatch(JSON.stringify(r.salida.payload) + r.avisos.join(""), new RegExp(CUENTA));
    const e = ejecutar(ID.registrarError, { error: { message: "falló " + CUENTA }, payload: { cuenta: CUENTA } });
    assert.equal(e.salida, null);
    assert.doesNotMatch(e.avisos.join(""), new RegExp(CUENTA));
    assert.equal(e.avisos.length, 1);
});

// ---------------------------------------------------------------------------------------------
// Validar resultado de visión: la 4.ª salida (solo modo prueba)
// ---------------------------------------------------------------------------------------------

function resultadoVision(extra = {}) {
    return { ts: "2026-11-16T09:30:12.000-06:00", modo: "prueba", decision: "reconocido", cuenta: CUENTA, conf: 0.78, margen: 0.21, lat_ms: LAT, ...extra };
}
const validar = (objeto, extra = {}) => ejecutar(ID.validar, { topic: "aula/vision/resultado", payload: typeof objeto === "string" ? objeto : JSON.stringify(objeto), retain: false, ...extra }, { ahora: AHORA });

test("un resultado de modo prueba sale por la 4.ª salida con los campos validados, y no llega a la base", () => {
    const r = validar(resultadoVision());
    assert.equal(r.salida.length, 4);
    assert.equal(r.salida[0], null, "la asistencia no lo recibe");
    assert.deepEqual(r.salida[3].payload, { decision: "reconocido", cuenta: CUENTA, conf: 0.78, margen: 0.21, motivo: null, lat_ms: LAT, ts_ms: Date.parse("2026-11-16T15:30:12.000Z") });
    assert.deepEqual(Object.keys(r.salida[3]), ["payload"]);
    // Su latencia se guarda como siempre (salida 3) y el aviso de que la asistencia lo ignora sigue.
    assert.deepEqual(r.salida[2].lat_ms, LAT);
    assert.equal(r.salida[1].payload, "Resultado de prueba: no marca asistencia (va a la prueba de reconocimiento)");
    assert.doesNotMatch(JSON.stringify([r.salida[1], r.salida[2]]) + r.avisos.join("") + r.logs.join(""), new RegExp(CUENTA));
});

test("la 4.ª salida lleva motivo válido o null y margen null si falta; un decimal de más en el motivo no pasa", () => {
    const sinMargen = resultadoVision({ decision: "desconocido", motivo: "galeria_vacia", cuenta: null, conf: null });
    delete sinMargen.margen;
    const a = validar(sinMargen).salida[3].payload;
    assert.equal(a.motivo, "galeria_vacia");
    assert.equal(a.margen, null);
    assert.equal(validar(resultadoVision({ motivo: "Con Espacios" })).salida[3].payload.motivo, null);
    assert.equal(validar(resultadoVision({ motivo: 7 })).salida[3].payload.motivo, null);
});

test("el modo pase no sale por la 4.ª salida, ni lo que se descarta, ni un retenido", () => {
    assert.equal(validar(resultadoVision({ modo: "pase" })).salida[3] ?? null, null);
    assert.equal(validar(resultadoVision({ modo: "pase" })).salida[0].fuente, "pase");
    for (const m of [
        validar(resultadoVision({ ts: "1970-01-01T00:00:00.000+00:00" })),
        validar(resultadoVision({ decision: "otra" })),
        validar("no es JSON"),
        validar(resultadoVision(), { retain: true }),
        validar(resultadoVision({ modo: "otro" }))
    ]) {
        assert.equal(m.salida?.[3] ?? null, null);
    }
});

test("todas las decisiones del modo prueba salen por la 4.ª salida, también sin_rostro y error", () => {
    for (const decision of ["reconocido", "dudoso", "desconocido", "sin_rostro", "imagen_insuficiente", "error"]) {
        const r = validar(resultadoVision({ decision, cuenta: decision === "reconocido" ? CUENTA : null, conf: decision === "reconocido" ? 0.9 : null }));
        assert.equal(r.salida[3].payload.decision, decision);
        assert.equal(r.salida[0], null);
    }
});

// ---------------------------------------------------------------------------------------------
// Forma de los flujos
// ---------------------------------------------------------------------------------------------

function alcanzables(desde) {
    const porId = new Map(flujos.map((n) => [n.id, n]));
    const vistos = new Set();
    const pendientes = [...desde];
    while (pendientes.length > 0) {
        const id = pendientes.pop();
        if (vistos.has(id) || !porId.has(id)) {
            continue;
        }
        vistos.add(id);
        const n = porId.get(id);
        for (const salida of n.wires || []) {
            pendientes.push(...salida);
        }
        if (n.type === "link out") {
            pendientes.push(...(n.links || []));
        }
    }
    return [...vistos].map((id) => porId.get(id));
}

test("la prueba nunca escribe en la base ni publica a los actuadores: lo que alcanza el resultado es solo el estado en memoria", () => {
    const alcance = alcanzables([ID.enlaceSale]);
    assert.deepEqual(alcance.map((n) => n.id).sort(), [ID.enlaceSale, ID.enlaceEntra, ID.guardar, ID.debug].sort());
    assert.ok(!alcance.some((n) => n.type === "postgresql" || n.type === "mqtt out"));
    // El rechazo de aula/vision/enrolamiento (segundo destino del mqtt in) solo suma ese mismo nodo.
    assert.deepEqual(nodo(ID.mqttEnrolamiento).wires, [[ID.progreso, ID.guardar]]);
});

test("el POST sale solo hacia el mqtt out que ya publica aula/vision/cmd y hacia la respuesta HTTP", () => {
    assert.deepEqual(nodo(ID.pedir).wires, [[ID.publicar], [ID.respuesta]]);
    const salida = nodo(ID.publicar);
    assert.equal(salida.type, "mqtt out");
    assert.equal(salida.qos, "1");
    assert.equal(salida.retain, "false");
    assert.equal(salida.topic, "");
    const ruta = nodo(ID.entradaPost);
    assert.equal(ruta.type, "http in");
    assert.equal(ruta.method, "post");
    assert.equal(ruta.url, "/api/enrolamiento/probar");
    assert.deepEqual(ruta.wires, [[ID.pedir]]);
});

test("el GET va a la función de estado, de ahí a un SELECT parametrizado de un solo campo y a la respuesta", () => {
    const ruta = nodo(ID.entradaGet);
    assert.equal(ruta.method, "get");
    assert.equal(ruta.url, "/api/enrolamiento/prueba");
    assert.deepEqual(ruta.wires, [[ID.estado]]);
    assert.deepEqual(nodo(ID.estado).wires, [[ID.seleccionNombre], [ID.respuesta]]);
    assert.deepEqual(nodo(ID.seleccionNombre).wires, [[ID.completar]]);
    assert.deepEqual(nodo(ID.completar).wires, [[ID.respuesta]]);
    const consulta = nodo(ID.seleccionNombre);
    assert.equal(consulta.type, "postgresql");
    assert.equal(consulta.postgreSQLConfig, "a1f0c0de00000003");
    assert.equal(consulta.query.replace(/\s+/g, " ").trim(), "SELECT nombre FROM alumnos WHERE no_cuenta = $1");
    assert.doesNotMatch(consulta.query, /embedding/i);
    // Desde el GET no se alcanza ningún mqtt out (la regla de get-solo-lectura.test.js, aquí a la vista).
    assert.ok(!alcanzables([ID.entradaGet]).some((n) => n.type === "mqtt out"));
});

test("los catch cubren cada cadena y solo responden o registran el error, sin cuentas", () => {
    const http = nodo(ID.catchHttp);
    assert.equal(http.type, "catch");
    assert.equal(http.uncaught, false);
    assert.deepEqual([...http.scope].sort(), [ID.pedir, ID.estado, ID.seleccionNombre, ID.completar].sort());
    assert.deepEqual(http.wires, [[ID.responder500]]);
    assert.deepEqual(nodo(ID.responder500).wires, [[ID.respuesta]]);
    const guardar = nodo(ID.catchGuardar);
    assert.equal(guardar.type, "catch");
    assert.deepEqual(guardar.scope, [ID.guardar]);
    assert.deepEqual(guardar.wires, [[ID.registrarError]]);
    assert.deepEqual(nodo(ID.registrarError).wires, [[]]);
});

test("el enlace entre pestañas: la 4.ª salida de la validación llega al enlace de Registro, y nada más cambia en sus salidas", () => {
    const v = nodo(ID.validar);
    assert.equal(v.outputs, 4);
    assert.deepEqual(v.wires, [["a1f0c0de000000ba"], ["a1f0c0de000000b7"], ["a1f0c0de000000e0"], [ID.enlaceSale]]);
    const sale = nodo(ID.enlaceSale);
    assert.equal(sale.type, "link out");
    assert.equal(sale.z, ID.tabPase);
    assert.deepEqual(sale.links, [ID.enlaceEntra]);
    const entra = nodo(ID.enlaceEntra);
    assert.equal(entra.type, "link in");
    assert.equal(entra.z, ID.tabRegistro);
    assert.deepEqual(entra.links, [ID.enlaceSale]);
    assert.deepEqual(entra.wires, [[ID.guardar]]);
    assert.deepEqual(nodo(ID.guardar).wires, [[ID.debug]]);
});
