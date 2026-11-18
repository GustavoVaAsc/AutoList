// Registro de Node-RED (logging de settings.js): el mismo formato de la consola por defecto, con los números de
// cuenta enmascarados. Va dentro de la imagen, junto a settings.js, en /etc/nodered.
//
// Por qué existe: un cuerpo JSON mal formado en un POST lo rechaza el body-parser del nodo http in antes de llegar al
// flujo, y el errorHandler de ese nodo hace node.warn(err). El mensaje de V8 de JSON.parse cita un fragmento del
// cuerpo, que puede traer un número de cuenta (un dato personal que el resto del proyecto nunca registra:
// nodered/AGENTS.md, secciones SQL y Pase de lista). Ningún nodo puede evitarlo, porque el error ocurre antes del
// flujo. Todo lo que Node-RED escribe en su consola pasa por un manejador de registro (nodes log/warn/error, el
// debug con console: true, el runtime), así que se enmascara ahí, en un solo lugar.
//   - Toda línea: una racha de 9 o más dígitos (un no_cuenta o algo más largo) se sustituye por *********.
//   - Líneas de nodos http in: además se enmascaran las rachas de 4 a 8 dígitos (la ventana que cita V8 puede cortar
//     una cuenta y dejar 8 de sus 9 dígitos a la vista) y el mensaje se acorta a 200 caracteres.
//     Los saltos de línea (CR y LF) se aplanan a un espacio: el fragmento del cuerpo que cita V8 es texto del que
//     envía la petición y no puede partir la línea del registro ni hacerse pasar por otra.
//     El enmascarado va antes del recorte, para no cortar una racha por la mitad y dejarla sin enmascarar.
// Con el nivel, las métricas y la auditoría de settings.js, el formato es el de la consola por defecto de Node-RED
// (@node-red/util, log.js): "16 Nov 15:30:12 - [warn] [http in:<id>] <mensaje>". Sin colores (chalk no colorea
// sin una terminal, y la consola del contenedor no lo es). `settings.verbose` se ignora a propósito: con él la consola
// por defecto escribe la pila (stack) del error, que no se imprime nunca aquí, ni la de los errores de http in. En las líneas de métricas y auditoría solo se
// enmascaran los valores de texto, no los números (la marca de tiempo tiene 13 dígitos).
// El manejador nunca lanza: si algo falla al dar formato, escribe una línea fija con el nombre del error y sin el
// contenido del mensaje.
"use strict";
const util = require("util");

const METRICA = 99;
const AUDITORIA = 98;
const NOMBRES_NIVEL = { 10: "fatal", 20: "error", 30: "warn", 40: "info", 50: "debug", 60: "trace", 98: "audit", 99: "metric" };
const MESES = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];
const MAX_CARACTERES_HTTP_IN = 200;
const MARCA_RECORTE = " [recortado]";
const TIPO_HTTP_IN = "http in";

// `corto` además enmascara las rachas de 4 a 8 dígitos.
function enmascarar(texto, corto = false) {
    let resultado = String(texto).replace(/[0-9]{9,}/g, "*********");
    if (corto) {
        resultado = resultado.replace(/[0-9]{4,8}/g, "****");
    }
    return resultado;
}

function dosDigitos(n) {
    return String(n).padStart(2, "0");
}

// La marca de tiempo del formato por defecto: "16 Nov 15:30:12" (hora local, sin año).
function marcaDeTiempo(fecha) {
    return fecha.getDate() + " " + MESES[fecha.getMonth()] + " " + dosDigitos(fecha.getHours()) + ":" + dosDigitos(fecha.getMinutes()) + ":" + dosDigitos(fecha.getSeconds());
}

// Lo mismo que hace la consola por defecto con msg.msg: un objeto con `message` (que no sea un Error: su toString
// no es "[object Object]") se registra por su mensaje; lo demás, por su texto.
function textoDelMensaje(mensaje) {
    try {
        if (typeof mensaje === "object" && mensaje !== null && mensaje.toString() === "[object Object]" && mensaje.message) {
            mensaje = mensaje.message;
        }
    } catch (e) {
        mensaje = "Exception trying to log: " + util.inspect(mensaje);
    }
    return String(mensaje);
}

// La línea que se escribe para un mensaje de registro de Node-RED ({level, msg, type, name, id, timestamp}).
function formatearLinea(msg, ahora = new Date()) {
    const nivel = NOMBRES_NIVEL[msg.level];
    let cuerpo;
    if (msg.level === METRICA || msg.level === AUDITORIA) {
        cuerpo = "[" + nivel + "] " + JSON.stringify(msg, (clave, valor) => (typeof valor === "string" ? enmascarar(valor) : valor));
    } else {
        const esHttpIn = msg.type === TIPO_HTTP_IN;
        const origen = msg.type ? "[" + msg.type + ":" + (msg.name || msg.id) + "] " : "";
        let mensaje = enmascarar(textoDelMensaje(msg.msg), esHttpIn);
        if (esHttpIn) {
            mensaje = mensaje.replace(/[\r\n]+/g, " ");
        }
        if (esHttpIn && mensaje.length > MAX_CARACTERES_HTTP_IN) {
            mensaje = mensaje.slice(0, MAX_CARACTERES_HTTP_IN) + MARCA_RECORTE;
        }
        cuerpo = "[" + nivel + "] " + enmascarar(origen) + mensaje; // el id del nodo lleva rachas de 8 dígitos: no se toca con el enmascarado corto
    }
    return marcaDeTiempo(ahora) + " - " + cuerpo;
}

// `settings.handler` de Node-RED: recibe la configuración del registro y devuelve la función que atiende cada mensaje.
// `escribir` es la salida (console.log en producción; la prueba pasa otra).
function crearManejadorLog(escribir = console.log) {
    return function () {
        return function (msg) {
            let linea;
            try {
                linea = formatearLinea(msg);
            } catch (error) {
                // Sin el mensaje: lo que falló al formatearlo podría ser justo lo que no se debe escribir.
                linea = marcaDeTiempo(new Date()) + " - [error] No se pudo dar formato a un mensaje de registro (" +
                    (error && typeof error.name === "string" ? error.name.slice(0, 40) : "error") + ")";
            }
            try {
                escribir(linea);
            } catch (e) {
                // La salida estándar cerrada (EPIPE) no debe tumbar a Node-RED por un mensaje de registro.
            }
        };
    };
}

module.exports = { enmascarar, formatearLinea, crearManejadorLog, MAX_CARACTERES_HTTP_IN };
