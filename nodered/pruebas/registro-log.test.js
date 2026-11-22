// Pruebas del registro de Node-RED (registro-log.js, que settings.js usa como `handler` de logging.console):
//   - el formato es el de la consola por defecto de Node-RED ("16 Nov 15:30:12 - [warn] [http in:<id>] <mensaje>");
//   - en toda línea, una racha de 9 o más dígitos (un número de cuenta) se enmascara;
//   - en las líneas de nodos http in, además las rachas de 4 a 8 dígitos (la ventana de V8 en el mensaje de un
//     JSON.parse que falla puede cortar una cuenta) y el mensaje se recorta a 200 caracteres, enmascarando antes de recortar;
//   - el manejador nunca lanza, aunque el mensaje sea hostil o la salida falle.
// El mensaje de JSON.parse de las pruebas sale del V8 que corre la prueba, no de un texto inventado. Las cuentas de aquí
// son de prueba. No prueba la integración con @node-red/util (no está en este repo): el contrato que usa, `handler(settings)`
// devuelve una función de un solo argumento, se comprueba aquí con la misma llamada.
// Sin dependencias: node --test nodered/pruebas/*.test.js
"use strict";
const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { enmascarar, formatearLinea, crearManejadorLog, instalarEnmascaradoDeConsola, MAX_CARACTERES_HTTP_IN } = require("../registro-log");
const util = require("node:util");

const CUENTA = "320068234";
const AHORA = new Date(2026, 10, 16, 15, 30, 12); // hora local: el formato también es local
const PREFIJO = "16 Nov 15:30:12 - ";
const linea = (msg) => formatearLinea({ timestamp: 1, ...msg }, AHORA);
const HTTP_IN = { type: "http in", id: "a1f0c0de00000131" };

// El mensaje real de V8 al fallar un JSON.parse sobre un cuerpo con una cuenta.
function errorDeJson(cuerpo) {
    try {
        JSON.parse(cuerpo);
    } catch (e) {
        return e;
    }
    assert.fail("el cuerpo debía ser inválido");
}

// ---- formato -------------------------------------------------------------------------------------

test("el formato es el de la consola por defecto: marca de tiempo local, [nivel], [tipo:nombre o id] y el mensaje", () => {
    assert.equal(linea({ level: 40, msg: "Started flows" }), PREFIJO + "[info] Started flows");
    assert.equal(linea({ level: 30, type: "function", id: "abc", msg: "aviso" }), PREFIJO + "[warn] [function:abc] aviso");
    assert.equal(linea({ level: 20, type: "function", id: "abc", name: "Mi nodo", msg: "falló" }), PREFIJO + "[error] [function:Mi nodo] falló");
    assert.equal(linea({ level: 10, msg: "grave" }), PREFIJO + "[fatal] grave");
    assert.equal(linea({ level: 50, msg: "detalle" }), PREFIJO + "[debug] detalle");
    assert.equal(formatearLinea({ level: 40, msg: "x" }, new Date(2026, 0, 5, 3, 4, 5)), "5 Jan 03:04:05 - [info] x");
});

test("un Error se escribe por su texto; un objeto con message, por ese mensaje; lo demás, por su texto", () => {
    assert.equal(linea({ level: 30, msg: new TypeError("mala cosa") }), PREFIJO + "[warn] TypeError: mala cosa");
    assert.equal(linea({ level: 30, msg: { message: "dentro" } }), PREFIJO + "[warn] dentro");
    assert.equal(linea({ level: 30, msg: 42 }), PREFIJO + "[warn] 42");
    assert.equal(linea({ level: 30, msg: undefined }), PREFIJO + "[warn] undefined");
    assert.equal(linea({ level: 30, msg: null }), PREFIJO + "[warn] null");
});

test("las líneas de métricas y de auditoría son JSON con el nivel, y solo se enmascaran los textos, no los números", () => {
    const m = { level: 99, timestamp: 1763307012345, nodeid: "a1", event: "node.x", cuenta: CUENTA };
    const l = linea(m);
    assert.match(l, /^16 Nov 15:30:12 - \[metric\] \{/);
    assert.ok(l.includes('"timestamp":1763307012345'), l);
    assert.ok(!l.includes(CUENTA));
    assert.ok(l.includes('"cuenta":"*********"'));
    assert.match(linea({ level: 98, timestamp: 1, event: "auth.login" }), /\[audit\] \{/);
});

// ---- enmascarado ---------------------------------------------------------------------------------

test("enmascarar: una racha de 9 o más dígitos se sustituye entera por 9 asteriscos; menos de 9 no se toca", () => {
    assert.equal(enmascarar("cuenta " + CUENTA + " fin"), "cuenta ********* fin");
    assert.equal(enmascarar("a" + CUENTA + "b"), "a*********b");
    assert.equal(enmascarar("1234567890123"), "*********");
    assert.equal(enmascarar(CUENTA + " y 999000001"), "********* y *********");
    assert.equal(enmascarar("12345678 y 0000"), "12345678 y 0000");
    assert.equal(enmascarar("a1f0c0de00000131"), "a1f0c0de00000131", "el id de un nodo (rachas de 8 dígitos) queda como está");
    assert.equal(enmascarar("sin dígitos"), "sin dígitos");
});

test("enmascarar con `corto` también tapa las rachas de 4 a 8 dígitos, y deja las de 1 a 3", () => {
    assert.equal(enmascarar("pos 22 de 12345678 y 1234 y 123", true), "pos 22 de **** y **** y 123");
    assert.equal(enmascarar(CUENTA, true), "*********");
});

test("toda línea de cualquier nodo o del sistema sale con las cuentas enmascaradas, en mensaje de texto, Error u objeto", () => {
    const casos = [
        { level: 30, msg: "descartado " + CUENTA },
        { level: 20, type: "postgresql", id: "pg", msg: new Error("falló la cuenta " + CUENTA + " en la base") },
        { level: 30, type: "function", id: "f", msg: { message: "objeto con " + CUENTA } },
        { level: 40, msg: "arranque " + CUENTA },
        { level: 30, type: "debug", id: "d", name: "cuenta " + CUENTA, msg: "x" },
        { level: 20, msg: { toString: () => CUENTA, message: undefined } }
    ];
    for (const c of casos) {
        const l = linea(c);
        assert.ok(!l.includes(CUENTA), l);
        assert.ok(l.includes("*********"), l);
        assert.ok(l.startsWith(PREFIJO));
    }
});

// ---- http in -------------------------------------------------------------------------------------

test("el error de un JSON mal formado en un POST (lo que registra el errorHandler de http in) no deja la cuenta ni un trozo de ella", () => {
    const cuerpos = [
        '{"cuenta":"' + CUENTA + '","consentimiento":tru}',
        '{"cuenta":"' + CUENTA + '" "consentimiento":true}',
        '{"a":1,"cuenta":' + CUENTA + ",}",
        'x{"cuenta":"' + CUENTA + '"}',
        '{"nombre":"Ana","cuenta":"' + CUENTA + '"' // truncado
    ];
    for (const cuerpo of cuerpos) {
        const error = errorDeJson(cuerpo);
        // Con el mensaje de V8 tal cual y con la forma que le da body-parser (un error con el cuerpo en `body`).
        for (const msg of [error, Object.assign(new SyntaxError(error.message), { body: cuerpo, statusCode: 400 })]) {
            const l = linea({ level: 30, ...HTTP_IN, msg });
            assert.ok(!l.includes(CUENTA), l);
            assert.doesNotMatch(l.slice(PREFIJO.length).replace(HTTP_IN.id, ""), /[0-9]{4}/, "ni una racha de 4 dígitos: " + l);
            assert.match(l, /^16 Nov 15:30:12 - \[warn\] \[http in:a1f0c0de00000131\] SyntaxError: /);
        }
    }
});

test("el id del nodo http in sale intacto (el enmascarado corto solo toca el mensaje)", () => {
    const l = linea({ level: 30, ...HTTP_IN, msg: "listo 12345" });
    assert.equal(l, PREFIJO + "[warn] [http in:a1f0c0de00000131] listo ****");
});

test("el mensaje de un http in se recorta a 200 caracteres, y solo el de un http in", () => {
    const largo = "palabra ".repeat(60); // 480 caracteres
    const http = linea({ level: 30, ...HTTP_IN, msg: largo });
    const cuerpo = http.slice((PREFIJO + "[warn] [http in:a1f0c0de00000131] ").length);
    assert.equal(cuerpo, largo.slice(0, MAX_CARACTERES_HTTP_IN) + " [recortado]");
    assert.equal(MAX_CARACTERES_HTTP_IN, 200);
    const otro = linea({ level: 30, type: "function", id: "f", msg: largo });
    assert.equal(otro, PREFIJO + "[warn] [function:f] " + largo);
    // Justo 200 no se recorta.
    const justo = linea({ level: 30, ...HTTP_IN, msg: "a".repeat(200) });
    assert.ok(!justo.includes("[recortado]"));
});

test("en un http in, un salto de línea dentro del mensaje (un fragmento citado del cuerpo) no parte la línea del registro", () => {
    const error = errorDeJson('{"cuenta":"' + CUENTA + '",\n"x":\r\n}');
    assert.match(error.message, /\n|\r/, "el mensaje real de V8 conserva el salto de línea");
    for (const msg of [error, "primera\nsegunda\r\ntercera\n\n\ncuarta"]) {
        const l = linea({ level: 30, ...HTTP_IN, msg });
        assert.doesNotMatch(l, /[\r\n]/, JSON.stringify(l));
        assert.equal(l.split("\n").length, 1);
    }
    assert.equal(linea({ level: 30, ...HTTP_IN, msg: "a\nb\r\n\r\nc" }), PREFIJO + "[warn] [http in:a1f0c0de00000131] a b c");
    // Un salto de línea antes del recorte cuenta como un espacio, no como dos caracteres.
    const l = linea({ level: 30, ...HTTP_IN, msg: "a".repeat(199) + "\r\n" + "b" });
    assert.ok(l.endsWith("a".repeat(199) + "  [recortado]"), l.slice(-30)); // 199 "a", el espacio que era CRLF (200.º) y la marca
    // Los demás nodos no se tocan: su línea es la que escribe la consola por defecto.
    assert.equal(linea({ level: 30, type: "function", id: "f", msg: "a\nb" }), PREFIJO + "[warn] [function:f] a\nb");
});

test("se enmascara antes de recortar: una cuenta en el borde del recorte no queda a medias", () => {
    for (const antes of [190, 195, 199, 200]) {
        const l = linea({ level: 30, ...HTTP_IN, msg: "x".repeat(antes) + CUENTA + "y".repeat(50) });
        assert.ok(!/[0-9]{3}/.test(l.slice(PREFIJO.length).replace(HTTP_IN.id, "")), "con " + antes + " antes: " + l);
        assert.ok(l.endsWith(" [recortado]"));
    }
    // Un mensaje largo de otro nodo con una cuenta en cualquier parte también la pierde, sin recortarse.
    const otro = linea({ level: 30, type: "function", id: "f", msg: "x".repeat(300) + CUENTA });
    assert.ok(!otro.includes(CUENTA) && otro.includes("*********") && !otro.includes("[recortado]"));
});

// ---- el manejador nunca lanza -------------------------------------------------------------------

test("el manejador escribe una línea por mensaje, y Node-RED lo crea con handler(settings) y lo llama con el mensaje", () => {
    const escritas = [];
    const fabrica = crearManejadorLog((t) => escritas.push(t));
    const manejador = fabrica({ level: "info", metrics: false, audit: false, handler: fabrica }); // lo que llama LogHandler
    assert.equal(typeof manejador, "function");
    manejador({ level: 30, type: "http in", id: "x", msg: new Error("cuerpo " + CUENTA), timestamp: Date.now() });
    assert.equal(escritas.length, 1);
    assert.match(escritas[0], /^[0-9]{1,2} [A-Z][a-z]{2} [0-9]{2}:[0-9]{2}:[0-9]{2} - \[warn\] \[http in:x\] Error: cuerpo \*\*\*\*\*\*\*\*\*$/);
});

test("un mensaje hostil no hace lanzar al manejador y deja una línea fija sin el contenido", () => {
    const circular = { level: 99, timestamp: 1, nodo: CUENTA };
    circular.yo = circular;
    const hostiles = [
        circular, // JSON.stringify lanza
        { level: 30, msg: { toString() { throw new Error("toString " + CUENTA); } } },
        { level: 30, msg: Symbol(CUENTA) },
        { get level() { throw new RangeError("nivel " + CUENTA); } },
        null,
        undefined
    ];
    for (const msg of hostiles) {
        const escritas = [];
        const manejador = crearManejadorLog((t) => escritas.push(t))();
        assert.doesNotThrow(() => manejador(msg));
        assert.equal(escritas.length, 1);
        assert.ok(!escritas[0].includes(CUENTA), escritas[0]);
    }
    const escritas = [];
    crearManejadorLog((t) => escritas.push(t))()(circular);
    assert.match(escritas[0], /\[error\] No se pudo dar formato a un mensaje de registro \(TypeError\)$/);
});

test("si la salida falla (por ejemplo, stdout cerrado) el manejador no lanza", () => {
    const manejador = crearManejadorLog(() => {
        throw new Error("EPIPE");
    })();
    assert.doesNotThrow(() => manejador({ level: 40, msg: "x" }));
});

// ---- console.error y console.warn (el camino del body-parser del editor y del finalhandler de Express) --------------

// Una consola de mentira con los mismos nombres que usa console: lo que cada método escribe, tal cual lo recibiría la salida.
function consolaFalsa() {
    const escrito = { error: [], warn: [], log: [], info: [] };
    const consola = {};
    for (const metodo of Object.keys(escrito)) {
        consola[metodo] = (...a) => void escrito[metodo].push(util.format(...a));
    }
    return { consola, escrito };
}

// La pila que escribe Express (logerror: console.error(err.stack)) cuando el body-parser rechaza un JSON, con el mensaje real de V8.
function pilaDeBodyParser(cuerpo) {
    const error = errorDeJson(cuerpo);
    error.stack = error.name + ": " + error.message + "\n    at JSON.parse (<anonymous>)\n" +
        "    at parse (/usr/src/node-red/node_modules/body-parser/lib/types/json.js:96:19)\n" +
        "    at /usr/src/node-red/node_modules/body-parser/lib/read.js:128:18\n" +
        "    at AsyncResource.runInAsyncScope (node:async_hooks:211:14)";
    return error.stack;
}

test("la pila de un JSON mal formado que escribe Express con console.error sale sin la cuenta y con todas sus líneas", () => {
    const { consola, escrito } = consolaFalsa();
    instalarEnmascaradoDeConsola(consola);
    for (const cuerpo of ['{"a":x' + CUENTA + "}", '{"cuenta":"' + CUENTA + '" "x"}', '{"cuenta":' + CUENTA + ",}"]) {
        const pila = pilaDeBodyParser(cuerpo);
        // (según el cuerpo, V8 cita un fragmento o solo la posición del error: en los dos casos no debe salir la cuenta)
        consola.error(pila);
        const l = escrito.error.at(-1);
        assert.ok(!l.includes(CUENTA), l);
        assert.equal(l.split("\n").length, pila.split("\n").length, "se conservan las líneas de la pila");
        assert.match(l, /\n    at JSON\.parse \(<anonymous>\)\n    at parse \(\/usr\/src\/node-red\/node_modules\/body-parser\/lib\/types\/json\.js:96:19\)\n/);
        assert.match(l, /^SyntaxError: /);
    }
    // El caso exacto de la placa: {"a":x320068234} en /api/actuadores.
    const exacto = pilaDeBodyParser('{"a":x320068234}');
    consola.error(exacto);
    assert.match(escrito.error.at(-1), /^SyntaxError: Unexpected token 'x', "\{"a":x\*{9}\}" is not valid JSON\n/);
});

test("console.warn también se enmascara; los demás métodos de la consola no se tocan (el registro de Node-RED sale por console.log y ya va enmascarado)", () => {
    const { consola, escrito } = consolaFalsa();
    const log = consola.log;
    const info = consola.info;
    instalarEnmascaradoDeConsola(consola);
    consola.warn("aviso " + CUENTA);
    assert.deepEqual(escrito.warn, ["aviso *********"]);
    assert.equal(consola.log, log);
    assert.equal(consola.info, info);
    // Lo que sale por console.log no se procesa dos veces: queda como lo escribió formatearLinea.
    const ya = linea({ level: 30, type: "function", id: "f", msg: "dato " + CUENTA });
    consola.log(ya);
    assert.deepEqual(escrito.log, [ya]);
});

test("se da formato primero y se enmascara el resultado: los argumentos, %s, %d, %o y los objetos también se enmascaran", () => {
    const { consola, escrito } = consolaFalsa();
    instalarEnmascaradoDeConsola(consola);
    consola.error("cuenta %s de %d", CUENTA, 7);
    consola.error("número %d", Number(CUENTA));
    consola.error("objeto", { cuenta: CUENTA, lista: [CUENTA] });
    consola.error("sin formato", CUENTA, "y", 12345678);
    consola.error("100%% de %s", CUENTA);
    consola.error("un solo argumento 100%% " + CUENTA); // como util.format: con un solo argumento no se procesan los %
    consola.error(new Error("falló " + CUENTA));
    consola.error();
    assert.equal(escrito.error[0], "cuenta ********* de 7");
    assert.equal(escrito.error[1], "número *********");
    assert.match(escrito.error[2], /^objeto \{ cuenta: '\*{9}', lista: \[ '\*{9}' \] \}$/);
    assert.equal(escrito.error[3], "sin formato ********* y 12345678", "ocho dígitos no son una cuenta");
    assert.equal(escrito.error[4], "100% de *********");
    assert.equal(escrito.error[5], "un solo argumento 100%% *********");
    assert.match(escrito.error[6], /^Error: falló \*{9}\n {4}at /);
    assert.equal(escrito.error[7], "");
    for (const l of escrito.error) {
        assert.ok(!l.includes(CUENTA));
    }
});

test("las líneas de la pila (\"    at ...\") se conservan tal cual, con sus sangrías y números, salvo las rachas de 9 o más dígitos", () => {
    const { consola, escrito } = consolaFalsa();
    instalarEnmascaradoDeConsola(consola);
    const texto = "Error: fallo en el arranque\n    at a (/x/a.js:1280:1)\n\tat b (/x/b.js:2:22222)\n    at c (/x/c." + CUENTA + ")\n";
    consola.error(texto);
    assert.equal(escrito.error[0], texto.replace(CUENTA, "*********"), "los números de 4 y 5 dígitos de las líneas de la pila quedan");
    // Dos pilas seguidas de un error con causa: el bloque final de líneas "at" es la cola; lo anterior, cabeza.
    consola.error("Error: uno\n    at a (/x/a.js:1:1)\nCaused by: otro\n    at b (/x/b.js:2:2)");
    assert.equal(escrito.error[1], "Error: uno     at a (/x/a.js:1:1) Caused by: otro\n    at b (/x/b.js:2:2)");
});

test("un mensaje de varias líneas sin pila sale en una sola línea; los saltos de línea finales se conservan", () => {
    const { consola, escrito } = consolaFalsa();
    instalarEnmascaradoDeConsola(consola);
    consola.error("primera\nsegunda\r\ntercera\n\n\ncuarta " + CUENTA + "\n");
    consola.warn("a\nb");
    assert.equal(escrito.error[0], "primera segunda tercera cuarta *********\n");
    assert.equal(escrito.warn[0], "a b");
});

// ---- el mensaje de un JSON.parse que falla (lo que cita V8 del cuerpo) ------------------------------------------------

// Un cuerpo inválido cuyo error trae, en su mensaje real de V8, `fragmento` (se comprueba: la prueba no es vacía).
function pilaConFragmento(cuerpo, fragmento) {
    const error = errorDeJson(cuerpo);
    assert.ok(error.message.includes(fragmento), "V8 debía citar " + JSON.stringify(fragmento) + ": " + JSON.stringify(error.message));
    error.stack = error.name + ": " + error.message + "\n    at JSON.parse (<anonymous>)\n" +
        "    at parse (/usr/src/node-red/node_modules/body-parser/lib/types/json.js:96:19)\n" +
        "    at /usr/src/node-red/node_modules/body-parser/lib/read.js:1280:18";
    return error.stack;
}
const sinRachas = (texto) => !/[0-9]{4}/.test(texto);
const PILA_REAL = /^[ \t]+at /;

test("una cuenta cortada por la ventana que cita V8 (cola o cabeza) no deja ni 4 dígitos en el mensaje, pero las líneas de la pila conservan sus números", () => {
    const { consola, escrito } = consolaFalsa();
    instalarEnmascaradoDeConsola(consola);
    const casos = [
        ['{"pad":1,"cuenta":"' + CUENTA + '","b":x,"c":1,"d":2}', "8234"], // la ventana empieza dentro de la cuenta: queda el final
        ['{"a":x' + CUENTA + ',"pad":1,"zz":2}', "3200"], // la ventana termina dentro de la cuenta: queda el principio
        ['{"a":x' + CUENTA + "}", CUENTA.slice(0, 4)]
    ];
    for (const [cuerpo, trozo] of casos) {
        const pila = pilaConFragmento(cuerpo, trozo);
        consola.error(pila);
        const l = escrito.error.at(-1);
        const [cabeza, ...cola] = l.split("\n");
        assert.ok(sinRachas(cabeza), "cabeza: " + cabeza);
        assert.ok(cabeza.includes("****"), cabeza);
        assert.match(cabeza, /^SyntaxError: /);
        assert.deepEqual(cola, [
            "    at JSON.parse (<anonymous>)",
            "    at parse (/usr/src/node-red/node_modules/body-parser/lib/types/json.js:96:19)",
            "    at /usr/src/node-red/node_modules/body-parser/lib/read.js:1280:18"
        ], "la cola queda tal cual: head-only");
    }
});

test("los demás mensajes de error de JSON (posición, fin inesperado, texto cortado) también enmascaran de 4 dígitos; un texto que no es de JSON conserva los de 4 a 8", () => {
    const { consola, escrito } = consolaFalsa();
    instalarEnmascaradoDeConsola(consola);
    for (const msg of [
        "Expected ',' or '}' after property value in JSON at position 1234 (line 1 column 5678)",
        "Unexpected non-whitespace character after JSON at position 4321",
        "Unexpected end of JSON input 9876",
        "Unexpected token 'x', ..._8234\",\"b\":x_... is not valid JSON"
    ]) {
        consola.error(msg);
        assert.ok(sinRachas(escrito.error.at(-1)), escrito.error.at(-1));
    }
    consola.error("posición 1234 de 12345678");
    assert.equal(escrito.error.at(-1), "posición 1234 de 12345678");
});

test("un cuerpo con saltos de línea no puede escribir una línea falsa en el registro: la cabeza del mensaje sale en una sola línea", () => {
    const { consola, escrito } = consolaFalsa();
    instalarEnmascaradoDeConsola(consola);
    const cuerpos = [
        '{"a":x\n29 Nov 10:00:00 - [warn] [http in:abc] falso\r\n}',
        '{"a":x\r\n\r\n29 Nov 10:00:00 - [warn] falso}',
        '{"a":x\n    at fake (/x.js:1:1)\n29 Nov 10:00:00 - [warn] falso\n}', // una línea que parece de pila, dentro del mensaje
        '{"a":x\n    at fake (/x.js:1:1)\n    at otra (/x.js:2:2)\n}'
    ];
    for (const cuerpo of cuerpos) {
        const error = errorDeJson(cuerpo);
        assert.match(error.message, /[\r\n]/, "V8 copia el salto de línea del cuerpo: " + JSON.stringify(error.message));
        consola.error(pilaConFragmento(cuerpo, error.message.includes("\r") ? "\r" : "\n"));
        const lineas = escrito.error.at(-1).split("\n");
        assert.equal(lineas.filter((x) => !PILA_REAL.test(x)).length, 1, "una sola línea que no es de pila: " + JSON.stringify(lineas));
        assert.match(lineas[0], /^SyntaxError: /);
        assert.ok(!lineas.some((x) => /^29 Nov/.test(x)), JSON.stringify(lineas));
        assert.equal(lineas.length, 4, "la cabeza y las tres líneas reales de la pila");
        assert.doesNotMatch(escrito.error.at(-1), /\r/);
    }
});

test("una pila sintética con líneas falsas de pila y de registro dentro del mensaje: solo el bloque final de líneas reales es la cola", () => {
    const { consola, escrito } = consolaFalsa();
    instalarEnmascaradoDeConsola(consola);
    consola.error('SyntaxError: Unexpected token \'x\', "{"a":x\n    at fake (/x.js:1:1)\n29 Nov 10:00:00 - [warn] falso 1234\n    at f"... is not valid JSON\n    at JSON.parse (<anonymous>)\n    at parse (/y.js:96:19)');
    const lineas = escrito.error[0].split("\n");
    assert.equal(lineas.length, 3);
    assert.match(lineas[0], /^SyntaxError: Unexpected token 'x', "\{"a":x +at fake \(\/x\.js:1:1\) 29 Nov 10:00:00 - \[warn\] falso \*{4} +at f"\.\.\. is not valid JSON$/);
    assert.deepEqual(lineas.slice(1), ["    at JSON.parse (<anonymous>)", "    at parse (/y.js:96:19)"]);
});

test("el envoltorio nunca lanza: un argumento hostil deja una línea fija sin el contenido, y una salida que falla no tumba a quien escribe", () => {
    const { consola, escrito } = consolaFalsa();
    instalarEnmascaradoDeConsola(consola);
    const hostil = { [util.inspect.custom]() { throw new RangeError("dentro " + CUENTA); } };
    const toStringHostil = { toString() { throw new TypeError("toString " + CUENTA); } };
    assert.doesNotThrow(() => consola.error(hostil));
    assert.doesNotThrow(() => consola.warn("%s", toStringHostil));
    assert.doesNotThrow(() => consola.error(Symbol(CUENTA)));
    assert.match(escrito.error[0], /^\[registro\] No se pudo dar formato a un mensaje de console\.error \(RangeError\)$/);
    assert.match(escrito.warn[0], /^\[registro\] No se pudo dar formato a un mensaje de console\.warn \(TypeError\)$/);
    for (const l of [...escrito.error, ...escrito.warn]) {
        assert.ok(!l.includes(CUENTA), l);
    }
    const rota = { error() { throw new Error("EPIPE"); }, warn() { throw new Error("EPIPE"); } };
    instalarEnmascaradoDeConsola(rota);
    assert.doesNotThrow(() => rota.error("x"));
    assert.doesNotThrow(() => rota.warn("x"));
});

test("instalarlo dos veces no envuelve dos veces, y una consola sin error ni warn no hace fallar la instalación", () => {
    const { consola, escrito } = consolaFalsa();
    instalarEnmascaradoDeConsola(consola);
    const una = consola.error;
    instalarEnmascaradoDeConsola(consola);
    assert.equal(consola.error, una);
    consola.error(CUENTA);
    assert.deepEqual(escrito.error, ["*********"]);
    assert.doesNotThrow(() => instalarEnmascaradoDeConsola({}));
});

test("settings.js instala el enmascarado de la consola antes de cualquier validación o arranque", () => {
    const texto = fs.readFileSync(path.join(__dirname, "..", "settings.js"), "utf8");
    const instalacion = texto.indexOf("\ninstalarEnmascaradoDeConsola();");
    assert.ok(instalacion > 0);
    assert.ok(instalacion < texto.indexOf("process.env.NODERED_ADMIN_USER"));
    assert.ok(instalacion < texto.indexOf("module.exports"));
    assert.equal(texto.split("instalarEnmascaradoDeConsola()").length - 1, 1);
});

// ---- settings.js y la imagen --------------------------------------------------------------------

test("settings.js usa el manejador en la clave console con el mismo nivel, sin métricas ni auditoría", () => {
    const texto = fs.readFileSync(path.join(__dirname, "..", "settings.js"), "utf8");
    assert.match(texto, /const \{ crearManejadorLog, instalarEnmascaradoDeConsola \} = require\("\.\/registro-log"\);/);
    const bloque = texto.slice(texto.indexOf("logging: {"));
    assert.match(bloque, /console: \{ level: "info", metrics: false, audit: false, handler: crearManejadorLog\(\) \}/);
    // La clave console es la única, y con `handler` Node-RED no usa la consola por defecto.
    assert.equal((bloque.match(/^\s{8}[a-z]+: \{/gm) || []).length, 1);
});

test("la imagen final corre con NODE_ENV=production (sin pila de errores para el cliente), después de instalar y sin afectar a la etapa del frontend", () => {
    const docker = fs.readFileSync(path.join(__dirname, "..", "Dockerfile"), "utf8");
    const lineas = docker.split("\n");
    const entornos = lineas.map((l, i) => [l, i]).filter(([l]) => /^ENV\s+NODE_ENV=production\s*$/.test(l));
    assert.equal(entornos.length, 1);
    const [, linea] = entornos[0];
    const etapa2 = lineas.findIndex((l) => /^FROM nodered\/node-red:/.test(l));
    const instalar = lineas.findIndex((l) => /^RUN npm ci --omit=dev /.test(l));
    const compilar = lineas.findIndex((l) => /^RUN npm run build$/.test(l));
    assert.ok(etapa2 >= 0 && linea > etapa2, "va en la etapa final");
    assert.ok(linea > instalar, "después de npm ci (que ya lleva --omit=dev explícito), para no cambiar lo que se instala");
    assert.ok(compilar >= 0 && compilar < etapa2, "la etapa del frontend (npm ci con las devDependencies de Vite y su build) queda antes y sin la variable");
    assert.ok(linea < lineas.findIndex((l) => /^CMD /.test(l)));
});

test("registro-log.js entra a la imagen: está en el COPY del Dockerfile y en la lista del .dockerignore", () => {
    const raiz = path.join(__dirname, "..");
    assert.match(fs.readFileSync(path.join(raiz, "Dockerfile"), "utf8"), /^COPY settings\.js autenticacion-api\.js registro-log\.js reporte_csv\.py \/etc\/nodered\/$/m);
    assert.match(fs.readFileSync(path.join(raiz, ".dockerignore"), "utf8"), /^!registro-log\.js$/m);
});
