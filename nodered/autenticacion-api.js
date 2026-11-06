// Autenticación de la API HTTP (/api/...), para usarse como httpNodeMiddleware en
// settings.js. Va dentro de la imagen, junto a settings.js, en /etc/nodered.
//
// Se usa un middleware propio y no httpNodeAuth porque httpNodeAuth es global y
// bloquearía al servicio de visión, que descarga GET /api/galeria solo con su
// token (X-Galeria-Token) y no tiene usuario ni contraseña:
//   - GET /api/galeria: únicamente X-Galeria-Token, igual a GALERIA_TOKEN. Las
//     credenciales del profesor no bastan: así quien tenga la sesión del
//     navegador no puede descargar embeddings (plan, sección 6.7).
//   - Todo lo demás: HTTP Basic con el único usuario del profesor.
// Cualquier ruta que Node-RED sirva por http in y no sea la galería exige Basic,
// aunque no empiece por /api: es la opción que falla cerrado.
// Contra CSRF (el navegador reenvía el Basic guardado aunque la petición la
// dispare otro sitio), los métodos que no son GET ni HEAD, ya autenticados, exigen
// además la cabecera propia X-Pase-Lista: 1 (un sitio ajeno no puede ponerla sin un
// preflight CORS, que esta API no concede) y rechazan un Origin o un
// Sec-Fetch-Site que no sea del mismo origen.
// Los estáticos de /app/ (httpStatic) y el editor (adminAuth) no pasan por aquí.
"use strict";
const crypto = require("crypto");

const REALM = "Pase de lista";
// Tope del cuerpo de cualquier petición de la API. Node-RED lee el cuerpo de los
// http in sin un tope propio fiable para texto crudo (como el CSV de la lista
// maestra), y los parsers corren después de este middleware. Se rechaza antes de
// leerlo, por Content-Length (tras autenticar). Una petición sin longitud
// conocida (Transfer-Encoding) no se admite: no se podría acotar.
const LIMITE_CUERPO_BYTES = 256 * 1024;
const RUTA_GALERIA = "/api/galeria";
const EJEMPLO = "cambiar_esta_clave"; // el placeholder de orangepi/.env.example
const MIN_TOKEN_GALERIA = 32;
const MIN_CLAVE_PROFESOR = 16;
const MIN_CARACTERES_DISTINTOS = 8;

// Falla cerrado ante secretos de ejemplo, repetidos o cortos. Los mensajes nombran
// las variables, nunca sus valores. La contraseña del editor solo está sujeta al
// placeholder y a no repetirse: ya está desplegada y no se quiere romper el arranque
// por ella con mínimos nuevos.
function validarSecretosApi({ claveAdmin, claveProfesor, tokenGaleria }) {
    const secretos = [
        ["NODERED_ADMIN_PASSWORD", claveAdmin],
        ["NODERED_PROFESOR_PASSWORD", claveProfesor],
        ["GALERIA_TOKEN", tokenGaleria]
    ];
    for (const [nombre, valor] of secretos) {
        // includes y no ===: también cae cambiar_esta_clave_larga (el de Portainer).
        if (String(valor).includes(EJEMPLO)) {
            throw new Error("autenticacion-api: " + nombre + " contiene el valor de ejemplo de .env.example: cámbialo (por ejemplo, con openssl rand -hex 16)");
        }
    }
    for (let i = 0; i < secretos.length; i++) {
        for (let j = i + 1; j < secretos.length; j++) {
            if (secretos[i][1] === secretos[j][1]) {
                throw new Error("autenticacion-api: " + secretos[i][0] + " y " + secretos[j][0] + " tienen el mismo valor: deben ser distintos");
            }
        }
    }
    if (String(tokenGaleria).length < MIN_TOKEN_GALERIA) {
        throw new Error("autenticacion-api: GALERIA_TOKEN debe tener al menos " + MIN_TOKEN_GALERIA + " caracteres (openssl rand -hex 16 da 32)");
    }
    if (String(claveProfesor).length < MIN_CLAVE_PROFESOR) {
        throw new Error("autenticacion-api: NODERED_PROFESOR_PASSWORD debe tener al menos " + MIN_CLAVE_PROFESOR + " caracteres (openssl rand -hex 16 da 32)");
    }
    for (const [nombre, valor] of [["GALERIA_TOKEN", tokenGaleria], ["NODERED_PROFESOR_PASSWORD", claveProfesor]]) {
        if (new Set(String(valor)).size < MIN_CARACTERES_DISTINTOS) {
            throw new Error("autenticacion-api: " + nombre + " debe tener al menos " + MIN_CARACTERES_DISTINTOS + " caracteres distintos entre sí (openssl rand -hex 16 cumple)");
        }
    }
}

// Comparación en tiempo constante que no filtra la longitud: se comparan los
// SHA-256 de los dos textos, que miden siempre lo mismo (timingSafeEqual exige
// longitudes iguales).
function sonIguales(a, b) {
    const ha = crypto.createHash("sha256").update(String(a), "utf8").digest();
    const hb = crypto.createHash("sha256").update(String(b), "utf8").digest();
    return crypto.timingSafeEqual(ha, hb);
}

// Usuario y contraseña del encabezado Authorization: Basic, o null si no hay o
// no tiene la forma esperada. La contraseña puede llevar ":" (RFC 7617).
function leerBasic(req) {
    const encabezado = req.headers.authorization;
    if (typeof encabezado !== "string") {
        return null;
    }
    const partes = /^basic +([A-Za-z0-9+/=]+) *$/i.exec(encabezado);
    if (partes === null) {
        return null;
    }
    const texto = Buffer.from(partes[1], "base64").toString("utf8");
    const separador = texto.indexOf(":");
    if (separador < 0) {
        return null;
    }
    return { usuario: texto.slice(0, separador), clave: texto.slice(separador + 1) };
}

// Mismo criterio de ruta que Express (sin distinguir mayúsculas y con o sin barra
// final): un http in en /api/galeria también atiende /API/Galeria/, y esa petición
// debe tratarse como la galería, no como otra ruta.
function esGaleria(req) {
    const ruta = String(req.path).toLowerCase().replace(/\/+$/, "");
    return ruta === RUTA_GALERIA;
}

function crearMiddlewareApi({ usuario, clave, tokenGaleria }) {
    if (!usuario || !clave || !tokenGaleria) {
        throw new Error("autenticacion-api: faltan el usuario, la contraseña del profesor o el token de la galería");
    }
    if (usuario.includes(":")) {
        throw new Error("autenticacion-api: el usuario del profesor no puede llevar dos puntos (HTTP Basic)");
    }

    function rechazar(res, desafio) {
        res.status(401).set("WWW-Authenticate", desafio).json({ error: "Autenticación requerida" });
    }

    // Tras autenticar, en los métodos que cambian estado: defensa contra CSRF.
    // Responde 403 y devuelve false si la petición no viene del frontend (o de un
    // cliente que manda la cabecera propia, como curl).
    function origenAceptable(req, res) {
        if (req.method === "GET" || req.method === "HEAD") {
            return true;
        }
        const sitio = req.headers["sec-fetch-site"];
        const origen = req.headers.origin;
        const host = req.headers.host;
        if (sitio !== undefined && sitio !== "same-origin") {
            res.status(403).json({ error: "Petición de otro origen rechazada" });
            return false;
        }
        if (origen !== undefined && (typeof host !== "string" || origen !== "https://" + host)) {
            res.status(403).json({ error: "Petición de otro origen rechazada" });
            return false;
        }
        if (req.headers["x-pase-lista"] !== "1") {
            res.status(403).json({ error: "Falta la cabecera X-Pase-Lista: 1 en las peticiones que cambian datos" });
            return false;
        }
        return true;
    }

    // Tras autenticar: responde y devuelve false si el cuerpo no se puede aceptar.
    function cuerpoAceptable(req, res) {
        if (req.headers["transfer-encoding"] !== undefined) {
            res.status(411).json({ error: "Se requiere Content-Length: no se admite Transfer-Encoding" });
            return false;
        }
        const longitud = req.headers["content-length"];
        if (longitud === undefined) {
            return true;
        }
        if (!/^[0-9]{1,15}$/.test(longitud)) {
            res.status(400).json({ error: "Content-Length no es válido" });
            return false;
        }
        if (Number(longitud) > LIMITE_CUERPO_BYTES) {
            res.status(413).json({ error: "El cuerpo supera el máximo de " + LIMITE_CUERPO_BYTES + " bytes" });
            return false;
        }
        return true;
    }

    return function autenticarApi(req, res, next) {
        // Cabeceras de seguridad de editor y API, y sin caché: las respuestas
        // llevan datos personales (y, en la galería, biométricos).
        res.set({
            "X-Content-Type-Options": "nosniff",
            "X-Frame-Options": "DENY",
            "Cache-Control": "no-store"
        });

        if (esGaleria(req)) {
            const token = req.get("X-Galeria-Token");
            // "X-Galeria-Token" como esquema: no es Basic, así el navegador no
            // abre su diálogo de usuario y contraseña en esta ruta.
            if (typeof token !== "string" || !sonIguales(token, tokenGaleria)) {
                return rechazar(res, "X-Galeria-Token");
            }
        } else {
            const credenciales = leerBasic(req);
            // Ambas comparaciones se hacen siempre: así el tiempo no revela si
            // acertó el usuario.
            const usuarioOk = sonIguales(credenciales === null ? "" : credenciales.usuario, usuario);
            const claveOk = sonIguales(credenciales === null ? "" : credenciales.clave, clave);
            if (credenciales === null || !usuarioOk || !claveOk) {
                return rechazar(res, 'Basic realm="' + REALM + '", charset="UTF-8"');
            }
        }

        if (!origenAceptable(req, res) || !cuerpoAceptable(req, res)) {
            return undefined;
        }
        return next();
    };
}

module.exports = { crearMiddlewareApi, validarSecretosApi, LIMITE_CUERPO_BYTES };
