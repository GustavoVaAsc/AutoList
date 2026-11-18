// Ajustes de Node-RED para la Orange Pi. Las credenciales llegan por variables
// de entorno (orangepi/.env); aquí no hay ningún secreto.
// Este archivo va dentro de la imagen, en /etc/nodered (directorio de root que el
// usuario node-red no puede modificar): tras cambiarlo hay que reconstruirla
// (docker compose up -d --build).
const fs = require("fs");
const { execFileSync } = require("child_process");
const bcrypt = require("bcryptjs");
const { crearMiddlewareApi, validarSecretosApi } = require("./autenticacion-api");
const { crearManejadorLog } = require("./registro-log");

const usuario = process.env.NODERED_ADMIN_USER;
const clave = process.env.NODERED_ADMIN_PASSWORD;
if (!usuario || !clave) {
    throw new Error("Faltan NODERED_ADMIN_USER o NODERED_ADMIN_PASSWORD: sin ellas el editor quedaría abierto (nodo exec = ejecución remota de código)");
}

// Autenticación de la API (/api/...): el profesor entra con HTTP Basic y el
// servicio de visión descarga la galería con X-Galeria-Token (ver
// autenticacion-api.js). Falla cerrado: si falta cualquiera de las tres, Node-RED
// no arranca, en lugar de servir la API (alumnos y embeddings) sin autenticar.
// Tampoco arranca con secretos de ejemplo, repetidos o cortos (validarSecretosApi).
const usuarioProfesor = process.env.NODERED_PROFESOR_USER;
const claveProfesor = process.env.NODERED_PROFESOR_PASSWORD;
const tokenGaleria = process.env.GALERIA_TOKEN;
if (!usuarioProfesor || !claveProfesor || !tokenGaleria) {
    throw new Error("Faltan NODERED_PROFESOR_USER, NODERED_PROFESOR_PASSWORD o GALERIA_TOKEN: sin ellas la API (alumnos y embeddings) quedaría abierta");
}
// Rechaza (sin mostrar valores) los secretos de ejemplo, repetidos o cortos.
validarSecretosApi({ claveAdmin: clave, claveProfesor: claveProfesor, tokenGaleria: tokenGaleria });
const autenticarApi = crearMiddlewareApi({ usuario: usuarioProfesor, clave: claveProfesor, tokenGaleria: tokenGaleria });

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
    // Sonner (avisos del frontend) intenta insertar una etiqueta <style> al cargar y
    // esta directiva la bloquea: la consola del navegador registra una violación de
    // CSP y es esperada. Es inofensiva, porque web/src/main.tsx importa la hoja de
    // Sonner como archivo propio. No la relajes con 'unsafe-inline'.
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
    // httpNodeMiddleware (existe httpStaticAuth, aparte): el frontend no lleva
    // secretos y sus datos salen de /api/..., que sí exige autenticación.
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

    // Editor: solo nosniff y no poder incrustarse en marcos. Sin CSP, que rompería
    // el editor. Node-RED monta el editor en / antes que lo demás, así que
    // httpAdminMiddleware alcanza también las demás rutas. Sin HSTS.
    httpAdminMiddleware: cabecerasBasicas,

    // API (las rutas http in de flows.json): las mismas cabeceras, sin caché, más
    // la autenticación de autenticacion-api.js. No se usa httpNodeAuth porque es
    // global y bloquearía al servicio de visión en GET /api/galeria.
    httpNodeMiddleware: autenticarApi,

    // Contexto de los flujos. El almacén por defecto sigue en memoria: la inscripción abierta, las
    // marcas de arranque y el resto se pierden al reiniciar a propósito (plan, sección 5.4, paso 10).
    // "persistente" (localfilesystem, en /data/context del volumen nodered_data) es solo para lo que
    // debe sobrevivir a un reinicio: hoy, flow.retiros_pendientes de la pestaña Registro (rostros
    // guardados tras cerrarse el registro cuyo retiro falló; contienen cuenta y fecha, no embeddings).
    // Un flow.get/flow.set lo usa pasando "persistente" como último argumento. flushInterval de 1 s
    // (el valor por defecto es 30 s) para perder lo mínimo si el proceso muere sin cerrar limpio;
    // en un cierre normal Node-RED vacía la caché al disco. Cambiar esto exige reconstruir la imagen.
    contextStorage: {
        default: { module: "memory" },
        persistente: { module: "localfilesystem", config: { dir: "/data", flushInterval: 1, cache: true } }
    },

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

    // Registro: la consola de siempre (mismo nivel, sin métricas ni auditoría), pero con un manejador propio que
    // enmascara los números de cuenta de toda línea y recorta las de los http in (registro-log.js explica por qué:
    // un cuerpo JSON mal formado lo rechaza el body-parser antes del flujo y su mensaje cita un fragmento del cuerpo).
    // Con la clave "console" y un `handler`, Node-RED usa este manejador en lugar de la consola por defecto.
    logging: {
        console: { level: "info", metrics: false, audit: false, handler: crearManejadorLog() }
    }
};
