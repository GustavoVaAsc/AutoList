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
| Métricas y asistencia | Funciona solo la sección de energía: tres cifras actuales, gráfica de la potencia y tabla de lecturas recientes. Consulta `GET /api/metricas/energia` cada 5 s. |
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
    main.tsx               arranque y fuentes
    estilos.css            tokens (variables CSS), temas claro y oscuro, movimiento y estilos
    tema.ts                preferencia de tema (Sistema, Claro, Oscuro) y su almacenamiento
    lecturasContexto.ts    reparte el estado del sondeo a la cabecera y a las vistas
    serie.ts               ventana visible y huecos de la gráfica
    App.tsx                encabezado, navegación por hash (#/metricas) y vistas
    api/energia.ts         cliente de /api/metricas/energia y validación de la respuesta
    useLecturas.ts         consulta periódica (5 s, en pausa con la pestaña oculta)
    formato.ts             horas (America/Mexico_City) y cifras
    vistas/                Alumnos, Registro, Metricas
    componentes/           TarjetaCifra, GraficaPotencia, TablaLecturas, EstadoVacio,
                           IndicadorEstado, SelectorTema
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
| `@fontsource/ibm-plex-sans`, `@fontsource/ibm-plex-mono` | Tipografías empaquetadas con el build: el aula puede no tener internet, así que no se carga nada de Google Fonts ni de un CDN. IBM Plex Sans (400, 500 y 600) e IBM Plex Mono (400), solo los subconjuntos latin y latin-ext, que se importan en `src/main.tsx`. La razón de elegir Plex está en el comentario de `src/estilos.css`. |

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
- **Identidad.** Base de Vercel (tokens, espaciado, elevación) con una capa propia: wordmark tipográfico "AutoList" (sin símbolo) y un solo color de marca, el verde asistencia (`--acento`). Se usa solo en el wordmark, la pestaña activa, la cifra de potencia, la línea y el relleno de la gráfica, el punto "en vivo" y el anillo de foco. Está medido: 5.0:1 sobre blanco y 4.8:1 sobre `#fafafa` en claro; 8.5:1 y 8.9:1 en oscuro. No se usa como texto sobre `--acento-suave` (4.4:1): ahí el texto es `--tinta`.
- **Estado de la placa en la cabecera.** Sale del sondeo, no de un dato inventado: "En vivo" si la última lectura tiene menos de 60 s según la hora de la placa, "Sin lecturas recientes" si es más vieja y "Sin conexión con la placa" si la API falla (mientras llega la primera respuesta dice "Conectando con la placa"). El texto es la información; el punto es redundante. El pulso del punto corre una vez por cada lectura nueva y nunca en bucle. Un `role="status"` oculto anuncia solo el cambio de estado, no el contador de segundos.
- **Tema.** Selector nativo con Sistema, Claro y Oscuro, guardado en `localStorage` y aplicado como `data-theme` en `<html>`. `public/tema.js` lo aplica antes de pintar para evitar el parpadeo; es un archivo externo porque la CSP prohíbe scripts en línea.
- **Movimiento.** Solo transiciones con propósito (curva `cubic-bezier(0.23, 1, 0.32, 1)`, de 100 a 200 ms): el indicador de la pestaña activa se desliza entre pestañas, la vista entra con opacidad y 4 px de desplazamiento, la gráfica se revela una sola vez al montar, y los botones tienen hover, active (`scale(0.97)`) y disabled. Las cifras y los datos que se refrescan cada 5 s no se animan. Todo se desactiva con `prefers-reduced-motion: reduce`.
- **Navegación honesta.** Alumnos y Registro llevan la etiqueta "Pronto" en la propia navegación, porque sus vistas todavía no funcionan.
