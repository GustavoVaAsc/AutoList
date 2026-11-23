// Pruebas de las funciones de la pestaña "Sesiones" de nodered/flows/flows.json:
//   aula/sesion/cmd (iniciar y terminar), aula/sesion/estado (publicación y reconciliación),
//   el reporte CSV (consulta, archivo de entrada, argumento del nodo exec) y el reinicio de la
//   pantalla.
// Cada función se extrae del propio flows.json y se ejecuta en un contexto vm con un `flow` de
// mentira. No prueba el nodo de Postgres (que el índice parcial acepte el CTE de iniciar es de la
// placa), ni el broker, ni el nodo exec: eso es de la placa. Los nombres y las cuentas de aquí
// son entradas de prueba de las funciones, no datos de la lista real.
// Sin dependencias: node --test nodered/pruebas/*.test.js
"use strict";
const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const flujos = JSON.parse(fs.readFileSync(path.join(__dirname, "..", "flows", "flows.json"), "utf8"));
const ID = {
    tab: "a1f0c0de00000090",
    entrada: "a1f0c0de00000092",
    validar: "a1f0c0de00000093",
    insertar: "a1f0c0de00000094",
    estadoIniciar: "a1f0c0de00000095",
    cerrar: "a1f0c0de00000096",
    estadoTerminar: "a1f0c0de00000097",
    descartados: "a1f0c0de00000098",
    publicarEstado: "a1f0c0de0000009a",
    publicarAsistencia: "a1f0c0de000000d4",
    alConectar: "a1f0c0de0000009c",
    seleccionar: "a1f0c0de0000009d",
    estadoBase: "a1f0c0de0000009e",
    reintentar: "a1f0c0de000000a0",
    pedirReporte: "a1f0c0de000000a2",
    colaReportes: "a1f0c0de000000af",
    consultaReporte: "a1f0c0de000000a3",
    prepararEntrada: "a1f0c0de000000a4",
    escribirEntrada: "a1f0c0de000000a5",
    argumento: "a1f0c0de000000a6",
    exec: "a1f0c0de000000a7",
    resultado: "a1f0c0de000000a8",
    registrarError: "a1f0c0de000000ab",
    reiniciar: "a1f0c0de000000ad",
    publicarReinicio: "a1f0c0de000000ae"
};

function nodo(id) {
    const n = flujos.find((x) => x.id === id);
    assert.ok(n, "no existe el nodo " + id);
    return n;
}

// Contexto `flow` de mentira (una copia por JSON, como un contexto que sí persiste).
function nuevoFlow() {
    const datos = new Map();
    return {
        get: (k) => (datos.has(k) ? JSON.parse(datos.get(k)) : undefined),
        set: (k, v) => {
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

// Ejecuta la función del nodo `id`. Devuelve { salida, avisos, logs, flow, global }; la salida se copia
// por JSON (los objetos del contexto vm son de otro realm). Una excepción se propaga.
// `ahora` (ms) fija Date.now() para probar el límite de iniciar.
function ejecutar(id, msg, { flow = nuevoFlow(), ahora, global = nuevoFlow() } = {}) {
    const n = nodo(id);
    assert.equal(n.type, "function");
    const avisos = [];
    const logs = [];
    const contexto = vm.createContext({ Buffer, Date: ahora === undefined ? Date : relojFijo(ahora), Math, console });
    const funcion = new vm.Script("(function (msg, node, flow, global) {" + n.func + "\n})").runInContext(contexto);
    const nodoFalso = { warn: (t) => avisos.push(String(t)), log: (t) => logs.push(String(t)), error() {} };
    const salida = funcion(msg, nodoFalso, flow, global);
    return { salida: salida === undefined || salida === null ? salida : JSON.parse(JSON.stringify(salida)), avisos, logs, flow, global };
}

const MATERIA = "Fundamentos de Sistemas Embebidos";
const INICIO = "2026-11-05T15:30:12+00:00";

function orden(objeto, extra = {}) {
    return { topic: "aula/sesion/cmd", payload: typeof objeto === "string" ? objeto : JSON.stringify(objeto), retain: false, ...extra };
}
function iniciar(extra = {}) {
    return { accion: "iniciar", materia: MATERIA, grupo: "1", tol_min: 10, ...extra };
}
// Cada `validar` sin `flow` ni `ahora` usa un contexto nuevo: el límite de iniciar no interfiere.
const validar = (m, opciones) => ejecutar(ID.validar, m, opciones);

function assertDescarte(r, fragmento) {
    assert.equal(r.salida[0], null, "no debe iniciar");
    assert.equal(r.salida[1], null, "no debe terminar");
    assert.ok(r.salida[2], "debe salir por el descarte");
    assert.match(r.salida[2].payload, /^Descartado, /);
    if (fragmento) {
        assert.match(r.salida[2].payload, fragmento);
    }
}

// ---------------------------------------------------------------------------------------------
// Validar orden de sesión
// ---------------------------------------------------------------------------------------------

test("iniciar válido sale por la salida 1 con los parámetros y sin el mensaje original", () => {
    const r = validar(orden(iniciar()));
    assert.deepEqual(r.salida[0].params, [MATERIA, "1", 10]);
    assert.equal(r.salida[0].fuente, "iniciar");
    assert.equal(r.salida[0].topic, undefined);
    assert.equal(r.salida[0].retain, undefined);
    assert.equal(r.salida[1], null);
    assert.equal(r.salida[2], null);
});

test("iniciar normaliza a NFC y recorta espacios", () => {
    const r = validar(orden(iniciar({ materia: "  Café  ", grupo: " 1 " })));
    assert.deepEqual(r.salida[0].params, ["Café", "1", 10]);
});

test("tol_min acepta 0 y 600", () => {
    assert.equal(validar(orden(iniciar({ tol_min: 0 }))).salida[0].params[2], 0);
    assert.equal(validar(orden(iniciar({ tol_min: 600 }))).salida[0].params[2], 600);
});

test("tol_min fuera de rango o que no es entero se descarta", () => {
    for (const malo of [-1, 601, 10.5, "10", null, true, [10], {}, Infinity, 1e21]) {
        assertDescarte(validar(orden(iniciar({ tol_min: malo }))), /tol_min/);
    }
    const sin = iniciar();
    delete sin.tol_min;
    assertDescarte(validar(orden(sin)), /tol_min/);
});

test("materia y grupo deben ser texto no vacío", () => {
    for (const campo of ["materia", "grupo"]) {
        for (const malo of [undefined, null, 7, true, [], {}, "", "   ", "\u00a0 "]) {
            const cuerpo = iniciar();
            if (malo === undefined) {
                delete cuerpo[campo];
            } else {
                cuerpo[campo] = malo;
            }
            assertDescarte(validar(orden(cuerpo)), new RegExp(campo));
        }
    }
});

test("materia y grupo tienen tope de 40 caracteres", () => {
    assert.ok(validar(orden(iniciar({ materia: "m".repeat(40) }))).salida[0]);
    assertDescarte(validar(orden(iniciar({ materia: "m".repeat(41) }))), /materia/);
    assert.ok(validar(orden(iniciar({ grupo: "g".repeat(40) }))).salida[0]);
    assertDescarte(validar(orden(iniciar({ grupo: "g".repeat(41) }))), /grupo/);
});

test("materia y grupo sin caracteres de control ni invisibles", () => {
    for (const campo of ["materia", "grupo"]) {
        for (const malo of ["a\nb", "a\tb", "a\u0000b", "a\u202eb", "a\u200bb", "a\u2028b", "a\u2029b", "a\u007fb", "a\u0085b", "a\ufeffb"]) {
            assertDescarte(validar(orden(iniciar({ [campo]: malo }))), new RegExp(campo));
        }
    }
});

test("un iniciar cuyo estado no cabría en el búfer de 256 bytes de la XIAO se descarta", () => {
    // Cada \u{1d4d0} pesa 4 bytes en UTF-8: 40 + 40 caracteres superan el tope del estado retenido.
    const largo = "\u{1d4d0}".repeat(40);
    assertDescarte(validar(orden(iniciar({ materia: largo, grupo: largo }))), /estado/);
    // El peor caso ASCII sí cabe.
    assert.ok(validar(orden(iniciar({ materia: "m".repeat(40), grupo: "g".repeat(40) }))).salida[0]);
});

// Límite de iniciar (CWE-770): un iniciar es una escritura en la base y, al cerrar una olvidada, un
// reporte con un proceso de Python. Un segundo iniciar en menos de 5 s se descarta; eso cubre
// también la reentrega duplicada de un QoS 1.
const T0 = 1_800_000_000_000;

test("un segundo iniciar en menos de 5 s se descarta con aviso y sin contenido", () => {
    const flow = nuevoFlow();
    assert.ok(validar(orden(iniciar()), { flow, ahora: T0 }).salida[0]);
    for (const dt of [0, 1, 1000, 4999]) {
        const r = validar(orden(iniciar({ materia: "TextoSecretoDeLaRed" })), { flow, ahora: T0 + dt });
        assertDescarte(r, /demasiado seguido/);
        assert.equal(r.avisos.length, 1);
        assert.doesNotMatch(r.avisos[0] + r.salida[2].payload, /TextoSecreto|Fundamentos/);
    }
});

test("la reentrega duplicada de un iniciar (QoS 1) se descarta", () => {
    const flow = nuevoFlow();
    const m = orden(iniciar());
    assert.ok(validar(m, { flow, ahora: T0 }).salida[0]);
    assertDescarte(validar(m, { flow, ahora: T0 + 20 }), /demasiado seguido/);
});

test("pasados 5 s, el siguiente iniciar vuelve a aceptarse", () => {
    const flow = nuevoFlow();
    validar(orden(iniciar()), { flow, ahora: T0 });
    assert.ok(validar(orden(iniciar()), { flow, ahora: T0 + 5000 }).salida[0]);
});

test("un iniciar descartado por el límite no renueva la ventana (un goteo no bloquea para siempre)", () => {
    const flow = nuevoFlow();
    validar(orden(iniciar()), { flow, ahora: T0 });
    assertDescarte(validar(orden(iniciar()), { flow, ahora: T0 + 3000 }), /demasiado seguido/);
    assert.ok(validar(orden(iniciar()), { flow, ahora: T0 + 5000 }).salida[0]);
});

test("un iniciar inválido no consume la ventana", () => {
    const flow = nuevoFlow();
    assertDescarte(validar(orden(iniciar({ tol_min: 601 })), { flow, ahora: T0 }), /tol_min/);
    assert.ok(validar(orden(iniciar()), { flow, ahora: T0 + 1 }).salida[0]);
});

test("un reloj que salta hacia atrás no bloquea: solo se descarta si pasó entre 0 y 5 s", () => {
    // Date.now() puede saltar (RTC, NTP) y la función no tiene performance ni process (mismo límite
    // que el vencimiento del registro, que usa ticks). Un salto hacia atrás deja el último en el
    // futuro: se acepta en vez de bloquear hasta alcanzarlo. Un salto hacia adelante también acepta.
    const flow = nuevoFlow();
    validar(orden(iniciar()), { flow, ahora: T0 });
    assert.ok(validar(orden(iniciar()), { flow, ahora: T0 - 3_600_000 }).salida[0]);
    assert.ok(validar(orden(iniciar()), { flow, ahora: T0 + 3_600_000 }).salida[0]);
});

test("terminar no está limitado y no cuenta para la ventana", () => {
    const flow = nuevoFlow();
    validar(orden(iniciar()), { flow, ahora: T0 });
    for (const dt of [0, 1, 2]) {
        assert.ok(validar(orden({ accion: "terminar" }), { flow, ahora: T0 + dt }).salida[1]);
    }
    const otro = nuevoFlow();
    validar(orden({ accion: "terminar" }), { flow: otro, ahora: T0 });
    assert.ok(validar(orden(iniciar()), { flow: otro, ahora: T0 + 1 }).salida[0]);
});

test("el instante del último iniciar vive en memoria (se pierde al reiniciar a propósito)", () => {
    const flow = nuevoFlow();
    validar(orden(iniciar()), { flow, ahora: T0 });
    assert.equal(flow.get("ultimo_iniciar_ms"), T0);
});

test("terminar sale por la salida 2 y ignora campos de más", () => {
    for (const cuerpo of [{ accion: "terminar" }, { accion: "terminar", id: 99, materia: "x" }]) {
        const r = validar(orden(cuerpo));
        assert.equal(r.salida[0], null);
        assert.equal(r.salida[2], null);
        assert.equal(r.salida[1].fuente, "terminar");
        assert.equal(r.salida[1].params, undefined);
    }
});

test("marcar es una propuesta: se descarta con aviso y no publica", () => {
    const r = validar(orden({ accion: "marcar", cuenta: "320068234" }));
    assertDescarte(r, /marcar/);
    assert.equal(r.avisos.length, 1);
    assert.doesNotMatch(r.avisos[0] + r.salida[2].payload, /320068234/);
});

test("una accion ausente, desconocida o con otra capitalización se descarta", () => {
    for (const cuerpo of [{}, { accion: "INICIAR", materia: "a", grupo: "b", tol_min: 1 }, { accion: "pausar" }, { accion: 1 }, { accion: ["iniciar"] }, { accion: null }]) {
        assertDescarte(validar(orden(cuerpo)), /accion/);
    }
});

test("el retain se descarta con aviso, aunque el mensaje sea válido", () => {
    const r = validar(orden(iniciar(), { retain: true }));
    assertDescarte(r, /retenido/);
    assert.equal(r.avisos.length, 1);
    assert.match(r.avisos[0], /aula\/sesion\/cmd/);
    const t = validar(orden({ accion: "terminar" }, { retain: true }));
    assertDescarte(t, /retenido/);
});

test("el payload vacío se ignora en silencio (es el borrado de la limpieza de retenidos)", () => {
    for (const vacio of ["", Buffer.alloc(0)]) {
        const r = validar({ topic: "aula/sesion/cmd", payload: vacio, retain: false });
        assert.equal(r.salida, null);
        assert.deepEqual(r.avisos, []);
    }
});

test("JSON inválido, no objeto, no texto o demasiado grande se descartan", () => {
    assertDescarte(validar(orden("{no es json")), /JSON/);
    for (const crudo of ["[]", "null", "7", "\"iniciar\"", "true"]) {
        assertDescarte(validar(orden(crudo)), /objeto/);
    }
    assertDescarte(validar({ topic: "aula/sesion/cmd", payload: { accion: "terminar" }, retain: false }), /texto/);
    assertDescarte(validar({ topic: "aula/sesion/cmd", payload: Buffer.from("{}"), retain: false }), /texto/);
    assertDescarte(validar(orden(JSON.stringify({ accion: "terminar", relleno: "x".repeat(2000) }))), /caracteres/);
});

test("el descarte y el aviso nunca repiten el contenido del mensaje", () => {
    const secreto = "TextoSecretoDeLaRed";
    const casos = [
        orden({ accion: secreto }),
        orden(iniciar({ materia: secreto + "\nx" })),
        orden(iniciar({ grupo: secreto.repeat(10) })),
        orden(iniciar({ tol_min: secreto })),
        orden(secreto),
        orden(iniciar(), { retain: true })
    ];
    for (const caso of casos) {
        const r = validar(caso);
        assert.doesNotMatch(JSON.stringify(r.salida) + r.avisos.join(" ") + r.logs.join(" "), new RegExp(secreto));
    }
});

// ---------------------------------------------------------------------------------------------
// Estado tras iniciar
// ---------------------------------------------------------------------------------------------

const FILA_INICIO = { id: 12, materia: "FSE", grupo: "1", inicio: INICIO, tol_min: 10, cerrada_id: null, faltantes: 10 };

test("tras iniciar se publica el estado del contrato, la asistencia inicial y no hay reporte", () => {
    const r = ejecutar(ID.estadoIniciar, { payload: [FILA_INICIO], params: ["FSE", "1", 10] });
    assert.deepEqual(JSON.parse(r.salida[0].payload), {
        activa: true, id: 12, materia: "FSE", grupo: "1", inicio: INICIO, tol_min: 10
    });
    assert.equal(r.salida[1], null);
    assert.deepEqual(JSON.parse(r.salida[2].payload), {
        activa: true, id: 12, presentes: 0, retardos: 0, faltantes: 10, ultimo: null
    });
    assert.equal(r.salida[0].topic, undefined, "el tópico, el QoS y el retain los fija el mqtt out");
    assert.equal(r.salida[2].topic, undefined);
});

test("el estado publicado cabe en el búfer de 256 bytes de la XIAO con el peor caso aceptado", () => {
    const m = "m".repeat(40);
    const g = "g".repeat(40);
    const r = ejecutar(ID.estadoIniciar, { payload: [{ ...FILA_INICIO, id: 2147483647, materia: m, grupo: g, tol_min: 600 }] });
    // 3 de cabecera fija + 2 de longitud del tópico + 18 del tópico + 2 del id de paquete.
    assert.ok(Buffer.byteLength(r.salida[0].payload) + 3 + 2 + 18 + 2 < 256);
});

// Las miniaturas del pase de lista (contexto global `miniaturas_pase`, solo en memoria) duran lo que la sesión.
const CON_FOTOS = { sesion_id: 12, fotos: { "320068234": "QUJD" } };

test("iniciar una sesión borra las miniaturas del pase de lista de la anterior", () => {
    const global = nuevoFlow();
    global.set("miniaturas_pase", CON_FOTOS);
    ejecutar(ID.estadoIniciar, { payload: [{ ...FILA_INICIO, id: 13, cerrada_id: 12 }] }, { global });
    assert.equal(global.get("miniaturas_pase"), undefined);
});

test("si iniciar falla, las miniaturas de la sesión abierta se conservan", () => {
    const global = nuevoFlow();
    global.set("miniaturas_pase", CON_FOTOS);
    assert.throws(() => ejecutar(ID.estadoIniciar, { payload: [] }, { global }));
    assert.deepEqual(global.get("miniaturas_pase"), CON_FOTOS);
});

test("si iniciar cerró una sesión olvidada, pide su reporte", () => {
    const r = ejecutar(ID.estadoIniciar, { payload: [{ ...FILA_INICIO, id: 13, cerrada_id: 12 }] });
    assert.equal(JSON.parse(r.salida[0].payload).id, 13);
    assert.deepEqual(r.salida[1], { sesion_id: 12 });
    assert.equal(JSON.parse(r.salida[2].payload).id, 13);
    assert.ok(r.logs.some((l) => /olvidada/.test(l)));
});

test("tras iniciar, una respuesta sin una fila válida no publica nada: lanza", () => {
    for (const payload of [[], undefined, null, "x", [FILA_INICIO, FILA_INICIO], [{ ...FILA_INICIO, id: "12" }], [{ ...FILA_INICIO, id: 0 }], [{ ...FILA_INICIO, faltantes: -1 }], [{ ...FILA_INICIO, faltantes: null }]]) {
        assert.throws(() => ejecutar(ID.estadoIniciar, { payload }));
    }
});

// ---------------------------------------------------------------------------------------------
// Estado tras terminar
// ---------------------------------------------------------------------------------------------

test("terminar con sesión abierta publica inactivo en estado y asistencia y pide el reporte", () => {
    const r = ejecutar(ID.estadoTerminar, { payload: [{ id: 12 }], fuente: "terminar" });
    assert.deepEqual(JSON.parse(r.salida[0].payload), { activa: false, id: 12 });
    assert.deepEqual(r.salida[1], { sesion_id: 12 });
    assert.equal(r.salida[2], null);
    assert.deepEqual(JSON.parse(r.salida[3].payload), { activa: false });
});

test("terminar con sesión abierta borra las miniaturas del pase de lista", () => {
    const global = nuevoFlow();
    global.set("miniaturas_pase", CON_FOTOS);
    ejecutar(ID.estadoTerminar, { payload: [{ id: 12 }], fuente: "terminar" }, { global });
    assert.equal(global.get("miniaturas_pase"), undefined);
});

test("terminar sin sesión abierta no cambia nada: sin reporte y republica el estado real", () => {
    const r = ejecutar(ID.estadoTerminar, { payload: [], fuente: "terminar" });
    assert.equal(r.salida[0], null, "no publica un estado inventado");
    assert.equal(r.salida[1], null, "no hay reporte");
    assert.equal(r.salida[2].payload, null);
    assert.equal(r.salida[2].fuente, "reconciliar", "el SELECT lleva el estado de la base al broker");
    assert.equal(r.salida[3], null);
    assert.ok(r.logs.length >= 1);
});

test("terminar: una respuesta imposible lanza", () => {
    for (const payload of [undefined, null, "x", [{ id: 1 }, { id: 2 }], [{ id: "1" }], [{}]]) {
        assert.throws(() => ejecutar(ID.estadoTerminar, { payload }));
    }
});

// ---------------------------------------------------------------------------------------------
// Reconciliación
// ---------------------------------------------------------------------------------------------

test("al conectar (punto verde) se marca la reconciliación pendiente y se pide el estado", () => {
    const flow = nuevoFlow();
    const r = ejecutar(ID.alConectar, { status: { fill: "green", shape: "dot", text: "node-red:common.status.connected" } }, { flow });
    assert.equal(r.salida.payload, null);
    assert.equal(r.salida.fuente, "reconciliar");
    assert.equal(flow.get("reconciliar_pendiente"), true);
});

test("otros estados del mqtt out no reconcilian", () => {
    for (const status of [{ fill: "red", shape: "ring" }, { fill: "yellow", shape: "ring" }, { fill: "green", shape: "ring" }, {}, undefined]) {
        const flow = nuevoFlow();
        const r = ejecutar(ID.alConectar, { status }, { flow });
        assert.equal(r.salida, null);
        assert.equal(flow.get("reconciliar_pendiente"), undefined);
    }
});

test("la base con una sesión abierta publica activa:true en estado y asistencia con la forma del contrato", () => {
    const flow = nuevoFlow();
    flow.set("reconciliar_pendiente", true);
    const r = ejecutar(ID.estadoBase, {
        payload: [{
            id: 12, materia: "FSE", grupo: "1", inicio: INICIO, tol_min: 10, abierta: true,
            presentes: 2, retardos: 1, faltantes: 7, ultimo_nombre: "Ana López", ultimo_estado: "presente"
        }]
    }, { flow });
    assert.deepEqual(JSON.parse(r.salida[0].payload), { activa: true, id: 12, materia: "FSE", grupo: "1", inicio: INICIO, tol_min: 10 });
    assert.deepEqual(JSON.parse(r.salida[1].payload), {
        activa: true, id: 12, presentes: 2, retardos: 1, faltantes: 7, ultimo: { nombre: "Ana López", estado: "presente" }
    });
    assert.equal(flow.get("reconciliar_pendiente"), undefined, "ya no hace falta reintentar");
    const sinUltimo = ejecutar(ID.estadoBase, {
        payload: [{
            id: 12, materia: "FSE", grupo: "1", inicio: INICIO, tol_min: 10, abierta: true,
            presentes: 0, retardos: 0, faltantes: 10, ultimo_nombre: null, ultimo_estado: null
        }]
    });
    assert.equal(JSON.parse(sinUltimo.salida[1].payload).ultimo, null);
});

test("la base sin sesión abierta publica inactivo con el id de la última y limpia asistencia", () => {
    const r = ejecutar(ID.estadoBase, { payload: [{ id: 12, materia: "FSE", grupo: "1", inicio: INICIO, tol_min: 10, abierta: false }] });
    assert.deepEqual(JSON.parse(r.salida[0].payload), { activa: false, id: 12 });
    assert.deepEqual(JSON.parse(r.salida[1].payload), { activa: false });
});

test("la base sin ninguna sesión publica inactivo sin id y limpia asistencia", () => {
    const r = ejecutar(ID.estadoBase, { payload: [] });
    assert.deepEqual(JSON.parse(r.salida[0].payload), { activa: false });
    assert.deepEqual(JSON.parse(r.salida[1].payload), { activa: false });
});

test("el estado de la base: una respuesta imposible lanza y deja la reconciliación pendiente", () => {
    const baseAbierta = { id: 1, materia: "FSE", grupo: "1", inicio: INICIO, tol_min: 10, abierta: true, presentes: 0, retardos: 0, faltantes: 10, ultimo_nombre: null, ultimo_estado: null };
    for (const payload of [
        undefined, null, "x", [{ id: 1, abierta: true }, { id: 2, abierta: false }], [{ id: "1", abierta: true }],
        [{ ...baseAbierta, presentes: -1 }], [{ ...baseAbierta, ultimo_nombre: "Ana", ultimo_estado: "falta" }],
        [{ ...baseAbierta, ultimo_nombre: null, ultimo_estado: "presente" }]
    ]) {
        const flow = nuevoFlow();
        flow.set("reconciliar_pendiente", true);
        assert.throws(() => ejecutar(ID.estadoBase, { payload }, { flow }));
        assert.equal(flow.get("reconciliar_pendiente"), true);
    }
});

test("cada 15 s solo reintenta si la reconciliación quedó pendiente", () => {
    const sin = ejecutar(ID.reintentar, { payload: 1 });
    assert.equal(sin.salida, null);
    const flow = nuevoFlow();
    flow.set("reconciliar_pendiente", true);
    const con = ejecutar(ID.reintentar, { payload: 1 }, { flow });
    assert.equal(con.salida.fuente, "reconciliar");
    assert.equal(con.salida.payload, null);
});

// ---------------------------------------------------------------------------------------------
// Errores
// ---------------------------------------------------------------------------------------------

test("un fallo de iniciar o terminar registra el SQLSTATE y reconcilia: no se publica un estado falso", () => {
    for (const fuente of ["iniciar", "terminar"]) {
        const r = ejecutar(ID.registrarError, { fuente, error: { code: "23505", message: "duplicate key ... (materia secreta)" }, params: ["Materia secreta", "1", 10] });
        assert.equal(r.salida.fuente, "reconciliar");
        assert.equal(r.salida.payload, null);
        assert.equal(r.avisos.length, 1);
        assert.match(r.avisos[0], /SQLSTATE 23505/);
        assert.doesNotMatch(r.avisos.join(" "), /secreta/);
    }
});

test("un fallo de la reconciliación la deja pendiente y no se reconcilia en bucle", () => {
    const flow = nuevoFlow();
    const r = ejecutar(ID.registrarError, { fuente: "reconciliar", error: { code: "08006" } }, { flow });
    assert.equal(r.salida, null);
    assert.equal(flow.get("reconciliar_pendiente"), true);
});

test("un fallo del reporte solo se registra", () => {
    const flow = nuevoFlow();
    const r = ejecutar(ID.registrarError, { sesion_id: 12, error: { message: "boom Ana López 320068234" } }, { flow });
    assert.equal(r.salida, null);
    assert.equal(flow.get("reconciliar_pendiente"), undefined);
    assert.doesNotMatch(r.avisos.join(" "), /320068234/);
});

test("un error sin código ni mensaje se registra como desconocido", () => {
    const r = ejecutar(ID.registrarError, { fuente: "terminar" });
    assert.match(r.avisos[0], /error desconocido/);
});

// ---------------------------------------------------------------------------------------------
// Reporte
// ---------------------------------------------------------------------------------------------

test("pedir reporte acepta solo un entero positivo del tamaño de un INT", () => {
    const r = ejecutar(ID.pedirReporte, { sesion_id: 12 });
    assert.deepEqual(r.salida, { sesion_id: 12, params: [12], payload: null });
    assert.deepEqual(ejecutar(ID.pedirReporte, { sesion_id: 2147483647 }).salida.params, [2147483647]);
    for (const malo of [0, -1, 1.5, "12", "12; rm -rf /", null, undefined, NaN, Infinity, 2147483648, true, [12], {}]) {
        assert.throws(() => ejecutar(ID.pedirReporte, { sesion_id: malo }), undefined, String(malo));
    }
});

const FILAS = [
    { no_cuenta: "320068234", nombre: "Ana López", estado: "presente", hora: "2026-11-05 09:12:33", extra: "no pasa" },
    { no_cuenta: "320068235", nombre: "=HYPERLINK(\"x\")", estado: "falta", hora: null }
];

test("preparar la entrada del reporte copia solo los cuatro campos y fija un nombre de archivo fijo", () => {
    const r = ejecutar(ID.prepararEntrada, { sesion_id: 12, payload: FILAS });
    assert.equal(r.salida.filename, "/data/reportes/.entrada-12.json");
    assert.deepEqual(JSON.parse(r.salida.payload), {
        sesion: 12,
        filas: [
            { no_cuenta: "320068234", nombre: "Ana López", estado: "presente", hora: "2026-11-05 09:12:33" },
            { no_cuenta: "320068235", nombre: "=HYPERLINK(\"x\")", estado: "falta", hora: null }
        ]
    });
    assert.equal(r.salida.sesion_id, 12);
});

test("un grupo sin alumnos produce una entrada con cero filas (el CSV queda con el encabezado)", () => {
    const r = ejecutar(ID.prepararEntrada, { sesion_id: 12, payload: [] });
    assert.deepEqual(JSON.parse(r.salida.payload), { sesion: 12, filas: [] });
});

test("preparar la entrada: respuesta que no es un arreglo, id inválido o demasiadas filas lanzan", () => {
    assert.throws(() => ejecutar(ID.prepararEntrada, { sesion_id: 12, payload: null }));
    assert.throws(() => ejecutar(ID.prepararEntrada, { sesion_id: 12, payload: "x" }));
    assert.throws(() => ejecutar(ID.prepararEntrada, { sesion_id: "12", payload: [] }));
    assert.throws(() => ejecutar(ID.prepararEntrada, { sesion_id: 12, payload: new Array(1001).fill(FILAS[0]) }));
});

test("al comando del exec solo llega el id entero: nunca texto de MQTT ni de HTTP", () => {
    const r = ejecutar(ID.argumento, { sesion_id: 12, payload: "{\"sesion\":12,\"filas\":[...]}", topic: "aula/sesion/cmd" });
    assert.equal(r.salida.payload, "12");
    assert.match(r.salida.payload, /^[1-9][0-9]{0,9}$/);
    for (const malo of [0, -3, 1.5, "12", "12; rm -rf /", "$(id)", null, undefined, 2147483648]) {
        assert.throws(() => ejecutar(ID.argumento, { sesion_id: malo, payload: "x" }), undefined, String(malo));
    }
});

test("el resultado del reporte registra el código sin datos de alumnos", () => {
    const bien = ejecutar(ID.resultado, { sesion_id: 12, payload: { code: 0 } });
    assert.equal(bien.avisos.length, 0);
    assert.match(bien.logs.join(" "), /sesión 12/);
    const mal = ejecutar(ID.resultado, { sesion_id: 12, payload: { code: 1 } });
    assert.equal(mal.avisos.length, 1);
    assert.match(mal.avisos[0], /sesión 12.*código 1/);
    const senal = ejecutar(ID.resultado, { sesion_id: 12, payload: { code: null, signal: "SIGKILL" } });
    assert.match(senal.avisos[0], /SIGKILL/);
    const raro = ejecutar(ID.resultado, { sesion_id: 12, payload: "Ana López" });
    assert.equal(raro.avisos.length, 1);
    assert.doesNotMatch(raro.avisos[0], /Ana/);
});

// ---------------------------------------------------------------------------------------------
// Estructura de la pestaña (lo que el editor y el contrato exigen)
// ---------------------------------------------------------------------------------------------

const enPestana = flujos.filter((n) => n.z === ID.tab);

test("la pestaña Sesiones existe y todos los cables apuntan a nodos de la pestaña", () => {
    const tab = nodo(ID.tab);
    assert.equal(tab.type, "tab");
    assert.equal(tab.label, "Sesiones");
    const ids = new Set(enPestana.map((n) => n.id));
    for (const n of enPestana) {
        for (const salida of n.wires || []) {
            for (const destino of salida) {
                assert.ok(ids.has(destino), n.id + " apunta a " + destino + ", que no está en la pestaña");
            }
        }
        for (const objetivo of n.scope || []) {
            assert.ok(ids.has(objetivo), n.id + " tiene en su alcance a " + objetivo);
        }
    }
});

test("los ids de toda la hoja de flujos son únicos", () => {
    const ids = flujos.map((n) => n.id);
    assert.equal(new Set(ids).size, ids.length);
});

test("el catch cubre cada función, consulta, archivo y exec de la cadena (menos el propio registro)", () => {
    const catchNodo = enPestana.find((n) => n.type === "catch");
    assert.ok(catchNodo);
    const esperado = enPestana
        .filter((n) => ["function", "postgresql", "file", "exec"].includes(n.type) && n.id !== ID.registrarError)
        .map((n) => n.id)
        .sort();
    assert.deepEqual([...catchNodo.scope].sort(), esperado);
});

test("las consultas son fijas, con parámetros enlazados y sin plantillas de texto", () => {
    const consultas = enPestana.filter((n) => n.type === "postgresql");
    assert.equal(consultas.length, 4);
    for (const c of consultas) {
        assert.doesNotMatch(c.query, /\{\{/, c.name + " no debe interpolar texto");
        assert.doesNotMatch(c.query, /\bDELETE\b|\bTRUNCATE\b|\bDROP\b|\bALTER\b/i);
        assert.equal(c.postgreSQLConfig, "a1f0c0de00000003");
    }
});

test("iniciar es una sola sentencia: cierra la olvidada, inserta, y el INSERT depende del UPDATE", () => {
    const q = nodo(ID.insertar).query;
    assert.match(q, /UPDATE sesiones SET fin = now\(\) WHERE fin IS NULL/);
    assert.match(q, /INSERT INTO sesiones \(materia, grupo, tolerancia_min\)/);
    // El INSERT lee de `previa` (un agregado sobre el UPDATE): así el UPDATE termina antes.
    assert.match(q, /FROM previa/);
    assert.match(q, /\$1::text, \$2::text, \$3::int/);
    assert.doesNotMatch(q, /\$4/);
    assert.match(nodo(ID.cerrar).query, /^UPDATE sesiones SET fin = now\(\) WHERE fin IS NULL RETURNING id$/);
});

test("los reportes pasan por una cola de a lo más 1 mensaje cada 2 s antes de la consulta, el archivo y el exec", () => {
    const d = nodo(ID.colaReportes);
    assert.equal(d.type, "delay");
    assert.equal(d.pauseType, "rate");
    assert.equal(d.rate, "1");
    assert.equal(d.nbRateUnits, "2");
    assert.equal(d.rateUnits, "second");
    assert.equal(d.drop, false, "encola y no descarta");
    // Pedir reporte → cola → consulta; y la consulta solo la alimenta la cola.
    assert.deepEqual(nodo(ID.pedirReporte).wires, [[ID.colaReportes]]);
    assert.deepEqual(d.wires, [[ID.consultaReporte]]);
    const alimentan = flujos.filter((n) => (n.wires || []).some((salida) => salida.includes(ID.consultaReporte)));
    assert.deepEqual(alimentan.map((n) => n.id), [ID.colaReportes]);
});

test("lo que espera en la cola es lo mínimo: el id y los parámetros, sin filas ni nombres", () => {
    const r = ejecutar(ID.pedirReporte, { sesion_id: 12, payload: FILAS, topic: "x", req: { a: 1 }, error: {} });
    assert.deepEqual(Object.keys(r.salida).sort(), ["params", "payload", "sesion_id"]);
    assert.equal(r.salida.payload, null);
});

test("el reporte usa la consulta del plan (7.3) con la zona explícita en la hora", () => {
    const q = nodo(ID.consultaReporte).query;
    assert.match(q, /LEFT JOIN asistencias/);
    assert.match(q, /COALESCE\(s\.estado, 'falta'\)/);
    assert.match(q, /WHERE a\.grupo = \(SELECT grupo FROM sesiones WHERE id = \$1::int\)/);
    assert.match(q, /AT TIME ZONE 'America\/Mexico_City'/);
    assert.doesNotMatch(q, /embedding/);
});

test("el único nodo exec tiene un comando fijo y solo recibe el argumento validado", () => {
    const execs = flujos.filter((n) => n.type === "exec");
    assert.equal(execs.length, 1);
    const e = execs[0];
    assert.equal(e.id, ID.exec);
    assert.equal(e.command, "/usr/bin/python3 -I -B /etc/nodered/reporte_csv.py");
    assert.doesNotMatch(e.command, /[;&|`$<>(){}\\"']/);
    assert.equal(e.addpay, "payload");
    assert.equal(e.append, "");
    assert.equal(e.useSpawn, "true", "sin shell");
    // Lo único que lo alimenta es "Argumento del reporte".
    const entradas = flujos.filter((n) => (n.wires || []).some((salida) => salida.includes(ID.exec)));
    assert.deepEqual(entradas.map((n) => n.id), [ID.argumento]);
    // Y a ese solo lo alimenta el nodo file que escribió la entrada.
    const previos = flujos.filter((n) => (n.wires || []).some((salida) => salida.includes(ID.argumento)));
    assert.deepEqual(previos.map((n) => n.id), [ID.escribirEntrada]);
});

test("el nodo file escribe en /data/reportes por una ruta que fija la función", () => {
    const f = nodo(ID.escribirEntrada);
    assert.equal(f.type, "file");
    assert.equal(f.filenameType, "msg");
    assert.equal(f.filename, "filename");
    assert.equal(f.createDir, true);
    assert.equal(f.overwriteFile, "true");
    assert.match(nodo(ID.prepararEntrada).func, /"\/data\/reportes\/\.entrada-" \+ id \+ "\.json"/);
});

test("el estado y la asistencia de sesión se publican retenidos con QoS 1 y en los tópicos del contrato", () => {
    const m = nodo(ID.publicarEstado);
    assert.equal(m.type, "mqtt out");
    assert.equal(m.topic, "aula/sesion/estado");
    assert.equal(m.qos, "1");
    assert.equal(m.retain, "true");
    assert.equal(m.broker, "a1f0c0de00000002");
    const a = nodo(ID.publicarAsistencia);
    assert.equal(a.type, "mqtt out");
    assert.equal(a.topic, "aula/sesion/asistencia");
    assert.equal(a.qos, "1");
    assert.equal(a.retain, "true");
    assert.equal(a.broker, "a1f0c0de00000002");
});

test("aula/sesion/cmd se consume con QoS 1 y el nodo de entrada va a la validación", () => {
    const m = nodo(ID.entrada);
    assert.equal(m.type, "mqtt in");
    assert.equal(m.topic, "aula/sesion/cmd");
    assert.equal(m.qos, "1");
    assert.equal(m.datatype, "utf8");
    assert.deepEqual(m.wires, [[ID.validar]]);
});

test("el inject Reiniciar pantalla publica {\"reiniciar\":1} en aula/pantalla/reinicio con QoS 1 y sin retain, solo a mano", () => {
    const i = nodo(ID.reiniciar);
    assert.equal(i.type, "inject");
    assert.equal(i.payload, "{\"reiniciar\":1}");
    assert.equal(i.payloadType, "str");
    assert.deepEqual(JSON.parse(i.payload), { reiniciar: 1 });
    assert.equal(i.repeat, "");
    assert.equal(i.crontab, "");
    assert.equal(i.once, false);
    assert.deepEqual(i.wires, [[ID.publicarReinicio]]);
    const m = nodo(ID.publicarReinicio);
    assert.equal(m.type, "mqtt out");
    assert.equal(m.topic, "aula/pantalla/reinicio");
    assert.equal(m.qos, "1");
    assert.equal(m.retain, "false");
});

test("no hay endpoints HTTP en la pestaña de sesiones", () => {
    assert.deepEqual(enPestana.filter((n) => n.type.startsWith("http")), []);
});

test("los nodos de debug de la pestaña no imprimen el mensaje completo", () => {
    for (const d of enPestana.filter((n) => n.type === "debug")) {
        assert.equal(d.complete, "payload");
    }
});

test("la pestaña no menciona nombres de alumnos en el log: ninguna función registra filas ni params", () => {
    for (const n of enPestana.filter((x) => x.type === "function")) {
        assert.doesNotMatch(n.func, /node\.(log|warn|error)\([^)]*(msg\.params|msg\.payload|filas|nombre)/, n.name);
    }
});
