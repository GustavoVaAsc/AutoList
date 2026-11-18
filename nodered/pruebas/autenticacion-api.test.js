// Pruebas de nodered/autenticacion-api.js (el httpNodeMiddleware de la API).
// Se ejecutan en el host, sin dependencias: node --test nodered/pruebas/*.test.js
"use strict";
const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { crearMiddlewareApi, soloRutasApi, validarSecretosApi, LIMITE_CUERPO_BYTES, RUTA_API } = require("../autenticacion-api");

const USUARIO = "profe";
const CLAVE = "clave:con:dos:puntos";
const TOKEN = "token-de-la-galeria";

function basic(usuario, clave) {
    return "Basic " + Buffer.from(usuario + ":" + clave, "utf8").toString("base64");
}

// Petición y respuesta mínimas, con la parte de Express que usa el middleware.
// Los métodos que cambian estado llevan, por omisión, las cabeceras que pone el
// frontend (X-Pase-Lista y el mismo origen); `csrf: false` las quita para probarlas.
const HOST = "placa.local:1880";
// `envolver`: el middleware tal como corre en httpAdminMiddleware (soloRutasApi). `encadenar`: el paso de httpAdminMiddleware
// seguido del de httpNodeMiddleware sobre la misma petición y la misma respuesta (lo que pasa con una petición aceptada).
function ejecutar(opciones, { ruta = "/api/alumnos", metodo = "GET", cabeceras = {}, csrf = true, envolver = false, encadenar = false } = {}) {
    const base = crearMiddlewareApi({ usuario: USUARIO, clave: CLAVE, tokenGaleria: TOKEN, ...opciones });
    const middleware = encadenar
        ? (req, res, next) => soloRutasApi(base)(req, res, () => base(req, res, next))
        : envolver ? soloRutasApi(base) : base;
    if (csrf && metodo !== "GET" && metodo !== "HEAD") {
        cabeceras = { Host: HOST, "X-Pase-Lista": "1", ...cabeceras };
    }
    const minusculas = {};
    for (const [k, v] of Object.entries(cabeceras)) minusculas[k.toLowerCase()] = v;
    const req = {
        method: metodo,
        path: ruta,
        headers: minusculas,
        get(nombre) { return minusculas[nombre.toLowerCase()]; }
    };
    const res = {
        statusCode: 200,
        cabeceras: {},
        cuerpo: undefined,
        status(c) { this.statusCode = c; return this; },
        set(a, b) {
            if (typeof a === "string") this.cabeceras[a.toLowerCase()] = b;
            else for (const [k, v] of Object.entries(a)) this.cabeceras[k.toLowerCase()] = v;
            return this;
        },
        json(c) { this.cuerpo = c; return this; }
    };
    let siguio = false;
    middleware(req, res, () => { siguio = true; });
    return { res, siguio };
}

test("Basic correcto deja pasar y fija cabeceras de seguridad", () => {
    const { res, siguio } = ejecutar({}, { cabeceras: { Authorization: basic(USUARIO, CLAVE) } });
    assert.equal(siguio, true);
    assert.equal(res.cabeceras["x-content-type-options"], "nosniff");
    assert.equal(res.cabeceras["x-frame-options"], "DENY");
    assert.equal(res.cabeceras["cache-control"], "no-store");
});

test("la contraseña puede llevar dos puntos y el esquema no distingue mayúsculas", () => {
    const cab = { Authorization: basic(USUARIO, CLAVE).replace("Basic", "bAsIc") };
    assert.equal(ejecutar({}, { cabeceras: cab }).siguio, true);
});

test("sin Authorization: 401 con WWW-Authenticate Basic", () => {
    const { res, siguio } = ejecutar({});
    assert.equal(siguio, false);
    assert.equal(res.statusCode, 401);
    assert.match(res.cabeceras["www-authenticate"], /^Basic realm="[^"]+"/);
    assert.equal(typeof res.cuerpo.error, "string");
});

for (const [nombre, valor] of [
    ["contraseña errónea", basic(USUARIO, "otra")],
    ["usuario erróneo", basic("otro", CLAVE)],
    ["sin dos puntos", "Basic " + Buffer.from("soloUsuario").toString("base64")],
    ["esquema Bearer", "Bearer abc"],
    ["Basic vacío", "Basic "],
    ["credenciales vacías", basic("", "")],
    ["prefijo de la contraseña", basic(USUARIO, CLAVE.slice(0, -1))],
    ["contraseña más larga", basic(USUARIO, CLAVE + "x")]
]) {
    test("Basic rechazado: " + nombre, () => {
        const { res, siguio } = ejecutar({}, { cabeceras: { Authorization: valor } });
        assert.equal(siguio, false);
        assert.equal(res.statusCode, 401);
        assert.ok(res.cabeceras["www-authenticate"]);
    });
}

test("GET /api/galeria con el token y sin Basic deja pasar", () => {
    const { siguio } = ejecutar({}, { ruta: "/api/galeria", cabeceras: { "X-Galeria-Token": TOKEN } });
    assert.equal(siguio, true);
});

test("GET /api/galeria no acepta las credenciales del profesor", () => {
    const { res, siguio } = ejecutar({}, { ruta: "/api/galeria", cabeceras: { Authorization: basic(USUARIO, CLAVE) } });
    assert.equal(siguio, false);
    assert.equal(res.statusCode, 401);
});

test("GET /api/galeria con Basic correcto y token erróneo: 401", () => {
    const { siguio, res } = ejecutar({}, { ruta: "/api/galeria", cabeceras: { Authorization: basic(USUARIO, CLAVE), "X-Galeria-Token": "malo" } });
    assert.equal(siguio, false);
    assert.equal(res.statusCode, 401);
});

test("GET /api/galeria: 401 sin pedir Basic (no abre el diálogo del navegador)", () => {
    const { res } = ejecutar({}, { ruta: "/api/galeria" });
    assert.equal(res.statusCode, 401);
    assert.doesNotMatch(res.cabeceras["www-authenticate"], /basic/i);
});

for (const [nombre, valor] of [
    ["más corto", TOKEN.slice(0, -1)],
    ["más largo", TOKEN + "x"],
    ["vacío", ""],
    ["de otro valor y misma longitud", "x".repeat(TOKEN.length)]
]) {
    test("token de la galería rechazado: " + nombre, () => {
        const { siguio, res } = ejecutar({}, { ruta: "/api/galeria", cabeceras: { "X-Galeria-Token": valor } });
        assert.equal(siguio, false);
        assert.equal(res.statusCode, 401);
    });
}

test("el token de la galería no sirve en las demás rutas", () => {
    const { siguio, res } = ejecutar({}, { ruta: "/api/alumnos", cabeceras: { "X-Galeria-Token": TOKEN } });
    assert.equal(siguio, false);
    assert.equal(res.statusCode, 401);
});

test("la ruta de la galería se reconoce igual que Express: mayúsculas y barra final", () => {
    for (const ruta of ["/API/Galeria", "/api/galeria/", "/api/galeria//"]) {
        const sin = ejecutar({}, { ruta, cabeceras: { Authorization: basic(USUARIO, CLAVE) } });
        assert.equal(sin.siguio, false, ruta);
        const con = ejecutar({}, { ruta, cabeceras: { "X-Galeria-Token": TOKEN } });
        assert.equal(con.siguio, true, ruta);
    }
});

test("rutas parecidas a la galería exigen Basic", () => {
    for (const ruta of ["/api/galeria/otra", "/api/galeriax", "/api/alumnos/galeria"]) {
        const { siguio } = ejecutar({}, { ruta, cabeceras: { "X-Galeria-Token": TOKEN } });
        assert.equal(siguio, false, ruta);
    }
});

test("una ruta fuera de /api también exige Basic (falla cerrado)", () => {
    assert.equal(ejecutar({}, { ruta: "/otra" }).siguio, false);
    assert.equal(ejecutar({}, { ruta: "/otra", cabeceras: { Authorization: basic(USUARIO, CLAVE) } }).siguio, true);
});

test("Content-Length mayor al límite: 413, y solo tras autenticar", () => {
    const grande = String(LIMITE_CUERPO_BYTES + 1);
    const sin = ejecutar({}, { metodo: "POST", cabeceras: { "Content-Length": grande } });
    assert.equal(sin.res.statusCode, 401);
    const con = ejecutar({}, { metodo: "POST", cabeceras: { Authorization: basic(USUARIO, CLAVE), "Content-Length": grande } });
    assert.equal(con.siguio, false);
    assert.equal(con.res.statusCode, 413);
});

test("Content-Length en el límite pasa", () => {
    const { siguio } = ejecutar({}, { metodo: "POST", cabeceras: { Authorization: basic(USUARIO, CLAVE), "Content-Length": String(LIMITE_CUERPO_BYTES) } });
    assert.equal(siguio, true);
});

test("Transfer-Encoding (cuerpo sin longitud conocida): 411", () => {
    const { siguio, res } = ejecutar({}, { metodo: "POST", cabeceras: { Authorization: basic(USUARIO, CLAVE), "Transfer-Encoding": "chunked" } });
    assert.equal(siguio, false);
    assert.equal(res.statusCode, 411);
});

test("Content-Length no numérico: 400", () => {
    const { siguio, res } = ejecutar({}, { metodo: "POST", cabeceras: { Authorization: basic(USUARIO, CLAVE), "Content-Length": "abc" } });
    assert.equal(siguio, false);
    assert.equal(res.statusCode, 400);
});

test("la galería también pasa por el límite de cuerpo", () => {
    const { res } = ejecutar({}, { ruta: "/api/galeria", cabeceras: { "X-Galeria-Token": TOKEN, "Content-Length": String(LIMITE_CUERPO_BYTES + 1) } });
    assert.equal(res.statusCode, 413);
});

test("falla cerrado: sin usuario, contraseña o token no se crea el middleware", () => {
    const base = { usuario: USUARIO, clave: CLAVE, tokenGaleria: TOKEN };
    for (const campo of Object.keys(base)) {
        for (const vacio of [undefined, ""]) {
            assert.throws(() => crearMiddlewareApi({ ...base, [campo]: vacio }), /autenticacion-api/);
        }
    }
    assert.throws(() => crearMiddlewareApi({ ...base, usuario: "a:b" }), /dos puntos/);
});

// ---------------------------------------------------------------- CSRF

function post(cabeceras, extra = {}) {
    return ejecutar({}, { metodo: "POST", ruta: "/api/alumnos/importar", csrf: false, cabeceras: { Authorization: basic(USUARIO, CLAVE), ...cabeceras }, ...extra });
}

test("CSRF: POST con X-Pase-Lista, mismo origen y Sec-Fetch-Site same-origin pasa", () => {
    const { siguio } = post({ Host: HOST, "X-Pase-Lista": "1", Origin: "https://" + HOST, "Sec-Fetch-Site": "same-origin" });
    assert.equal(siguio, true);
});

test("CSRF: POST sin Origin ni Sec-Fetch-Site (curl) pasa si lleva X-Pase-Lista", () => {
    assert.equal(post({ Host: HOST, "X-Pase-Lista": "1" }).siguio, true);
});

test("CSRF: falta la cabecera propia X-Pase-Lista: 403", () => {
    const { siguio, res } = post({ Host: HOST });
    assert.equal(siguio, false);
    assert.equal(res.statusCode, 403);
});

test("CSRF: X-Pase-Lista distinta de 1: 403", () => {
    for (const valor of ["0", "true", "", "1, 1"]) {
        const { siguio, res } = post({ Host: HOST, "X-Pase-Lista": valor });
        assert.equal(siguio, false, JSON.stringify(valor));
        assert.equal(res.statusCode, 403);
    }
});

test("CSRF: Sec-Fetch-Site cross-site, same-site o none: 403", () => {
    for (const valor of ["cross-site", "same-site", "none"]) {
        const { siguio, res } = post({ Host: HOST, "X-Pase-Lista": "1", "Sec-Fetch-Site": valor });
        assert.equal(siguio, false, valor);
        assert.equal(res.statusCode, 403);
    }
});

test("CSRF: Origin ajeno, null, http o de otro puerto: 403", () => {
    for (const origen of ["https://sitio-malo.example", "null", "http://" + HOST, "https://placa.local:1881", "https://placa.local"]) {
        const { siguio, res } = post({ Host: HOST, "X-Pase-Lista": "1", Origin: origen });
        assert.equal(siguio, false, origen);
        assert.equal(res.statusCode, 403);
    }
});

test("CSRF: Origin presente y sin Host: 403", () => {
    const { siguio, res } = post({ "X-Pase-Lista": "1", Origin: "https://" + HOST });
    assert.equal(siguio, false);
    assert.equal(res.statusCode, 403);
});

test("CSRF: otros métodos que cambian estado también lo exigen", () => {
    for (const metodo of ["POST", "PUT", "PATCH", "DELETE"]) {
        const { siguio, res } = ejecutar({}, { metodo, csrf: false, cabeceras: { Authorization: basic(USUARIO, CLAVE), Host: HOST } });
        assert.equal(siguio, false, metodo);
        assert.equal(res.statusCode, 403, metodo);
    }
});

test("CSRF: GET y HEAD no lo exigen (el frontend lee sin la cabecera)", () => {
    for (const metodo of ["GET", "HEAD"]) {
        const { siguio } = ejecutar({}, { metodo, cabeceras: { Authorization: basic(USUARIO, CLAVE) } });
        assert.equal(siguio, true, metodo);
    }
});

test("CSRF: primero se autentica (sin Basic, 401 aunque falten las cabeceras) y luego se revisa el CSRF antes del tope de cuerpo", () => {
    const sin = ejecutar({}, { metodo: "POST", csrf: false });
    assert.equal(sin.res.statusCode, 401);
    const grande = post({ Host: HOST, "Content-Length": String(LIMITE_CUERPO_BYTES + 1) });
    assert.equal(grande.res.statusCode, 403);
});

test("CSRF: la respuesta 403 no es un desafío de autenticación", () => {
    const { res } = post({ Host: HOST });
    assert.equal(res.cabeceras["www-authenticate"], undefined);
});

// Endpoints de registro (POST /api/enrolamiento, POST /api/enrolamiento/cancelar y
// DELETE /api/alumnos/:cuenta/rostro). El middleware no mira el Content-Type: dejar
// pasar un JSON y rechazar un tipo incorrecto (415) es cosa de cada flujo ("Validar y
// apartar inscripción" en flows.json). Aquí se fija que la protección CSRF y el tope de
// cuerpo cubren esas rutas sin cambios y que no bloquean application/json.
const JSON_PROPIO = { Host: HOST, "X-Pase-Lista": "1", Origin: "https://" + HOST, "Sec-Fetch-Site": "same-origin" };

test("registro: POST application/json autenticado, con X-Pase-Lista y del mismo origen pasa", () => {
    const { siguio } = post({ ...JSON_PROPIO, "Content-Type": "application/json", "Content-Length": "58" }, { ruta: "/api/enrolamiento" });
    assert.equal(siguio, true);
});

test("registro: POST /api/enrolamiento/cancelar sin cuerpo pasa con X-Pase-Lista", () => {
    assert.equal(post(JSON_PROPIO, { ruta: "/api/enrolamiento/cancelar" }).siguio, true);
});

test("registro: el middleware no mira el Content-Type (el flujo responde 415)", () => {
    const { siguio } = post({ ...JSON_PROPIO, "Content-Type": "text/plain" }, { ruta: "/api/enrolamiento" });
    assert.equal(siguio, true);
});

test("registro: DELETE /api/alumnos/:cuenta/rostro pasa con X-Pase-Lista y del mismo origen, sin cuerpo", () => {
    const { siguio } = post(JSON_PROPIO, { metodo: "DELETE", ruta: "/api/alumnos/320068234/rostro" });
    assert.equal(siguio, true);
});

test("registro: DELETE sin X-Pase-Lista o de otro origen: 403", () => {
    for (const cabeceras of [{ Host: HOST }, { ...JSON_PROPIO, Origin: "https://sitio-malo.example" }, { ...JSON_PROPIO, "Sec-Fetch-Site": "cross-site" }]) {
        const { siguio, res } = post(cabeceras, { metodo: "DELETE", ruta: "/api/alumnos/320068234/rostro" });
        assert.equal(siguio, false);
        assert.equal(res.statusCode, 403);
    }
});

test("registro: los POST de registro sin credenciales: 401", () => {
    for (const ruta of ["/api/enrolamiento", "/api/enrolamiento/cancelar"]) {
        const { siguio, res } = ejecutar({}, { metodo: "POST", ruta, cabeceras: { "Content-Type": "application/json" } });
        assert.equal(siguio, false, ruta);
        assert.equal(res.statusCode, 401, ruta);
    }
    const baja = ejecutar({}, { metodo: "DELETE", ruta: "/api/alumnos/320068234/rostro" });
    assert.equal(baja.siguio, false);
    assert.equal(baja.res.statusCode, 401);
});

test("registro: GET /api/enrolamiento/estado exige Basic y no el token de la galería", () => {
    assert.equal(ejecutar({}, { ruta: "/api/enrolamiento/estado", cabeceras: { Authorization: basic(USUARIO, CLAVE) } }).siguio, true);
    const conToken = ejecutar({}, { ruta: "/api/enrolamiento/estado", cabeceras: { "X-Galeria-Token": TOKEN } });
    assert.equal(conToken.siguio, false);
    assert.equal(conToken.res.statusCode, 401);
});

// Prueba de reconocimiento (POST /api/enrolamiento/probar y GET /api/enrolamiento/prueba, pestaña "Registro"
// de flows.json). El POST publica una orden a la visión, así que hereda Basic, X-Pase-Lista y el mismo origen
// sin cambios en el middleware; el GET es de solo lectura (get-solo-lectura.test.js) y solo acepta Basic.
test("prueba de reconocimiento: POST sin cuerpo, autenticado, con X-Pase-Lista y del mismo origen pasa", () => {
    assert.equal(post(JSON_PROPIO, { ruta: "/api/enrolamiento/probar" }).siguio, true);
});

test("prueba de reconocimiento: POST sin X-Pase-Lista o de otro origen: 403; sin credenciales o con el token de la galería: 401", () => {
    for (const cabeceras of [
        { Host: HOST },
        { ...JSON_PROPIO, Origin: "https://sitio-malo.example" },
        { ...JSON_PROPIO, "Sec-Fetch-Site": "cross-site" }
    ]) {
        const { siguio, res } = post(cabeceras, { ruta: "/api/enrolamiento/probar" });
        assert.equal(siguio, false);
        assert.equal(res.statusCode, 403);
    }
    const sin = ejecutar({}, { metodo: "POST", ruta: "/api/enrolamiento/probar" });
    assert.equal(sin.siguio, false);
    assert.equal(sin.res.statusCode, 401);
    const conToken = ejecutar({}, { metodo: "POST", ruta: "/api/enrolamiento/probar", cabeceras: { "X-Galeria-Token": TOKEN } });
    assert.equal(conToken.siguio, false);
    assert.equal(conToken.res.statusCode, 401);
});

test("prueba de reconocimiento: GET /api/enrolamiento/prueba exige Basic y no el token de la galería", () => {
    assert.equal(ejecutar({}, { ruta: "/api/enrolamiento/prueba", cabeceras: { Authorization: basic(USUARIO, CLAVE) } }).siguio, true);
    const conToken = ejecutar({}, { ruta: "/api/enrolamiento/prueba", cabeceras: { "X-Galeria-Token": TOKEN } });
    assert.equal(conToken.siguio, false);
    assert.equal(conToken.res.statusCode, 401);
});

// Control manual de los actuadores (POST /api/actuadores, pestaña "Actuadores" de flows.json). Cambia el
// estado de la XIAO, así que la ruta tiene que quedar detrás de Basic, X-Pase-Lista y el mismo origen sin
// cambios en el middleware; el 415, el 400 y el 429 son del flujo (flujo-actuadores.test.js).
test("actuadores: POST autenticado, con X-Pase-Lista y del mismo origen pasa", () => {
    const { siguio } = post({ ...JSON_PROPIO, "Content-Type": "application/json", "Content-Length": "13" }, { ruta: "/api/actuadores" });
    assert.equal(siguio, true);
});

test("actuadores: sin X-Pase-Lista, de otro origen o de otro sitio: 403 y la orden no llega al flujo", () => {
    for (const cabeceras of [
        { Host: HOST, "Content-Type": "application/json" },
        { ...JSON_PROPIO, Origin: "https://sitio-malo.example" },
        { ...JSON_PROPIO, "Sec-Fetch-Site": "cross-site" },
        { ...JSON_PROPIO, "X-Pase-Lista": "0" }
    ]) {
        const { siguio, res } = post(cabeceras, { ruta: "/api/actuadores" });
        assert.equal(siguio, false);
        assert.equal(res.statusCode, 403);
    }
});

test("actuadores: sin credenciales, o solo con el token de la galería: 401", () => {
    const sin = ejecutar({}, { metodo: "POST", ruta: "/api/actuadores", cabeceras: { "Content-Type": "application/json" } });
    assert.equal(sin.siguio, false);
    assert.equal(sin.res.statusCode, 401);
    const conToken = ejecutar({}, { metodo: "POST", ruta: "/api/actuadores", cabeceras: { "X-Galeria-Token": TOKEN } });
    assert.equal(conToken.siguio, false);
    assert.equal(conToken.res.statusCode, 401);
});

test("actuadores: el tope de cuerpo y Transfer-Encoding también la cubren", () => {
    const grande = post({ ...JSON_PROPIO, "Content-Length": String(LIMITE_CUERPO_BYTES + 1) }, { ruta: "/api/actuadores" });
    assert.equal(grande.res.statusCode, 413);
    const chunked = post({ ...JSON_PROPIO, "Transfer-Encoding": "chunked" }, { ruta: "/api/actuadores" });
    assert.equal(chunked.res.statusCode, 411);
});

// ---------------------------------------------------------------- secretos

const BUENOS = {
    claveAdmin: "a".repeat(12),
    claveProfesor: "0123456789abcdef",
    tokenGaleria: "fedcba9876543210".repeat(2)
};

test("secretos: valores válidos pasan (el administrador sin mínimo de longitud)", () => {
    assert.doesNotThrow(() => validarSecretosApi(BUENOS));
    assert.doesNotThrow(() => validarSecretosApi({ ...BUENOS, claveAdmin: "x" }));
});

test("secretos: el valor de ejemplo se rechaza en cada uno, con el nombre de la variable", () => {
    const ejemplo = "cambiar_esta_clave";
    assert.throws(() => validarSecretosApi({ ...BUENOS, claveAdmin: ejemplo }), /NODERED_ADMIN_PASSWORD/);
    assert.throws(() => validarSecretosApi({ ...BUENOS, claveProfesor: ejemplo }), /NODERED_PROFESOR_PASSWORD/);
    assert.throws(() => validarSecretosApi({ ...BUENOS, tokenGaleria: ejemplo }), /GALERIA_TOKEN/);
});

test("secretos: dos iguales se rechazan, nombrando las dos variables y no el valor", () => {
    const valor = "d".repeat(40);
    const casos = [
        [{ ...BUENOS, claveAdmin: valor, claveProfesor: valor }, /NODERED_ADMIN_PASSWORD.*NODERED_PROFESOR_PASSWORD/],
        [{ ...BUENOS, claveAdmin: valor, tokenGaleria: valor }, /NODERED_ADMIN_PASSWORD.*GALERIA_TOKEN/],
        [{ ...BUENOS, claveProfesor: valor, tokenGaleria: valor }, /NODERED_PROFESOR_PASSWORD.*GALERIA_TOKEN/]
    ];
    for (const [entrada, patron] of casos) {
        assert.throws(() => validarSecretosApi(entrada), (e) => patron.test(e.message) && !e.message.includes(valor));
    }
});

test("secretos: GALERIA_TOKEN de menos de 32 y la contraseña del profesor de menos de 16 se rechazan", () => {
    const token32 = "0123456789abcdef".repeat(2);
    const clave16 = "0123456789abcdef";
    assert.doesNotThrow(() => validarSecretosApi({ ...BUENOS, tokenGaleria: token32 }));
    assert.throws(() => validarSecretosApi({ ...BUENOS, tokenGaleria: token32.slice(1) }), /GALERIA_TOKEN.*32/);
    assert.doesNotThrow(() => validarSecretosApi({ ...BUENOS, claveProfesor: clave16 }));
    assert.throws(() => validarSecretosApi({ ...BUENOS, claveProfesor: clave16.slice(1) }), /NODERED_PROFESOR_PASSWORD.*16/);
});

test("secretos: cualquier valor que contenga el placeholder se rechaza (también el de Portainer)", () => {
    for (const valor of ["cambiar_esta_clave_larga", "xxcambiar_esta_clavexx", "0123456789abcdefcambiar_esta_clave"]) {
        assert.throws(() => validarSecretosApi({ ...BUENOS, claveAdmin: valor }), /NODERED_ADMIN_PASSWORD/, valor);
        assert.throws(() => validarSecretosApi({ ...BUENOS, claveProfesor: valor }), /NODERED_PROFESOR_PASSWORD/, valor);
        assert.throws(() => validarSecretosApi({ ...BUENOS, tokenGaleria: valor }), /GALERIA_TOKEN/, valor);
    }
});

test("secretos: el token y la contraseña del profesor necesitan al menos 8 caracteres distintos", () => {
    assert.throws(() => validarSecretosApi({ ...BUENOS, claveProfesor: "p".repeat(16) }), /NODERED_PROFESOR_PASSWORD.*distintos/);
    assert.throws(() => validarSecretosApi({ ...BUENOS, tokenGaleria: "t".repeat(32) }), /GALERIA_TOKEN.*distintos/);
    assert.throws(() => validarSecretosApi({ ...BUENOS, tokenGaleria: "ab".repeat(16) }), /GALERIA_TOKEN.*distintos/);
    assert.throws(() => validarSecretosApi({ ...BUENOS, claveProfesor: "abcdefg".repeat(3) }), /NODERED_PROFESOR_PASSWORD.*distintos/, "7 distintos");
    assert.doesNotThrow(() => validarSecretosApi({ ...BUENOS, claveProfesor: "abcdefgh".repeat(2) }), "8 distintos");
    assert.doesNotThrow(() => validarSecretosApi({ ...BUENOS, tokenGaleria: "abcdefgh".repeat(4) }), "8 distintos");
});

test("secretos: la contraseña del editor no tiene mínimos de longitud ni de variedad", () => {
    for (const claveAdmin of ["x", "a".repeat(12), "1234"]) {
        assert.doesNotThrow(() => validarSecretosApi({ ...BUENOS, claveAdmin }));
    }
});

test("secretos: los mensajes nunca llevan el valor", () => {
    for (const entrada of [
        { ...BUENOS, tokenGaleria: "CORTO-SECRETO" },
        { ...BUENOS, claveProfesor: "CORTA-SECRETA" },
        { ...BUENOS, claveProfesor: "SSSSSSSSSSSSSSSSSECRETA" },
        { ...BUENOS, claveAdmin: "SECRETA-cambiar_esta_clave" }
    ]) {
        assert.throws(() => validarSecretosApi(entrada), (e) => !/SECRET/.test(e.message));
    }
});

// ---------------------------------------------------------------- httpAdminMiddleware: la API se autentica antes de los parsers del editor

// Lo que importa de una respuesta (sus métodos son funciones distintas en cada objeto).
const datos = (res) => ({ statusCode: res.statusCode, cabeceras: res.cabeceras, cuerpo: res.cuerpo });

const SIN_AUTENTICAR = [
    "/api", "/api/", "/api/alumnos", "/api/alumnos/importar", "/api/enrolamiento/estado", "/api/sesiones/12/csv", "/api/galeria",
    "/API/Alumnos", "/Api/alumnos/", "/aPi", "/API/GALERIA/", "/api/inexistente", "/api/x/y/z"
];

// Rutas que no son de la API: el editor, /auth, los estáticos de /app/, las de administración y variantes que Express no
// enruta a ningún http in (compara la ruta sin decodificar ni normalizar).
const NO_API = [
    "/", "/flows", "/auth/login", "/auth/token", "/settings", "/nodes", "/context/flow/abc", "/app/", "/app/assets/index.js",
    "/apis", "/apix/y", "/api-x", "/apiary", "/x/api/y", "/admin/api", "/%61pi/alumnos", "//api/alumnos", "/ api/x", "", "/red/red.js"
];

test("soloRutasApi: /api, /api/... y sus variantes de mayúsculas y barra final se rechazan sin credenciales, con el mismo 401 y las mismas cabeceras que el middleware directo", () => {
    for (const ruta of SIN_AUTENTICAR) {
        const envuelto = ejecutar({}, { ruta, envolver: true });
        const directo = ejecutar({}, { ruta });
        assert.equal(envuelto.siguio, false, ruta);
        assert.equal(envuelto.res.statusCode, 401, ruta);
        assert.deepEqual(datos(envuelto.res), datos(directo.res), ruta);
        assert.equal(envuelto.res.cabeceras["cache-control"], "no-store");
        assert.equal(envuelto.res.cabeceras["x-content-type-options"], "nosniff");
    }
});

test("soloRutasApi: la galería sigue pidiendo solo su token (con Basic, 401 sin desafío Basic) y /API/Galeria/ cuenta como la galería", () => {
    for (const ruta of ["/api/galeria", "/API/Galeria/"]) {
        const conBasic = ejecutar({}, { ruta, envolver: true, cabeceras: { Authorization: basic(USUARIO, CLAVE) } });
        assert.equal(conBasic.siguio, false, ruta);
        assert.equal(conBasic.res.cabeceras["www-authenticate"], "X-Galeria-Token");
        assert.equal(ejecutar({}, { ruta, envolver: true, cabeceras: { "X-Galeria-Token": TOKEN } }).siguio, true, ruta);
    }
});

test("soloRutasApi: lo que no es /api pasa sin credenciales y sin tocar la respuesta (ni cabeceras, ni estado)", () => {
    for (const ruta of NO_API) {
        for (const metodo of ["GET", "POST"]) {
            const r = ejecutar({}, { ruta, metodo, envolver: true, csrf: false });
            assert.equal(r.siguio, true, metodo + " " + JSON.stringify(ruta));
            assert.deepEqual(r.res.cabeceras, {}, "no fija cabeceras: de eso se encarga cabecerasBasicas de settings.js");
            assert.equal(r.res.statusCode, 200);
            assert.equal(r.res.cuerpo, undefined);
        }
    }
    // Una ruta sin forma de ruta no hace fallar nada: pasa (Express no la enruta).
    for (const path of [undefined, null, 5, {}]) {
        let siguio = false;
        const res = { status() { assert.fail("no debía responder"); } };
        assert.doesNotThrow(() => soloRutasApi(() => assert.fail("no debía autenticar"))({ path }, res, () => { siguio = true; }));
        assert.equal(siguio, true);
    }
});

test("RUTA_API: solo /api, /api/ y lo que cuelga de /api/, sin distinguir mayúsculas", () => {
    for (const ok of ["/api", "/api/", "/api/x", "/API/x", "/aPi/x/y"]) {
        assert.ok(RUTA_API.test(ok), ok);
    }
    for (const no of ["/", "/apix", "/api-x", "/apiX/y", "/%61pi", "//api", "/x/api", " /api", "/api\n", "api/x"]) {
        assert.ok(!RUTA_API.test(no), JSON.stringify(no));
    }
});

// Cada escenario: la petición y lo que debe responder el middleware cuando corre en httpAdminMiddleware, antes de que ningún parser lea un byte.
const ESCENARIOS_CUERPO = [
    ["413: Content-Length mayor que el tope", { metodo: "POST", cabeceras: { Authorization: basic(USUARIO, CLAVE), "Content-Length": String(LIMITE_CUERPO_BYTES + 1) } }, 413],
    ["411: Transfer-Encoding", { metodo: "POST", cabeceras: { Authorization: basic(USUARIO, CLAVE), "Transfer-Encoding": "chunked" } }, 411],
    ["400: Content-Length que no es un número", { metodo: "POST", cabeceras: { Authorization: basic(USUARIO, CLAVE), "Content-Length": "abc" } }, 400],
    ["403: sin X-Pase-Lista", { metodo: "POST", csrf: false, cabeceras: { Authorization: basic(USUARIO, CLAVE), Host: HOST, "Content-Length": "10" } }, 403],
    ["403: de otro origen", { metodo: "POST", cabeceras: { Authorization: basic(USUARIO, CLAVE), Origin: "https://sitio-malo.example", "Content-Length": "10" } }, 403],
    ["403: Sec-Fetch-Site de otro sitio", { metodo: "POST", cabeceras: { Authorization: basic(USUARIO, CLAVE), "Sec-Fetch-Site": "cross-site", "Content-Length": "10" } }, 403],
    ["401: sin credenciales y con un cuerpo enorme (gana el 401: no se mira el cuerpo antes de autenticar)", { metodo: "POST", cabeceras: { "Content-Length": String(50 * 1024 * 1024) } }, 401],
    ["401: credenciales erróneas", { metodo: "POST", cabeceras: { Authorization: basic(USUARIO, "mala"), "Content-Length": "10" } }, 401]
];

test("soloRutasApi: los rechazos por cuerpo, origen o credenciales (401, 403, 411, 413) salen del paso de httpAdminMiddleware, idénticos a los del middleware directo", () => {
    for (const [nombre, peticion, estado] of ESCENARIOS_CUERPO) {
        for (const ruta of ["/api/actuadores", "/API/Alumnos/importar"]) {
            const envuelto = ejecutar({}, { ...peticion, ruta, envolver: true });
            const directo = ejecutar({}, { ...peticion, ruta });
            assert.equal(envuelto.siguio, false, nombre);
            assert.equal(envuelto.res.statusCode, estado, nombre);
            assert.deepEqual(datos(envuelto.res), datos(directo.res), nombre);
        }
    }
});

test("soloRutasApi: una petición aceptada pasa, y que el middleware corra dos veces (httpAdminMiddleware y luego httpNodeMiddleware) da lo mismo que correrlo una", () => {
    const aceptadas = [
        { ruta: "/api/alumnos", cabeceras: { Authorization: basic(USUARIO, CLAVE) } },
        { ruta: "/API/Alumnos/", cabeceras: { Authorization: basic(USUARIO, CLAVE) } },
        { ruta: "/api/galeria", cabeceras: { "X-Galeria-Token": TOKEN } },
        { ruta: "/api/actuadores", metodo: "POST", cabeceras: { Authorization: basic(USUARIO, CLAVE), "Content-Type": "application/json", "Content-Length": "13" } },
        { ruta: "/api/alumnos/importar", metodo: "POST", cabeceras: { Authorization: basic(USUARIO, CLAVE), "Content-Length": String(LIMITE_CUERPO_BYTES) } },
        { ruta: "/api/enrolamiento/probar", metodo: "POST", cabeceras: { Authorization: basic(USUARIO, CLAVE) } }
    ];
    for (const peticion of aceptadas) {
        const una = ejecutar({}, peticion);
        const envuelta = ejecutar({}, { ...peticion, envolver: true });
        const dos = ejecutar({}, { ...peticion, encadenar: true });
        assert.equal(una.siguio, true, peticion.ruta);
        assert.equal(envuelta.siguio, true);
        assert.equal(dos.siguio, true);
        assert.deepEqual(datos(dos.res), datos(una.res), "no cambia la respuesta ni sus cabeceras");
        assert.equal(dos.res.statusCode, 200);
    }
    // Y lo que se rechaza lo rechaza el primer paso, sin llegar al segundo.
    for (const [nombre, peticion, estado] of ESCENARIOS_CUERPO) {
        const dos = ejecutar({}, { ...peticion, ruta: "/api/alumnos", encadenar: true });
        assert.equal(dos.siguio, false, nombre);
        assert.equal(dos.res.statusCode, estado, nombre);
    }
});

// El orden (httpAdminMiddleware antes del bodyParser.json y urlencoded del editor, que leen el cuerpo de cualquier ruta) se verificó
// leyendo el código instalado de Node-RED 5.0.9: @node-red/editor-api/lib/index.js, líneas 50-60 (adminApp.use(httpAdminMiddleware) en
// la 52, bodyParser.json y urlencoded en la 59-60, con el límite apiMaxLength || '5mb' de la 58). Si se sube Node-RED, hay que volver a
// comprobarlo.
test("settings.js: httpAdminMiddleware lleva las cabeceras básicas y después la autenticación de la API solo para /api, y apiMaxLength baja a 1mb", () => {
    const texto = fs.readFileSync(path.join(__dirname, "..", "settings.js"), "utf8");
    assert.match(texto, /const \{ crearMiddlewareApi, soloRutasApi, validarSecretosApi \} = require\("\.\/autenticacion-api"\);/);
    assert.match(texto, /^\s+httpAdminMiddleware: \[cabecerasBasicas, soloRutasApi\(autenticarApi\)\],$/m);
    assert.match(texto, /^\s+httpNodeMiddleware: autenticarApi,$/m, "los http in siguen con su propia autenticación");
    assert.match(texto, /^\s+apiMaxLength: "1mb",$/m);
    assert.equal((texto.match(/apiMaxLength:/g) || []).length, 1);
    // El editor no se queda sin espacio: el despliegue manda todo flows.json (compacto), y la API ya limita su cuerpo.
    const flujos = fs.readFileSync(path.join(__dirname, "..", "flows", "flows.json"), "utf8");
    const compacto = Buffer.byteLength(JSON.stringify(JSON.parse(flujos)), "utf8");
    const tope = 1024 * 1024;
    assert.ok(compacto * 2 < tope, "flows.json compacto (" + compacto + " bytes) debe caber con holgura en 1mb");
    assert.ok(LIMITE_CUERPO_BYTES < tope, "el tope de la API (256 KiB) cabe en el del parser");
});

test("settings.js no cambia httpAdminRoot ni httpNodeRoot: soloRutasApi (RUTA_API, /^\\/api/) supone que el editor y los http in cuelgan de /", () => {
    const texto = fs.readFileSync(path.join(__dirname, "..", "settings.js"), "utf8");
    // Con otra raíz (por ejemplo /editor o /nodos), req.path dentro de httpAdminMiddleware dejaría de ser "/api/..." y la autenticación
    // previa a los parsers no se aplicaría: si algún día se cambia una raíz, hay que cambiar RUTA_API y soloRutasApi con ella.
    const sinComentarios = texto.split("\n").filter((linea) => !/^\s*\/\//.test(linea)).join("\n");
    assert.doesNotMatch(sinComentarios, /\bhttpAdminRoot\b/);
    assert.doesNotMatch(sinComentarios, /\bhttpNodeRoot\b/);
    assert.doesNotMatch(sinComentarios, /\bhttpRoot\b/);
});
