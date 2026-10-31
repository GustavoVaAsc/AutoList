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

Los errores se escriben por stderr y el proceso sale con código 1 si hay
algún error, 0 si todo está bien y 2 si el contrato o la entrada no se pueden
leer.

### Ejemplos

En [`ejemplos/`](ejemplos/) hay dos ficheros:

- `payloads_validos.json`: un mensaje válido por cada tópico del contrato.
- `payloads_invalidos.json`: mensajes con un tipo incorrecto, un campo
  requerido que falta, un campo desconocido, un tópico inexistente y un
  `payload` que no es un objeto.

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
