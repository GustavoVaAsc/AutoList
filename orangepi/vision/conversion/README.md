# Conversión de los modelos de la NPU a `.nb`

Esta guía explica cómo convertir los dos modelos del servicio de visión al formato `.nb` que ejecuta la NPU de la Orange Pi (Allwinner A733). Los modelos son el detector RetinaFace y el embedder `w600k_mbf`. La conversión se hace en una laptop **Windows x86_64 con Docker Desktop (backend WSL2)**, en dos partes:

1. **Yordi** prepara la laptop y hace a mano lo que necesita navegador o `sudo` ([Lo que hace Yordi a mano](#lo-que-hace-yordi-a-mano-antes-de-darle-la-guía-al-agente)).
2. **Un agente de código** sigue el resto de la guía sin pasos interactivos. Termina en `~/npu/entrega/`, con los `.nb`, `SHA256SUMS` y el [reporte](#plantilla-de-reporte). No toca la placa.

Después, Yordi copia los archivos a la placa, y Yordi o Gus verifican allí los tensores ([Verificación en la placa](#verificación-en-la-placa)).

`orangepi/vision/` es el carril de Gustavo (Gus). La conversión la hace Yordi, con el acuerdo de Gus. Los ajustes del código del detector los hace Gus ([Lo que hace Gus después](#lo-que-hace-gus-después)).

## Contenido

- [Cómo leer esta guía](#cómo-leer-esta-guía)
- [Qué se produce](#qué-se-produce)
- [Reglas que no se rompen](#reglas-que-no-se-rompen)
- [Lo que hace Yordi a mano, antes de darle la guía al agente](#lo-que-hace-yordi-a-mano-antes-de-darle-la-guía-al-agente)
- [Comprobaciones de arranque del agente](#comprobaciones-de-arranque-del-agente)
- [Descargas del agente](#descargas-del-agente)
- [Cargar la imagen de Acuity y crear el contenedor](#cargar-la-imagen-de-acuity-y-crear-el-contenedor)
- [Matriz de variantes](#matriz-de-variantes)
- [Cómo funcionan los scripts del zoo](#cómo-funcionan-los-scripts-del-zoo)
- [Detector RetinaFace](#detector-retinaface)
- [Imágenes de calibración](#imágenes-de-calibración)
- [Embedder `w600k_mbf`](#embedder-w600k_mbf)
- [Entrega en la laptop](#entrega-en-la-laptop)
- [Verificación en la placa](#verificación-en-la-placa)
- [Lo que hace Gus después](#lo-que-hace-gus-después)
- [Limpieza](#limpieza)
- [Problemas conocidos](#problemas-conocidos)
- [Plantilla de reporte](#plantilla-de-reporte)
- [Fuentes](#fuentes)
- [Ver también](#ver-también)

## Cómo leer esta guía

Cada valor técnico lleva su fuente y una de estas marcas:

| Marca | Significa |
|---|---|
| **Verificado** | Sale tal cual de una fuente citada: el model zoo de Allwinner, la guía de Radxa, el código de InsightFace o de biubug6, o el código del repo. |
| **Deducido** | Se calcula a partir de una fuente verificada, pero nadie lo ha observado. |
| **Sin probar** | Es una sugerencia. Nadie lo ha ejecutado con esta combinación. Si falla, se anota en el reporte y se sigue con la siguiente variante. |

Abreviaturas de las rutas:

| Abreviatura | Ruta |
|---|---|
| `zoo:` | Raíz del model zoo extraído, `awnpu_model_zoo-v0.9.0-20260116-83a67d4b/` |
| `repo:` | Raíz de este repositorio (AutoList) |
| `radxa:rf` | Guía de RetinaFace de Radxa para la A7A (Allwinner A733): https://docs.radxa.com/cubie/a7a/app-dev/npu-dev/model-zoo/retinaface |
| `radxa:env` | Instalación de ACUITY de Radxa: https://docs.radxa.com/cubie/a7a/app-dev/npu-dev/cubie-acuity-env |
| `radxa:quant` | Precisión de la cuantización: https://docs.radxa.com/cubie/a7a/app-dev/npu-dev/cubie-quant-acc-improve |
| `radxa:nbinfo` | Herramienta `nbinfo`: https://docs.radxa.com/cubie/a7a/app-dev/npu-dev/cubie-nbinfo |
| `radxa:zoo` | Descarga del model zoo: https://docs.radxa.com/cubie/a7a/app-dev/npu-dev/model-zoo/model-zoo-download |

La versión china de las páginas de Radxa es la completa: es la que trae el log de la A733 con los tensores.

## Qué se produce

Se producen dos modelos. La placa solo ejecuta los `.nb`, y la conversión necesita una máquina x86: la imagen Docker de Acuity v2.0.10.2 (`ubuntu-npu:v2.0.10.2`) es para x86 (`radxa:env`; plan, sección 6.2).

| Modelo | Para qué | Nombre final en la placa | Fuente del nombre |
|---|---|---|---|
| Detector RetinaFace | Encuentra la cara y sus 5 puntos | `/opt/autolist/models/detector.nb` | **Verificado:** `repo:orangepi/vision/src/main.cpp` (`kDirectorioModelos` y `kNombreDetector`) |
| Embedder `w600k_mbf` (MobileFaceNet de InsightFace, 512 dimensiones) | Convierte el recorte alineado de 112×112 en un vector | `/opt/autolist/models/w600k_mbf-uint8-v1.nb` | **Verificado:** el servicio busca `/opt/autolist/models/<servicio.modelo>.nb`, y `repo:orangepi/vision/vision.conf.example` pone `servicio.modelo = "w600k_mbf-uint8-v1"` |

El servicio busca los dos archivos al arrancar. Si falta alguno, o si un `.nb` no encaja con el código, sale con el código **78** y systemd no lo reinicia. Con `--dry-run` arranca sin modelos (`repo:orangepi/vision/README.md`, sección "El proceso de producción").

**El agente no renombra nada.** Entrega cada variante con su nombre de variante ([Matriz de variantes](#matriz-de-variantes)). El renombrado al nombre final lo hace una persona al instalar en la placa.

## Reglas que no se rompen

- **Nada de esto va a git.** Ni los `.nb`, ni los `.onnx`, ni los `.json`, `.data`, `.quantize` o `.yml` que genera Acuity, ni los pesos, ni las imágenes de calibración, ni los manifiestos. El `.gitignore` solo cubre `*.nb`, `*.onnx` y `*.tflite`, y algunos directorios de dataset. Por eso el área de trabajo va **fuera del clon del repo**, en `~/npu/`. El agente no hace commit ni push. Si encuentra un error en esta guía, lo anota en el reporte.
- **Calibración solo con datos públicos.** El detector usa las imágenes de WIDER FACE que trae el zoo (`zoo:examples/dataset/widerface_10/`), y el embedder, recortes de LFW. **Nunca fotos del equipo ni de alumnos.** Los recortes de rostros son datos biométricos. No salen de `~/npu/` (`repo:orangepi/vision/README.md`, sección `herramientas/alinear_lfw.sh`).
- **El agente no toca la placa.** No hace `ssh` ni `scp` y no necesita su contraseña. La copia y la instalación las hace Yordi.
- **`sudo` y borrar contenedores.** En la laptop, el agente no usa `sudo`: lo que lo necesita (`apt`) lo hace Yordi antes. El agente sí puede detener y borrar **su propio** contenedor `model-zoo` en la laptop. La prohibición de `docker rm`, `docker stop` y `sudo` de `repo:orangepi/AGENTS.md` es para la placa.
- **No se piden credenciales.** El agente nunca pide un token, una contraseña ni una clave en el chat. Esta tarea no necesita `orangepi/.env` ni `firmware/xiao/secrets.h`; no los abras.
- **Sin pasos interactivos.** Todos los comandos de la guía se ejecutan sin terminal: `docker exec` sin `-it` y nada de paginadores ni editores.
- **Si un paso falla dos veces, para.** Anota el comando y el error exacto en el reporte y sigue con la siguiente variante. Si el que falló es un paso común (la imagen o el contenedor), detente.

## Lo que hace Yordi a mano, antes de darle la guía al agente

Esto necesita navegador, `sudo` o las credenciales de Yordi. El agente arranca solo cuando todo existe ([Comprobaciones de arranque del agente](#comprobaciones-de-arranque-del-agente)).

### 1. WSL, Docker Desktop y memoria

1. Instalar Ubuntu en WSL. En PowerShell como administrador (comando estándar de Microsoft, **sin probar** en esta laptop): `wsl --install -d Ubuntu-22.04`.
2. En Docker Desktop: en Settings > General, "Use the WSL 2 based engine"; en Settings > Resources > WSL integration, activar la distro Ubuntu. Referencia: https://docs.docker.com/desktop/features/wsl/
3. Memoria de WSL. Docker Desktop con WSL2 corre los contenedores dentro de la máquina virtual de WSL, y su memoria la fija `%UserProfile%\.wslconfig`. La cuantización corre en CPU (`--device CPU`, `zoo:scripts_model_convert/pegasus_quantize.sh:57`) con la red completa en memoria. Ninguna fuente dice cuánta memoria necesita Acuity. **Sugerencia sin probar**, para una laptop de 16 GB:

   ```ini
   [wsl2]
   memory=12GB
   swap=16GB
   ```

   Se aplica con `wsl --shutdown` en PowerShell y volviendo a abrir Ubuntu y Docker Desktop. Con 8 GB de RAM, deja `memory` en unos 6 GB y sube `swap`. Referencia: https://learn.microsoft.com/windows/wsl/wsl-config
4. Disco: **sugerencia sin probar**, al menos 40 GB libres en el disco de WSL. El zoo comprimido mide 209 MB (**verificado**). El tamaño de la imagen de Acuity no está documentado: el zip, el tar y la imagen cargada coexisten un momento.

### 2. Paquetes de Ubuntu

En la terminal de Ubuntu:

```bash
sudo apt-get update
sudo apt-get install -y git wget unzip dos2unix python3 python3-venv python3-pip g++ pkg-config libopencv-dev
git config --global core.autocrlf input
```

`g++`, `pkg-config` y `libopencv-dev` los pide `repo:herramientas/alinear_lfw.sh`: C++20 y `pkg-config opencv4`. `python3-venv` es para los entornos de `onnxsim` y de `landmarks_retinaface.py`. `core.autocrlf input` evita que git convierta los scripts a CRLF: el repo no tiene `.gitattributes`.

### 3. El clon del repo

El repo se clona en `~/npu/AutoList`, en la rama `edge/sistema`, que es la que lleva esta guía y `landmarks_retinaface.py` hasta que se fusione a `main`. Lo hace Yordi con sus credenciales, o el agente con las credenciales que Yordi ya tiene configuradas en WSL. El agente nunca pide un token: si `git` le pide usuario o contraseña, se detiene y lo reporta.

```bash
mkdir -p ~/npu && cd ~/npu
git clone https://github.com/GustavoVaAsc/AutoList.git
git -C ~/npu/AutoList checkout edge/sistema
```

**Antes de que el agente clone**, Yordi commitea y empuja a `edge/sistema` la carpeta `orangepi/vision/conversion/` completa: esta guía, `landmarks_retinaface.py` y sus pruebas. Si el agente clona antes, le faltan archivos. La comprobación de arranque lo detecta: `ls ~/npu/AutoList/orangepi/vision/conversion/` tiene que listar los tres.

### 4. Descargas con navegador

Todas van a `~/npu/descargas/`. Desde el navegador de Windows quedan en `C:\Users\<usuario>\Downloads`. Se copian con `cp /mnt/c/Users/<usuario>/Downloads/<archivo> ~/npu/descargas/`.

| Archivo | De dónde | Nota |
|---|---|---|
| `docker_images_v2.0.x.zip` (imagen de Acuity para la A733) | https://netstorage.allwinnertech.com:5001/sharing/Mh23BhPHq (`radxa:env`, pestaña A733) | Página de Synology. **No** uses el enlace de la T527 (`docker_images_v1.8.x.zip`, `ubuntu-npu:v1.8.13`). Si el archivo se llama distinto, déjalo con su nombre: el agente lo busca con `find`. |
| `Retinaface_resnet50_320.onnx` | Prueba primero `https://netstorage.allwinnertech.com:5001/sharing/YObunQV8S` (**sin verificar**: es el mismo identificador con el puerto HTTPS de la imagen). Si no abre, usa `http://netstorage.allwinnertech.com:5000/sharing/YObunQV8S` (`zoo:examples/retinaface/README.md:38`). | Obligatorio: lo usan D1 y los landmarks de calibración. |
| `Retinaface_mobilenet0.25_320.onnx` | Igual, con el identificador `xt9rDVXzI` (`zoo:examples/retinaface/README.md:41`) | Opcional (variante D3). |
| `lfw.tgz` | Fuente oficial de la Universidad de Massachusetts: http://vis-www.cs.umass.edu/lfw/ | **No verificada:** nadie del equipo la ha bajado. Va por HTTP, sin cifrar, y LFW es un dataset de uso de investigación. Anota su sha256. |
| `nbinfo` (opcional) | `https://github.com/ZIFENG278/ai-sdk/raw/refs/heads/main/tools/nbinfo` (`radxa:nbinfo`) | Binario de un fork personal, sin commit fijado. Solo si Yordi decide confiar en él; si no, no se baja y el agente no lo usa. |
| `Resnet50_Final.pth` (opcional, solo D2) | https://drive.google.com/drive/folders/1oZRSG0ZegbVkVwUd8wUIQx8W7yfZ_ki1 (`zoo:examples/retinaface/README.md:9`) | Solo si se quiere la variante D2 a 640. |

Al terminar, Yordi registra los hashes de lo que bajó a mano, con solo los archivos que existan:

```bash
cd ~/npu/descargas
sha256sum docker_images_v2.0.x.zip Retinaface_resnet50_320.onnx lfw.tgz > SHA256SUMS.descargas
# Si también bajó estos:
# sha256sum Retinaface_mobilenet0.25_320.onnx nbinfo Resnet50_Final.pth >> SHA256SUMS.descargas
cat SHA256SUMS.descargas
```

## Comprobaciones de arranque del agente

El agente empieza aquí. Si alguna comprobación falla, se detiene y lo reporta. La de los hashes va sola y encadenada, para que un `FAILED` corte el paso:

```bash
cd ~/npu/descargas && ls -l && sha256sum -c SHA256SUMS.descargas && echo "HASHES OK"
```

Si no imprime `HASHES OK`, el agente para. Después:

```bash
git -C ~/npu/AutoList rev-parse --abbrev-ref HEAD
ls ~/npu/AutoList/orangepi/vision/conversion/
docker version
docker run --rm hello-world
free -g
df -h ~
```

Resultado esperado: la rama es `edge/sistema`; en `conversion/` están este `README.md`, `landmarks_retinaface.py` y `test_landmarks_retinaface.py`; `docker` responde sin `sudo` y `hello-world` termina bien. Anota `free -g` y `df -h` en el reporte.

Trabaja siempre en `~/npu/`, en el sistema de archivos de WSL, no en `/mnt/c`. En `/mnt/c` la E/S es mucho más lenta, se pierden los bits de ejecución y los enlaces simbólicos de `convert_model_env.sh` pueden no funcionar.

Estructura del área de trabajo:

```
~/npu/
  descargas/                                  zips, tar, ONNX y lfw.tgz tal como se bajan
  AutoList/                                   clon del repo (esta guía, landmarks_retinaface.py, herramientas/)
  awnpu_model_zoo-v0.9.0-20260116-83a67d4b/   model zoo; se monta en el contenedor como /workspace
  lfw/                                        LFW extraído (fuera del repo)
  calib/                                      lista de imágenes, manifiesto y recortes de 112x112 (fuera del repo)
  venv/                                       Python para landmarks_retinaface.py
  venv-onnx/                                  Python para onnxsim
  entrega/                                    los .nb, SHA256SUMS, resultados-conversion.txt y reporte.md
```

## Descargas del agente

Estas descargas sí las hace el agente. Cada una se comprueba con su sha256 (**verificado** el 5 nov). Cada bloque corre en `bash -c 'set -euo pipefail; …'`: si un `sha256sum -c` falla, el bloque se corta ahí y no extrae ni copia el archivo sin verificar. Si un bloque sale con un código distinto de 0, el agente se detiene y lo reporta.

```bash
bash -c 'set -euo pipefail
cd ~/npu/descargas
wget -q https://dl.radxa.com/cubie/allwinner-model-zoo.tar.gz
echo "f209bbcbc1222c52035ff0ea9796b9de6cdb96598a1c5cf7aaec2a20ab2673a5  allwinner-model-zoo.tar.gz" | sha256sum -c -
tar -xzf allwinner-model-zoo.tar.gz -C ~/npu/
ls ~/npu/awnpu_model_zoo-v0.9.0-20260116-83a67d4b'
echo "salida: $?"

bash -c 'set -euo pipefail
cd ~/npu/descargas
wget -q https://github.com/deepinsight/insightface/releases/download/v0.7/buffalo_sc.zip
echo "57d31b56b6ffa911c8a73cfc1707c73cab76efe7f13b675a05223bf42de47c72  buffalo_sc.zip" | sha256sum -c -
mkdir -p buffalo_sc && unzip -o -q buffalo_sc.zip -d buffalo_sc
W=$(find ~/npu/descargas/buffalo_sc -name w600k_mbf.onnx | head -n 1)
test -n "$W"
echo "9cc6e4a75f0e2bf0b1aed94578f144d15175f357bdc05e815e5c4a02b319eb4f  $W" | sha256sum -c -
cp "$W" ~/npu/descargas/w600k_mbf.onnx'
echo "salida: $?"
```

El zoo de `dl.radxa.com` es la v0.9.0 (`awnpu_model_zoo-v0.9.0-20260116-83a67d4b`). `radxa:zoo` menciona una v1.0.0 en el portal de Allwinner, que pide registro: esta guía usa la v0.9.0.

Extraer LFW:

```bash
mkdir -p ~/npu/lfw && tar -xzf ~/npu/descargas/lfw.tgz -C ~/npu/lfw
find ~/npu/lfw -name '*.jpg' | wc -l
```

La estructura habitual de `lfw.tgz` es `lfw/<Persona>/<Persona>_0001.jpg` (**sin verificar**). El `find` funciona con cualquier anidamiento.

## Cargar la imagen de Acuity y crear el contenedor

Cargar la imagen (`radxa:env`, pestaña A733). Los nombres de los zips cambian según la versión, así que el tar se busca con `find`:

```bash
cd ~/npu/descargas
unzip -o -q docker_images_v2.0.x.zip
find ~/npu/descargas -name 'ubuntu-npu_v2.0.10.2.tar*'
```

Si `find` devuelve el `.tar.zip`, descomprímelo en su directorio y carga el `.tar`:

```bash
Z=$(find ~/npu/descargas -name 'ubuntu-npu_v2.0.10.2.tar.zip' | head -n 1)
( cd "$(dirname "$Z")" && unzip -o -q "$(basename "$Z")" )
T=$(find ~/npu/descargas -name 'ubuntu-npu_v2.0.10.2.tar' | head -n 1)
docker load -i "$T"
docker images | grep ubuntu-npu
```

Resultado esperado: una línea con `ubuntu-npu` y la etiqueta `v2.0.10.2`. Si la etiqueta es `v1.8.13`, es la imagen de la T527 y no sirve ([Problemas conocidos](#problemas-conocidos)). Escribe el nombre de la imagen como en los bloques de código: en una frase de `radxa:env` lleva un guion no ASCII (`ubuntu‑npu`).

Crear el contenedor con la raíz del zoo montada en `/workspace`, como en `radxa:zoo`. Si ya existe un contenedor `model-zoo` de un intento anterior, se arranca en lugar de crearlo:

```bash
cd ~/npu/awnpu_model_zoo-v0.9.0-20260116-83a67d4b
if docker container inspect model-zoo >/dev/null 2>&1; then
  docker start model-zoo
else
  docker run --ipc=host -d -v "$PWD":/workspace --name model-zoo ubuntu-npu:v2.0.10.2 tail -f /dev/null
fi
docker ps --filter name=model-zoo
```

Si el contenedor existente monta otra carpeta o usa otra imagen, el agente puede borrarlo (`docker rm -f model-zoo`) y crearlo otra vez: es suyo y está en la laptop.

**Comprobar el entorno antes de nada.** Los scripts salen con `Need to set environment variable ACUITY_PATH` si falta esa variable (`zoo:scripts_model_convert/pegasus_import.sh:4-7`), y el export usa además `${VIV_SDK}` (`zoo:scripts_model_convert/pegasus_export_ovx_nbg.sh:68,88`):

```bash
docker exec model-zoo bash -lc 'echo "ACUITY_PATH=$ACUITY_PATH VIV_SDK=$VIV_SDK"; ls /workspace'
```

Resultado esperado: las dos variables con una ruta, y en `/workspace`, los directorios del zoo (`examples`, `scripts_model_convert`, ...). La guía de Radxa ejecuta los scripts nada más entrar al contenedor, así que la imagen debería traer las variables puestas (**sin probar**). **Si alguna sale vacía, el agente se detiene y lo reporta.** No inventa rutas. Para el reporte puede añadir la salida de `docker exec model-zoo bash -lc 'env | grep -iE "acuity|viv"'`: es el entorno de la imagen de Acuity en la laptop, no de un contenedor del proyecto. El plan (sección 6.2) menciona un "`env.sh v3`" sin más detalle.

Todos los comandos de Acuity de esta guía siguen la misma forma, sin `-it`:

```bash
docker exec model-zoo bash -lc 'cd /workspace/examples/<modelo>/convert_model && <comando>'
```

Los archivos que crea el contenedor quedan con dueño `root` en `~/npu/`. Antes de copiarlos o borrarlos desde Ubuntu:

```bash
docker exec model-zoo chown -R "$(id -u):$(id -g)" /workspace/examples
```

`$(id -u)` y `$(id -g)` se evalúan en Ubuntu, antes de llegar al contenedor: son los del usuario de WSL.

## Matriz de variantes

El código de la visión todavía no está ajustado al modelo de referencia ([Lo que hace Gus después](#lo-que-hace-gus-después)). Por eso se producen varias variantes, para que Gus elija sin volver a convertir.

El nombre de cada archivo lo pone el propio script de export: `<NAME>_<cuantización>_<plataforma>.nb` (`zoo:scripts_model_convert/pegasus_export_ovx_nbg.sh:107`), copiado a `../model/` (`:111`).

| ID | `NAME` (directorio de trabajo) | Archivo que produce | Prioridad | Nombre al instalar en la placa |
|---|---|---|---|---|
| D1 | `Retinaface_resnet50_320` (`zoo:examples/retinaface/convert_model/`) | `Retinaface_resnet50_320_uint8_a733.nb` | **Obligatoria y recomendada.** Es la referencia de Radxa. | `detector.nb` |
| D2 | `Retinaface_resnet50_640` (mismo directorio) | `Retinaface_resnet50_640_uint8_a733.nb` | Opcional: hay que exportar el ONNX desde PyTorch | `detector.nb`, si Gus la elige en lugar de D1 |
| D3 | `Retinaface_mobilenet0.25_320` (mismo directorio) | `Retinaface_mobilenet0.25_320_uint8_a733.nb` | Opcional: el ONNX está publicado y la conversión es barata | `detector.nb`, si Gus la elige |
| EA | `w600k_mbf_a` (`zoo:examples/w600k_mbf_a/convert_model/`, nuevo) | `w600k_mbf_a_uint8_a733.nb` | **Obligatoria.** Con nodo de preproceso. | `w600k_mbf-uint8-v1.nb`, si Gus elige A |
| EB | `w600k_mbf_b` (`zoo:examples/w600k_mbf_b/convert_model/`, nuevo) | `w600k_mbf_b_uint8_a733.nb` | **Obligatoria.** Sin nodo de preproceso. | `w600k_mbf-uint8-v1.nb`. Es la candidata por omisión, porque encaja con el embedder del código de hoy. |
| EA-pcq | `w600k_mbf_a` | `w600k_mbf_a_pcq_a733.nb` | Opcional | Con otro nombre de modelo, por ejemplo `w600k_mbf-pcq-v1.nb` y `servicio.modelo = "w600k_mbf-pcq-v1"` |
| EB-pcq | `w600k_mbf_b` | `w600k_mbf_b_pcq_a733.nb` | Opcional | Igual que EA-pcq |
| EA-int16 | `w600k_mbf_a` | `w600k_mbf_a_int16_a733.nb` | Opcional | Por ejemplo, `w600k_mbf-int16-v1.nb` |
| EB-int16 | `w600k_mbf_b` | `w600k_mbf_b_int16_a733.nb` | Opcional | Igual que EA-int16 |

Orden de trabajo: D1, la calibración del embedder, EB y EA. Si queda tiempo: EA-pcq, EB-pcq, los int16, D3 y D2.

El nombre del modelo del servicio solo admite letras, dígitos, `.`, `_` y `-`, hasta 64 caracteres (`repo:orangepi/vision/src/process_config.cpp`, `nombre_modelo_valido`). Cambiar de embedder invalida los embeddings registrados (plan, sección 6.2).

## Cómo funcionan los scripts del zoo

Cada modelo pasa por tres pasos: importar, cuantizar y exportar. Se ejecutan dentro del contenedor, desde el directorio `convert_model/` del modelo, con los enlaces que crea `convert_model_env.sh`:

| Paso | Comando | Qué hace | Fuente |
|---|---|---|---|
| Enlaces | `./convert_model_env.sh` | Crea los enlaces a `../../../scripts_model_convert/pegasus_{import,inference,quantize,export_ovx_nbg}.sh` | `zoo:examples/retinaface/convert_model/convert_model_env.sh` |
| Importar | `./pegasus_import.sh <NAME>` | Ejecuta `pegasus import onnx --model <NAME>.onnx` y genera `<NAME>.json`, `<NAME>.data`, `<NAME>_inputmeta.yml` y `<NAME>_postprocess_file.yml`. Después ejecuta `./config_yml.py <NAME>`, que escribe MEAN, SCALE, REVERSE_CHANNEL y el nodo de preproceso en el inputmeta, y activa el nodo de postproceso. | `zoo:scripts_model_convert/pegasus_import.sh:86-96,122-152,154-217` |
| Cuantizar | `./pegasus_quantize.sh <NAME> <uint8\|int16\|pcq\|bf16> <iteraciones>` | Ejecuta `pegasus quantize --device CPU --iterations <n> --rebuild --quantizer <q> --qtype <t>` y genera `<NAME>_<sufijo>.quantize` | `zoo:scripts_model_convert/pegasus_quantize.sh:22-63` |
| Simular (opcional) | `./pegasus_inference.sh <NAME> uint8` | Inferencia en el simulador de la PC | `zoo:examples/retinaface/README.md:88-90` |
| Exportar | `./pegasus_export_ovx_nbg.sh <NAME> <cuantización> a733` | Ejecuta `pegasus export ovxlib --pack-nbg-unify --optimize VIP9000NANODI_PLUS_PID0X1000003B ...`, renombra `network_binary.nb` a `<NAME>_<cuantización>_a733.nb`, lo copia a `../model/` y borra `./wksp/` | `zoo:scripts_model_convert/pegasus_export_ovx_nbg.sh:30-31,85-96,107-117` |

Tipos de cuantización (`pegasus_quantize.sh:25-36`, **verificado**):

| Argumento | Cuantizador | `qtype` | Sufijo del archivo |
|---|---|---|---|
| `uint8` | `asymmetric_affine` | `uint8` | `uint8` |
| `int16` | `dynamic_fixed_point` | `int16` | `int16` |
| `pcq` | `perchannel_symmetric_affine` | `int8` | `pcq` |
| `bf16` | `qbfloat16` | `qbfloat16` | `bf16` |

El tercer argumento de `pegasus_quantize.sh` es el número de iteraciones (`--iterations`). El README del zoo lo rotula como `<calibration_set_size>` (`zoo:examples/retinaface/README.md:85`). Esta guía le pasa el número de líneas del `dataset.txt`.

Dos detalles de los scripts, según la lectura del código (**sin probar**):

- Cada script empieza con `pushd $NAME` y termina con `popd`. El zoo pone el ONNX directamente en `convert_model/` (`zoo:examples/retinaface/README.md:44`), sin un subdirectorio `<NAME>/`. Por eso salen `pushd: <NAME>: No such file or directory` y `popd: directory stack empty`. El script sigue en `convert_model/`, porque no usa `set -e`. Es inofensivo, y es lo que hace que `config_yml.py` (`$(dirname "$0")/config_yml.py`), el `DATASET` relativo (`../../dataset/...`) y la salida `../model/` apunten a donde dice el README. **No crees un subdirectorio `<NAME>/`.**
- Hay un `config_yml.py` por directorio `convert_model/`. Por eso EA y EB van en directorios distintos.

## Detector RetinaFace

### Configuración de referencia

Es la de `zoo:examples/retinaface/convert_model/config_yml.py:9-26`, sin cambios (**verificado**; la repite `zoo:examples/retinaface/README.md:56-72`):

```python
# "database" allowed types: "TEXT, NPY, H5FS, SQLITE, LMDB, GENERATOR, ZIP"
DATASET = '../../dataset/widerface_10/dataset.txt'
DATASET_TYPE = "TEXT"

# mean, scale
MEAN    = [104,117,123]
SCALE   = [1,1,1]

# reverse_channel: True bgr, False rgb
REVERSE_CHANNEL = True

# add_preproc_node, True or False
ADD_PREPROC_NODE = True
PREPROC_TYPE = "IMAGE_RGB"

# add_postproc_node, quant output -> float32 output
ADD_POSTPROC_NODE = True
```

| Clave | Valor | Efecto | Fuente |
|---|---|---|---|
| `MEAN`, `SCALE` | `[104,117,123]`, `[1,1,1]` | La media va dentro del `.nb`: se alimenta el byte crudo 0..255 | `config_yml.py:13-14`; biubug6 `detect.py:94` (`img -= (104, 117, 123)`) |
| `REVERSE_CHANNEL` | `True` | Orden BGR (comentario del propio archivo: "True bgr, False rgb") | `config_yml.py:16-17` |
| `ADD_PREPROC_NODE`, `PREPROC_TYPE` | `True`, `"IMAGE_RGB"` | Entrada uint8 intercalada (HWC) sin cuantizar | `config_yml.py:19-23`; log de `radxa:rf` |
| `ADD_POSTPROC_NODE` | `True` | Salidas en float32 | `config_yml.py:25-26,96-104` |

### D1: R50 a 320×320 (recomendada)

Son los comandos de `radxa:rf` con la plataforma `a733`. El README del zoo pone `mr536` como ejemplo, y las dos plataformas usan el mismo `--optimize` (`pegasus_export_ovx_nbg.sh:28-31`).

```bash
cp ~/npu/descargas/Retinaface_resnet50_320.onnx \
   ~/npu/awnpu_model_zoo-v0.9.0-20260116-83a67d4b/examples/retinaface/convert_model/

docker exec model-zoo bash -lc 'cd /workspace/examples/retinaface/convert_model \
  && ./convert_model_env.sh \
  && ./pegasus_import.sh Retinaface_resnet50_320'

docker exec model-zoo bash -lc 'cd /workspace/examples/retinaface/convert_model \
  && N=$(wc -l < ../../dataset/widerface_10/dataset.txt) && echo "iteraciones=$N" \
  && ./pegasus_quantize.sh Retinaface_resnet50_320 uint8 "$N"'

docker exec model-zoo bash -lc 'cd /workspace/examples/retinaface/convert_model \
  && ./pegasus_export_ovx_nbg.sh Retinaface_resnet50_320 uint8 a733 \
  && ls -l ../model/'
```

`$(wc -l ...)` va dentro de las comillas simples, así que se evalúa en el contenedor. El `dataset.txt` de `widerface_10` tiene 10 rutas, pero la última no termina en salto de línea, así que `wc -l` da **9** (**verificado** en el archivo). El README del zoo usa 10 (`zoo:examples/retinaface/README.md:86`). Para D1, usa `10` en lugar de `"$N"` si quieres reproducir exactamente a Radxa, y anota cuál usaste.

Resultado esperado: `../model/Retinaface_resnet50_320_uint8_a733.nb`, de unos 19 MB (el de Radxa mide 19 056 048 bytes, `radxa:rf`). Durante la importación se imprime `set preprocess param mean [104, 117, 123]`, `set reverse_channel True`, `set add_preproc_node True` y `add_postproc_node: false -> true` (`config_yml.py:63-103`).

### D2: R50 a 640×640 (opcional)

Por qué: con la cámara de la XIAO en UXGA, a 3 m la cara mide unos 12 px en la entrada de 320, y el ancla mínima de RetinaFace es de 16 px (plan, sección 6.2; biubug6 `data/config.py`, `min_sizes = [[16, 32], [64, 128], [256, 512]]`). A 640, la cara mide el doble.

No hay ONNX de 640 publicado: hay que exportarlo desde PyTorch, fuera del contenedor. Es la única parte de la guía que usa biubug6, PyTorch y los pesos de Google Drive. Pasos de `zoo:examples/retinaface/README.md:13-32`, con 640 en lugar de 320:

1. Clonar biubug6 en el commit de referencia y dejar los pesos en `weights/`:

   ```bash
   cd ~/npu
   git clone https://github.com/biubug6/Pytorch_Retinaface.git
   git -C Pytorch_Retinaface checkout b984b4b775b2c4dced95c1eadd195a5c7d32a60b
   mkdir -p Pytorch_Retinaface/weights && cp ~/npu/descargas/Resnet50_Final.pth Pytorch_Retinaface/weights/
   ```

2. Añadir `type=int` al argumento `--long_side` de `convert_to_onnx.py` (línea 20). El README del zoo escribe la coma en chino (`type=int，default=640`); este `sed` usa la coma ASCII:

   ```bash
   sed -i "s/parser.add_argument('--long_side', default=640,/parser.add_argument('--long_side', type=int, default=640,/" ~/npu/Pytorch_Retinaface/convert_to_onnx.py
   grep -n "long_side" ~/npu/Pytorch_Retinaface/convert_to_onnx.py
   ```

3. Exportar en un venv con PyTorch, torchvision y OpenCV. Esto está **sin probar**: el repo es antiguo, ninguna fuente fija la versión de PyTorch y el script usa `torch.onnx._export` (`convert_to_onnx.py:85`). Con `--network resnet50`, biubug6 crea el backbone con `models.resnet50(pretrained=True)` (`models/retinaface.py:70`), que baja los pesos de ImageNet la primera vez:

   ```bash
   python3 -m venv ~/npu/venv-torch
   ~/npu/venv-torch/bin/pip install torch torchvision opencv-python-headless numpy
   cd ~/npu/Pytorch_Retinaface
   ~/npu/venv-torch/bin/python convert_to_onnx.py --trained_model ./weights/Resnet50_Final.pth --network resnet50 --long_side 640 --cpu
   mv FaceDetector.onnx ~/npu/awnpu_model_zoo-v0.9.0-20260116-83a67d4b/examples/retinaface/convert_model/Retinaface_resnet50_640.onnx
   ```

4. Convertir con la misma `config_yml.py` de D1:

   ```bash
   docker exec model-zoo bash -lc 'cd /workspace/examples/retinaface/convert_model \
     && ./pegasus_import.sh Retinaface_resnet50_640 \
     && ./pegasus_quantize.sh Retinaface_resnet50_640 uint8 10 \
     && ./pegasus_export_ovx_nbg.sh Retinaface_resnet50_640 uint8 a733'
   ```

**Riesgo:** el README del zoo dice que su ONNX de 320 está "导出、修改和验证" (exportado, modificado y verificado), pero no dice qué se modificó. Un ONNX exportado por nosotros puede diferir en el orden o el nombre de las salidas. D2 se valida en la placa: tiene que dar tres salidas de 4, 2 y 10 valores por ancla ([Verificación en la placa](#verificación-en-la-placa)).

### D3: MobileNet0.25 a 320×320 (opcional)

Mismo procedimiento que D1, con el ONNX publicado. Usa la misma configuración de anclas que R50 (biubug6 `data/config.py`, `cfg_mnet` y `cfg_re50`). No hay medida publicada en la A733.

```bash
cp ~/npu/descargas/Retinaface_mobilenet0.25_320.onnx \
   ~/npu/awnpu_model_zoo-v0.9.0-20260116-83a67d4b/examples/retinaface/convert_model/
docker exec model-zoo bash -lc 'cd /workspace/examples/retinaface/convert_model \
  && ./pegasus_import.sh Retinaface_mobilenet0.25_320 \
  && ./pegasus_quantize.sh Retinaface_mobilenet0.25_320 uint8 10 \
  && ./pegasus_export_ovx_nbg.sh Retinaface_mobilenet0.25_320 uint8 a733'
```

No está probado que Acuity acepte el `.` del nombre. Si falla, renombra el ONNX a `Retinaface_mnet025_320.onnx` y usa ese `NAME`.

## Imágenes de calibración

### Detector

Se usan las imágenes de `zoo:examples/dataset/widerface_10/` con su `dataset.txt`: una ruta relativa por línea, de `./test1.jpg` a `./test10.jpg`. Es lo que usa la configuración de referencia (**verificado**) y viene con el zoo.

### Embedder

Unos 100 recortes **alineados** de 112×112 de LFW. Se hacen en tres pasos, todo en Ubuntu y fuera del repo (**todo sin probar**):

1. `landmarks_retinaface.py` saca los 5 landmarks de cada imagen.
2. `repo:herramientas/alinear_lfw.sh` aplica el warp del servicio.
3. Se arma el `dataset.txt`.

**Por qué así.** `repo:orangepi/vision/README.md` pide que, para cuantizar, los landmarks salgan del detector, para que el recorte sea el que verá la placa. `repo:orangepi/vision/conversion/landmarks_retinaface.py` corre **el mismo ONNX de RetinaFace R50 a 320** del zoo con onnxruntime en la CPU, con el letterbox y la decodificación de referencia, y escribe el manifiesto en el formato TAB de `alinear_lfw`. `alinear_lfw` aplica el mismo warp de 5 puntos que el servicio (`vision::align_face` hacia `kArcFaceReference`). La herramienta se niega a escribir dentro del repo y sale con código 1 (`repo:herramientas/alinear_lfw.sh`).

#### 1. El entorno de Python

```bash
python3 -m venv ~/npu/venv
~/npu/venv/bin/pip install onnxruntime numpy opencv-python-headless
~/npu/venv/bin/python ~/npu/AutoList/orangepi/vision/conversion/landmarks_retinaface.py --help
```

Interfaz: `--onnx RUTA.onnx --lista imagenes.txt --salida manifiesto.tsv [--umbral 0.4] [--max N] [--rgb]`. Si `--help` dice otra cosa, manda `--help`: anótalo en el reporte. Lo que hay que saber del script (`repo:orangepi/vision/conversion/landmarks_retinaface.py`):

| Punto | Comportamiento |
|---|---|
| Códigos de salida | 0 si termina bien, 1 por error y 2 por argumentos inválidos. En stderr solo imprime los conteos de imágenes escritas y omitidas, sin rutas. |
| La lista | Una ruta de imagen por línea. Usa rutas **absolutas**. El script escribe siempre la ruta absoluta en el manifiesto (`os.path.abspath`), aunque la lista traiga relativas. `alinear_lfw` resuelve las rutas desde su propio directorio de trabajo, así que una relativa apuntaría a otro sitio. |
| Orden de canales | Por omisión, BGR. Con `--rgb`, RGB. Cuál es el correcto con este ONNX se decide en el paso 2. |
| La salida | El script rechaza escribirla dentro del repo. La raíz del repo es `PASELISTA_RAIZ_REPO`, o tres niveles por encima del script. Con el clon en `~/npu/AutoList` y la salida en `~/npu/calib/`, no hace falta tocar nada. |
| El ONNX | Espera el ONNX **sin** el nodo de preproceso de Acuity, es decir, el original de Allwinner (`Retinaface_resnet50_320.onnx`): `float` NCHW `[1,3,320,320]`. Hace el letterbox con relleno 114 y resta la media `[104,117,123]`. Con otro ONNX, falla con un mensaje claro. |
| Pruebas | `cd ~/npu/AutoList/orangepi/vision/conversion && python3 -B -m unittest` (solo biblioteca estándar). Pasan en la placa. En la laptop están **sin probar**: ejecútalas antes de seguir. |

El camino completo con onnxruntime está **sin probar**.

#### 2. Comprobación manual con la imagen de la demo

Antes de procesar LFW, el script se prueba con `zoo:examples/retinaface/model/test.jpg`, la imagen de la demo de Radxa. Se ejecuta dos veces, en BGR (por omisión) y con `--rgb`, para decidir el orden de canales:

```bash
mkdir -p ~/npu/calib
echo ~/npu/awnpu_model_zoo-v0.9.0-20260116-83a67d4b/examples/retinaface/model/test.jpg > ~/npu/calib/demo.txt
L=~/npu/AutoList/orangepi/vision/conversion/landmarks_retinaface.py
O=~/npu/descargas/Retinaface_resnet50_320.onnx
~/npu/venv/bin/python "$L" --onnx "$O" --lista ~/npu/calib/demo.txt --salida ~/npu/calib/demo_bgr.tsv
~/npu/venv/bin/python "$L" --onnx "$O" --lista ~/npu/calib/demo.txt --salida ~/npu/calib/demo_rgb.tsv --rgb
cut -f2- ~/npu/calib/demo_bgr.tsv
cut -f2- ~/npu/calib/demo_rgb.tsv
```

`cut -f2-` quita la ruta y deja los 10 números (formato TAB de `alinear_lfw`). Si `--help` no lista `--rgb`, la versión del script es anterior: ejecuta solo la primera pasada y anótalo.

La demo de la A733 da una cara con la caja `[244, 46, 363, 209]` y estos 5 puntos (log de `radxa:rf`):

```
275.10 113.49
328.03 112.26
300.95 147.94
277.57 165.17
326.80 165.17
```

El script escribe los landmarks, no la caja. Para cada orden, compara sus 10 números con esos 5 puntos, en el mismo orden, y calcula la diferencia máxima en píxeles. Este cálculo lee los dos archivos y no imprime rutas:

```bash
for o in bgr rgb; do
  cut -f2- ~/npu/calib/demo_$o.tsv | awk -v o=$o 'BEGIN { split("275.10 113.49 328.03 112.26 300.95 147.94 277.57 165.17 326.80 165.17", r, " ") }
    { m = 0; dentro = 1
      for (i = 1; i <= 10; i++) { d = $i - r[i]; if (d < 0) d = -d; if (d > m) m = d }
      for (i = 1; i <= 10; i += 2) if ($i < 244 || $i > 363 || $(i+1) < 46 || $(i+1) > 209) dentro = 0
      printf "%s: diferencia maxima %.2f px, todos dentro de la caja: %s\n", o, m, (dentro ? "si" : "no") }
    END { if (NR == 0) print o ": sin cara detectada (repórtalo)" }'
done
```

**Se elige el orden con la menor diferencia máxima**, siempre que sus 5 puntos caigan dentro de la caja `[244, 46, 363, 209]`. Si las dos pasadas dan diferencias parecidas, gana la que deja los puntos mejor centrados en la caja. La demo corre el `.nb` cuantizado y el script el ONNX en flotante, así que no van a coincidir exactos. **Sugerencia sin probar:** el orden bueno debería quedar a unos 3 px. En el reporte van las **dos** diferencias máximas y el orden elegido, no solo si se cumplen los 3 px. `zoo:examples/retinaface/figures/out_retinaface.png` muestra el resultado de la demo dibujado.

`test.jpg` viene en el tarball v0.9.0 del zoo (**verificado** el 5 nov: 81 332 bytes, idéntico a `zoo:examples/dataset/widerface_10/test10.jpg`). Si en tu copia no está, usa `widerface_10/test10.jpg`, que es la misma imagen.

Si no hay forma de hacer esta comparación, la alternativa es inspeccionar a ojo 3 recortes ya alineados de LFW (paso 4). En cada uno, los ojos van arriba, la nariz al centro y la boca abajo, y el ojo izquierdo de la imagen es el de menor x. El agente puede abrir los PNG como imagen. Si no puede, lo hace Yordi.

Con la inspección visual no se pueden comparar los dos órdenes con números: genera los recortes con BGR y, si no cuadran, repite los pasos 3 y 4 con `--rgb`. Si ninguno de los dos órdenes cuadra, o si la comparación falla dos veces, para y repórtalo: sin landmarks fiables no se calibra el embedder.

#### 3. Lista de imágenes y manifiesto

Se toman unas 150 imágenes de personas distintas, para que queden unas 100 tras las omisiones. La selección es determinista: con el mismo `lfw.tgz`, cualquiera la repite.

```bash
find ~/npu/lfw -name '*_0001.jpg' | sort | awk 'NR % 30 == 1' | head -n 150 > ~/npu/calib/imagenes.txt
wc -l ~/npu/calib/imagenes.txt

~/npu/venv/bin/python ~/npu/AutoList/orangepi/vision/conversion/landmarks_retinaface.py \
  --onnx ~/npu/descargas/Retinaface_resnet50_320.onnx \
  --lista ~/npu/calib/imagenes.txt \
  --salida ~/npu/calib/manifiesto_lfw.tsv
# Añade --rgb al final si en el paso 2 ganó RGB.
wc -l ~/npu/calib/manifiesto_lfw.tsv
```

Usa el orden elegido en el paso 2. El `wc -l` debe coincidir con las "escritas" que el script imprime en stderr. `find` sobre `~/npu/lfw` escribe rutas absolutas, que es lo recomendado: el manifiesto queda con rutas absolutas y `alinear_lfw` las encuentra desde cualquier directorio. LFW tiene unas 5 700 personas, así que una de cada 30 da más de 150 (**sin verificar** contra el archivo). Si el script sale con 1 o 2, para y repórtalo con su mensaje. No pegues `imagenes.txt` ni el manifiesto en el reporte: las rutas llevan nombres de personas.

#### 4. Recortes alineados

```bash
cd ~/npu/AutoList
herramientas/pruebas_alinear_lfw.sh
herramientas/alinear_lfw.sh --manifest ~/npu/calib/manifiesto_lfw.tsv --out ~/npu/calib/lfw_112
ls ~/npu/calib/lfw_112 | wc -l
```

La herramienta omite, con un aviso por número de línea, las imágenes que no puede leer y las de landmarks degenerados. Una línea mal formada aborta todo el lote (`repo:orangepi/vision/README.md`). Si no compila en Ubuntu, para y repórtalo: la laptop trae g++ 11 y OpenCV 4.5, y la placa, donde se escribió, g++ 12 y OpenCV 4.6. Yordi decide si se ejecuta en la placa.

#### 5. El `dataset.txt` del embedder

```bash
D=~/npu/awnpu_model_zoo-v0.9.0-20260116-83a67d4b/examples/dataset/lfw_112
mkdir -p "$D" && cp ~/npu/calib/lfw_112/*.png "$D"/
( cd "$D" && ls *.png | sed 's|^|./|' > dataset.txt && wc -l dataset.txt )
```

Resultado esperado: unas 100 líneas. Si son menos de 50, para y repórtalo. El formato `./archivo` es el de `widerface_10/dataset.txt`, y el zoo ya usa PNG en sus sets de calibración (`zoo:examples/dataset/ocr/`, `zoo:examples/dataset/cityscapes_10/`). Aquí, `sed` termina cada línea con un salto de línea, así que `wc -l` da el número exacto de imágenes.

## Embedder `w600k_mbf`

### Lo que se sabe del modelo

| Punto | Valor | Fuente |
|---|---|---|
| Entrada | `input.1`, float32, `[N,3,112,112]`, con **lote dinámico** | Grafo de `w600k_mbf.onnx` leído el 5 nov (producer `pytorch` 1.8, opset 11). El comando de abajo lo vuelve a comprobar. |
| Salida | `516`, `[1,512]` float32, sin normalizar L2 | Grafo leído el 5 nov. El servicio ya normaliza. |
| Normalización | `(x − 127.5) / 127.5`, **fuera** del grafo (el grafo no empieza con Sub ni Mul) | InsightFace `python-package/insightface/model_zoo/arcface_onnx.py:36-42` |
| Color | RGB (`swapRB=True` sobre la imagen BGR de OpenCV) | `arcface_onnx.py:92-93` |
| Alineación | `arcface_dst` de 5 puntos, igual que `kArcFaceReference` | InsightFace `utils/face_align.py:6-9`; `repo:orangepi/vision/include/vision/alignment.hpp` |

El zoo de Allwinner no trae ningún ejemplo de ArcFace ni de MobileFaceNet. Las configuraciones de abajo están **sin probar**: copian la estructura de `zoo:examples/*/convert_model/config_yml.py` con los valores de InsightFace.

### Fijar el lote a 1

Los modelos de la NPU necesitan una forma estática. El zoo lo resuelve con `onnxsim` (`zoo:examples/yolov5/README.md`, sección 静态shape). **Sin probar** con este modelo:

```bash
python3 -m venv ~/npu/venv-onnx
~/npu/venv-onnx/bin/pip install onnx onnxsim
cd ~/npu/descargas
~/npu/venv-onnx/bin/python -m onnxsim w600k_mbf.onnx w600k_mbf_b1.onnx --overwrite-input-shape input.1:1,3,112,112
~/npu/venv-onnx/bin/python -c "import onnx; m = onnx.load('w600k_mbf_b1.onnx'); print([(i.name, [d.dim_value or d.dim_param for d in i.type.tensor_type.shape.dim]) for i in m.graph.input]); print([o.name for o in m.graph.output])"
```

Manda la comprobación final. Resultado esperado: `[('input.1', [1, 3, 112, 112])]` y `['516']`. Si no sale así, para y repórtalo.

### Preparar los directorios de EA y EB

Cada opción tiene su propio `convert_model/` con su `config_yml.py`, a la misma profundidad que `retinaface/`. Así los enlaces de `convert_model_env.sh` y las rutas relativas funcionan igual:

```bash
Z=~/npu/awnpu_model_zoo-v0.9.0-20260116-83a67d4b/examples
for v in a b; do
  mkdir -p "$Z/w600k_mbf_$v/convert_model" "$Z/w600k_mbf_$v/model"
  cp "$Z/retinaface/convert_model/convert_model_env.sh" "$Z/retinaface/convert_model/config_yml.py" "$Z/w600k_mbf_$v/convert_model/"
  cp ~/npu/descargas/w600k_mbf_b1.onnx "$Z/w600k_mbf_$v/convert_model/w600k_mbf_$v.onnx"
done
```

Si los directorios ya existen de un intento anterior y el contenedor los dejó con dueño `root`, ejecuta antes el `chown` de [Cargar la imagen](#cargar-la-imagen-de-acuity-y-crear-el-contenedor).

### EA: con nodo de preproceso

Se editan las constantes del `config_yml.py` copiado. El resto del archivo no cambia: el `sed` solo toca líneas que empiezan en la columna 0, y las del código están indentadas.

```bash
cd ~/npu/awnpu_model_zoo-v0.9.0-20260116-83a67d4b/examples/w600k_mbf_a/convert_model
sed -i \
  -e "s|^DATASET = .*|DATASET = '../../dataset/lfw_112/dataset.txt'|" \
  -e "s|^MEAN .*|MEAN    = [127.5, 127.5, 127.5]|" \
  -e "s|^SCALE .*|SCALE   = [0.0078431, 0.0078431, 0.0078431]|" \
  -e "s|^REVERSE_CHANNEL = .*|REVERSE_CHANNEL = False|" \
  -e "s|^ADD_PREPROC_NODE = .*|ADD_PREPROC_NODE = True|" \
  config_yml.py
grep -nE '^(DATASET|MEAN|SCALE|REVERSE_CHANNEL|ADD_|PREPROC_TYPE)' config_yml.py
```

Resultado esperado del `grep` (el `sed` se probó en una copia del `config_yml.py` del zoo):

```
9:DATASET = '../../dataset/lfw_112/dataset.txt'
10:DATASET_TYPE = "TEXT"
13:MEAN    = [127.5, 127.5, 127.5]
14:SCALE   = [0.0078431, 0.0078431, 0.0078431]
17:REVERSE_CHANNEL = False
20:ADD_PREPROC_NODE = True
23:PREPROC_TYPE = "IMAGE_RGB"
26:ADD_POSTPROC_NODE = True
```

`SCALE` es el multiplicador, no la desviación. En el zoo, mobilenetv2 usa `SCALE = [0.0171247, ...]`, que es 1/58.4 (`zoo:examples/mobilenetv2/convert_model/config_yml.py`), y yolov5 usa `0.0039216` = 1/255. Aquí, 1/127.5 = 0.0078431. `REVERSE_CHANNEL = False` es RGB, como el ONNX: los modelos del zoo en RGB (yolov5, mobilenetv2 y resnet50v2) también usan `False`.

Convertir:

```bash
docker exec model-zoo bash -lc 'cd /workspace/examples/w600k_mbf_a/convert_model \
  && ./convert_model_env.sh \
  && ./pegasus_import.sh w600k_mbf_a \
  && N=$(wc -l < ../../dataset/lfw_112/dataset.txt) && echo "iteraciones=$N" \
  && ./pegasus_quantize.sh w600k_mbf_a uint8 "$N" \
  && ./pegasus_export_ovx_nbg.sh w600k_mbf_a uint8 a733 \
  && ls -l ../model/'
```

Resultado esperado (**sin probar**, por analogía con el detector): entrada uint8 intercalada `3 112 112 1` sin cuantizar, y salida float32 de 512 valores.

### EB: sin nodo de preproceso

Igual que EA, con `ADD_PREPROC_NODE = False`:

```bash
cd ~/npu/awnpu_model_zoo-v0.9.0-20260116-83a67d4b/examples/w600k_mbf_b/convert_model
sed -i \
  -e "s|^DATASET = .*|DATASET = '../../dataset/lfw_112/dataset.txt'|" \
  -e "s|^MEAN .*|MEAN    = [127.5, 127.5, 127.5]|" \
  -e "s|^SCALE .*|SCALE   = [0.0078431, 0.0078431, 0.0078431]|" \
  -e "s|^REVERSE_CHANNEL = .*|REVERSE_CHANNEL = False|" \
  -e "s|^ADD_PREPROC_NODE = .*|ADD_PREPROC_NODE = False|" \
  config_yml.py
grep -nE '^(DATASET|MEAN|SCALE|REVERSE_CHANNEL|ADD_|PREPROC_TYPE)' config_yml.py

docker exec model-zoo bash -lc 'cd /workspace/examples/w600k_mbf_b/convert_model \
  && ./convert_model_env.sh \
  && ./pegasus_import.sh w600k_mbf_b \
  && N=$(wc -l < ../../dataset/lfw_112/dataset.txt) && echo "iteraciones=$N" \
  && ./pegasus_quantize.sh w600k_mbf_b uint8 "$N" \
  && ./pegasus_export_ovx_nbg.sh w600k_mbf_b uint8 a733 \
  && ls -l ../model/'
```

El `grep` debe dar lo mismo que en EA, salvo `20:ADD_PREPROC_NODE = False`. `PREPROC_TYPE` queda en el archivo, pero no se usa sin el nodo.

Resultado esperado (**sin probar**): entrada planar `112 112 3 1` en uint8 con cuantización afín, escala ≈ 2/255 (≈ 0.00784) y zero point ≈ 128, y salida float32 de 512 valores. El código de hoy la acepta con `"embed_escala": "arcface"`, y al arrancar comprueba que el rango del tensor cubra [−1, 1] con un margen del 1 % (`repo:orangepi/vision/README.md`, sección "Los adaptadores sobre la NPU"). Si la calibración no ve píxeles 0 y 255, la escala sale menor y el servicio puede negarse a arrancar. La escala y el zero point reales se ven en la placa ([Verificación en la placa](#verificación-en-la-placa)).

### pcq e int16 (opcionales)

Reutilizan la importación ya hecha: solo se repiten la cuantización y el export, que son baratos. Por ejemplo, para EA:

```bash
docker exec model-zoo bash -lc 'cd /workspace/examples/w600k_mbf_a/convert_model \
  && N=$(wc -l < ../../dataset/lfw_112/dataset.txt) \
  && ./pegasus_quantize.sh w600k_mbf_a pcq "$N" \
  && ./pegasus_export_ovx_nbg.sh w600k_mbf_a pcq a733 \
  && ./pegasus_quantize.sh w600k_mbf_a int16 "$N" \
  && ./pegasus_export_ovx_nbg.sh w600k_mbf_a int16 a733'
```

Para EB, lo mismo con `w600k_mbf_b`. El export busca `<NAME>_pcq.quantize` y `<NAME>_int16.quantize`, que es lo que escribe la cuantización (`pegasus_quantize.sh:29-32,61`; `pegasus_export_ovx_nbg.sh:78-92`). El plan prevé int16, o la cuantización mixta, como primer recurso si uint8 pierde precisión (plan, sección 6.2). Qué formato de entrada sale con int16, sobre todo en EB, está **sin probar**.

### KLD (opcional, solo si sobra tiempo)

`radxa:quant` recomienda, si la precisión no basta, añadir `--algorithm kl_divergence --batch-size 100 --divergence-first-quantize-bits 12` (y `--MLE`) al comando de `pegasus quantize`. Los scripts son enlaces a `scripts_model_convert/`, así que editarlos cambia todos los modelos. Si lo pruebas, trabaja sobre una copia (`cp ../../../scripts_model_convert/pegasus_quantize.sh ./pegasus_quantize_kld.sh`), edítala con `sed` y usa otro `NAME` para no pisar la variante uint8. Está **sin probar** en este modelo.

## Entrega en la laptop

El agente termina aquí. Copia los `.nb` **por nombre**, solo los que produjo, a `~/npu/entrega/`:

```bash
docker exec model-zoo chown -R "$(id -u):$(id -g)" /workspace/examples
E=~/npu/awnpu_model_zoo-v0.9.0-20260116-83a67d4b/examples
mkdir -p ~/npu/entrega
cp "$E/retinaface/model/Retinaface_resnet50_320_uint8_a733.nb" ~/npu/entrega/
cp "$E/w600k_mbf_a/model/w600k_mbf_a_uint8_a733.nb" ~/npu/entrega/
cp "$E/w600k_mbf_b/model/w600k_mbf_b_uint8_a733.nb" ~/npu/entrega/
# Opcionales, solo si se produjeron, por ejemplo:
# cp "$E/w600k_mbf_a/model/w600k_mbf_a_pcq_a733.nb" ~/npu/entrega/
# cp "$E/retinaface/model/Retinaface_resnet50_640_uint8_a733.nb" ~/npu/entrega/

cd ~/npu/entrega
ls -l *.nb > resultados-conversion.txt
sha256sum *.nb > SHA256SUMS
cat SHA256SUMS >> resultados-conversion.txt
cat resultados-conversion.txt
```

Después escribe `~/npu/entrega/reporte.md` con la [plantilla](#plantilla-de-reporte) y comprueba que el repo sigue limpio:

```bash
git -C ~/npu/AutoList status --short
```

No debe aparecer nada.

### `nbinfo` (opcional)

Solo si Yordi bajó `nbinfo`, lo revisó y su hash está en `SHA256SUMS.descargas` (y `sha256sum -c` dio `OK`). Si no, se omite: la verificación que cuenta es la de la placa.

```bash
chmod +x ~/npu/descargas/nbinfo
cd ~/npu/entrega
for f in *.nb; do
  echo "=================== $f"
  ~/npu/descargas/nbinfo -b "$f"
  ~/npu/descargas/nbinfo -in "$f"
  ~/npu/descargas/nbinfo -out "$f"
done >> resultados-conversion.txt 2>&1
tail -n 80 resultados-conversion.txt
```

`nbinfo` imprime `Size of Dim[0]`, `Dim[1]`, etc. en el mismo orden que el log de la demo, y en `-b`, el `Target` (`radxa:nbinfo`). Para las variantes a733, **deducido**: `Target: 0x1000003b`, el PID de `VIP9000NANODI_PLUS_PID0X1000003B` (`pegasus_export_ovx_nbg.sh:30-31`), que coincide con el ejemplo de `radxa:nbinfo`.

### Lo que no se sube a git

- Los `.nb`, `.onnx`, `.json`, `.data`, `.quantize`, `*_inputmeta.yml` y `*_postprocess_file.yml`.
- `SHA256SUMS`, `SHA256SUMS.descargas`, `resultados-conversion.txt` y `reporte.md`: el reporte se entrega aparte.
- `lfw.tgz`, LFW extraído, `imagenes.txt`, los manifiestos, los recortes de 112×112 y los `dataset.txt` que los listan.
- Los pesos `Resnet50_Final.pth`, `buffalo_sc.zip`, el zip y el tar de la imagen de Acuity, y `nbinfo`.
- Los venvs y la copia de biubug6.

## Verificación en la placa

No lo hace el agente de la laptop: lo hacen Yordi o Gus en la placa. Es la verificación principal de las formas de los tensores.

### 1. Copiar (Yordi)

Desde Ubuntu en la laptop:

```bash
ssh orangepi@<ip-de-la-placa> 'mkdir -p ~/modelos-nb'
scp ~/npu/entrega/*.nb ~/npu/entrega/SHA256SUMS ~/npu/entrega/resultados-conversion.txt orangepi@<ip-de-la-placa>:~/modelos-nb/
```

En la placa:

```bash
cd ~/modelos-nb && sha256sum -c SHA256SUMS && echo "HASHES OK"
```

Sin `HASHES OK` no se sigue: hay que volver a copiar.

### 2. Leer los tensores con `npu_smoke` (sin `sudo`)

`build/npu_smoke <modelo.nb> <entrada.dat> [iteraciones]` carga cualquier `.nb` desde cualquier ruta. Por cada tensor imprime `dims(w,h,c,n)`, `format`, `quant`, `scale`, `zero_point` y `bytes` (`repo:orangepi/vision/apps/npu_smoke.cpp`). Lo hace **antes** de leer la entrada. Si el archivo de entrada no mide exactamente lo que ocupa el tensor, sale con `entrada 0: se esperaban N bytes ...`. Así, una primera pasada con un archivo de 1 byte da las formas y el tamaño exacto de la entrada. Necesita el build del servicio (`cmake -S . -B build && cmake --build build -j4`, `repo:orangepi/vision/README.md`, sección "Compilar") y que el usuario `orangepi` pueda abrir `/dev/vipcore`.

```bash
cd <checkout>/orangepi/vision
printf x > /tmp/un_byte.dat
for f in ~/modelos-nb/*.nb; do echo "== $f"; build/npu_smoke "$f" /tmp/un_byte.dat 1; done
```

Después, con el tamaño que pidió cada modelo, una pasada real que mide el tiempo:

```bash
head -c 307200 /dev/zero > /tmp/cero_d1.dat
build/npu_smoke ~/modelos-nb/Retinaface_resnet50_320_uint8_a733.nb /tmp/cero_d1.dat 20
```

`npu_smoke` imprime las dimensiones en el orden en que las da VIPLite, con la etiqueta `(w,h,c,n)`. Con la entrada intercalada, la primera dimensión es el canal.

| Variante | Entrada esperada | Salidas esperadas | Estado |
|---|---|---|---|
| D1 | `3x320x320x1`, `uint8`, `quant=none`, 307 200 bytes | 3 salidas en este orden: `4x4200x1` (cajas), `2x4200x1` (scores) y `10x4200x1` (landmarks), `fp32`, `quant=none` | **Verificado:** log de la A733 en `radxa:rf` (`input 0 dim 3 320 320 1, data_format=2, quant_format=0, none-quant`; `buffer size: 307200`; `output 0 dim 4 4200 1 0, data_format=0`, igual en las otras dos) |
| D2 | `3x640x640x1`, `uint8`, `quant=none` | Las mismas, con 16 800 anclas | **Deducido:** (80² + 40² + 20²) × 2 = 16 800, con strides 8, 16 y 32 y 2 anclas por posición (biubug6 `data/config.py`) |
| D3 | Igual que D1 | Igual que D1 | **Deducido:** misma configuración de anclas |
| EA | `3x112x112x1`, `uint8`, `quant=none` | 1 salida de 512 valores, `fp32` | **Sin probar** |
| EB | `112x112x3x1`, `uint8`, `quant=affine`, `scale` ≈ 0.00784, `zero_point` ≈ 128 | 1 salida de 512 valores, `fp32` | **Sin probar** |
| pcq, int16 | Lo que salga: anótalo | 512 valores | **Sin probar** |

Si un `.nb` no carga (`error: ...` y código 1), el driver lo rechaza: puede estar truncado o venir de otra versión de Acuity.

### 3. Arranque del servicio (Gus, con `sudo`)

El servicio solo lee los modelos de `/opt/autolist/models/`, así que esta prueba necesita instalarlos con `sudo` y tener la configuración y las credenciales de `/etc/pase-lista/` (`repo:orangepi/vision/README.md`, sección "La unidad systemd"). La hace una persona:

```bash
sudo install -d -m 0755 /opt/autolist/models
sudo install -m 0644 ~/modelos-nb/Retinaface_resnet50_320_uint8_a733.nb /opt/autolist/models/detector.nb
sudo install -m 0644 ~/modelos-nb/w600k_mbf_b_uint8_a733.nb /opt/autolist/models/w600k_mbf-uint8-v1.nb
```

Al 5 de noviembre, `/opt/autolist/` no existe en la placa. Al arrancar, el servicio construye el detector y el embedder y valida sus tensores: número de entradas, formato, canales, lote, rango representable, formato de salida y número de elementos frente al layout. Si algo no encaja, sale con el **78** y el mensaje `[vision] el .nb no encaja con el codigo: <motivo>`. Si carga, imprime `[vision] detector y embedder sobre la NPU cargados (...)` (`repo:orangepi/vision/src/main.cpp`). Con D1 y el código de hoy, lo esperado es el 78.

`build/vision_dry_run` y `--dry-run` **no** sirven para esto: usan el detector y el embedder falsos y no abren los `.nb` (`repo:orangepi/vision/README.md`, sección "Recorrido en seco"; `src/main.cpp`).

## Lo que hace Gus después

Solo como contexto: son cambios de código de `orangepi/vision/`, del carril de Gus, y no son parte de la conversión. El código de hoy no encaja con el modelo de referencia y falla cerrado al arrancar con D1. Lo que hay que ajustar:

| Punto | El modelo de referencia | Fuente |
|---|---|---|
| Entrada | uint8 intercalada (HWC, `3 320 320 1`) en lugar de planar | Log de `radxa:rf`; `zoo:examples/retinaface/retinaface_pre.cpp:69` |
| Salidas | 3 tensores (cajas, scores y landmarks), no uno de 15 valores por ancla | Log de `radxa:rf`; `zoo:examples/retinaface/retinaface_post.cpp:172-174,227-229` |
| Score | 2 columnas por ancla, `[fondo, cara]`, con la softmax dentro; se usa la columna 1 | `retinaface_post.cpp:200,211`; biubug6 `models/retinaface.py`, `F.softmax` en la fase `test` |
| Varianzas | 0.1 para el centro y los landmarks y 0.2 para el tamaño, multiplicadas por el tamaño del ancla | `retinaface_post.cpp:188,202-205`; biubug6 `data/config.py` y `utils/box_utils.py` |
| Anclas | `{16,32}`, `{64,128}` y `{256,512}` px para los strides 8, 16 y 32; cuadradas, sin escalar con la entrada, con centro en `(k+0.5)·stride` | `retinaface_post.cpp:130-164`; biubug6 `layers/functions/prior_box.py` |
| Landmarks | Relativos al ancla, no a la caja decodificada | `retinaface_post.cpp:218-219` |
| Color | BGR (`"color": "bgr"` en `vision.conf`) | `config_yml.py:17` |
| Redimensionado | Letterbox centrado con relleno 114 | `zoo:examples/retinaface/retinaface_pre.cpp:47-76` |
| Embedder EB | `"embed_escala": "arcface"` en `vision.conf` | `repo:orangepi/vision/README.md` |

La prueba de punta a punta usa `zoo:examples/retinaface/model/test.jpg`: con los cambios hechos, debería dar la caja `[244, 46, 363, 209]` y los 5 puntos de la demo (`radxa:rf`, citados en [Imágenes de calibración](#2-comprobación-manual-con-la-imagen-de-la-demo)). Cuando el detector corra en la placa, Gus puede regenerar el manifiesto de calibración con el `.nb` y volver a cuantizar el embedder.

## Limpieza

Hasta que Gus confirme los `.nb`, se conserva todo lo de `~/npu/`, para poder volver a cuantizar sin repetir los pasos. Cuando los confirme:

```bash
rm -r ~/npu/calib
rm -r ~/npu/awnpu_model_zoo-v0.9.0-20260116-83a67d4b/examples/dataset/lfw_*
rm -r ~/npu/lfw
docker rm -f model-zoo
```

Los recortes, el manifiesto y LFW extraído son lo único con rostros. No hace falta guardarlos para reproducir la conversión: con `lfw.tgz` (su sha256 está en `SHA256SUMS.descargas`), la regla de selección determinista y `landmarks_retinaface.py` se regeneran igual. `lfw.tgz`, los ONNX, la imagen de Acuity y el zoo se pueden conservar en `~/npu/descargas/` o borrar; decide Yordi.

## Problemas conocidos

| Problema | Síntoma | Qué hacer |
|---|---|---|
| Fines de línea CRLF en los scripts | `/bin/bash^M: bad interpreter` o `$'\r': command not found` | `dos2unix` sobre el script. Clona y extrae dentro de WSL, con `core.autocrlf input`. No edites los scripts con un editor de Windows. |
| Falta de memoria en WSL | `Killed`, código 137, o el contenedor se detiene durante la cuantización o el export | Para y repórtalo: Yordi sube `memory` y `swap` en `.wslconfig` y reinicia WSL. Anota en qué paso pasó. |
| Lote dinámico en el ONNX | La importación falla, o el inputmeta trae una dimensión de lote vacía o simbólica | Fija el lote a 1 con `onnxsim` ([Fijar el lote a 1](#fijar-el-lote-a-1)) y comprueba que `input.1` sea `[1, 3, 112, 112]` antes de importar. |
| La calibración no ve los extremos 0 y 255 (EB) | En la placa, `npu_smoke` da en la entrada una `scale` menor que ≈ 0.00784 o un `zero_point` lejos de 128, y el servicio se niega a arrancar porque el rango no cubre [−1, 1] | Anota la escala y el zero point. No hay solución verificada. EA no tiene este problema, porque su entrada no se cuantiza. **Sugerencia sin probar:** añadir al set de calibración recortes con zonas saturadas. A cambio, se desplazan los rangos de las capas internas. |
| Versión equivocada del toolkit | El contenedor es `ubuntu-npu:v1.8.13`, o el zip es `docker_images_v1.8.x.zip` | La v1.8.x es para la T527, no para la A733 (`radxa:env`, `radxa:rf`). Usa `ubuntu-npu:v2.0.10.2`. El script pasa el mismo `--optimize` con cualquier imagen, así que el nombre del `.nb` no delata el error: cambia la versión de Acuity que lo genera. |
| `pushd: <NAME>: No such file or directory` y `popd: directory stack empty` | Salen en cada script | Son inofensivos ([Cómo funcionan los scripts del zoo](#cómo-funcionan-los-scripts-del-zoo)). No crees el subdirectorio. |
| `ACUITY_PATH` o `VIV_SDK` vacías | El `echo` de comprobación sale vacío, o el script dice `Need to set environment variable ACUITY_PATH` | Para y repórtalo ([Cargar la imagen](#cargar-la-imagen-de-acuity-y-crear-el-contenedor)). No inventes la ruta. |
| `Can not find <NAME>_<q>.quantize` | El export sale | La cuantización de ese tipo no se hizo o falló: repítela una vez y mira su salida. |
| `docker run` dice que el nombre `model-zoo` ya existe | `Conflict. The container name "/model-zoo" is already in use` | Usa el bloque `if docker container inspect ...` de [Cargar la imagen](#cargar-la-imagen-de-acuity-y-crear-el-contenedor), que hace `docker start`. Si monta otra carpeta, `docker rm -f model-zoo` y crearlo otra vez. |
| Enlaces o permisos rotos | `convert_model_env.sh` falla o los scripts no son ejecutables | Estás en `/mnt/c`: mueve el zoo a `~/npu/`. Si los archivos son de `root`, haz el `chown` por el contenedor. |
| Un `.nb` de ejemplo del zoo en la entrega | Aparece `yolov5s_rt_uint8_a733.nb` | Lo trae el zoo en `examples/yolov5/model/` y no es de caras. Por eso la entrega copia por nombre. |
| `git` pide usuario o contraseña al clonar | El `git clone` se queda esperando o falla | Para y repórtalo. Nunca pidas un token en el chat. |
| `convert_to_onnx.py` falla (D2) | Errores de PyTorch o de `torch.onnx._export` | Anota la versión de PyTorch y el error. D2 es opcional. |

## Plantilla de reporte

El agente deja esto en `~/npu/entrega/reporte.md`:

````markdown
# Conversión de modelos NPU: reporte

Fecha: <AAAA-MM-DD>
Laptop: <CPU>, `free -g`: <...>, `df -h ~`: <...>
Docker: <versión>  Imagen: `docker images | grep ubuntu-npu` -> <salida>
ACUITY_PATH=<...>  VIV_SDK=<...>

## Comprobaciones de arranque
- `sha256sum -c SHA256SUMS.descargas`: <OK / qué falló>
- Rama del clon: <...>
- Hashes del zoo, buffalo_sc.zip y w600k_mbf.onnx: <OK / qué falló>

## Variantes
| ID | Archivo | Producida | sha256 | Notas |
|---|---|---|---|---|
| D1 | Retinaface_resnet50_320_uint8_a733.nb | sí/no | ... | iteraciones usadas: 9 o 10 |
| EA | w600k_mbf_a_uint8_a733.nb | ... | ... | ... |
| EB | w600k_mbf_b_uint8_a733.nb | ... | ... | ... |
| (opcionales) | ... | ... | ... | ... |

## Calibración del embedder
- Comprobación con test.jpg: diferencia máxima con la demo en BGR <...> px y en RGB <...> px; puntos dentro de la caja (BGR/RGB): <...>; orden elegido: <BGR/RGB> (o inspección visual, y por qué)
- Imágenes en la lista: <n>; líneas del manifiesto: <n>; recortes: <n>; líneas de dataset.txt: <n>
- `--help` de landmarks_retinaface.py coincide con la interfaz de la guía: sí/no
(sin rutas ni nombres de personas)

## Comandos ejecutados
<los comandos, en orden, con lo que cambió respecto a la guía>

## resultados-conversion.txt
<contenido; con nbinfo, si se usó>

## Lo que falló o difirió de la guía
<paso, error exacto, qué se intentó (máximo dos veces)>

## Estado final
- Archivos en ~/npu/entrega/: <ls -l>
- `git -C ~/npu/AutoList status --short`: <salida>
````

## Fuentes

| Fuente | Qué se tomó |
|---|---|
| `zoo:scripts_model_convert/pegasus_import.sh`, `pegasus_quantize.sh`, `pegasus_export_ovx_nbg.sh` y `pegasus_inference.sh` | Comandos, tipos de cuantización, `--optimize` de la A733 y nombres de archivo |
| `zoo:examples/retinaface/README.md`, `convert_model/config_yml.py`, `convert_model/convert_model_env.sh`, `retinaface_pre.cpp`, `retinaface_post.cpp` y `model_config.h` | Configuración de referencia, export de biubug6, preproceso y postproceso |
| `zoo:examples/mobilenetv2/` y `zoo:examples/yolov5/` | `SCALE` como multiplicador, `REVERSE_CHANNEL = False` en RGB, `onnxsim` y `pcq` |
| `zoo:examples/dataset/widerface_10/dataset.txt` | Set y formato de calibración del detector |
| `radxa:env`, `radxa:zoo`, `radxa:rf`, `radxa:quant` y `radxa:nbinfo` | Imagen Docker, contenedor, log de la A733, KLD y `nbinfo` |
| biubug6 `Pytorch_Retinaface`, commit `b984b4b`: `convert_to_onnx.py`, `detect.py`, `data/config.py` y `models/retinaface.py` | Export a 640, anclas, varianzas y media |
| InsightFace `arcface_onnx.py` y `face_align.py` | Normalización, color y alineación de `w600k_mbf` |
| `repo:orangepi/vision/README.md`, `vision.conf.example`, `src/main.cpp`, `src/process_config.cpp` y `apps/npu_smoke.cpp` | Rutas y nombres de los modelos, código 78, bloque `npu`, validación al arrancar y salida de `npu_smoke` |
| `repo:orangepi/vision/conversion/landmarks_retinaface.py` | Landmarks de calibración (interfaz acordada; su comportamiento real lo dice `--help`) |
| `repo:herramientas/alinear_lfw.sh` y `alinear_lfw.cpp` | Recortes de calibración |
| `repo:docs/HIGH-LEVEL-PLAN.md`, secciones 6.2 y 9 | Decisión de modelos, versión de Acuity, fallback a int16 y LFW |

## Ver también

- [README del servicio de visión](../README.md): el bloque `npu`, el código 78, `npu_smoke` y `herramientas/alinear_lfw.sh`
- [Plan del proyecto](../../../docs/HIGH-LEVEL-PLAN.md), sección 6.2: modelos y conversión
- [AGENTS.md de la raíz](../../../AGENTS.md): reglas de seguridad y datos
