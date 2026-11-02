// Ajustes de Node-RED para la Orange Pi. Las credenciales llegan por variables
// de entorno (orangepi/.env); aquí no hay ningún secreto.
// Este archivo va dentro de la imagen, en /etc/nodered (directorio de root que el
// usuario node-red no puede modificar): tras cambiarlo hay que reconstruirla
// (docker compose up -d --build).
const fs = require("fs");
const { execFileSync } = require("child_process");
const bcrypt = require("bcryptjs");

const usuario = process.env.NODERED_ADMIN_USER;
const clave = process.env.NODERED_ADMIN_PASSWORD;
if (!usuario || !clave) {
    throw new Error("Faltan NODERED_ADMIN_USER o NODERED_ADMIN_PASSWORD: sin ellas el editor quedaría abierto (nodo exec = ejecución remota de código)");
}

// HTTPS con certificado autofirmado, para el editor, la API y los archivos
// estáticos. Se genera en el primer arranque, y de nuevo si cambia
// NODERED_TLS_SAN, y se guarda en el volumen /data (nunca en git) con la llave
// en modo 600. Para forzar uno nuevo, borrar /data/certs del volumen y reiniciar.
// Si no se puede generar, la excepción impide el arranque (no hay HTTP de respaldo).
const sanCertificado = process.env.NODERED_TLS_SAN || "DNS:localhost,IP:127.0.0.1";
const dirCertificados = "/data/certs";
const archivoLlave = dirCertificados + "/nodered.key";
const archivoCertificado = dirCertificados + "/nodered.crt";
const archivoSan = dirCertificados + "/nodered.san"; // SAN con el que se generaron
function certificadoVigente() {
    return [archivoLlave, archivoCertificado, archivoSan].every((f) => fs.existsSync(f)) &&
        fs.readFileSync(archivoSan, "utf8") === sanCertificado;
}
if (!certificadoVigente()) {
    fs.mkdirSync(dirCertificados, { recursive: true, mode: 0o700 });
    // Se genera en temporales y se renombra al final; el archivo del SAN va
    // último, así un corte a medias obliga a regenerar.
    const temporales = [archivoLlave, archivoCertificado, archivoSan].map((f) => f + ".tmp");
    const [tmpLlave, tmpCertificado, tmpSan] = temporales;
    try {
        execFileSync("openssl", [
            "req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:prime256v1",
            "-nodes", "-days", "3650", "-subj", "/CN=nodered",
            "-addext", "subjectAltName=" + sanCertificado,
            "-keyout", tmpLlave, "-out", tmpCertificado
        ], { stdio: "pipe" });
        fs.chmodSync(tmpLlave, 0o600);
        fs.writeFileSync(tmpSan, sanCertificado, { mode: 0o644 });
        fs.renameSync(tmpLlave, archivoLlave);
        fs.renameSync(tmpCertificado, archivoCertificado);
        fs.renameSync(tmpSan, archivoSan);
    } finally {
        temporales.forEach((f) => fs.rmSync(f, { force: true }));
    }
}

// El nodo mqtt-broker solo acepta usuario y contraseña como "credenciales", y
// Node-RED las lee de /data/flows_cred.json. Se escribe ahí, en cada arranque,
// el usuario "nodered" (el del contrato MQTT, sección 6; no es un secreto) y el
// texto ${MQTT_NODERED_PASSWORD}, que Node-RED sustituye por la variable de
// entorno. El broker usa el nombre de usuario como client id
// (use_username_as_clientid): el clientid del nodo en flows.json se ignora. El
// archivo vive en el volumen, nunca en git. Se conservan las demás credenciales
// que existan. (El nodo de Postgres no lo necesita: lee sus variables de
// entorno directamente.)
const ID_BROKER_MQTT = "a1f0c0de00000002"; // debe coincidir con el id del nodo "Mosquitto" en nodered/flows/flows.json
const archivoCredenciales = "/data/flows_cred.json";
let credenciales = {};
try {
    credenciales = JSON.parse(fs.readFileSync(archivoCredenciales, "utf8"));
} catch (e) {
    if (e.code !== "ENOENT" && !(e instanceof SyntaxError)) {
        throw e;
    }
}
// "$" marca un archivo cifrado (de un arranque anterior con otra clave): se regenera.
if (credenciales === null || typeof credenciales !== "object" || "$" in credenciales) {
    credenciales = {};
}
credenciales[ID_BROKER_MQTT] = { user: "nodered", password: "${MQTT_NODERED_PASSWORD}" };
fs.writeFileSync(archivoCredenciales, JSON.stringify(credenciales), { mode: 0o600 });

// Cabeceras de seguridad. CSP del frontend: todo solo del mismo origen (el
// frontend no usa CDN ni fuentes externas y no habla MQTT; connect-src 'self'
// cubre la API), sin objetos, sin <base>, sin marcos que lo contengan.
const CSP_FRONTEND = [
    "default-src 'self'",
    "script-src 'self'",
    "style-src 'self'",
    "img-src 'self' data:",
    "font-src 'self'",
    "connect-src 'self'",
    "object-src 'none'",
    "base-uri 'none'",
    "form-action 'self'",
    "frame-ancestors 'none'"
].join("; ");
const cabecerasBasicas = (req, res, next) => {
    res.set({
        "X-Content-Type-Options": "nosniff",
        "X-Frame-Options": "DENY"
    });
    next();
};

module.exports = {
    // Credenciales sin cifrar: con ${VAR} no hay secretos en el archivo.
    credentialSecret: false,

    uiPort: process.env.PORT || 1880,

    // Un solo servidor HTTPS: el editor, /api/... y httpStatic.
    https: {
        key: fs.readFileSync(archivoLlave),
        cert: fs.readFileSync(archivoCertificado)
    },

    // Editor protegido con usuario y contraseña. bcrypt se calcula al arrancar
    // a partir de la variable de entorno, así no se guarda ningún hash en git.
    adminAuth: {
        type: "credentials",
        users: [{
            username: usuario,
            password: bcrypt.hashSync(clave, 8),
            permissions: "*"
        }]
    },

    // Frontend compilado (web/), copiado a la imagen por nodered/Dockerfile en un
    // directorio de root. Se sirve en /app/ para no tapar el editor, que sigue en
    // /; Vite compila con base '/app/' (web/vite.config.ts). httpStatic no pasa por
    // httpNodeAuth (existe httpStaticAuth, aparte): el frontend no lleva secretos y
    // sus datos salen de /api/..., que sí tendrá httpNodeAuth.
    // "middleware" (propio de cada entrada de httpStatic; no existe un
    // httpStaticMiddleware global) fija las cabeceras de seguridad solo de /app/.
    // La CSP no permite nada de otros orígenes; el CSS y el JS del build son
    // archivos propios y la gráfica asigna estilos por el DOM, que la CSP no bloquea.
    httpStatic: [{
        path: "/usr/share/nodered-web",
        root: "/app/",
        middleware: (req, res, next) => {
            res.set({
                "Content-Security-Policy": CSP_FRONTEND,
                "X-Content-Type-Options": "nosniff",
                "Referrer-Policy": "no-referrer",
                "X-Frame-Options": "DENY"
            });
            next();
        }
    }],

    // Editor y API: solo nosniff y no poder incrustarse en marcos. Sin CSP, que
    // rompería el editor. Node-RED monta el editor en / antes que lo demás, así que
    // httpAdminMiddleware alcanza todas las rutas; httpNodeMiddleware lo repite
    // para las rutas http in (la API) por si el editor se mueve de raíz. Sin HSTS.
    httpAdminMiddleware: cabecerasBasicas,
    httpNodeMiddleware: cabecerasBasicas,

    // La API HTTP (/api/...) queda abierta por ahora. httpNodeAuth entra el
    // 11 nov (plan, sección 12): se añade aquí con { user, pass: <hash bcrypt> }.

    // flows.json legible en los diffs de git.
    flowFilePretty: true,

    // Sin proyectos de Git del editor.
    editorTheme: {
        projects: { enabled: false }
    },

    // Sin instalar ni subir módulos npm, ni desde la paleta ni desde los nodos
    // function: los nodos se agregan en nodered/Dockerfile.
    functionExternalModules: false,
    externalModules: {
        palette: { allowInstall: false, allowUpload: false },
        modules: { allowInstall: false }
    },

    logging: {
        console: { level: "info", metrics: false, audit: false }
    }
};
