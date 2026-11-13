# Spec: la cámara de la XIAO por ZeroTier, de punta a punta

Estado: **ronda 3, sin preguntas abiertas**, 13 nov. W0 está hecho; W1, W2 y W3 están delegados.

## Cambios en la ronda 2

- **`puente-forma`:** se elige **(A)**, `puente_aula` en C++, en `herramientas/puente_aula/`: relay HTTP de `/capture` más reenvío TCP transparente del MQTT. W3 está delegado.
- **`puente-host`:** esta laptop para las pruebas; un equipo dedicado del aula para el despliegue. Es el mismo binario y la misma unidad de systemd de usuario.
- **Comentario general:** el plan original **no se borra**. La pasada de documentación (W6) agrega a `HIGH-LEVEL-PLAN.md` la arquitectura actual con el puente como **desviación** del plan original, que sigue escrito. Además deja como **pendiente** la comunicación directa XIAO ↔ MQTT, por ejemplo con un router con ZeroTier en el aula, que es la opción (C).
- **E3 corregida:** un `res=` desconocido (por ejemplo `QXGA`) sale en **SVGA**, el valor por omisión, como dice S3. Antes decía "como mucho XGA".

## Cambios en la ronda 1

- **`topologia-final`:** la Orange Pi sigue remota, así que el puente es **parte permanente del sistema**. Se rediseña abajo como componente de producción: servicio de systemd, reconexión y sin escribir en disco.
- **`mqtt-v1` y el comentario general:** no hay recorte temporal. La XIAO corre el **firmware completo con MQTT** desde ya y el MQTT también pasa por el puente. `SOLO_CAMARA` se elimina. En esta prueba solo se conecta físicamente la cámara.
- **Consecuencia nueva:** con el PIR desconectado, `D0` quedaba flotando (`pinMode(PIN_PIR, INPUT)`, `xiao.ino:343`). Eso podía publicar `{"mov":1}` falsos y disparar ráfagas de reconocimiento, que son justo el trabajo de cámara que queremos evitar. W1 lo cambia a `INPUT_PULLDOWN`. Hay que verificar en la placa que, con el PIR conectado, el divisor 10k/20k junto con el pull-down interno sigue dando un nivel alto válido.
- **`umbral-termico`:** aviso a 65 °C, corte a 75 °C y reanudación por debajo de 65 °C. Además, W1 publica `temp` en el latido de 30 s de `aula/camara/estado`, que ya define el contrato. Así se vigila la placa desde la Orange Pi.
- **`puente-ubicacion`:** proyecto C++ aparte en `herramientas/`. Como ya no es solo de la cámara, el nombre provisional pasa a `puente_aula`.
- **`aprobar`:** se delegaron W0 (documentación de la ESP32), W1 (firmware) y W2 (firma en la visión). W3 queda en espera porque el puente cambió de alcance después de la aprobación.
- **Aparte:** `docs/specs/camara-xiao-zerotier.md` (tu commit `eac75f1`) sigue en el repo. Este documento es la versión viva; al final se sincroniza con ese archivo.

## La arquitectura final de red

La XIAO no puede unirse a ZeroTier, y la Orange Pi no está en su LAN. Dos tráficos tienen que cruzar:

| Tráfico | Quién inicia | Desde | Hacia |
|---|---|---|---|
| Fotos (`GET /capture`, firmado) | Orange Pi | ZeroTier | XIAO, en la LAN |
| MQTT (PIR, estado, energía, actuadores) | XIAO | LAN | Mosquitto de la Orange Pi, por ZeroTier |

Hay tres formas de cruzar. Las tres sirven igual para MQTT y para las fotos.

### (A) Puente de aplicación en C++ en un equipo Linux de la LAN

```
Orange Pi ──ZeroTier──► puente_aula :<puerto-http> ──LAN──► XIAO :80 /capture
XIAO ──LAN──► puente_aula :1883 ──ZeroTier──► Mosquitto de la Orange Pi :1883
```

- **HTTP:** escucha solo en la IP de ZeroTier del equipo, acepta solo a la Orange Pi, reenvía solo `GET /capture` con la query y las cabeceras `X-Captura-*` intactas, tiene tope de 2 MB y plazos, y deja una sola petición en vuelo.
- **MQTT:** reenvío **TCP transparente**. El puente no interpreta MQTT: usuario, contraseña, ACL y LWT siguen siendo de Mosquitto. Escucha solo en la IP de LAN, acepta solo la IP de la XIAO y reenvía a la IP de ZeroTier de la Orange Pi. Si se cae cualquiera de los dos lados, cierra el otro: la XIAO reconecta y Mosquitto dispara el LWT.
- **Servicio de systemd** con reinicio automático. Configuración en un archivo; nada se escribe en disco; los logs no llevan cuerpos, firmas ni payloads.
- **Exposición:** la misma del diseño original. MQTT en claro solo dentro de la LAN, como antes. Entre el equipo y la Orange Pi, ZeroTier ya cifra.
- **Costo:** código propio que mantener y probar. **No necesita root.**

### (B) Ruteo de red en el mismo equipo, sin código

Se activa el reenvío de IP del equipo, se crea una ruta administrada de ZeroTier hacia la subred de la LAN, se escriben reglas de nftables que solo dejan pasar `XIAO:80` y `OrangePi:1883`, y el gateway de la XIAO pasa a ser ese equipo (con IP estática, que W1 ya soporta).

- **Ventajas:** no hay código; la XIAO habla MQTT directamente con la IP de ZeroTier de la Orange Pi.
- **Costos:** configuración con root en ese equipo. Si esa configuración se equivoca, expone toda la LAN a la red ZeroTier. Además depende de que el equipo esté encendido, igual que (A).

### (C) Router con ZeroTier en el aula

Un router con cliente de ZeroTier (OpenWrt, o un GL.iNet que lo trae de fábrica) da el Wi-Fi a la XIAO y está en la red ZeroTier.

- **Ventajas:** la XIAO habla MQTT y sirve fotos **directamente**, como en el diseño original con la Orange Pi en la LAN. No hay puente que programar ni computadora que mantener encendida.
- **Costos:** comprar y configurar el router.
- **Si se elige (C), W3 desaparece.**

### Dónde corre (A) o (B)

`docs/HIGH-LEVEL-PLAN.md:602` dice que **"la laptop dejó de ser una dependencia del sistema"**. Un puente permanente en esta laptop vuelve a poner esa dependencia: si la laptop se suspende, se apaga o sale del aula, la XIAO pierde el MQTT y la Orange Pi pierde las fotos. Para desarrollar está bien. En el aula hace falta un equipo que esté siempre encendido.

**Mi recomendación:**

- **Para no frenarnos:** (A) en esta laptop ahora, para las pruebas.
- **Para el despliegue:** (A) en un equipo dedicado del aula, una Raspberry Pi o similar con ZeroTier. Es el mismo binario y la misma unidad de systemd.
- **Si se puede comprar hardware:** (C) es la opción más limpia y deja (A) como plan B.

**Decidido (ronda 2):** (A), `puente_aula` en C++.

**Decidido (ronda 2):** esta laptop para las pruebas; un equipo dedicado en el aula para el despliegue.

### Riesgo de MQTT a distancia

El firmware descarta los retenidos que llegan en los 1500 ms siguientes a suscribirse, porque PubSubClient no expone la bandera retain. Por internet y ZeroTier, un retenido podría tardar más que esa ventana y aplicarse como orden en vivo. Por ejemplo, un `luz:1` retenido encendería la tira.

Se mide en las pruebas (E7). Si la ventana no alcanza, el remedio previsto ya está documentado: migrar a esp-mqtt. No se cambia ahora.

## Objetivo y alcance

**Objetivo.** El servicio de visión de la Orange Pi saca fotos reales de la XIAO igual que hoy las saca de `camara_falsa`, y la XIAO entra al MQTT de la Orange Pi. El registro y el pase de lista pasan de punta a punta, sin volver a poner la placa en el régimen que quemó la anterior.

**Fuera de alcance:** la vista previa del profesor, el INA219, esp-mqtt y el cifrado de las fotos. Periféricos físicos en esta prueba: solo la cámara.

**Cómo se hace:** rama `feat/camara-xiao-zerotier` desde `main`. Las ramas viejas de la firma y de la webcam solo sirven de referencia.

## El modo seguro de la cámara (W1)

| Id | Requisito |
|---|---|
| S1 | Nada de captura continua: `fb_count = 1` y `CAMERA_GRAB_WHEN_EMPTY`. |
| S2 | Cámara apagada en reposo: arranca con la primera petición firmada y válida, y se apaga tras 30 s sin capturas. Se mide la latencia de la primera foto. |
| S3 | `res=` con lista blanca (`HVGA`, `VGA`, `SVGA`, `XGA`): SVGA por omisión, nunca más que XGA. |
| S4 | Como mucho 4 capturas en 2 s, con un mínimo entre ráfagas; arriba de eso, 429 sin tocar la cámara. |
| S5 | Corte térmico: aviso a 65 °C, corte a 75 °C (503 `temperatura`, cámara apagada), reanudación por debajo de 65 °C. `temp` va en el latido de 30 s. |
| S6 | Sin `/stream` y una captura a la vez (503 `ocupada`). |
| S7 | El PIR con `INPUT_PULLDOWN`, para que no dispare con nada conectado. IP estática opcional en `secrets.h`. |
| S8 | Hardware de la prueba: solo el USB-C de la PC, solo la cámara, antena externa y disipador si se tiene. |

## Workstreams

| # | Workstream | Estado | Revisión |
|---|---|---|---|
| W0 | La parte de la ESP32 de `docs/vista-previa` en `HIGH-LEVEL-PLAN.md` | **hecho** | solo docs |
| W1 | Firmware en modo seguro, con MQTT completo | **delegado** | reviewer + auditor |
| W2 | Firma HMAC en la visión | **delegado** | reviewer + auditor |
| W3 | `puente_aula` (HTTP + TCP de MQTT, systemd) | **delegado** | reviewer + auditor |
| W4 | Configuración: secreto, IPs y `MQTT_HOST` hacia el puente | Gustavo, a mano | — |
| W5 | Pruebas de punta a punta | Gustavo, con la placa | — |
| W6 | READMEs, `RESULTADOS-PRUEBAS.md`, la arquitectura del puente en el plan y la sincronización con `docs/specs/` | con aprobación previa | — |

## Plan de pruebas de punta a punta

Ante una temperatura de corte, olor o calor al tacto: **desconectar el USB y parar.**

| Etapa | Qué | Criterio |
|---|---|---|
| E0 | Pruebas de host, `ctest` en la Orange Pi y pruebas del puente | Todas verdes |
| E1 | Flashear y dejarla 10 min en reposo, con el monitor serie abierto | La cámara **no** se inicializa; el PIR no dispara; se anota la temperatura interna y la externa |
| E2 | Fotos por la LAN: `curl` sin firma y `probar_captura` firmado | 401 sin firma; 200 en SVGA; 403 con un nonce repetido; se anota la latencia de la primera foto |
| E3 | Ráfagas por encima del límite y `res=QXGA` | 429; `QXGA` sale en SVGA (el valor por omisión) y nada pasa de XGA |
| E4 | Fotos desde la Orange Pi a través del puente | 401 sin firma; 200 con el servicio; un origen fuera de la lista se rechaza |
| E5 | Una ráfaga de 4 cada 5 s durante 15 min | No llega al corte, o el corte actúa y la placa se recupera |
| E6 | Registro de 5 poses y pase de lista (PIR publicado a mano con otro usuario, **nunca** con `xiao`) | Embedding guardado, `presente`, latencias anotadas |
| E7 | MQTT a distancia: latido, LWT al quitar la alimentación, retenido de `luz:1` y reinicio | Llegan el latido y el LWT; el retenido **no** se aplica al reconectar |

## Aprobación

W3 aprobado y delegado (ronda 2).
