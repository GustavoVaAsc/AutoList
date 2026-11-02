// Pruebas de nodered/autenticacion-api.js (el httpNodeMiddleware de la API).
// Se ejecutan en el host, sin dependencias: node --test nodered/pruebas/*.test.js
"use strict";
const test = require("node:test");
const assert = require("node:assert/strict");
const { crearMiddlewareApi, validarSecretosApi, LIMITE_CUERPO_BYTES } = require("../autenticacion-api");

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
function ejecutar(opciones, { ruta = "/api/alumnos", metodo = "GET", cabeceras = {}, csrf = true } = {}) {
    const middleware = crearMiddlewareApi({ usuario: USUARIO, clave: CLAVE, tokenGaleria: TOKEN, ...opciones });
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
