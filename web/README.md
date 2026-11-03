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
| Métricas y asistencia | Funciona solo la sección de energía: potencia actual y datos de la ventana de 30 min, gráfica de la potencia y tabla de lecturas recientes, más un estado vacío de asistencia. Consulta `GET /api/metricas/energia` cada 5 s. |
| Alumnos | Funciona: lista de alumnos con filtro por grupo y estado del rostro (`GET /api/alumnos`, cada 10 s), e importación de la lista maestra en CSV (`POST /api/alumnos/importar`) con zona para soltar el archivo, diálogo para las cuentas existentes y avisos. La API ya está desplegada en la placa; la vista todavía no se ha probado en el navegador contra la placa. |
| Registro | Estado vacío con la etiqueta "Pronto" en la navegación y un enlace a Alumnos; llega en un avance posterior. |

El frontend no se conecta a MQTT en esta etapa: no usa `mqtt.js` ni el puerto 9001. Todo sale de la API HTTP de Node-RED.

La API exige HTTP Basic (usuario del profesor, `NODERED_PROFESOR_USER`): al abrir `/app/`, el navegador muestra su diálogo de usuario y contraseña en la primera consulta a `/api/...` y reenvía las credenciales por su cuenta, porque el frontend es del mismo origen. No hay pantalla de inicio de sesión propia. `/app/` en sí es público. Si la persona cancela el diálogo (401), la vista de energía y la lista de alumnos detienen su sondeo y muestran "Reintentar", en lugar de reabrir el diálogo cada pocos segundos.

**Todo `POST`, `PUT`, `PATCH` o `DELETE` que haga el frontend debe mandar la cabecera `X-Pase-Lista: 1`** (el middleware de la API la exige como defensa contra CSRF; sin ella, 403). Los `GET` no la necesitan; la importación de la vista Alumnos (`src/api/alumnos.ts`) envía `Content-Type: text/csv; charset=utf-8` y `X-Pase-Lista: 1`. Esa petición exige además el mismo origen: con `npm run dev` el navegador manda `Origin: http://localhost:5173` y la API responde 403, así que la importación solo se prueba desde `https://<ip-de-la-placa>:1880/app/` (la lista y el resto de la vista sí funcionan con el proxy de desarrollo).

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

No hay linter en `web/`. Las pruebas de la lógica pura del frontend (Alumnos: interpretación de las respuestas de la importación, revisión del archivo y resúmenes; y la cuenta de las cifras) se ejecutan desde la raíz del repo, sin dependencias, con `node --test web/pruebas/*.test.js` (Node 22.18 o posterior, que carga el `.ts` quitando los tipos). Los componentes no tienen pruebas: se verifican a mano en el navegador.

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
    animacion.ts           matemática pura de las cifras que cuentan (la prueba web/pruebas)
    lecturasContexto.ts    reparte el estado del sondeo a la cabecera y a las vistas
    serie.ts               ventana visible y huecos de la gráfica
    App.tsx                encabezado, navegación por hash (#/metricas) y vistas
    api/energia.ts         cliente de /api/metricas/energia y validación de la respuesta
    api/alumnos.ts         cliente de GET /api/alumnos y de POST /api/alumnos/importar (solo el fetch)
    api/alumnosRespuestas.ts  lógica pura de Alumnos: interpreta 200, 400, 409 (los dos), 413, 415 y demás, revisa el archivo, valida la lista (sin imports; la prueba web/pruebas)
    useLecturas.ts         consulta periódica (5 s, en pausa con la pestaña oculta)
    useAlumnos.ts          consulta periódica de la lista (10 s), mismo patrón que useLecturas
    useImportacion.ts      flujo de la importación: revisión local, envío, diálogo de decisión, avisos
    formato.ts             horas (America/Mexico_City) y cifras
    vistas/                Alumnos, Registro, Metricas
    componentes/           Pagina, Cifra, NumeroAnimado, MarcaAsistencia, GraficaPotencia,
                           TablaLecturas, EstadoVacio, AvisoObsoleta, IndicadorEstado, SelectorTema;
                           de Alumnos: TablaAlumnos, EsqueletoTabla, ZonaCSV,
                           AvisoImportacion, DialogoExistentes; Avisos (Sonner)
  pruebas/
    alumnos-respuestas.test.js   node:test de api/alumnosRespuestas.ts
    animacion.test.js            node:test de animacion.ts
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
- **Composición por vista (RHYTHM 2).** Alumnos es una hoja con encabezado de documento (materia, grupo y conteos de lo que se ve), renglones numerados con su regla y un margen con la importación del CSV. Métricas es una hoja de dos columnas (la potencia y las demás cifras como renglones con puntos guía, y la gráfica en línea de tinta sin relleno) y una hoja de lecturas. Registro es una hoja en blanco con una casilla grande y un enlace real a Alumnos. Nada de héroe más tarjetas ni de filas de estadísticas, y sin cuadrícula ni renglones de fondo: las reglas existen solo donde hay renglones de datos. Los conteos del encabezado de Alumnos siguen al grupo elegido. Ancho fluido con tope: margen lateral de 32 px (16 px en pantallas de 640 px o menos) y contenido de hasta 1680 px, centrado; la cabecera y las vistas comparten el contenedor. El margen del CSV mide 19 rem fijos y el ancho extra de la tabla va a Nombre (ver [DESIGN.md](DESIGN.md#radio-espaciado-y-elevación)).
- **Estado de la placa en la cabecera.** Antes decía "Sin lecturas recientes" en cualquier vista, un dato de la XIAO que no tenía que ver con la pantalla. Ahora marca solo lo que es cierto en todas: si la última consulta de energía, la que `App` hace en cualquier vista, respondió. La lista de alumnos tiene su propio sondeo y no alimenta el indicador. "Placa conectada" (respondió), "Sin sesión en la API" (401), "Sin conexión con la placa" (falló) y "Conectando con la placa" (todavía sin respuesta). La frescura de las lecturas de la XIAO vive en Métricas, en `AvisoObsoleta`. La marca de asistencia (palomita, casilla vacía o tachita roja) es redundante y no brilla ni pulsa, el texto lo lee el lector de pantalla y un `role="status"` oculto anuncia solo los cambios de estado (no el inicial).
- **Selector de tema.** Nativo, con Sistema, Claro y Oscuro, guardado en `localStorage` y aplicado como `data-theme` en `<html>`. `public/tema.js` lo aplica antes de pintar para evitar el parpadeo; es un archivo externo porque la CSP prohíbe scripts en línea.
- **Movimiento (MOTION 2): visible, con propósito y apagable.** El detalle de cada movimiento y su propósito está en [DESIGN.md](DESIGN.md#movimiento). Los renglones de la lista se escriben de arriba abajo con un barrido de izquierda a derecha (360 ms, escalón de 40 ms, solo los 12 primeros y solo al abrir la vista); la palomita se traza (`stroke-dashoffset`, 560 ms) cuando un alumno pasa a "registrado"; la vista que se va sale en 120 ms y la nueva entra en 300 ms con 6 px de subida y 50 ms entre bloques; la zona del CSV responde al arrastre; las cifras cuentan hasta su valor (`requestAnimationFrame` sin estado de React). Curva de salida fuerte para entradas y salidas y una curva suave de punta a punta para lo que se escribe. Sin tarjetas que suban ni sombras de realce: solo el diálogo y los avisos flotan y tienen sombra. El único bucle es el pulso del esqueleto de carga, limitado a 4 ciclos de 1.2 s (menos de 5 s). **Todo se apaga con `prefers-reduced-motion: reduce`**: la regla global quita animaciones y transiciones (también las de Sonner y el fondo del diálogo), el cambio de vista es inmediato, las cifras muestran su valor final y ningún estado inicial oculta contenido, así que la página queda quieta y completa, con la palomita ya trazada.
- **Navegación honesta.** Registro lleva la etiqueta "Pronto" en la propia navegación, porque su vista todavía no funciona. Alumnos la perdió al implementarse.
- **Alumnos: la lista se consulta completa y el filtro es local.** `GET /api/alumnos` se pide sin `?grupo=` cada 10 s (misma mecánica que la energía: se pausa con la pestaña oculta y se detiene con un 401) y el selector de grupo sale de los grupos de esa lista. Alternativa descartada: consultar con `?grupo=`, que obliga a una segunda consulta para saber qué grupos existen y repinta con cada cambio. Un fallo no borra la lista que ya se tenía (la vista dice de qué hora son los datos). El frontend copia solo `cuenta`, `nombre`, `grupo` y `rostro`: aunque la API mandara más campos, no los conserva. Los nombres y grupos vienen del CSV y se pintan solo como texto (sin `dangerouslySetInnerHTML`); el nombre se parte en cualquier punto (`overflow-wrap: anywhere`), se aísla para texto bidireccional (`<bdi>` y `unicode-bidi: isolate`; en el `<option>` del filtro, con las marcas FSI y PDI de Unicode) y en pantallas angostas la tarjeta se desplaza en horizontal. `AvisoObsoleta` no se usa aquí: avisa de lecturas de energía viejas (estado de la placa) y no tiene sentido para una lista de alumnos.
- **Alumnos: importación del CSV.** El servidor manda; la revisión local solo evita un viaje con lo evidente (vacío, más de 256 KiB, que no sea texto). Las respuestas se interpretan en `api/alumnosRespuestas.ts`, con funciones puras y pruebas. El 409 con `escrito: false` abre el diálogo y espera: la vista nunca reenvía con `?existentes=` sin que la persona lo haya elegido, y "Cancelar" no escribe nada. El 409 con `escrito: true` (carrera) se muestra como advertencia con lo que sí se escribió y recarga la lista. Los textos de la API que hablan de `?existentes=` son para curl y no se muestran al profesor. Un fallo de red o un tiempo agotado dice que no se sabe si se importó el archivo, y manda a revisar la lista. Solo hay un envío a la vez, decidido con refs y no con el estado de React. El POST no se aborta al salir de la vista (abortar no deshace lo que la API ya escribió): termina y su resultado sale en un toast, porque Sonner vive en `App`. El plazo es de 30 s, o de 120 s mientras la lista esté sin sesión (el navegador puede volver a pedir las credenciales). La revisión local acepta por la extensión (`.csv` o `.txt`) cualquier tipo declarado, porque Windows con Excel declara `application/vnd.ms-excel` y otros sistemas `application/octet-stream`; sin extensión, solo `text/*`. La zona cancela el arrastre de archivos en toda la ventana fuera de ella, para que soltar uno en otro lado no lo abra en el navegador y pierda la página.
- **Diálogo: `<dialog>` nativo con `showModal()`.** El navegador deja inerte el resto de la página, devuelve el foco al botón que lo tenía y entrega Escape como `cancel`; nada de eso se reescribe. La Tab desde el último botón pasa al navegador (la barra de direcciones) y vuelve al primero: la página de fondo nunca recibe el foco, y esa salida es lo que el navegador considera accesible. Foco inicial en "Cancelar", la única acción que no cambia nada: ninguna de las dos decisiones queda como predeterminada. Sin cierre por clic en el fondo, para que un clic suelto no cancele una decisión. Mientras se reenvía, los botones y Escape no hacen nada (`aria-disabled`, no `disabled`, para no perder el foco), y cancelar es idempotente: un doble Escape o un doble clic dan un solo aviso.
- **Movimiento de Alumnos.** Casi todo es `transform` y `opacity`, con estas excepciones: el realce del renglón que cambió y el de la fila bajo el puntero (`background-color`), el color de las transiciones de botones y campos y el trazo de la palomita (`stroke-dashoffset`), todos en elementos pequeños y de cambios raros. El barrido del renglón usa `clip-path` con destino explícito (`inset(0 0 0 0)`): `clip-path` no interpola entre `inset()` y `none`. Una fila cuyo rostro cambia entre dos consultas resalta y, si pasó a "registrado", su palomita se traza una vez. El sondeo y el filtro no repiten la entrada de los renglones. Esqueleto: un solo elemento cambia de opacidad (4 ciclos de 1,2 s). Diálogo: entra con un resorte (transición CSS con `linear()`, ζ = 0,55, 420 ms, rebote de 12,6 %, desde 20 px abajo y 90 %) y sale en 180 ms con `--ease-entrada`; son transiciones, así que se pueden interrumpir y se revierten desde donde van. La salida no depende de `overlay` (que Safari y Firefox no animan): el código espera `transitionend` y entonces llama a `close()`, con un respaldo de 400 ms, y con movimiento reducido cierra al instante. Zona para soltar: al arrastrar encima escala 1,015 y muestra un anillo de pluma (pseudoelemento que solo cambia de opacidad), al soltar rebota 340 ms y al rechazar el archivo se sacude 400 ms; el texto del rechazo sale siempre en un aviso `role="alert"`. Botones: encogido a `scale(0.97)` al presionar.
- **Avisos: Sonner 2.0.7 (versión exacta; `react` y `react-dom` `^18 || ^19`).** Un único `<Toaster>` (`componentes/Avisos.tsx`) montado en `App`. Se usa para el éxito de la importación (con los conteos) y para "Importación cancelada"; los errores no son toasts, porque deben quedarse en pantalla y llevar sus filas (avisos `role="alert"` bajo la zona). **La CSP del frontend (`style-src 'self'`) bloquea la etiqueta `<style>` que Sonner inyecta al cargar** (la consola lo registra una vez como violación y es inofensivo); por eso `main.tsx` importa `sonner/dist/styles.css`, que Vite empaqueta como archivo propio, antes de `estilos.css`. `estilos.css` ajusta la hoja de Sonner a los tokens (hoja, tinta, borde de tinta, radio de 2 px, tipografía; con sombra, porque flota) y `Avisos.tsx` le pasa la marca de asistencia como icono de éxito, advertencia y error con la misma especificidad y gana por orden, así que sigue el tema Claro u Oscuro sin pasarle `theme`. Las transiciones de Sonner (400 ms con `ease`) pasan a `--d-toast` (320 ms al entrar y reubicarse) con `--ease-entrada`, y la salida a 200 ms, el tiempo que Sonner espera antes de desmontar; con `prefers-reduced-motion` quedan sin movimiento por la regla global y por la propia de Sonner. Un toast queda debajo de un `<dialog>` modal (el diálogo está en la capa superior): los toasts de éxito y de cancelación se lanzan justo cuando el diálogo empieza a cerrarse, así que lo único que lo tapa son sus 180 ms de salida.
