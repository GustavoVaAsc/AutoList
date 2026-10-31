# Servicio de visión (NPU)

Aquí va el servicio de visión en C++ nativo que corre en la Orange Pi Zero 3W.

- Detección de rostro (SCRFD o RetinaFace), alineación a 112×112 y embedding
  (MobileFaceNet o ArcFace) ejecutados en la NPU.
- Galería local en SQLite, sincronizada desde Postgres con `GET /api/galeria`.
- Atiende `aula/vision/cmd` (`enrolar`, `cancelar`, `probar`) y publica
  `aula/vision/resultado`, `aula/vision/enrolamiento` y `aula/vision/embedding`.
- Mejora de imagen en CPU (CLAHE y gamma) y, como experimento, Zero-DCE y
  superresolución en la NPU.

Capa: **borde**. Responsable: **Gustavo** (sección 12.2 del plan).

Los modelos `.nb` convertidos y los scripts de conversión quedan fuera de git.
