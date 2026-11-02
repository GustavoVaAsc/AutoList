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
| Métricas y asistencia | Funciona solo la sección de energía: potencia actual y datos de la ventana de 30 min, gráfica de la potencia y tabla de lecturas recientes. Consulta `GET /api/metricas/energia` cada 5 s. |
| Alumnos | Armazón, con la etiqueta "Pronto" en la navegación y un aviso de que llega en un avance posterior. |
| Registro | Armazón, con la etiqueta "Pronto" en la navegación y un aviso de que llega en un avance posterior. |

El frontend no se conecta a MQTT en esta etapa: no usa `mqtt.js` ni el puerto 9001. Todo sale de la API HTTP de Node-RED.

La API no tiene autenticación hoy. Cuando entre `httpNodeAuth` (11 nov), el navegador pedirá usuario y contraseña al consultar `/api/...`.

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

No hay linter ni pruebas automatizadas en `web/` todavía.

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
  index.html
  vite.config.ts           base '/app/' y proxy de /api para desarrollo
  src/
    main.tsx               arranque
    estilos.css            fuente Inter (@font-face), tokens (variables CSS), temas claro y oscuro, movimiento y estilos
    tema.ts                preferencia de tema (Sistema, Claro, Oscuro) y su almacenamiento
    lecturasContexto.ts    reparte el estado del sondeo a la cabecera y a las vistas
    serie.ts               ventana visible y huecos de la gráfica
    App.tsx                encabezado, navegación por hash (#/metricas) y vistas
    api/energia.ts         cliente de /api/metricas/energia y validación de la respuesta
    useLecturas.ts         consulta periódica (5 s, en pausa con la pestaña oculta)
    formato.ts             horas (America/Mexico_City) y cifras
    vistas/                Alumnos, Registro, Metricas
    componentes/           Hero, Cifra, GraficaPotencia, TablaLecturas, EstadoVacio,
                           AvisoObsoleta, IndicadorEstado, SelectorTema
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
| `@fontsource-variable/inter` | Tipografía Inter variable empaquetada con el build: el aula puede no tener internet, así que no se carga nada de Google Fonts ni de un CDN. Solo se empaquetan los subconjuntos latin y latin-ext, que `src/estilos.css` declara con `@font-face` (el `wght.css` del paquete traería también cirílico, griego y vietnamita). |

## Decisiones

- **Consulta periódica y no MQTT.** Es más simple y la lectura de energía no necesita latencia de milisegundos. La siguiente consulta se agenda al terminar la anterior, y se pausa si la pestaña está oculta.
- **Aviso de lecturas viejas.** Si la última lectura tiene más de 60 s, la vista lo dice, porque la XIAO pudo dejar de publicar. La antigüedad se calcula con la cabecera `Date` de la respuesta de la API (el reloj de la placa, el mismo que selló la lectura) y no con la hora de la laptop; si la cabecera falta, se usa el reloj del navegador.
- **Gráfica: ventana y huecos.** La gráfica dibuja solo los últimos 30 min respecto de la última lectura (las lecturas anteriores siguen en la tabla, y la vista lo dice). Con el ritmo normal de la XIAO las 100 lecturas que se piden caben en la ventana; recorta solo tras un paro largo, para que una ráfaga vieja no aplaste la reciente. Dentro de la ventana, la línea se corta donde el intervalo entre lecturas supera 5 veces el intervalo habitual (la mediana de los intervalos), con un mínimo de 30 s. Una lectura aislada se dibuja como punto. Si el rango cruza la medianoche, las marcas del eje y la tabla incluyen el día. El criterio está en `src/serie.ts`.
- **`npm run build` incluye `tsc`.** Un error de tipos impide compilar y, con ello, reconstruir la imagen de Node-RED. Se mantiene así a propósito: es preferible no desplegar un frontend que no pasa los tipos.
- **Una fila inválida pone la vista en error.** Si una sola lectura de la respuesta no tiene hora, voltaje, corriente y potencia numéricos, la vista muestra el error y no dibuja las demás. Se mantiene a propósito: el flujo de Node-RED ya valida cada lectura al ingerirla (`orangepi/README.md`, sección Energía), así que una fila inválida indica un fallo real y no un dato que haya que ocultar.
- **Cabeceras de seguridad.** Node-RED las fija en `nodered/settings.js`: en `/app/`, una CSP que solo permite el mismo origen, más `nosniff`, `Referrer-Policy: no-referrer` y `X-Frame-Options: DENY`; en el editor y la API, solo `nosniff` y `X-Frame-Options: DENY`. Una dependencia nueva que cargue algo de otro origen, o estilos en línea en el HTML, chocará con esa CSP.
- **Navegación por hash.** Node-RED sirve archivos estáticos y no sabe resolver rutas de una SPA: con `#/metricas`, recargar la página no da 404.
- **Zona horaria.** Todas las horas se muestran en `America/Mexico_City`, sin importar la zona de la laptop.
- **Sin cifras inventadas.** Una vista sin datos dice que no hay datos. Alumnos y Registro no muestran ejemplos.
- **Lenguaje visual inspirado en Apple: gramática y tokens, nunca la marca.** Se toman las secciones a todo el ancho que alternan fondo, la escala tipográfica, los radios y la barra translúcida. No hay logo, nombres de productos ni textos que hagan confundible la página con apple.com. Cada decisión visual tiene su razón en un comentario de `src/estilos.css`.
- **Un solo acento: el verde asistencia.** Cumple el papel que Apple da a su azul: enlaces, control activo (el anillo del segmento), foco, línea de la gráfica, punto "en vivo", "List" del wordmark y el botón primario si llega a existir. En superficies claras es `#1a7f4b` (5.02:1 sobre blanco, 4.6:1 sobre perla); sobre las secciones casi negras, la variante clara `#4cc38a` (6.7:1 sobre `#272729`). El rojo y el ámbar son solo estados de error y advertencia. La cifra de potencia va en tinta, no en el acento.
- **Tipografía.** `-apple-system, BlinkMacSystemFont, "Inter Variable", "Inter", sans-serif`: en una Mac sale la fuente del sistema y en Windows o Android sale Inter, nunca Segoe ni Roboto (por eso no hay `system-ui` antes de Inter). Escala: hero 56 px (40 px bajo 1068 px y 34 px bajo 640 px), peso 600, interlínea 1.07 y tracking de -0.28 px; display de 40 px para el `h1`; titular de sección de 28 px; cuerpo de 17 px con interlínea 1.47 y tracking de -0.374 px; caption de 14 px con -0.224 px. Pesos 300, 400 y 600, nunca 500. Las horas y las unidades usan `tabular-nums` y no una fuente monoespaciada. Excepciones a la escala, con su razón en el CSS: la etiqueta "Pronto" (12 px, dentro del segmento) y la entradilla en pantallas angostas (21 px).
- **Secciones que alternan fondo.** La vista Métricas y asistencia es una pila de secciones a todo el ancho, con el contenido centrado a un máximo de 1200 px; el cambio de color es el separador, sin bordes ni sombras entre ellas. Cada vista tiene un `h1` y sus secciones llevan `h2` (en Métricas: Energía, que solo leen los lectores de pantalla, la gráfica y la tabla). Hero en gris perla `#f5f5f7` (`h1`, entradilla, la potencia como cifra protagonista, datos de la ventana de 30 min, hora de la última lectura y aviso de lecturas viejas), gráfica en `#272729` y tabla en blanco, dentro de una tarjeta. Alumnos y Registro usan el mismo hero con su estado vacío.
- **Barra superior fija y translúcida.** Perla al 92 % con `backdrop-filter: saturate(180%) blur(20px)`, el único desenfoque de la página; con `prefers-reduced-transparency: reduce` el fondo es sólido. Lleva el wordmark, las tres vistas como control segmentado en píldora (con `aria-current`; Alumnos y Registro conservan su "Pronto"), el indicador de estado y el selector de tema con su etiqueta "Tema". Solo es fija (`sticky`) desde 960 px de ancho y 600 px de alto, donde mide 56 px en una sola fila; ahí `html` lleva `scroll-padding-top` y el `h1` y los titulares de sección `scroll-margin-top`, para que el foco programático no quede tapado. En el resto se desplaza con la página: bajo 960 px el control pasa a su propia fila, y a 360 px el estado baja a una tercera, con lo que la barra mide hasta unos 136 px; fija, taparía media ventana en horizontal o con zoom al 400 % (WCAG 1.4.10 y 2.4.11).
- **Modo oscuro.** Barra translúcida oscura (`#1d1d1f` al 92 %), hero y tabla en negro, gráfica en `#272729` y tarjeta en `#1d1d1f`: la alternancia sigue leyéndose sin bordes. La sección de la gráfica usa los mismos tokens (`--g-*`) en ambos temas. Contrastes medidos en ambos temas (texto sobre perla, blanco y casi negro, acento, ejes, "Pronto" y contornos de controles): todo el texto cumple 4.5:1 y lo no textual 3:1, con una excepción: el wordmark, que es un logotipo y está exento (WCAG 1.4.3). Su peor caso, "List" sobre la barra con la sección oscura debajo (fondo `#e5e5e7`), mide 4.0:1 en claro y 7.5:1 en oscuro. El texto secundario claro es `#515154` y no el gris habitual, porque la barra translúcida sobre la sección oscura baja a `#e5e5e7` de fondo (6.3:1).
- **Radios y sombras.** Radios de 8 px (controles compactos y tooltip), 18 px (tarjetas y avisos) y píldora (acciones, control segmentado y selector). No hay sombras en tarjetas, botones ni texto: la elevación sale del cambio de superficie y del desenfoque de la barra. El segmento activo lleva un anillo de 1 px (un borde) en el acento. El relleno degradado de la gráfica es el único degradado, y su propósito está escrito en `GraficaPotencia.tsx`.
- **Antislop.** Sin rayas largas, sin emoji, sin mayúsculas sostenidas con tracking amplio y sin degradados decorativos. Los encabezados llevan mayúscula inicial y no terminan en punto.
- **Estado de la placa en la cabecera.** Sale del sondeo, no de un dato inventado: "En vivo" si la última lectura tiene menos de 60 s según la hora de la placa, "Sin lecturas recientes" si es más vieja y "Sin conexión con la placa" si la API falla (mientras llega la primera respuesta dice "Conectando con la placa"). El texto es la información; el punto es redundante. El pulso del punto corre una vez por cada lectura nueva y nunca en bucle. Un `role="status"` oculto anuncia solo el cambio de estado, no el contador de segundos.
- **Tema.** Selector nativo con Sistema, Claro y Oscuro, guardado en `localStorage` y aplicado como `data-theme` en `<html>`. `public/tema.js` lo aplica antes de pintar para evitar el parpadeo; es un archivo externo porque la CSP prohíbe scripts en línea.
- **Movimiento.** Solo transiciones con propósito (curva `cubic-bezier(0.23, 1, 0.32, 1)`, de 100 a 200 ms): el segmento activo se desliza entre vistas (200 ms) y solo al cambiar de ruta, la vista entra con opacidad y 4 px de desplazamiento, la gráfica se revela una sola vez al montar, y botones, segmentos y selector se encogen a `scale(0.95)` al presionar (100 ms). Las cifras y los datos que se refrescan cada 5 s no se animan. Todo se desactiva con `prefers-reduced-motion: reduce`.
- **Navegación honesta.** Alumnos y Registro llevan la etiqueta "Pronto" en la propia navegación, porque sus vistas todavía no funcionan.
