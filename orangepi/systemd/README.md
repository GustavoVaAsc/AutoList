# Unidades systemd

Aquí van las unidades systemd de la Orange Pi Zero 3W.

- `orangepi-pantalla.service`: ejecuta el programa Python de la ILI9341 con el
  usuario del servicio en los grupos `spi`, `gpio` e `i2c`.
- `Restart=always`: el arranque y la supervisión reales los hace systemd, no
  Node-RED. El nodo `exec` de Node-RED solo reinicia la pantalla a mano.
- Aquí van también los overlays de SPI que exponen `/dev/spidev*`, que son
  requisito previo para que la pantalla funcione.

Capa: **borde**. Responsable: **Arnau** (sección 12.2 del plan).
