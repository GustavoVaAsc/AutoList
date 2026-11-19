# Revisión de slop del frontend: qué se arregla

## Cambios en la ronda 1

La ronda 1 llegó sin respuestas. En el chat elegiste aplicar todas las recomendaciones, y resolviste tres puntos que quedaban abiertos:

- Fases (`fases`): dos fases. La fase 1 abarca todos los hallazgos T-* y V-* y empieza por C-TEST-1. La fase 2 queda para otro PR: C-API-*, C-HOOK-1, C-COM-1, C-CSS-*, los duplicados y el resto de C-TEST.
- Última foto (`ultima-foto`): se conserva como está.
- Probar reconocimiento (`prueba-ubicacion`): pasa a Métricas, junto a Sistema.
- Nombre de la XIAO (`nombre-xiao`): se llamará "Nodo del aula".
- Animaciones (`animaciones`): C-GLO-1, C-GLO-2, C-DEAD-1 y C-ASI-2 entran en la fase 1.
- Materia (V-ALU-1): pasa a un subtítulo.
- T-MET-4: el aviso dirá "Orden enviada: abrir puerta".
- T-MET-7: las etapas de la latencia se conservan.
- Actuadores: siguen dentro de Métricas.

**Decisión que necesito:** de los hallazgos de la revisión (textos, diseño visual y código), cuáles se aplican y en qué orden. Marca en cada bloque lo que quieres; lo que no marques se queda como está. Puedes comentar cualquier línea.

La revisión se hizo sobre `main` (b6a6597). La separación de **Asistencia** y **Métricas** ya está hecha en `work-yordi` (sin desplegar), así que varios hallazgos de "Métricas y asistencia" ya quedan resueltos a medias.

Cada hallazgo tiene un ID (T = texto, V = visual, C = código) y una severidad: **alto**, **medio** o **bajo**.

## Dos hallazgos que contradicen lo que ya decidiste

- **V-REG-2** propone **quitar el recuadro "Última foto"** porque repite la miniatura de la pose. Tú lo pediste explícitamente ("un recuadro donde salga la miniatura abajo y las miniaturas chicas"). Mi recomendación: **conservarlo**.
- **V-REG-1** propone **mover "Probar reconocimiento" de Registro a Métricas**, porque es diagnóstico y no parte del alta, y así Registro queda con una sola acción. Esto no lo habías decidido tú: lo puse yo en el margen de Registro. Mi recomendación: **moverlo a Métricas**, junto a Sistema, que es donde se diagnostica.

```kosmos-question
id: ultima-foto
type: single
prompt: ¿Qué hago con el recuadro "Última foto" (V-REG-2)?
options:
  - Conservarlo como está (recomendado)
  - Conservarlo, pero más simple (sin leyenda ni detalle)
  - Quitarlo y dejar solo las miniaturas por pose
```

```kosmos-question
id: prueba-ubicacion
type: single
prompt: ¿Dónde va "Probar reconocimiento" (V-REG-1)?
options:
  - Moverlo a Métricas, junto a Sistema (recomendado)
  - Dejarlo en el margen de Registro
```

## Orden de trabajo

Mi recomendación es hacerlo en **dos fases**:

1. **Fase 1, lo que ve la profesora:** textos (T-*) y diseño (V-*). Se despliega antes de las pruebas en clase.
2. **Fase 2, el código por dentro:** cliente HTTP único, hook de sondeo único, comentarios y CSS (C-*). Se nota poco en la interfaz y toca mucho código. Va en un PR aparte, cuando no haya pruebas en curso.

Antes de cambiar textos hay que ajustar las pruebas que comparan cadenas literales (**C-TEST-1**): si no, cada cambio de texto rompe pruebas. Eso va al inicio de la fase 1 y no necesita tu decisión.

```kosmos-question
id: fases
type: single
prompt: ¿Cómo lo hacemos?
options:
  - Dos fases, textos y diseño primero (recomendado)
  - Todo junto en un solo cambio
  - Solo textos y diseño; el código se queda
```

## Global y cabecera

- **T-GLO-1 (alto).** Los errores hablan como un ingeniero: "La API respondió con el error 502…", "avisa al responsable del borde", "Node-RED no pudo procesar… Revisa su registro". Hay unas 40 variantes. Propuesta: cinco textos cortos ("No hay conexión con la placa.", "La placa tardó en responder.", "Falta iniciar sesión.", "La placa respondió algo inesperado. Intenta de nuevo.") y el detalle técnico a la consola del navegador.
- **T-GLO-2 (medio).** Terminología inconsistente: "visión" / "servicio de visión"; "Registro" / "Registro de rostro"; "Similitud" / "Confianza" / "Umbral coseno" / "sim."; "Lista de alumnos" / "padrón" / "lista maestra". Propuesta: un término por cosa en todas las vistas.
- **T-GLO-3 (bajo).** Cabecera: "Sin sesión en la API" → "Falta iniciar sesión"; "Placa conectada · visión y cámara en línea" → "Placa conectada" y solo nombrar lo que falla.
- **T-GLO-4 (bajo).** Registro muestra una ruta del repo (`docs/consentimiento.md`) en la interfaz. Quitarla.
- **V-GLO-1 (medio).** Tres avisos para el mismo hecho ("no hay datos recientes de la placa"): el indicador de la cabecera, `AvisoObsoleta` y el aviso de error de la vista. Dejar el indicador y un aviso por vista.
- **V-GLO-2 (bajo).** La entradilla bajo el título de cada vista enumera lo que ya se ve. Quitarla.

```kosmos-question
id: global
type: multi
prompt: Global y cabecera, ¿qué se aplica?
options:
  - T-GLO-1 errores en lenguaje de la profesora
  - T-GLO-2 un término por cosa
  - T-GLO-3 cabecera más corta
  - T-GLO-4 quitar la ruta docs/consentimiento.md
  - V-GLO-1 un solo aviso de datos viejos por vista
  - V-GLO-2 quitar entradillas
```

**Cómo nombrar a la XIAO en la interfaz.** El revisor propone llamarla "Cámara" en todas partes. Pero la XIAO también mueve la puerta, el LED, el buzzer y la luz, así que "Cámara sin conexión" en la hoja de actuadores confundiría.

```kosmos-question
id: nombre-xiao
type: single
prompt: ¿Cómo se llama la XIAO de cara a la profesora?
options:
  - Nodo del aula (cámara y actuadores)
  - Cámara, en todas partes
  - Dejar XIAO
```

## Alumnos

- **T-ALU-1 (alto).** Aviso de error: "No se pudo cargar la lista de alumnos / {error técnico} Se muestran los datos de las 14:32. La vista lo vuelve a intentar cada 10 s." → "No se pudo actualizar la lista. Mostrando la de las 14:32."
- **T-ALU-2 a T-ALU-7 (medio).** Textos largos del estado vacío, de la búsqueda sin resultados, de la zona CSV, del diálogo de cuentas existentes, del diálogo de retirar rostro y del aviso de importación parcial. Cada uno se acorta a una o dos frases. Ejemplo, retirar rostro: "Se borran su rostro y su consentimiento. El alumno sigue en la lista. No se puede deshacer."
- **T-ALU-8 a T-ALU-10 (bajo).** Etiquetas de conteo que cambian de frase, "con desplazamiento horizontal" en las etiquetas para lector de pantalla y un "Sin datos" que repite el aviso de error.
- **V-ALU-1 (medio).** El encabezado de la hoja tiene seis elementos antes de la primera fila. "Materia" es un texto fijo (`Fundamentos de Sistemas Embebidos`) que no se puede cambiar. Moverlo a un subtítulo, o quitarlo.
- **V-ALU-2 (medio).** La zona para soltar el CSV ocupa el margen siempre, aunque ya haya lista. Propuesta: un botón "Importar CSV" que la abra, y la zona visible solo con la lista vacía.

```kosmos-question
id: alumnos
type: multi
prompt: Alumnos, ¿qué se aplica?
options:
  - T-ALU-1 aviso de error corto
  - T-ALU-2..7 acortar textos de vacío, búsqueda, CSV y diálogos
  - T-ALU-8..10 detalles de etiquetas y lector de pantalla
  - V-ALU-1 materia como subtítulo (o quitarla)
  - V-ALU-2 zona CSV plegada tras un botón
```

## Registro

- **T-REG-1/T-REG-2 (alto).** El margen apila: alumno elegido, aviso de rostro existente, casilla, "El formato de consentimiento está pendiente de redactar (`docs/consentimiento.md`). Sin la casilla marcada no se puede iniciar el registro.", botón y "Listo para iniciar". Propuesta: alumno, casilla, botón y una línea "Falta: …" solo si falta algo.
- **T-REG-3 a T-REG-6 (medio).** "Esperando al servicio de visión… se cancela solo a los 5 minutos", motivos de fallo con jerga ("falló la NPU", "La base de datos no guardó el rostro", "Motivo sin traducir (código X)") y "Node-RED no tiene ningún registro abierto…". Se acortan y dicen qué hacer.
- **T-REG-7 (medio).** Prueba de reconocimiento: el texto de ayuda, el pie sobre la luz de la tira y los contadores "Van 3 s sin resultado / Resultado de hace 5 s / Se pidió hace 12 s". Propuesta: "Comprueba a quién reconoce la cámara. No marca asistencia." y quitar el resto.
- **T-REG-8 a T-REG-12 (bajo).** Textos de "Última foto", el aviso de parecido, "Comprobando…" sin marca y puntos suspensivos inconsistentes en los botones.
- **V-REG-3/V-REG-4 (bajo).** Estados sin la marca de asistencia (rompe `web/DESIGN.md`), y "advertencia" que se ve igual que "neutro".

```kosmos-question
id: registro
type: multi
prompt: Registro, ¿qué se aplica? (Última foto y la prueba se deciden arriba)
options:
  - T-REG-1/2 margen con solo lo que se decide
  - T-REG-3..6 estados y fallos en lenguaje claro
  - T-REG-7 prueba de reconocimiento sin contadores ni notas
  - T-REG-8..12 detalles de textos y botones
  - V-REG-3/4 marca en todos los estados; advertencia distinta de neutro
```

## Asistencia

- **T-ASI-1 (alto).** "Sesión en curso · el CSV se genera al cerrar en la pantalla del aula" → "En curso. El CSV estará al cerrar la sesión."
- **T-ASI-2 (medio).** El selector muestra `#12 · Fundamentos de Sistemas Embebidos · Grupo 3CM1 · 12 nov 10:00 (Cerrada)`. Quitar el id interno y la materia, que es siempre la misma: "12 nov 10:00 · 3CM1 · Cerrada". El botón "Descargar CSV (#12)" pasa a "Descargar CSV".
- **T-ASI-3 (medio).** Hoja de confianza con jerga: "Umbral coseno configurado", "Sin marcas faciales", motivos largos. Propuesta: título "Reconocimiento", quitar el umbral (es una constante 0.4 copiada del servidor; si cambia allá, la cifra miente, C-ASI-3) y tres motivos cortos: "No reconocido", "Ya registrado" e "Imagen insuficiente".
- **T-ASI-4 a T-ASI-8 (medio/bajo).** Cargas largas, "padrón", "Limpiar/Borrar búsqueda", cinco formas de decir vacío en la tabla ("En espera", "Sin registro", "Sin dato", "Sin rostro"…), y "15 min" sin la palabra tolerancia.
- **V-ASI-1 (alto).** Cada fila repite cuenta, hora y similitud dos veces (en las columnas y en el renglón chico bajo el nombre). El lector de pantalla lo lee todo dos veces. Dejar una sola estructura.
- **V-ASI-2 (medio).** La tabla de intentos rechazados casi siempre está vacía. Plegarla en "Intentos rechazados (N)".
- **V-ASI-3 (medio).** El encabezado repite materia, grupo, horario y tolerancia, que ya están en el selector. Una sola línea de subtítulo.
- **V-ASI-4 (bajo).** "Sesión en curso…" sustituye al botón de descarga y mueve el encabezado. Dejar el botón deshabilitado con su explicación.

```kosmos-question
id: asistencia
type: multi
prompt: Asistencia, ¿qué se aplica?
options:
  - T-ASI-1 texto de sesión en curso
  - T-ASI-2 selector sin id ni materia
  - T-ASI-3 hoja "Reconocimiento" sin umbral ni jerga
  - T-ASI-4..8 cargas, vacíos y etiquetas
  - V-ASI-1 filas sin datos duplicados
  - V-ASI-2 intentos rechazados plegados
  - V-ASI-3 encabezado de una línea
  - V-ASI-4 botón de descarga deshabilitado en vez de texto
```

## Métricas

- **T-MET-1 (alto).** La hoja Sistema tiene notas para quien programa: "La temperatura es la de la zona más caliente del SoC…" y un párrafo sobre el broker, los estados retenidos y los avisos de desconexión. También muestra RSSI, heap y NVS en la columna Detalle. Propuesta: quitar las notas y dejar en Detalle solo "Cámara encendida/apagada" y "Modo"; RSSI, heap y NVS solo si fallan.
- **T-MET-2 (alto).** Nombres de tópicos MQTT en la interfaz ("publicando en `aula/camara/energia`", "`aula/vision/resultado`"). Quitarlos.
- **T-MET-3 (alto).** Actuadores: "Órdenes manuales a la XIAO. Salen por el broker…" y las tres notas por grupo (la del LED es ilegible). Quitarlas.
- **T-MET-4 (alto).** Los avisos "Se pidió abrir la puerta", "Se pidió el LED azul"… pasan a "Puerta abierta" y "LED azul". Ojo: la orden sale, pero la XIAO puede no confirmarla. Mi recomendación es "Orden enviada: abrir puerta", que es corto y no promete lo que no se sabe.
- **T-MET-5/T-MET-6 (medio).** Tres textos de bloqueo y dos cajas para el mismo estado. Dejar un aviso: "Sin conexión con {nodo}: las órdenes se activan al volver." El diálogo de la puerta queda en "Abrir la puerta / Se cierra sola a los 3 s.", sin la palabra "servo".
- **T-MET-7 (medio).** Latencia: quitar la nota explicativa. Etiquetas "Última", "Mediana (100)" y "Máximo (100)", y un guion en lugar de "no corrió". Las etapas (captura, detección, embedding) se quedan: tú pediste la latencia para medir.
- **T-MET-8 a T-MET-13 (medio/bajo).** Quitar "(hora de Ciudad de México)" y "cada 5 s"; cuatro cargas casi iguales pasan a un esqueleto sin párrafo; "Mostrar las 100 lecturas" pasa a "Ver todas"; quitar la nota de lecturas omitidas de la gráfica.
- **V-MET-2 (alto).** Los 11 botones de actuadores pesan lo mismo, y "Abrir puerta" (la acción con riesgo) no se distingue de "Pitido corto". Propuesta: la puerta destacada; LED, buzzer y luz en filas de botones pequeños.
- **V-MET-3/V-MET-4 (medio).** Dos avisos dentro de Actuadores pasan a una línea de estado. En Sistema, el contador "hace 12 s" que cambia cada segundo pasa a la hora.
- **V-MET-1/V-MET-7 (medio).** Plegar lecturas recientes y la tabla de latencia ("Ver tabla"); quedan visibles las cifras y la gráfica.

```kosmos-question
id: metricas
type: multi
prompt: Métricas, ¿qué se aplica?
options:
  - T-MET-1 Sistema sin notas técnicas; detalle solo si falla
  - T-MET-2 sin nombres de tópicos MQTT
  - T-MET-3 Actuadores sin notas
  - T-MET-4 avisos de orden ("Orden enviada: abrir puerta")
  - T-MET-5/6 un solo aviso de bloqueo; diálogo de puerta sin "servo"
  - T-MET-7 latencia con etiquetas cortas (se quedan las etapas)
  - T-MET-8..13 horas, cargas y botones de tablas
  - V-MET-2 puerta destacada; demás botones pequeños
  - V-MET-3/4 una línea de estado en Actuadores; hora en vez de contador
  - V-MET-1/7 lecturas y tabla de latencia plegadas
```

**Actuadores dentro de Métricas.** Lo decidiste así hace un momento. El revisor sugiere de nuevo una pestaña propia; yo lo dejaría como está.

## Animaciones y adornos

- **C-GLO-1 (medio).** El subrayado que se desliza entre pestañas usa unas 60 líneas (ResizeObserver, fuentes y estado de animación). Un borde con CSS lo sustituye.
- **C-GLO-2 (bajo).** Un fundido de salida de 90 ms entre vistas con temporizador manual. Basta una entrada en CSS.
- **C-DEAD-1 (bajo).** `NumeroAnimado`: las cifras cuentan desde 0 durante 800 ms en cada carga. Quitarlo, junto con su prueba.
- **C-ASI-2 (bajo).** Las filas de asistencia entran escalonadas. Se queda solo el resalte de la fila nueva, que sí sirve en vivo.

```kosmos-question
id: animaciones
type: multi
prompt: ¿Qué animaciones se simplifican?
options:
  - C-GLO-1 subrayado de pestañas con CSS
  - C-GLO-2 fundido entre vistas solo con CSS
  - C-DEAD-1 quitar el conteo animado de cifras
  - C-ASI-2 quitar el escalonado de filas (se queda el resalte)
```

## Código (fase 2 si eliges dos fases)

- **C-API-1 (alto).** Hay cuatro copias del cliente HTTP (`pedirJson` y sus clones en energía, alumnos y registro), con los mismos mensajes. Se unifican en `api/http.ts` (unas 150 líneas menos). Va ligado a T-GLO-1.
- **C-HOOK-1 (alto).** Cinco hooks de sondeo copian el mismo bucle de unas 100 líneas cada uno. Se extrae un `useSondeo` (unas 400 líneas menos) y se le añaden pruebas (C-TEST-2), porque el sondeo es lo que más falla en el aula (401, plazos agotados).
- **C-API-2/3/4/5 (medio).** Funciones de lectura (`objeto`, `texto`…) repetidas en 6 archivos; la tabla de mensajes por código HTTP, repetida 4 veces; la hora del servidor, calculada en 3 sitios.
- **C-COM-1 (alto).** Comentarios de 8 a 25 líneas que narran decisiones pasadas. Se podan unas 250 líneas.
- **C-CSS-1/2 (medio).** `estilos.css` tiene 2519 líneas. Hay que quitar lo que muera con los cambios de arriba y renombrar clases con nombre ajeno a su uso (`hoja--intentos`, que usan Sistema y Latencia).
- **C-MET/C-REG/C-ALU/C-ASI (medio/bajo).** Componentes duplicados (`Renglon`, las tablas expandibles, `MARCA_DE_TONO`, `useAhora`, `entero`, `MATERIA`), `HojaEnergia` y `HojaIntentos` como componentes propios, el foco frágil tras retirar un rostro y tres diálogos casi iguales que pasan a un `DialogoConfirmacion`.
- **C-TEST-1 (medio).** Pruebas que comparan textos literales o constantes. Se reescriben para comprobar tipo y tono.

```kosmos-question
id: codigo
type: multi
prompt: Código, ¿qué se aplica?
options:
  - C-API-1 un solo cliente HTTP
  - C-HOOK-1 un solo hook de sondeo con pruebas
  - C-API-2..5 utilidades de lectura y mensajes compartidos
  - C-COM-1 podar comentarios
  - C-CSS-1/2 limpiar y renombrar CSS
  - C-MET/REG/ALU/ASI duplicados y componentes propios
  - C-TEST-1 pruebas sin textos literales
```

## Lo que no está en la lista

La documentación (`web/README.md`, `web/DESIGN.md` y el README raíz) quedó desactualizada con la separación y lo estará más con estos cambios. Te preguntaré si la actualizo cuando el código esté listo. Mi recomendación es actualizarla, porque `web/DESIGN.md` es la referencia de diseño que siguen los agentes.

```kosmos-question
id: notas
type: text
prompt: ¿Algo que viste en el navegador y que no aparece aquí?
```
