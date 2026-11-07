# Dirección visual de AutoList

Reading this as: panel del profesor para pasar lista y administrar su grupo, para un profesor en laptop o en el proyector del aula, en el lenguaje de una lista de asistencia impresa (papel y tinta), dial ENERGY 2 / RHYTHM 2 / MOTION 2.

Este archivo manda sobre `src/estilos.css`: si una decisión visual cambia, se cambia aquí en el mismo cambio. Los tokens de código llevan los mismos nombres que usa este texto.

## Contenido

- [Por qué esta dirección](#por-qué-esta-dirección)
- [Dials](#dials)
- [Paleta](#paleta)
- [Tipografía](#tipografía)
- [El motivo: la marca de asistencia](#el-motivo-la-marca-de-asistencia)
- [Composición](#composición)
- [Radio, espaciado y elevación](#radio-espaciado-y-elevación)
- [Movimiento](#movimiento)
- [Voz del texto](#voz-del-texto)
- [La pantalla ILI9341](#la-pantalla-ili9341)
- [Lo que se descartó](#lo-que-se-descartó)

## Por qué esta dirección

El proyecto reemplaza la lista de asistencia impresa del profesor. Esa hoja ya tiene identidad: papel, renglones con su regla, columnas alineadas, un encabezado de formulario con campos para llenar (materia, grupo) y una marca de bolígrafo en cada renglón. Se toma ese objeto como lenguaje en lugar de copiar el aspecto de otro producto.

La versión anterior imitaba un panel de base de datos (oscuro, esmeralda, tarjetas de 1 px, Inter). El equipo la rechazó porque no tenía identidad propia: con otro nombre habría servido para cualquier panel.

## Dials

| Dial | Valor | Qué significa aquí |
|---|---|---|
| ENERGY | 2 | Papel cálido, tinta casi negra y una sola tinta de acento. Un foco por pantalla: en Alumnos, la hoja de la lista; en Métricas, la potencia; en Registro, la lista para elegir y, con un registro abierto, lo que dice el resumen del registro. |
| RHYTHM | 2 | Las tres vistas usan las mismas piezas (hoja, regla, marca) con composiciones distintas. Alumnos es una hoja con un margen para la importación. Métricas es una hoja de dos columnas (cifras con puntos guía y gráfica) y otra de lecturas. Registro es una hoja con su margen: la lista para elegir y, con un registro abierto, las cinco poses como renglones. |
| MOTION | 2 | Los renglones se escriben, la marca se traza, la vista cambia con un fundido corto y la zona del CSV responde al arrastre. Todo se apaga con `prefers-reduced-motion`. |

## Paleta

Papel, tinta y una tinta de acento. Los neutros no cuentan en el límite de R-29; los estados usan el rojo y la propia tinta, sin un tercer color de adorno.

Tema claro por defecto (papel). El oscuro (papel de noche) es la misma hoja con la luz apagada: fondo de tinta tibia y tinta de papel, sin pasar a gris frío ni a negro puro. Razón del tema claro: es una lista de papel y en un proyector se lee mejor clara. Sin preferencia guardada el tema es Claro. "Sistema" sigue al sistema operativo.

| Token | Claro | Oscuro | Para qué (una línea) |
|---|---|---|---|
| `--fondo` | `#f1ecdf` | `#15130f` | La mesa donde está la hoja; un papel un poco más oscuro que la hoja separa sin sombra. |
| `--hoja` | `#fcf9f1` | `#1e1b16` | El papel de la lista: un blanco cálido, no gris ni puro. |
| `--papel2` | `#f2ecdd` | `#29251d` | Banda de la cabecera de tabla y fila bajo el puntero. |
| `--tinta` | `#1c1a16` | `#f0e9d8` | Texto principal y reglas de encabezado: casi negro, con un punto de calidez. |
| `--cuerpo` | `#3f3b32` | `#d0c8b5` | Texto corrido. |
| `--suave` | `#625b4c` | `#a39b87` | Etiquetas de campo, pies y casilla vacía. |
| `--regla` | `#c2bba8` | `#4a443a` | Regla de renglón; decorativa, la información no depende de ella. |
| `--borde-control` | `#857d6a` | `#8b8472` | Borde de lo que se opera (botón, campo, zona): 3:1 o más. |
| `--pluma` | `#1b3c9c` | `#9fb5ff` | La tinta del bolígrafo con que se marca la lista (acento). |
| `--sobre-pluma` | `#fcf9f1` | `#15130f` | Texto sobre el relleno de pluma. |
| `--error` | `#a3241a` | `#ff9d8f` | Errores y faltas reales, y nada más. Su fondo es `--error-fondo` (`#fbe9e3` y `#2e1813`). |

Uso de la pluma, con mesura y solo en tres sitios: la acción principal (los botones "Elegir archivo CSV" e "Iniciar registro"), el estado registrado o presente (la marca y su texto, también "Foto aceptada" en las poses) y el foco (el anillo de 2 px). La zona del CSV la usa mientras se arrastra encima, porque es la acción principal respondiendo. El rojo no aparece salvo en errores y en la marca de falta.

Las advertencias no tienen color propio: llevan tinta sobre `--papel2` con borde de tinta. Un tono ámbar sería un cuarto color y la advertencia ya dice lo que es con su título.

Contraste calculado con el verificador de `antislop-human` (`contrast-check.py`), texto normal contra 4,5:1 y no texto contra 3:1:

| Par | Claro | Oscuro |
|---|---|---|
| tinta sobre hoja | 16,51 | 14,18 |
| cuerpo sobre hoja | 10,60 | 10,31 |
| suave sobre hoja | 6,40 | 6,21 |
| suave sobre `--papel2` | 5,71 | 5,52 |
| suave sobre el tinte de pluma de la zona del CSV al arrastrar (el par más justo de texto) | 4,92 | 5,18 |
| pluma sobre hoja (texto "Registrado") | 9,19 | 8,60 |
| texto del botón sobre relleno de pluma | 9,19 | 9,30 |
| error sobre su fondo | 6,33 | 8,34 |
| borde de control sobre hoja (el par más justo de no texto) | 3,88 | 4,61 |
| anillo de foco sobre hoja | 9,19 | 8,60 |

Se comprobaron además los estados con tinte (fila resaltada, zona al arrastrar y al rechazar, botón al pasar el cursor y al presionar), la casilla, las marcas y el anillo de foco, en los dos temas. Todos cumplen.

## Tipografía

Dos familias, autoalojadas con `@fontsource` y versión exacta (la CSP es `style-src 'self'` y el aula no tiene internet). Se declaran con `@font-face` a mano, solo los subconjuntos latin y latin-ext, para que el build no empaquete cirílico ni griego sin uso.

- **Source Serif 4** (`@fontsource/source-serif-4` 5.3.1, peso 600): títulos y cifras grandes. Es una serif de texto diseñada para leerse en papel y en pantalla, con voz de documento escolar. Razón: la lista impresa se titula en serif y el título tiene que sentirse como el de un documento, no como el de una aplicación.
- **Libre Franklin** (`@fontsource/libre-franklin` 5.3.0, pesos 400 y 600): etiquetas, tablas, cuerpo y botones. Es la revisión de Franklin Gothic, la grotesca de los formularios impresos y de las tablas de periódico. Razón: los campos con su etiqueta, las columnas y las cuentas de la lista llevan esa voz de formulario, y sus cifras son tabulares.

No se usan Inter, Geist ni Space Grotesk. Las cuentas, las horas y los conteos piden `font-variant-numeric: tabular-nums` para que las columnas queden alineadas y las cifras no bailen al contar. El mono del sistema se usa solo en el texto de código (`no_cuenta,nombre,grupo`, el tópico MQTT), nunca como estética.

Escala: título de hoja 34 px (28 en pantallas angostas); título de sección 22 px; cuerpo 16 px; tabla 16 px (en proyector se lee desde el fondo del aula); etiquetas y pies 13 px; cifra protagonista 56 px (44). Sin mayúsculas con espaciado amplio: las etiquetas van en minúsculas y en peso 600.

## El motivo: la marca de asistencia

Una casilla de papel y la marca que el profesor pone en ella. Se dibuja con un SVG en línea hecho a mano (sin librería de iconos ni ilustraciones), con trazos algo irregulares para que se lea como bolígrafo y no como un icono de biblioteca.

| Marca | Dibujo | Dónde |
|---|---|---|
| Presente | Casilla con una palomita de pluma que sale por arriba | Alumno con rostro registrado o asistencia a tiempo; "Placa conectada"; avisos de éxito; foto aceptada de una pose; "Registro completado" |
| Retardo | Casilla con una diagonal en tinta de la hoja (`--tinta`) | Asistencia registrada después de la tolerancia, siempre junto a la palabra "Retardo" (sin introducir ámbar) |
| Pendiente | Casilla vacía, en tinta suave | Alumno con rostro pendiente o aún sin registro en una sesión abierta; "Conectando" y "Sin sesión"; estados vacíos; pose pendiente, esperando o tomándose; registro en espera o en curso |
| Falta | Casilla con una tachita en rojo | Falta al cierre de la sesión o fallo real: "Sin conexión con la placa"; avisos de error; foto rechazada de una pose; registro terminado con error |

Es el gesto repetido: columna del rostro de Alumnos, resumen de la hoja, indicador de la cabecera, estados vacíos y avisos. Siempre acompaña a un texto que dice el estado: la marca refuerza y nunca es la única señal. El logo sigue siendo la palabra "AutoList" en texto, en la serif; no hay logotipo.

## Composición

- **Alumnos** (foco: la lista). Una hoja con encabezado de documento: título "Lista de alumnos", los campos de formulario Materia ("Fundamentos de Sistemas Embebidos", del plan) y Grupo, y los conteos reales de lo que se ve (alumnos, con rostro, pendientes). Como en un formulario impreso, la materia es texto preimpreso: va en la serif, sin línea de llenado. Solo el grupo, el espacio por llenar (el filtro), lleva su línea de tinta. Debajo, los renglones con su regla, numerados, con las columnas alineadas. La importación del CSV vive en el margen de la hoja (19 rem fijos, no crece con la ventana), a la derecha desde 1000 px de ancho y debajo en las angostas, como acción secundaria. En una pantalla angosta caben nombre y estado; la cuenta y el grupo pasan debajo del nombre.
- **Métricas y asistencia.** Cabecera de la vista, seguida de la hoja de asistencia por sesión (selector de sesión, materia y grupo, horario y tolerancia, búsqueda por nombre o cuenta, botón de descarga de CSV cuando la sesión cerró, resumen de inscritos/presentes/retardos/faltas o pendientes y tabla nominal con hora y similitud coseno), la hoja de confianza e intentos rechazados en dos columnas (cifras con puntos guía y tabla de rechazos), la hoja de energía en dos columnas (potencia protagonista con cifras en renglones y gráfica de tinta sin relleno), la hoja de lecturas recientes y el estado vacío honesto de latencia de inferencia.
- **Alumnos, búsqueda y acciones.** El campo de búsqueda va en el encabezado, junto al grupo, como otro espacio por llenar: sin caja, sobre su línea de tinta. Los tres campos (materia, grupo, búsqueda) comparten renglón cuando la hoja mide 44 rem o más (consulta de contenedor, porque el ancho que importa es el de la hoja y no el de la ventana); con menos, la materia va arriba. En cada renglón, la celda del Rostro lleva el estado y su acción ("Registrar rostro" o "Retirar rostro", botón de 32 px en pantallas anchas y de 44 px en táctiles y angostas, apilado bajo el estado en un teléfono). La confirmación de retirar es el mismo diálogo de las cuentas existentes, de la misma hoja con su regla doble.
- **Registro.** La composición de Alumnos: hoja y margen de 19 rem (debajo de la hoja bajo 1000 px). Con la vista libre, la hoja es la lista para elegir (renglones con una casilla de opción, los pendientes primero; los registrados en tinta suave y sin poder elegirse) con la búsqueda en su encabezado, y el margen es una hoja chica con el alumno elegido, la casilla de consentimiento, el aviso de que el formato está pendiente y "Iniciar registro", la única acción principal. Con un registro abierto, el encabezado lleva alumno, cuenta y grupo como campos del formulario; debajo, el resumen (la marca de asistencia que toca y el título en la serif: "Esperando al servicio de visión", "Registro en curso", "Guardando el rostro", "Registro completado"...), la advertencia de parecido si la hay y las cinco poses como renglones numerados con su regla: lo que se le pide al alumno, y la marca con el estado en palabras. Una foto rechazada lleva debajo el motivo. El margen lleva "Cancelar registro" o, al terminar, "Registrar a otro alumno". Sin miniaturas de las fotos: el contrato no define su transporte, y un espacio de relleno sería una imagen inventada.

No hay cuadrícula, renglones ni papel milimétrico de fondo (R-07): las reglas existen solo donde hay renglones de datos. Nada de héroe más tarjetas ni de fila de estadísticas.

## Radio, espaciado y elevación

- **Radio.** El papel tiene esquinas casi rectas: hojas, tablas y avisos de 2 px; botones y campos de 3 px; el diálogo de 4 px. Sin píldoras. Razón: una hoja impresa no tiene esquinas redondas y el radio pequeño, constante y deliberado es parte del lenguaje. La jerarquía la dan la regla y el tono, no el radio.
- **Espaciado.** Base de 4 px (4, 8, 12, 16, 24, 32, 48, 64). El ritmo varía a propósito: el encabezado de la hoja respira (32 px de relleno), los renglones son compactos (12 px de relleno vertical, renglón de unos 48 px) y entre bloques de la vista hay 24 px. En pantallas angostas (640 px o menos), 16 px de margen y de relleno.
- **Ancho: fluido con tope.** El contenido crece con la ventana, con un margen lateral fijo de 32 px, y se detiene en 1680 px (margen incluido, `--contenedor`) y se centra. Razón: una hoja de lista que ocupa toda la ventana en un laptop o en el proyector se lee mejor, y el tope evita que en un monitor muy ancho las columnas queden tan separadas que el ojo pierda el renglón. La cabecera y las vistas comparten el mismo contenedor, así que la marca y el selector de tema quedan alineados con los bordes de las hojas en todos los anchos. El ancho extra se reparte así: en Alumnos casi todo va a la hoja (el margen del CSV mide 19 rem fijos) y dentro de la tabla a la columna Nombre (las demás miden lo que su contenido), para que Rostro no se aleje de Nombre más de lo necesario; en Métricas la columna de cifras mide 23 rem y la gráfica se queda con el resto (y crece en alto, de 320 a 460 px); en Registro y en los textos corridos la línea de lectura no pasa de unos 62 a 80 caracteres aunque la hoja crezca.
- **Reglas.** Renglón: 1 px en `--regla`. Cabecera de tabla: 1,5 px de tinta debajo. Encabezado de la hoja: regla doble de tinta (como una hoja impresa). Solo donde hay datos.
- **Elevación.** La hoja se separa de la mesa por tono y un borde de 1 px, sin sombra. Solo flotan, y por eso tienen sombra, el diálogo y los avisos (toasts), porque cubren el contenido.

## Movimiento

Curva de salida fuerte (`cubic-bezier(0.23, 1, 0.32, 1)`) para entradas y salidas, y una curva suave de principio a fin (`cubic-bezier(0.45, 0.05, 0.3, 1)`) para lo que se escribe (el renglón y la marca), porque con la fuerte el trazo terminaría en una décima y no se vería. Sin rebotes salvo el diálogo. Las salidas son más rápidas que las entradas. Con `prefers-reduced-motion: reduce` se eliminan animaciones espaciales, barridos, resortes y escalonados, dejando solo fundidos breves de opacidad y color (120 ms) y ningún estado inicial oculta contenido.

| Qué | Cómo | Para qué |
|---|---|---|
| Renglones de la lista | Entran de arriba abajo, cada uno con un barrido de izquierda a derecha (240 ms, escalón de 25 ms, solo los 8 primeros, solo al abrir la vista) | Se leen como si se escribieran en la hoja y dicen en qué orden leer |
| Marca trazada en vivo | La palomita o diagonal se traza (`stroke-dashoffset`, 480 ms) cuando un alumno pasa a "registrado", marca asistencia en vivo o una pose pasa a "aceptada" | Comunica el cambio de estado en tiempo real |
| Subrayado de navegación | Línea de 1 px escalada en GPU (`translate3d` y `scaleX`, 220 ms) | Continuidad espacial entre vistas sin recalcular layout |
| Cambio de vista | La que se va sale en 90 ms con un fundido; la nueva entra en 220 ms con 6 px de subida y un escalón de 35 ms entre bloques | Marca que el contenido cambió con rapidez de herramienta |
| Zona del CSV | Al arrastrar encima crece 1,5 % y se pinta de pluma; al soltar rebota una vez; si rechaza el archivo, se sacude | Dice que la zona oye el arrastre, que recibió el archivo o que lo rechazó |
| Cifras | Cuentan hasta su valor (800 ms la primera vez, 300 ms al cambiar) | Muestran que el dato es nuevo |
| Esqueleto de carga | Pulso de opacidad, 4 ciclos de 1,2 s y se detiene | Dice que se espera una respuesta, sin bucle infinito |
| Diálogo y avisos | Entrada breve (diálogo con un resorte corto, avisos de 320 ms) y salida rápida (160 ms) | Son lo único que flota |

Se quitaron la tarjeta que sube al pasar el cursor, las sombras de realce y la barra de progreso.

## Voz del texto

Español, sin rayas largas, sin relleno ni frases de anuncio. Los botones nombran la acción ("Elegir archivo CSV", "Actualizar nombre y grupo", "Reintentar"). Los estados vacíos dicen la causa y la siguiente acción. No hay cifras ni ejemplos inventados: una vista sin datos lo dice. La materia sale del plan (línea "Materia" al inicio de `docs/HIGH-LEVEL-PLAN.md`); todo lo demás sale de la API.

## La pantalla ILI9341

La misma identidad pasa a la pantalla de 320×240: fondo `--hoja` y tinta `--tinta`, la casilla con su marca a 24 px (se lee bien en pequeño porque son tres trazos), título en la serif y renglones con su regla de 1 px. El panel de la pantalla usará los tokens de arriba y no los de otro tema. Ese programa es de otro carril y no se toca aquí.

## Lo que se descartó

- **Oscuro por defecto, Inter, acento esmeralda y tarjetas de 1 px** (la dirección anterior): copiaba un producto y no tenía identidad propia (R-30, R-06, R-21, R-20).
- **Relleno degradado bajo la línea de la gráfica**: sin gradientes. La línea es de tinta y no lleva relleno.
- **Fila de estadísticas y héroe**: no hay datos que la justifiquen y es la plantilla que se quiere evitar (R-05).
- **Fuentes manuscritas para "la mano del profesor"**: gimmick; la marca dibujada a mano ya carga ese gesto.
- **Papel con renglones de fondo**: cuadrícula decorativa sin datos detrás (R-07).
