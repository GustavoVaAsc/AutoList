# Herramientas

Aquí van las herramientas de apoyo del proyecto, todas en Python 3 con la
biblioteca estándar: no hay dependencias externas ni hace falta `pip install`.

## `validar_protocolo.py`

Valida payloads MQTT contra el contrato de telemetría
[`docs/mqtt-topics.json`](../docs/mqtt-topics.json). El contrato es la versión
legible por máquina de la tabla de tópicos de
[`docs/protocolos.md`](../docs/protocolos.md), y el validador es la forma de
comprobar que el firmware, los flujos de Node-RED y el servicio de visión
hablan el mismo idioma antes de conectarlos al broker.

Cada mensaje tiene esta forma:

```json
{
  "topic": "aula/vision/resultado",
  "payload": { "cuenta": "320068234", "conf": 0.78 }
}
```

Un fichero puede contener un mensaje suelto o una lista de mensajes.

### Uso

```bash
# Validar uno o varios ficheros
python3 herramientas/validar_protocolo.py mensajes.json

# Validar desde la entrada estándar
cat mensajes.json | python3 herramientas/validar_protocolo.py

# Listar los tópicos del contrato
python3 herramientas/validar_protocolo.py --lista

# Usar otro contrato
python3 herramientas/validar_protocolo.py --contrato otro_contrato.json mensajes.json
```

### Qué comprueba

- Que el tópico exista en el contrato.
- Que no falte ningún campo marcado como requerido.
- Que los tipos coincidan: un entero vale donde se pide un flotante, pero un
  booleano no vale como entero.
- Que no aparezcan campos que no estén en el contrato.
- Que el valor de los campos con `enum` esté entre los admitidos: la acción de
  `aula/vision/cmd` y de `aula/sesion/cmd`, el LED, el servo, la pose, el
  motivo, el buzzer y la luz.
- Que los campos numéricos estén dentro de los límites que declara el
  contrato: `conf` entre 0 y 1, `paso` entre 1 y 5 y `total` igual a 5.
- Que el vector de `embedding` tenga la longitud que declara el contrato:
  `EMBEDDING_DIM` (512 por defecto), que se lee de la variable de entorno.
- Que se cumplan las reglas declaradas dentro de `items` (`enum`, `min`, `max`
  y `length`): se aplican a cada elemento del array.
- Que `aula/actuador/cmd` lleve al menos un campo.
- Que se cumplan las reglas condicionales: `aula/sesion/cmd` con
  `accion: iniciar` exige `materia`, `grupo` y `tol_min`, y `aula/vision/cmd`
  con `accion: enrolar` o `accion: probar` exige `cuenta`.

El validador **no inventa reglas**: si el contrato no declara un límite, un
`enum` o una condición para un campo, no lo comprueba. Las reglas viven en
[`docs/mqtt-topics.json`](../docs/mqtt-topics.json), con `enum`, `min`, `max`,
`length`, `min_fields` y `required_if`; dentro de `items` valen las mismas
reglas (`enum`, `min`, `max` y `length`), aplicadas a cada elemento del array.

Además, el script es defensivo con la entrada: no lee más de 16 MiB de un
fichero ni de la entrada estándar (si se pasa de ahí, avisa y sale con código
2) y sanea lo que imprime, así que un tópico con secuencias de escape ANSI no
redibuja la terminal y los valores se truncan a 200 caracteres.

### El contrato se valida a sí mismo

Al cargarlo, el script comprueba que el contrato sea coherente. Si el JSON
está mal editado, aborta con **código 2** y un mensaje que dice qué entrada
del JSON es la que está mal, en vez de reventar con una traza:

- cada campo declarado tiene que tener `name` y `type`;
- cada `type` declarado tiene que ser uno de los soportados (`int`, `float`,
  `string`, `bool`, `object` y `array`). Con un `type` desconocido, como
  `"decimal"` por un typo, la comprobación de tipos se desactivaría en
  silencio y dejaría pasar cualquier valor;
- cada tópico tiene que empezar por el prefijo `base_topic` que declara el
  contrato;
- los ejemplos del propio contrato tienen que cumplir las reglas que el
  contrato declara. El ejemplo del `embedding` va abreviado a 4 valores y solo
  se acepta porque el JSON lo marca con `ejemplo_abreviado` en ese campo: un
  ejemplo abreviado solo vale si el propio JSON lo declara así.

Los errores se escriben por stderr y el proceso sale con código 1 si hay
algún error de validación, 0 si todo está bien y 2 si el contrato o la entrada
no se pueden leer o no hay mensajes que validar.

### Ejemplos

En [`ejemplos/`](ejemplos/) hay dos ficheros:

- `payloads_validos.json`: un mensaje válido por cada tópico del contrato,
  más los casos límite de las reglas nuevas (`conf` en 0 y en 1, `paso` en 1 y
  en 5, un `aula/actuador/cmd` con un solo campo, un `aula/sesion/cmd` de
  `terminar` y el vector de `embedding` con los 512 valores).
- `payloads_invalidos.json`: mensajes con un tipo incorrecto, un campo
  requerido que falta, un campo desconocido, un tópico inexistente, un
  `payload` que no es un objeto, un valor fuera de un `enum`, un `conf` o un
  `paso` fuera de rango, un `total` que no es 5, un `embedding` corto, un
  `aula/actuador/cmd` sin campos, una regla condicional que no se cumple y un
  tópico con escapes ANSI.

```bash
python3 herramientas/validar_protocolo.py ejemplos/payloads_validos.json
python3 herramientas/validar_protocolo.py ejemplos/payloads_invalidos.json
```

## Otras herramientas

Pendientes de escribir: enrolamiento por lote para los experimentos,
calibración del umbral con LFW y reporte CSV de asistencia.

Capa: **transversal**: herramientas de experimentos y validación.
Responsables: **Gustavo** (calibración LFW y enrolamiento por lote) y **Yordi**
(reporte CSV), según la sección 12.2 del plan.
