# Formato de consentimiento para participantes

> **Borrador para revisión del profesor, no usar tal cual.**
> Este documento es un borrador de trabajo: los datos de la institución, los
> plazos y los datos de contacto aún no están confirmados y están marcados con
> `<!-- TODO: ... -->`. No debe entregarse ni firmarse hasta que el profesor lo
> revise y complete.

## Datos del proyecto

- **Proyecto:** Pase de lista con reconocimiento facial en Edge AI.
- **Materia:** Fundamentos de Sistemas Embebidos 2027-1.
- **Profesor responsable:** Eduardo Fragoso Navarro.
- **Equipo:** Gustavo, Yordi y Arnau.
- **Institución:** <!-- TODO: nombre de la institución -->
- **Sede / aula donde se realiza:** <!-- TODO: edificio y aula -->

## Qué datos se recogen

El sistema **no guarda fotos de las personas**. De cada participante se guarda
únicamente un **embedding**: un vector numérico de 512 dimensiones que resume
la identidad facial. Ese vector:

- se calcula en la NPU de la Orange Pi a partir de las fotos que se toman
  durante el registro;
- se guarda en la base de datos Postgres de la PC, en la tabla `alumnos`, junto
  al número de cuenta y al grupo;
- **nunca llega al navegador**: el frontend web no tiene permiso de lectura
  sobre los embeddings.

Las fotos se descartan nada más calcular el embedding; solo se conservan
miniaturas en base64 que circulan por MQTT durante el registro y que no se
guardan.

Además, y solo para el fonctionnement del aula, se registran la fecha y hora de
cada registro de asistencia, el estado (presente o retardo) y la confianza del
reconocimiento.

## Para qué se usan los datos

- Identificar automáticamente a los participantes durante el pase de lista y
  registrar su asistencia.
- Generar el reporte de asistencia de cada sesión (presentes, retardos y
  faltas).
- Medir el rendimiento del sistema en los experimentos de la materia: latencia,
  potencia, precisión y tasa de falsos positivos.

Los datos **no** se usan con ninguna otra finalidad, no se comparten con
terceros y no se usan para entrenar ni para mejorar ningún modelo.

## Quién puede acceder a los datos

- **El profesor** y el equipo del proyecto, con las credenciales de la base de
  datos de la PC.
- El **servicio de visión** de la Orange Pi, que necesita la galería de
  embeddings para comparar rostro.
- El **frontend web** solo ve el estado de cada alumno (pendiente o registrado)
  y su lista de asistencia: **no** puede leer los embeddings. La restricción se
  aplica con `httpNodeAuth` en Node-RED y con las ACL del broker MQTT.

## Base legal y ética

La base de la autorización es la **participación voluntaria** en una práctica
académica de la materia **Fundamentos de Sistemas Embebidos 2027-1**, bajo la
supervisión del profesor **Eduardo Fragoso Navarro**.

<!-- TODO: confirmar con el profesor y con la institución el marco exacto de
protección de datos y si hace falta algún consentimiento institucional
adicional (por ejemplo, de la dirección del centro o del comité de
experimentación). -->

La participación no altera ninguna nota ni la evaluación del aprendizaje: el
sistema solo sustituye al pase de lista manual.

## Duración de la conservación

<!-- TODO: acordar con el profesor el plazo exacto de conservación y
documentarlo aquí. -->

Propuesta provisional: los embeddings y los registros de asistencia se conservan
**hasta el final del curso académico 2027-1** y se eliminan al terminar la
materia. Los archivos de la base de datos de la PC se borran por completo.

## Retirada del consentimiento

La participación es voluntaria en cualquier momento y sin dar explicaciones.

Para retirar el consentimiento basta con **borrar el embedding** del alumno: es
la operación `DELETE /api/alumnos/:cuenta/rostro` del frontend web, que pone el
embedding a `NULL` y marca el consentimiento como retirado. A partir de ese
momento la persona queda registrada como **pendiente de rostro** y deja de
reconocierse.

Si además se quiere borrar el historial de asistencia, hay que hacerlo
directamente en la base de datos de la PC.

## Riesgos y limitaciones

- El reconocimiento tiene tasa de falsos positivos y de falsos negativos: con
  poca luz, con la cabeza girada o con oclusión puede fallar. Por eso el sistema
  avisa al profesor y permite el registro manual.
- Un embedding es un dato biométrico: aunque no se guarden fotos, es información
  sensible que identifica a la persona.
- Existe riesgo de suplantación con una foto (riesgo documentado en el plan
  del proyecto); la detección de vida por parpadeo queda como trabajo opcional.

## Contacto

- **Profesor de la materia:** Eduardo Fragoso Navarro.
  <!-- TODO: correo institucional y horario de atención -->
- **Equipo del proyecto:** Gustavo, Yordi y Arnau.
  <!-- TODO: correo de contacto del equipo -->
- **Institución:** <!-- TODO: datos de contacto de la institución -->

## Firma

- **Nombre y apellidos:** ______________________________
- **Número de cuenta:** ______________________________
- **Grupo:** __________
- **Fecha:** ____ / ____ / ______
- **Firma:** ______________________________

<!-- TODO: añadir el texto de aceptación del consentimiento y la casilla de
"no acepto", para que la retirada sea explícita y quede registrada. -->
