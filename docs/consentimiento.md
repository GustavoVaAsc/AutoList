# Formato de consentimiento para participantes

> **Borrador para revisión del profesor, no usar tal cual.**
> **Estado del documento: BORRADOR.** Este documento es un borrador de trabajo:
> los datos de la institución, los plazos y los datos de contacto aún no están
> confirmados y están marcados con `<!-- TODO: ... -->`. No debe entregarse ni
> firmarse hasta que el profesor lo revise y complete.

## PENDIENTE antes de cualquier demostración con personas reales

Esta lista es la puerta: **mientras un punto de esta lista siga abierto, el
sistema no se demuestra con personas reales**. Son huecos legales y de
seguridad que el equipo no puede cerrar por su cuenta, porque necesitan datos
reales de la institución y una decisión del profesor.

- [ ] **Base legal** del tratamiento de los datos biométricos.
      <!-- TODO: confirmar con el profesor y con la institución la base legal
      exacta (participación voluntaria en una práctica académica, o lo que
      corresponda) y si hace falta una autorización institucional adicional. -->
- [ ] **Plazo de conservación** de los embeddings y de los registros de
      asistencia.
      <!-- TODO: acordar con el profesor el plazo exacto de conservación y
      documentarlo en la sección "Duración de la conservación". -->
- [ ] **Responsable institucional** del tratamiento (además del profesor
      responsable de la práctica).
      <!-- TODO: confirmar quién es el responsable institucional y sus datos de
      contacto. -->
- [ ] **Transferencias** a terceros: si los datos salen del proyecto, a quién y
      con qué base.
      <!-- TODO: confirmar si hay alguna transferencia (nube,eltas, otra
      institución) y dejar constancia de la base de la transferencia. -->
- [ ] **Forma y plazo de ejercicio de derechos** de los participantes
      (acceso, rectificación, supresión y oposición).
      <!-- TODO: documentar el canal concreto, el plazo de respuesta y quién
      lo atiende. -->
- [ ] **Menores de edad**: qué pasa con los participantes que no son mayores de
      edad y qué consentimiento hace falta.
      <!-- TODO: confirmar con el profesor y con la institución el
      procedimiento aplicable a menores de edad. -->
- [ ] **Medidas de seguridad** del despliegue: el broker y la base de datos
      hoy viajan en claro por la red del aula (sección 7 del contrato de
      telemetría).
      <!-- TODO: decidir qué medidas de seguridad se aplican antes de la
      demostración (TLS, acceso restringido a la red, borrado de las
      miniaturas). -->
- [ ] **Historial del repositorio**: `docs/SPEC.md` sigue en el historial de
      git aunque ya no esté en la versión actual, y el repositorio está en
      GitHub.
      <!-- TODO: purgar el documento interno del historial del repositorio
      antes de la demostración. -->

## Datos del proyecto

- **Proyecto:** Pase de lista con reconocimiento facial en Edge AI.
- **Materia:** Fundamentos de Sistemas Embebidos 2027-1.
- **Profesor responsable:** Eduardo Fragoso Navarro.
- **Equipo:** Gustavo, Yordi y Arnau.
- **Institución:** <!-- TODO: nombre de la institución -->
- **Sede / aula donde se realiza:** <!-- TODO: edificio y aula -->

## Qué datos se recogen

De cada participante se guarda un **embedding**: un vector numérico de 512
dimensiones que resume la identidad facial. Ese vector:

- se calcula en la NPU de la Orange Pi a partir de las fotos que se toman
  durante el registro;
- se guarda en la base de datos Postgres de la PC, en la tabla `alumnos`, junto
  al número de cuenta y al grupo.

**El embedding no se sirve al navegador.** El usuario MQTT del frontend no tiene
permiso de lectura sobre `aula/vision/embedding`: solo Node-RED lo lee, porque
es el único que guarda el vector en Postgres. Y el único endpoint que
devuelve embeddings, `GET /api/galeria`, está en un realm de autenticación
propio del servicio de visión, que el navegador no tiene.

**Las fotos de la captura no se guardan**: se descartan en cuanto se calcula el
embedding. Lo que sí circula es la **miniatura en base64** de cada pose durante
el registro: viaja en claro por MQTT en `aula/vision/enrolamiento` y la
reciben el frontend y la pantalla. El sistema no las escribe en disco de forma
deliberada, pero un nodo `debug` de Node-RED sobre ese tópico las guardaría,
así que el despliegue no debe llevar ese nodo.

Además, y solo para el funcionamiento del aula, se registran la fecha y hora de
cada registro de asistencia, el estado (presente, retardo o falta) y la
confianza del reconocimiento.

**La pantalla ILI9341 del aula muestra nombres y estados**: en la vista de
sesión, el nombre y el estado del último registro; en la vista de lista, los
presentes, los retardos y los faltantes frente al resto de la clase. Es una
pantalla visible en el aula, así que quien esté en la sala ve esa información.

## Para qué se usan los datos

- Identificar automáticamente a los participantes durante el pase de lista y
  registrar su asistencia.
- Generar el reporte de asistencia de cada sesión (presentes, retardos y
  faltas).
- Medir el rendimiento del sistema en los experimentos de la materia: latencia,
  potencia, precisión y tasa de falsos positivos.

Los datos **no** se usan con ninguna otra finalidad, no se usan para entrenar
ni para mejorar ningún modelo.

Sobre las transferencias: dentro del funcionamiento del sistema los datos no
salen de los componentes del proyecto, que están todos en la red local del
aula. Pero el repositorio del proyecto está en GitHub y el documento interno
`docs/SPEC.md` sigue en el historial de git, donde se puede leer. Mientras eso
no se purgue, la afirmación "no se comparten con terceros" no es del todo
cierta, y por eso está en la lista de pendientes de arriba.

## Quién puede acceder a los datos

- **El profesor** y el equipo del proyecto, con las credenciales de la base de
  datos de la PC.
- El **servicio de visión** de la Orange Pi, que necesita la galería de
  embeddings para comparar rostro.
- El **frontend web** solo ve el estado de cada alumno (pendiente o registrado)
  y su lista de asistencia: **no** puede leer los embeddings. La restricción se
  aplica con las ACL del broker (el usuario del frontend no tiene permiso de
  lectura sobre `aula/vision/embedding`) y con el realm de la API, donde
  `GET /api/galeria` no está en el realm del navegador.

La credencial del frontend es de solo lectura, pero viaja en el bundle JS que
sirve la página: quien tenga acceso a esa página puede extraerla y con ella ver
los nombres y los estados de la clase.

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
- Un embedding es un dato biométrico: es información sensible que identifica a
  la persona, aunque no se guarden las fotos de la captura.
- Existe riesgo de suplantación con una foto (riesgo documentado en el plan
  del proyecto); la detección de vida por parpadeo queda como trabajo opcional.
- El broker, la base de datos y la API HTTP viajan **en claro** por la red del
  aula: no hay TLS. Queda escrito en la sección 7 del contrato de telemetría.

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
