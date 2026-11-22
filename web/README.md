# Frontend web (AutoList)

Panel del profesor, en Vite + React + TypeScript (plan, sección 6.7). Se compila al construir la imagen de Node-RED, que lo sirve en `https://<ip-de-la-placa>:1880/app/`. El editor de Node-RED sigue en `/`.

## Contenido

- [Estado](#estado)
- [Requisitos](#requisitos)
- [Desarrollo desde una laptop](#desarrollo-desde-una-laptop)
- [Comandos](#comandos)
- [Despliegue](#despliegue)
- [Estructura](#estructura)
- [Dependencias](#dependencias)
- [Decisiones](#decisiones)

## Estado

| Vista | Estado |
|---|---|
| Asistencia | Funciona la asistencia por sesión (`GET /api/sesiones`, `GET /api/sesiones/:id` y descarga del reporte `GET /api/sesiones/:id/csv` al cerrar la sesión), la similitud de la sesión y los intentos rechazados (`desconocido`, `duplicado`, `imagen_insuficiente`), estos plegados bajo la tabla nominal. Es la vista inicial. |
| Métricas | En este orden: Sistema (`GET /api/sistema/estado`, cada 5 s: visión, cámara y actuadores en línea, y temperatura, CPU y RAM de la placa), "Probar reconocimiento" (`POST /api/enrolamiento/probar` y `GET /api/enrolamiento/prueba`), Actuadores (`POST /api/actuadores`, con confirmación para abrir la puerta), la sección de energía (`GET /api/metricas/energia`, cada 5 s): potencia actual, gráfica de los últimos 30 min y tabla de lecturas recientes, y la latencia de inferencia (`GET /api/metricas/latencia`, cada 5 s, 100 mediciones) con su resumen y su tabla de `origen = 'npu'`. |
| Alumnos | Funciona: lista de alumnos con filtro por grupo, búsqueda por nombre o cuenta (en el cliente, sin acentos ni mayúsculas) y estado del rostro (`GET /api/alumnos`, cada 10 s); en cada renglón, "Registrar rostro" (va a Registro con la cuenta elegida) si está pendiente o "Retirar rostro" (confirmación con `<dialog>` y `DELETE /api/alumnos/:cuenta/rostro`) si ya está registrado; e importación de la lista maestra en CSV (`POST /api/alumnos/importar`) con zona para soltar el archivo, diálogo para las cuentas existentes y avisos. La búsqueda y el retiro del rostro se verificaron en el navegador el 3 nov (paso 23 de la verificación de `orangepi/AGENTS.md`). |
| Registro | Funciona; verificado en el navegador el 3 nov (paso 24 de la verificación de `orangepi/AGENTS.md`): elegir al alumno (pendientes primero, con búsqueda), casilla de consentimiento, iniciar (`POST /api/enrolamiento`), las 5 poses con su estado y motivo, advertencia de parecido, resultado final y "Cancelar registro" (`POST /api/enrolamiento/cancelar`). El progreso es un sondeo de `GET /api/enrolamiento/estado` cada 1 s mientras hay un registro abierto, y se retoma al recargar la página. Con la miniatura del rostro de cada pose mientras el registro está abierto (efímera: no se guarda en ninguna parte). Bajo las cinco poses, el recuadro "Última foto" con la última captura. El control de actuadores vive en Métricas. |

El frontend no se conecta a MQTT en esta etapa: no usa `mqtt.js` ni el puerto 9001. Todo sale de la API HTTP de Node-RED.

La API exige HTTP Basic (usuario del profesor, `NODERED_PROFESOR_USER`): al abrir `/app/`, el navegador muestra su diálogo de usuario y contraseña en la primera consulta a `/api/...` y reenvía las credenciales por su cuenta, porque el frontend es del mismo origen. No hay pantalla de inicio de sesión propia. `/app/` en sí es público. Si la persona cancela el diálogo (401), la vista de energía, la lista de alumnos y el progreso del registro detienen su sondeo y muestran "Reintentar", en lugar de reabrir el diálogo cada pocos segundos.

**Todo `POST`, `PUT`, `PATCH` o `DELETE` que haga el frontend debe mandar la cabecera `X-Pase-Lista: 1`** (el middleware de la API la exige como defensa contra CSRF; sin ella, 403). Los `GET` no la necesitan; la importación de la vista Alumnos (`src/api/alumnos.ts`) envía `Content-Type: text/csv; charset=utf-8` y `X-Pase-Lista: 1`; iniciar el registro (`src/api/registro.ts`) envía `Content-Type: application/json` y la misma cabecera, y cancelar el registro y retirar el rostro (`DELETE`) solo la cabecera. Esa petición exige además el mismo origen: en `npm run dev`, `vite.config.ts` ajusta la cabecera `Origin` hacia `PLACA_URL` en el proxy local sin relajar el middleware en producción.

## Requisitos

- Node 22 o posterior (Vite 8 exige 20.19 o más). Si el Node de tu equipo es más viejo, usa un contenedor efímero; ver [Comandos](#comandos).
- Para ver datos de verdad, la placa con el stack levantado y la IP alcanzable desde tu laptop.

## Desarrollo desde una laptop

1. Instala las dependencias, una sola vez:

   ```bash
   cd web
   npm ci
   ```

2. Arranca Vite con el proxy hacia la placa. `PLACA_URL` es la dirección HTTPS de Node-RED; sustituye `<ip-de-la-placa>` por la IP de la placa:

   ```bash
   PLACA_URL=https://<ip-de-la-placa>:1880 npm run dev
   ```

   También se puede guardar en `web/.env.local` (`PLACA_URL=https://<ip-de-la-placa>:1880`), que git ignora.

3. Abre `http://localhost:5173/app/`. La ruta lleva `/app/` porque el build usa ese prefijo (`base` en `vite.config.ts`).

Vite reenvía `/api` a la placa. Como el certificado de Node-RED es autofirmado, el proxy lo acepta sin verificarlo (`secure: false`). Es solo para desarrollo: el navegador habla en HTTP con tu servidor local y no necesita aceptar ningún certificado. Si no defines `PLACA_URL`, el proxy apunta a `https://localhost:1880`.

Para probar con lecturas, en la placa y desde `orangepi/`: `../herramientas/simular_energia.sh 5 1`.

## Comandos

Desde `web/`:

| Acción | Comando |
|---|---|
| Servidor de desarrollo | `npm run dev` |
| Revisar tipos | `npm run typecheck` |
| Compilar (revisa tipos y escribe `dist/`) | `npm run build` |
| Probar el build local en `localhost:4173/app/` (usa el mismo proxy de `/api`) | `npm run preview` |

No hay linter en `web/`. Las pruebas de la lógica pura del frontend (Alumnos: interpretación de las respuestas de la importación, revisión del archivo y resúmenes; Registro: lectura del estado, traducción de poses y motivos, respuestas de iniciar, cancelar y retirar, búsqueda de alumnos y la máquina de la vista; Asistencia: sesiones, detalle e intentos rechazados; el formato de horas y cifras; Sistema, Actuadores, Latencia y Prueba, con la lectura de sus respuestas; y los errores de vista, `elegirError`) se ejecutan desde la raíz del repo, sin dependencias, con `node --test web/pruebas/*.test.js` (Node 22.18 o posterior, que carga el `.ts` quitando los tipos). Los componentes no tienen pruebas: se verifican a mano en el navegador.

Sin Node 22 en el equipo, ejecuta npm en un contenedor efímero (la imagen y el digest son los del `Dockerfile` de Node-RED):

```bash
docker run --rm -u 1000:1000 -v "$PWD":/app -w /app \
  node:22-bookworm-slim@sha256:4b150b48b4116e2a22f2c39cc27025fbf4f153e2078fd3b14d159fdb5c7279da \
  npm run build
```

## Despliegue

El build de `web/` no se versiona (`dist/` está en `.gitignore`) ni se copia a mano: lo compila el `Dockerfile` de `nodered/`.

1. Sube el sufijo de la etiqueta de la imagen en `orangepi/docker-compose.yml` (`pase-lista/nodered:5.0.9-N`).
2. Desde `orangepi/`:

   ```bash
   docker compose up -d --no-deps --build nodered
   ```

3. Espera a que `docker compose ps` muestre `nodered` en `healthy` (hasta 60 s) y abre `https://<ip-de-la-placa>:1880/app/`.

El build usa `npm ci --ignore-scripts`, así que `package-lock.json` manda: si cambias una dependencia, versiona también el lock. La imagen final contiene solo el `dist/`, en `/usr/share/nodered-web` (directorio de root, sin escritura para Node-RED).

## Estructura

```
web/
  DESIGN.md                dirección visual (lista de asistencia: papel, tinta, marca de asistencia); manda sobre estilos.css
  index.html
  vite.config.ts           base '/app/' y proxy de /api para desarrollo
  src/
    main.tsx               arranque
    estilos.css            fuentes (@font-face), tokens (variables CSS con light-dark()), movimiento y estilos
    tema.ts                preferencia de tema (Sistema, Claro, Oscuro) y su almacenamiento
    lecturasContexto.ts    reparte el estado del sondeo a la cabecera y a las vistas
    sistemaContexto.ts     reparte el estado del sondeo del sistema (App) a la cabecera y a Métricas
    serie.ts               ventana visible y huecos de la gráfica
    App.tsx                encabezado, navegación por hash (#/metricas) y vistas
    api/asistencia.ts      cliente de GET /api/sesiones, GET /api/sesiones/:id y GET /api/sesiones/:id/csv
    api/asistenciaRespuestas.ts  lógica pura de asistencia por sesión e intentos rechazados (sin imports; la prueba web/pruebas)
    api/energia.ts         cliente de /api/metricas/energia y validación de la respuesta
    api/alumnos.ts         cliente de GET /api/alumnos y de POST /api/alumnos/importar (solo el fetch)
    api/alumnosRespuestas.ts  lógica pura de Alumnos: interpreta 200, 400, 409 (los dos), 413, 415 y demás, revisa el archivo, valida la lista (sin imports; la prueba web/pruebas)
    api/registro.ts        cliente de GET /api/enrolamiento/estado, POST /api/enrolamiento, POST /api/enrolamiento/cancelar y DELETE /api/alumnos/:cuenta/rostro (solo el fetch)
    api/registroRespuestas.ts  lógica pura de Registro: lee el estado, traduce poses y motivos, interpreta las respuestas, busca y ordena alumnos y es la máquina de la vista (sin imports; la prueba web/pruebas)
    api/actuadores.ts         cliente de POST /api/actuadores (solo el fetch)
    api/actuadoresRespuestas.ts  lógica pura de Actuadores: vocabulario de las órdenes, interpreta 200, 400, 415 y 429, y disponibilidad (sin imports; la prueba web/pruebas)
    api/errorVista.ts         elige el error que muestra una vista con varios sondeos (sin imports; la prueba web/pruebas)
    api/latencia.ts           cliente de GET /api/metricas/latencia (solo el fetch)
    api/latenciaRespuestas.ts lógica pura de la latencia: resumen y mediciones (sin imports; la prueba web/pruebas)
    api/prueba.ts             cliente de GET /api/enrolamiento/prueba y POST /api/enrolamiento/probar (solo el fetch)
    api/pruebaRespuestas.ts   lógica pura de la prueba de reconocimiento: interpreta 202, 409 y 429, y el resultado (sin imports; la prueba web/pruebas)
    api/sistema.ts            cliente de GET /api/sistema/estado (solo el fetch)
    api/sistemaRespuestas.ts  lógica pura del estado del sistema: visión, cámara, actuadores y métricas (sin imports; la prueba web/pruebas)
    useAsistencia.ts       sondeo de sesiones y detalle nominal (5 s, en pausa con la pestaña oculta)
    useSistema.ts          sondeo de GET /api/sistema/estado (5 s, en pausa con la pestaña oculta; 401 lo detiene)
    useActuadores.ts       envío de órdenes con enfriamiento de 1 s, y reconsulta del estado 0,6 s después de cada una
    useLatencia.ts         sondeo de GET /api/metricas/latencia (5 s, en pausa con la pestaña oculta; 401 lo detiene)
    usePrueba.ts           solicitud de la prueba y sondeo de su resultado (1 s mientras está pendiente; 401 lo detiene)
    useUltimaCaptura.ts    la última miniatura de cada pose, ajustada a la consulta del registro (efímera)
    useLecturas.ts         consulta periódica (5 s, en pausa con la pestaña oculta)
    useAlumnos.ts          consulta periódica de la lista (10 s), mismo patrón que useLecturas
    useImportacion.ts      flujo de la importación: revisión local, envío con guardia de módulo, diálogo de decisión, avisos
    useRegistro.ts         sondeo del registro (1 s con uno abierto, en pausa con la pestaña oculta), iniciar, cancelar y retomar
    useRetiro.ts           retiro del rostro: confirmación, envío y aviso
    useDialogo.ts          apertura y cierre animado de un <dialog> nativo (lo usan DialogoRetiro y DialogoExistentes)
    formato.ts             horas (America/Mexico_City) y cifras
    vistas/                Alumnos, Registro, Asistencia, Metricas
    componentes/           Pagina, Cifra, Vacio, MarcaAsistencia, HojaAsistencia, GraficaPotencia,
                           TablaLecturas, EstadoVacio, AvisoObsoleta, IndicadorEstado, SelectorTema;
                           de Alumnos: TablaAlumnos, EsqueletoTabla, ZonaCSV,
                           AvisoImportacion, DialogoExistentes, DialogoRetiro, CampoBusqueda;
                           de Métricas: HojaSistema, PruebaReconocimiento, HojaActuadores, DialogoPuerta, HojaLatencia, TablaLatencias;
                           de Registro: SeleccionAlumno, ProgresoRegistro, UltimaFoto;
                           Avisos (Sonner)
  pruebas/
    alumnos-respuestas.test.js     node:test de api/alumnosRespuestas.ts
    asistencia-respuestas.test.js  node:test de api/asistenciaRespuestas.ts
    registro-respuestas.test.js    node:test de api/registroRespuestas.ts
    actuadores-respuestas.test.js  node:test de api/actuadoresRespuestas.ts
    latencia-respuestas.test.js    node:test de api/latenciaRespuestas.ts
    prueba-respuestas.test.js      node:test de api/pruebaRespuestas.ts
    sistema-respuestas.test.js     node:test de api/sistemaRespuestas.ts
    error-vista.test.js            node:test de api/errorVista.ts
    formato.test.js                node:test de formato.ts
    texto.js                       comprobaciones de tono para los textos de la profesora (no es una prueba; la usan las demás)
  public/
    tema.js                aplica el tema guardado antes de pintar (archivo externo por la CSP)
```

## Dependencias

| Paquete | Para qué |
|---|---|
| `react`, `react-dom` | Interfaz (plan, sección 6.7) |
| `vite`, `@vitejs/plugin-react` | Servidor de desarrollo y build |
| `typescript`, `@types/react`, `@types/react-dom` | Tipos y revisión estática |
| `recharts` | Gráficas. La vista de métricas sumará asistencia, latencia y confianza (plan, sección 6.6), y con recharts son más series del mismo tipo de componente. |
| `sonner` (2.0.7, exacta) | Avisos tipo toast (éxito de la importación, importación cancelada). Pares `react` y `react-dom` `^18 \|\| ^19`, sin dependencias propias. Se importa su hoja `sonner/dist/styles.css` como archivo (ver Decisiones, Avisos). |
| `@fontsource/source-serif-4` (5.3.1, exacta) | Serif de documento para los títulos y las cifras grandes (peso 600). Empaquetada con el build: el aula puede no tener internet y la CSP no admite fuentes de otro origen. |
| `@fontsource/libre-franklin` (5.3.0, exacta) | Grotesca de formulario impreso para etiquetas, tablas, cuerpo y botones (pesos 400 y 600), con cifras tabulares. Solo se empaquetan los subconjuntos latin y latin-ext, que `src/estilos.css` declara con `@font-face` (los `400.css` de los paquetes traerían también cirílico, griego y vietnamita). La razón de cada familia está en [DESIGN.md](DESIGN.md#tipografía). |

## Decisiones

- **Consulta periódica y no MQTT.** Es más simple y la lectura de energía no necesita latencia de milisegundos. La siguiente consulta se agenda al terminar la anterior, y se pausa si la pestaña está oculta.
- **Aviso de lecturas viejas.** Si la última lectura tiene más de 60 s, la vista lo dice, porque la XIAO pudo dejar de publicar. La antigüedad se calcula con la cabecera `Date` de la respuesta de la API (el reloj de la placa, el mismo que selló la lectura) y no con la hora de la laptop; si la cabecera falta, se usa el reloj del navegador.
- **Gráfica: ventana y huecos.** La gráfica dibuja solo los últimos 30 min respecto de la última lectura (las lecturas anteriores siguen en la tabla, y la vista lo dice). Con el ritmo normal de la XIAO las 100 lecturas que se piden caben en la ventana; recorta solo tras un paro largo, para que una ráfaga vieja no aplaste la reciente. Dentro de la ventana, la línea se corta donde el intervalo entre lecturas supera 5 veces el intervalo habitual (la mediana de los intervalos), con un mínimo de 30 s. Una lectura aislada se dibuja como punto. Si el rango cruza la medianoche, las marcas del eje y la tabla incluyen el día. El criterio está en `src/serie.ts`.
- **`npm run build` incluye `tsc`.** Un error de tipos impide compilar y, con ello, reconstruir la imagen de Node-RED. Se mantiene así a propósito: es preferible no desplegar un frontend que no pasa los tipos.
- **Una fila inválida pone la vista en error.** Si una sola lectura de la respuesta no tiene hora, voltaje, corriente y potencia numéricos, la vista muestra el error y no dibuja las demás. Se mantiene a propósito: el flujo de Node-RED ya valida cada lectura al ingerirla (`orangepi/README.md`, sección Energía), así que una fila inválida indica un fallo real y no un dato que haya que ocultar.
- **Cabeceras de seguridad.** Node-RED las fija en `nodered/settings.js`: en `/app/`, una CSP que solo permite el mismo origen, más `nosniff`, `Referrer-Policy: no-referrer` y `X-Frame-Options: DENY`; en el editor y la API, solo `nosniff` y `X-Frame-Options: DENY`. Una dependencia nueva que cargue algo de otro origen, o estilos en línea en el HTML, chocará con esa CSP.
- **Navegación por hash.** Node-RED sirve archivos estáticos y no sabe resolver rutas de una SPA: con `#/metricas`, recargar la página no da 404.
- **Zona horaria.** Todas las horas se muestran en `America/Mexico_City`, sin importar la zona de la laptop.
- **Sin cifras inventadas.** Una vista sin datos dice que no hay datos. Alumnos solo muestra lo que devuelve la API y Registro no muestra ejemplos.
- **Dirección visual: la lista de asistencia impresa.** La identidad sale del objeto que el proyecto reemplaza y está en [DESIGN.md](DESIGN.md), que manda sobre `src/estilos.css`: Design Read, dials (ENERGY 2 / RHYTHM 2 / MOTION 2), paleta, tipografía, motivo, radio, espaciado y movimiento, cada decisión con su razón. Una dirección anterior imitaba un panel de base de datos (oscuro, Inter, esmeralda); el equipo la descartó por no tener identidad propia.
- **Tema claro por defecto, con el oscuro completo.** Es una lista de papel y en un proyector se lee mejor clara: sin elección guardada el tema es Claro (`public/tema.js` lo fija antes de pintar). El selector ofrece Sistema, Claro y Oscuro: "Sistema" se guarda como `system`, no pone atributo y sigue `prefers-color-scheme`. El oscuro es el mismo papel de noche. Los colores se escriben una vez con `light-dark()` y `color-scheme` (`:root` declara `light dark`; `data-theme` lo fija), así que ningún token está duplicado entre temas. Requiere un navegador de 2024 en adelante (Chrome 123, Firefox 120, Safari 17.5).
- **Papel, tinta y una pluma.** Papel cálido (`#fcf9f1` la hoja, `#f1ecdf` la mesa), tinta casi negra y una tinta de acento, la del bolígrafo con que se marca la lista (`#1b3c9c`; `#9fb5ff` en oscuro). La pluma va solo en la acción principal, el estado registrado y el foco. El rojo es solo para errores y faltas reales. Las advertencias no tienen color propio.
- **La marca de asistencia es el motivo.** Una casilla de papel con una palomita de pluma (presente), vacía (pendiente) o con una tachita roja (falta), dibujada a mano en SVG (`componentes/MarcaAsistencia.tsx`), sin librería de iconos. Aparece en la columna del rostro de Alumnos, en el encabezado de la hoja, en el indicador de la placa, en los estados vacíos y en los avisos. Siempre lleva su texto al lado.
- **Contraste verificado en los dos temas** con el script de la habilidad antislop-human: todo el texto cumple 4.5:1 y los bordes de controles, el anillo de foco y las marcas cumplen 3:1. Los pares y sus razones están en [DESIGN.md](DESIGN.md#paleta); la regla de renglón es decorativa y la información no depende de ella.
- **Tipografía de documento.** Source Serif 4 para títulos y cifras grandes y Libre Franklin para todo lo demás, con `tabular-nums` en cuentas, horas y conteos. Nada de Inter, Geist ni Space Grotesk. Escala: cifra protagonista 56 px (44 en pantallas angostas), título 34 px (28), sección 22 px (20), cuerpo y tablas 16 px, etiquetas 13 px. Mono del sistema solo en el texto de código.
- **Composición por vista (RHYTHM 2).** Alumnos es una hoja con encabezado de documento (materia, grupo y conteos de lo que se ve), renglones numerados con su regla y un margen con la importación del CSV. Métricas es una hoja de dos columnas (la potencia y las demás cifras como renglones con puntos guía, y la gráfica en línea de tinta sin relleno) y una hoja de lecturas. Registro es una hoja con su margen, como Alumnos: la lista para elegir al alumno con el consentimiento al lado y, con un registro abierto, las cinco poses con la acción que sigue. Nada de héroe más tarjetas ni de filas de estadísticas, y sin cuadrícula ni renglones de fondo: las reglas existen solo donde hay renglones de datos. Los conteos del encabezado de Alumnos siguen al grupo elegido y a la búsqueda. Ancho fluido con tope: margen lateral de 32 px (16 px en pantallas de 640 px o menos) y contenido de hasta 1680 px, centrado; la cabecera y las vistas comparten el contenedor. El margen del CSV mide 19 rem fijos y el ancho extra de la tabla va a Nombre (ver [DESIGN.md](DESIGN.md#radio-espaciado-y-elevación)).
- **Estado de la placa en la cabecera.** Antes decía "Sin lecturas recientes" en cualquier vista, un dato de la XIAO que no tenía que ver con la pantalla. Ahora marca solo lo que es cierto en todas: si la última consulta de energía, la que `App` hace en cualquier vista, respondió. La lista de alumnos tiene su propio sondeo y no alimenta el indicador. "Placa conectada" (respondió), "Sin sesión en la API" (401), "Sin conexión con la placa" (falló) y "Conectando con la placa" (todavía sin respuesta). La frescura de las lecturas de la XIAO vive en Métricas, en `AvisoObsoleta`. La marca de asistencia (palomita, casilla vacía o tachita roja) es redundante y no brilla ni pulsa, el texto lo lee el lector de pantalla y un `role="status"` oculto anuncia solo los cambios de estado (no el inicial).
- **Selector de tema.** Nativo, con Sistema, Claro y Oscuro, guardado en `localStorage` y aplicado como `data-theme` en `<html>`. `public/tema.js` lo aplica antes de pintar para evitar el parpadeo; es un archivo externo porque la CSP prohíbe scripts en línea.
- **Movimiento (MOTION 2): visible, con propósito y apagable.** El detalle de cada movimiento y su propósito está en [DESIGN.md](DESIGN.md#movimiento). Los renglones de la lista se escriben de arriba abajo con un barrido de izquierda a derecha (240 ms, escalón de 25 ms, solo los 8 primeros y solo al abrir la vista; `TablaAlumnos.tsx`); la palomita se traza (`stroke-dashoffset`, 480 ms) cuando un alumno pasa a "registrado"; la vista nueva aparece con un fundido de 220 ms y sus bloques suben 6 px con 35 ms entre ellos (`.vista` y `.pagina > *` en `estilos.css`), sin salida animada de la vista anterior; la zona del CSV responde al arrastre. Las cifras no animan: muestran su valor. Curva de salida fuerte para entradas y salidas y una curva suave de punta a punta para lo que se escribe. Sin tarjetas que suban ni sombras de realce: solo el diálogo y los avisos flotan y tienen sombra. El único bucle es el pulso del esqueleto de carga, limitado a 4 ciclos de 1.2 s (menos de 5 s). **Todo se apaga con `prefers-reduced-motion: reduce`**: la regla global quita animaciones y transiciones (también las de Sonner y el fondo del diálogo), el cambio de vista es inmediato, las cifras muestran su valor final y ningún estado inicial oculta contenido, así que la página queda quieta y completa, con la palomita ya trazada.
- **Navegación honesta.** Una vista que aún no funciona lo dice en la propia navegación con una etiqueta "Pronto"; hoy las cuatro vistas funcionan y ninguna la lleva (Registro la perdió al implementarse). La ruta es lo que va antes del `?` del hash: `#/registro?cuenta=320068234` abre Registro con esa cuenta elegida (es el enlace de "Registrar rostro" en Alumnos).
- **Alumnos: la lista se consulta completa y el filtro es local.** `GET /api/alumnos` se pide sin `?grupo=` cada 10 s (misma mecánica que la energía: se pausa con la pestaña oculta y se detiene con un 401) y el selector de grupo sale de los grupos de esa lista. Alternativa descartada: consultar con `?grupo=`, que obliga a una segunda consulta para saber qué grupos existen y repinta con cada cambio. Un fallo no borra la lista que ya se tenía (la vista dice de qué hora son los datos). El frontend copia solo `cuenta`, `nombre`, `grupo` y `rostro`: aunque la API mandara más campos, no los conserva. Los nombres y grupos vienen del CSV y se pintan solo como texto (sin `dangerouslySetInnerHTML`); el nombre se parte en cualquier punto (`overflow-wrap: anywhere`), se aísla para texto bidireccional (`<bdi>` y `unicode-bidi: isolate`; en el `<option>` del filtro, con las marcas FSI y PDI de Unicode) y en pantallas angostas la tarjeta se desplaza en horizontal. `AvisoObsoleta` no se usa aquí: avisa de lecturas de energía viejas (estado de la placa) y no tiene sentido para una lista de alumnos.
- **Alumnos: importación del CSV.** El servidor manda; la revisión local solo evita un viaje con lo evidente (vacío, más de 256 KiB, que no sea texto). Las respuestas se interpretan en `api/alumnosRespuestas.ts`, con funciones puras y pruebas. El 409 con `escrito: false` abre el diálogo y espera: la vista nunca reenvía con `?existentes=` sin que la persona lo haya elegido, y "Cancelar" no escribe nada. El 409 con `escrito: true` (carrera) se muestra como advertencia con lo que sí se escribió y recarga la lista. Los textos de la API que hablan de `?existentes=` son para curl y no se muestran al profesor. Un fallo de red o un tiempo agotado dice que no se sabe si se importó el archivo, y manda a revisar la lista. Solo hay un envío a la vez, decidido con refs y no con el estado de React. El POST no se aborta al salir de la vista (abortar no deshace lo que la API ya escribió): termina y su resultado sale en un toast, porque Sonner vive en `App`. El plazo es de 30 s, o de 120 s mientras la lista esté sin sesión (el navegador puede volver a pedir las credenciales). La revisión local acepta por la extensión (`.csv` o `.txt`) cualquier tipo declarado, porque Windows con Excel declara `application/vnd.ms-excel` y otros sistemas `application/octet-stream`; sin extensión, solo `text/*`. La zona cancela el arrastre de archivos en toda la ventana fuera de ella, para que soltar uno en otro lado no lo abra en el navegador y pierda la página.
- **Diálogo: `<dialog>` nativo con `showModal()`.** El navegador deja inerte el resto de la página, devuelve el foco al botón que lo tenía y entrega Escape como `cancel`; nada de eso se reescribe. La Tab desde el último botón pasa al navegador (la barra de direcciones) y vuelve al primero: la página de fondo nunca recibe el foco, y esa salida es lo que el navegador considera accesible. Foco inicial en "Cancelar", la única acción que no cambia nada: ninguna de las dos decisiones queda como predeterminada. Sin cierre por clic en el fondo, para que un clic suelto no cancele una decisión. Mientras se reenvía, los botones y Escape no hacen nada (`aria-disabled`, no `disabled`, para no perder el foco), y cancelar es idempotente: un doble Escape o un doble clic dan un solo aviso.
- **Movimiento de Alumnos.** Casi todo es `transform` y `opacity`, con estas excepciones: el realce del renglón que cambió y el de la fila bajo el puntero (`background-color`), el color de las transiciones de botones y campos y el trazo de la palomita (`stroke-dashoffset`), todos en elementos pequeños y de cambios raros. El barrido del renglón usa `clip-path` con destino explícito (`inset(0 0 0 0)`): `clip-path` no interpola entre `inset()` y `none`. Una fila cuyo rostro cambia entre dos consultas resalta y, si pasó a "registrado", su palomita se traza una vez. El sondeo y el filtro no repiten la entrada de los renglones. Esqueleto: un solo elemento cambia de opacidad (4 ciclos de 1,2 s). Diálogo: entra con un resorte (transición CSS con `linear()`, ζ = 0,55, 420 ms, rebote de 12,6 %, desde 20 px abajo y 90 %) y sale en 180 ms con `--ease-entrada`; son transiciones, así que se pueden interrumpir y se revierten desde donde van. La salida no depende de `overlay` (que Safari y Firefox no animan): el código espera `transitionend` y entonces llama a `close()`, con un respaldo de 400 ms, y con movimiento reducido cierra al instante. Zona para soltar: al arrastrar encima escala 1,015 y muestra un anillo de pluma (pseudoelemento que solo cambia de opacidad), al soltar rebota 340 ms y al rechazar el archivo se sacude 400 ms; el texto del rechazo sale siempre en un aviso `role="alert"`. Botones: encogido a `scale(0.97)` al presionar.
- **Alumnos: búsqueda y acciones por renglón.** La búsqueda vive en el encabezado de la hoja, junto al filtro de grupo, y filtra en el cliente (`filtrarAlumnos`): nombre o cuenta, sin acentos ni mayúsculas, todas las palabras en cualquier orden, el texto literal (no es una expresión regular). Los conteos siguen a lo visible (grupo y búsqueda); sin coincidencias hay un estado vacío con "Borrar búsqueda". El encabezado usa una consulta de contenedor (`container-type` en `.hoja__encabezado`): con 44 rem o más de ancho de hoja caben materia, grupo y búsqueda en un renglón; con menos, la materia va arriba. La acción de cada renglón va en la celda del Rostro, junto al estado (apilada en un teléfono, con 44 px de alto): "Registrar rostro" para un pendiente y "Retirar rostro" para uno registrado, y no un enlace a Registro en todos, porque volver a registrar a quien tiene rostro exige retirarlo antes (plan, sección 5.4) y ese enlace no llevaría a nada.
- **Retirar rostro: `<dialog>` con la misma mecánica que el de las cuentas existentes** (`DialogoRetiro`, apertura y cierre en `useDialogo`): `showModal()`, foco inicial en "Cancelar" (la única acción que no cambia nada), Escape cierra sin retirar, sin cierre por clic en el fondo y botones con `aria-disabled` mientras se envía. Es destructivo (borra el embedding y el consentimiento), así que el texto dice qué se borra, que el alumno sigue en la lista y que no se puede deshacer. El resultado sale en un toast (éxito, "sin cambios" si ya estaba pendiente, 404, 409 con registro abierto o fallo) y la lista se recarga; cuando la promesa de `recargar` (de `useAlumnos`) confirma que la lista nueva está pintada y el diálogo ya se cerró, el foco vuelve a la acción de esa fila (`data-accion-cuenta`: el botón pasa a ser el enlace "Registrar rostro") o, si la fila ya no está, al título de la hoja. "Borrar búsqueda" deja el foco en el campo de búsqueda. `DialogoExistentes` no se tocó: `useDialogo` copia su mecánica para no arriesgar lo que ya funcionaba, y migrarlo a `useDialogo` queda como limpieza posterior.
- **Registro: sondeo, no MQTT** (decisión de Yordi; plan, sección 6.7). `useRegistro` consulta `GET /api/enrolamiento/estado` una vez al montarse (si hay un registro abierto, se retoma) y cada segundo mientras hay uno abierto, con el mismo patrón que `useAlumnos`: la siguiente consulta se agenda al terminar la anterior, se pausa con la pestaña oculta (y consulta de inmediato al volver) y se detiene con un 401 con "Reintentar". Si la primera consulta falla, reintenta cada 3 s. La fase de la vista sale de una máquina pura (`siguienteVista`: comprobando, libre, iniciando, abierto, terminado) con pruebas. Un registro ya cerrado solo se muestra si se vio abierto en esta vista: al cargar la página, un "cerrado" es de una sesión anterior y no se enseña. Si el registro abierto desaparece (Node-RED se reinició y perdió la inscripción), la vista vuelve a la lista con un aviso.
- **Registro: honestidad con la visión.** Mientras la visión no publica nada, el estado es `esperando_vision` y la vista dice "Esperando al servicio de visión", con el tiempo transcurrido según el reloj de la placa (cabecera `Date`, no el de la laptop) y la advertencia de que el registro se cancela solo a los 5 minutos. Las cinco poses quedan pendientes: no se simula avance. Las miniaturas de las fotos se muestran junto al nombre de la pose mientras el registro está abierto: el servicio de visión las emite con la pose, Node-RED las guarda solo en el contexto del flujo y las sirve por `GET /api/enrolamiento/estado`; al cerrarse el registro se borran (no se persisten). La vista muestra el estado y el motivo de cada pose, traducidos (`traducirMotivo`; un código nuevo se nombra tal cual en lugar de ocultarlo).
- **Registro: el consentimiento es de cada alumno y de cada registro.** La casilla "El alumno firmó el formato de consentimiento" se desmarca al elegir a otro alumno (y volver a uno anterior no la recupera) y tras iniciar. "Iniciar registro" está deshabilitado sin ella. El texto del formato no se redacta aquí: la vista dice que está pendiente (`docs/consentimiento.md`, plan, secciones 5.4 y 6.4). Los alumnos con rostro se ven en la lista pero no se pueden elegir; una cuenta elegida por el enlace que ya tiene rostro lo explica y manda a Alumnos. Al terminar el foco pasa al título de la hoja, porque el botón que lo tenía desaparece.
- **Composición de Registro.** La misma de Alumnos (hoja y margen de 19 rem): con la vista libre, la hoja es la lista para elegir (renglones con una casilla de opción nativa, pendientes primero) y el margen lleva el consentimiento y "Iniciar registro"; con un registro abierto, la hoja lleva el alumno, el resumen con su marca de asistencia y las cinco poses como renglones (la marca es palomita si la foto se aceptó, tachita si se rechazó y casilla vacía en los demás casos, siempre con su texto), y el margen, "Cancelar registro" o, al terminar, "Registrar a otro alumno". Detalle en [DESIGN.md](DESIGN.md#composición).
- **Métricas: el orden de las hojas.** Sistema, Probar reconocimiento y Actuadores van primero, porque son lo que hace falta para una prueba de punta a punta (si la visión y la XIAO están en línea, si el reconocimiento responde y qué pasa al abrir la puerta); después la energía y la latencia de inferencia. La asistencia por sesión tiene su propia vista, Asistencia, que es la inicial. Cada hoja tiene su consulta: Sistema y Latencia, cada 5 s; Actuadores no sondea y reconsulta el estado de Sistema tras cada orden. La hoja de Sistema dice la hora de cada dato que muestra, porque la visión, la XIAO y la placa no llegan a la vez.
- **Actuadores: la puerta pide confirmación, el resto no.** "Abrir puerta" abre un `<dialog>` (`DialogoPuerta`) que dice que el servo abre ahora y se cierra solo a los 3 s, y que no hay orden para cerrarla antes. El foco inicial va en "Cancelar", y mientras la orden va en camino ambos botones quedan con `aria-disabled`. Las demás órdenes (LED, buzzer, luz) salen con un clic. Los botones quedan desactivados si la XIAO está fuera de línea o si falla la consulta del estado (`disponibilidadActuadores`), y la hoja dice "Último estado conocido" en lugar de "Ahora". Una orden espera 1 s antes de la siguiente, igual que el límite de la API: el 429 que llegue igual se muestra como orden no enviada.
- **Prueba de reconocimiento y última foto: son honestas.** En Métricas, "Probar reconocimiento" pide la prueba y sondea su resultado cada 1 s mientras está pendiente; el resultado muestra decisión, alumno (solo con `reconocido` o `dudoso`), similitud, margen y latencia, y dice cuánto hace que llegó. Un 409 (registro abierto o prueba en curso) y un 429 se explican en la vista, y ninguna respuesta se inventa. "Última foto" muestra la última miniatura bajo las cinco poses mientras el registro está abierto: recuadro de 4:3, de hasta 320 px, pixelado (la ampliación deja ver que son 160×120) y se borra al cerrar el registro.
- **Sondeos con 401: "Reintentar" y no un diálogo en bucle.** Cuando la API pide usuario y contraseña y la persona cancela, los sondeos de Sistema, Latencia, Prueba, Energía, Alumnos y Registro se detienen y la vista ofrece "Reintentar" en lugar de volver a abrir el diálogo cada pocos segundos. Los errores de otros fallos siguen con su reintento automático.
- **Varias consultas, un solo aviso: `elegirError`.** Métricas combina los errores de sus cuatro consultas con `elegirError` (`api/errorVista.ts`). Si algún sondeo está detenido por 401, el aviso muestra el error de ese sondeo y ofrece "Reintentar"; si no, muestra el primer error. Así el aviso no dice "La vista no insistirá sola" junto al error de un sondeo que sí sigue insistiendo.
- **Avisos: Sonner 2.0.7 (versión exacta; `react` y `react-dom` `^18 || ^19`).** Un único `<Toaster>` (`componentes/Avisos.tsx`) montado en `App`. Se usa para el éxito de la importación (con los conteos) y para "Importación cancelada"; los errores no son toasts, porque deben quedarse en pantalla y llevar sus filas (avisos `role="alert"` bajo la zona). **La CSP del frontend (`style-src 'self'`) bloquea la etiqueta `<style>` que Sonner inyecta al cargar** (la consola lo registra una vez como violación y es inofensivo); por eso `main.tsx` importa `sonner/dist/styles.css`, que Vite empaqueta como archivo propio, antes de `estilos.css`. `estilos.css` ajusta la hoja de Sonner a los tokens (hoja, tinta, borde de tinta, radio de 2 px, tipografía; con sombra, porque flota) y `Avisos.tsx` le pasa la marca de asistencia como icono de éxito, advertencia y error con la misma especificidad y gana por orden, así que sigue el tema Claro u Oscuro sin pasarle `theme`. Las transiciones de Sonner (400 ms con `ease`) pasan a `--d-toast` (320 ms al entrar y reubicarse) con `--ease-entrada`, y la salida a 200 ms, el tiempo que Sonner espera antes de desmontar; con `prefers-reduced-motion` quedan sin movimiento por la regla global y por la propia de Sonner. Un toast queda debajo de un `<dialog>` modal (el diálogo está en la capa superior): los toasts de éxito y de cancelación se lanzan justo cuando el diálogo empieza a cerrarse, así que lo único que lo tapa son sus 180 ms de salida.
