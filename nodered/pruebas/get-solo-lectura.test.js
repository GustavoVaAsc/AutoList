// Regla: ningún `http in` con método GET (ni HEAD) cambia datos ni publica en MQTT.
// El middleware de la API no exige X-Pase-Lista en GET y HEAD (autenticacion-api.js:
// la defensa contra CSRF depende de que esos métodos sean de solo lectura), así que
// una ruta GET que escribiera quedaría expuesta a CSRF.
// La prueba recorre flows.json: desde cada `http in` GET sigue los cables (y los
// `link out` y los `catch` que cubren un nodo alcanzado) y falla si llega a un nodo
// `postgresql` con INSERT, UPDATE, DELETE, TRUNCATE o ALTER (o sin consulta fija, que
// no se puede revisar) o a un `mqtt out`. Sirve igual para los flujos futuros.
// Sin dependencias: node --test nodered/pruebas/*.test.js
"use strict";
const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

const ESCRITURA = /\b(INSERT|UPDATE|DELETE|TRUNCATE|ALTER)\b/i;
const METODOS_DE_LECTURA = ["get", "head"];

// Devuelve las violaciones: [{ ruta: "get /api/x", nodo: "<nombre> (<id>)", motivo }].
function buscarViolaciones(nodos) {
    const porId = new Map(nodos.map((n) => [n.id, n]));
    const violaciones = [];
    for (const entrada of nodos.filter((n) => n.type === "http in" && METODOS_DE_LECTURA.includes(n.method))) {
        const ruta = entrada.method + " " + entrada.url;
        const alcanzados = new Set([entrada.id]);
        const pendientes = [entrada.id];
        const alcanzar = (id) => {
            if (porId.has(id) && !alcanzados.has(id)) {
                alcanzados.add(id);
                pendientes.push(id);
            }
        };
        for (;;) {
            while (pendientes.length > 0) {
                const nodo = porId.get(pendientes.pop());
                for (const salida of nodo.wires || []) {
                    salida.forEach(alcanzar);
                }
                if (nodo.type === "link out") {
                    (nodo.links || []).forEach(alcanzar);
                }
            }
            // Un `catch` recibe los errores de los nodos de su alcance (o de todos, sin
            // alcance): lo que cuelga de él también corre dentro de la petición GET.
            const nuevos = nodos.filter((n) => n.type === "catch" && !alcanzados.has(n.id) &&
                (!Array.isArray(n.scope) || n.scope.some((id) => alcanzados.has(id))));
            if (nuevos.length === 0) {
                break;
            }
            nuevos.forEach((n) => alcanzar(n.id));
        }
        for (const id of alcanzados) {
            const nodo = porId.get(id);
            const nombre = (nodo.name || nodo.type) + " (" + nodo.id + ")";
            if (nodo.type === "mqtt out") {
                violaciones.push({ ruta, nodo: nombre, motivo: "publica en MQTT" });
            } else if (nodo.type === "postgresql") {
                if (typeof nodo.query !== "string" || nodo.query.trim() === "") {
                    violaciones.push({ ruta, nodo: nombre, motivo: "consulta no fija: no se puede comprobar que solo lea" });
                } else if (ESCRITURA.test(nodo.query)) {
                    violaciones.push({ ruta, nodo: nombre, motivo: "su consulta escribe (" + nodo.query.match(ESCRITURA)[1].toUpperCase() + ")" });
                }
            }
        }
    }
    return violaciones;
}

test("la prueba detecta un GET que escribe en Postgres, publica en MQTT o llega por un enlace", () => {
    const sintetico = [
        { id: "g1", type: "http in", method: "get", url: "/api/malo", wires: [["f1"]] },
        { id: "f1", type: "function", name: "paso", wires: [["pg1", "mq1"]] },
        { id: "pg1", type: "postgresql", name: "borra", query: "update alumnos set nombre = 'x'", wires: [[]] },
        { id: "mq1", type: "mqtt out", name: "publica", topic: "aula/x", wires: [] },
        { id: "g2", type: "http in", method: "get", url: "/api/malo2", wires: [["lo1"]] },
        { id: "lo1", type: "link out", links: ["li1"], wires: [] },
        { id: "li1", type: "link in", links: ["lo1"], wires: [["pg2"]] },
        { id: "pg2", type: "postgresql", name: "dinamico", query: "", wires: [[]] },
        { id: "g3", type: "http in", method: "get", url: "/api/malo3", wires: [["pg3"]] },
        { id: "pg3", type: "postgresql", name: "sutil", query: "WITH x AS (DELETE FROM alumnos RETURNING 1) SELECT 1", wires: [[]] },
        { id: "g4", type: "http in", method: "get", url: "/api/malo4", wires: [["f4"]] },
        { id: "f4", type: "function", name: "con error", wires: [[]] },
        { id: "c4", type: "catch", scope: ["f4"], wires: [["mq4"]] },
        { id: "mq4", type: "mqtt out", name: "publica en error", wires: [] }
    ];
    const rutas = new Set(buscarViolaciones(sintetico).map((v) => v.ruta));
    assert.deepEqual([...rutas].sort(), ["get /api/malo", "get /api/malo2", "get /api/malo3", "get /api/malo4"]);
    const motivos = buscarViolaciones(sintetico).map((v) => v.nodo + ": " + v.motivo).join("\n");
    assert.match(motivos, /borra/);
    assert.match(motivos, /publica/);
    assert.match(motivos, /dinamico/);
});

test("la prueba no se queja de un GET que solo lee, ni de un POST que escribe", () => {
    const sano = [
        { id: "g1", type: "http in", method: "get", url: "/api/ok", wires: [["pg1"]] },
        { id: "pg1", type: "postgresql", name: "lee", query: "SELECT updated_at, insertado FROM t WHERE x = $1", wires: [["r1"]] },
        { id: "r1", type: "http response", wires: [] },
        { id: "p1", type: "http in", method: "post", url: "/api/escribe", wires: [["pg2"]] },
        { id: "pg2", type: "postgresql", name: "escribe", query: "INSERT INTO t VALUES ($1)", wires: [[]] }
    ];
    assert.deepEqual(buscarViolaciones(sano), []);
});

test("la prueba termina con ciclos en los cables", () => {
    const ciclo = [
        { id: "g1", type: "http in", method: "get", url: "/api/c", wires: [["f1"]] },
        { id: "f1", type: "function", wires: [["f2"]] },
        { id: "f2", type: "function", wires: [["f1"]] }
    ];
    assert.deepEqual(buscarViolaciones(ciclo), []);
});

test("flows.json: ningún http in GET ni HEAD cambia datos ni publica en MQTT", () => {
    const flujos = JSON.parse(fs.readFileSync(path.join(__dirname, "..", "flows", "flows.json"), "utf8"));
    const rutasGet = flujos.filter((n) => n.type === "http in" && METODOS_DE_LECTURA.includes(n.method));
    assert.ok(rutasGet.length >= 3, "se esperaban las rutas GET de la API");
    assert.deepEqual(buscarViolaciones(flujos), []);
});
