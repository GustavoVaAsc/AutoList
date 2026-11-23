cmake_minimum_required(VERSION 3.16)
project(vision LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)
enable_testing()

# --- Dependencias -----------------------------------------------------------
# OpenCV de Debian por pkg-config (>= 4.6; bookworm trae 4.6, trixie 4.10). Un .so sin
# enlace de desarrollo (p. ej. libopencv_*.so.407 suelto en /usr/lib, sin headers) no entra
# por -l. --as-needed evita que opencv4.pc meta decenas de modulos (viz, cvv...) como DT_NEEDED.
find_package(PkgConfig REQUIRED)
pkg_check_modules(OPENCV REQUIRED IMPORTED_TARGET opencv4>=4.6)
find_package(Threads REQUIRED)
# nlohmann/json: la capa JSON del contrato MQTT (include/vision/json_payload.hpp).
find_package(nlohmann_json 3.11 REQUIRED)
# libmosquitto: el bus MQTT (broker del borde). cpp-httplib: el cliente HTTP (Node-RED).
pkg_check_modules(MOSQUITTO REQUIRED IMPORTED_TARGET libmosquitto)
pkg_check_modules(CPPHTTPLIB REQUIRED IMPORTED_TARGET cpp-httplib)
# OpenSSL (libcrypto): el AES-256-GCM de la respuesta de la captura (captura_firma.cpp). Ya
# estaba en el enlace por cpp-httplib (HTTPS de la galeria); se pide explicito porque
# vision_core lo usa directamente.
find_package(OpenSSL REQUIRED)

# Runtime VIPLite del fabricante. libNBGlinker exporta vip_*; depende de libVIPhal.
# VIPLITE_ROOT (-DVIPLITE_ROOT=... o variable de entorno) apunta a otra instalacion.
set(VIPLITE_ROOT "$ENV{VIPLITE_ROOT}" CACHE PATH "Prefijo del runtime VIPLite (include/ y lib/)")
find_path(VIPLITE_INCLUDE_DIR vip_lite.h HINTS ${VIPLITE_ROOT}/include)
find_library(VIPLITE_LIBRARY NBGlinker HINTS ${VIPLITE_ROOT}/lib)
find_library(VIPHAL_LIBRARY VIPhal HINTS ${VIPLITE_ROOT}/lib)
if(NOT VIPLITE_INCLUDE_DIR OR NOT VIPLITE_LIBRARY OR NOT VIPHAL_LIBRARY)
  message(FATAL_ERROR "VIPLite no encontrado (vip_lite.h, libNBGlinker, libVIPhal)")
endif()

# --- Logica del servicio (sin NPU) ------------------------------------------
add_library(vision_core STATIC
  src/alignment.cpp
  src/captura_firma.cpp
  src/gallery.cpp
  src/head_pose.cpp
  src/hmac_sha256.cpp
  src/image_header.cpp
  src/json_payload.cpp
  src/npu_preprocess.cpp
  src/process_config.cpp
  src/reloj_guardia.cpp
  src/retinaface_decode.cpp
  src/sharpness.cpp
  src/tensor.cpp
  src/thumbnail.cpp
  src/vision_service.cpp
  src/wall_clock.cpp)
target_include_directories(vision_core PUBLIC include)
target_link_libraries(vision_core PUBLIC PkgConfig::OPENCV nlohmann_json::nlohmann_json
                      OpenSSL::Crypto)
target_link_options(vision_core PUBLIC -Wl,--as-needed)

# --- Wrapper de la NPU ------------------------------------------------------
add_library(vision_npu STATIC src/npu.cpp)
target_include_directories(vision_npu SYSTEM PRIVATE ${VIPLITE_INCLUDE_DIR})
target_link_libraries(vision_npu PUBLIC vision_core ${VIPLITE_LIBRARY} ${VIPHAL_LIBRARY})

# --- Adaptadores del detector y el embedder sobre la NPU --------------------
# El detector (RetinaFace) y el embedder (ArcFace) sobre npu::Network. La decodificacion
# de RetinaFace y el preproceso de imagen viven en vision_core: son codigo puro y se
# prueban sin la NPU.
add_library(vision_models STATIC
  src/npu_face_detector.cpp
  src/npu_face_embedder.cpp)
target_link_libraries(vision_models PUBLIC vision_npu vision_core)

# --- Adaptadores reales (MQTT y HTTP) ---------------------------------------
# Biblioteca aparte: vision_core se queda sin dependencias de red, para que las
# pruebas del nucleo sigan siendo rapidas y no dependan del broker.
add_library(vision_transport STATIC
  src/gallery_store.cpp
  src/httplib_client.cpp
  src/mqtt_bus.cpp)
target_include_directories(vision_transport PUBLIC include)
target_link_libraries(vision_transport PUBLIC
  vision_core
  PkgConfig::MOSQUITTO
  PkgConfig::CPPHTTPLIB
  nlohmann_json::nlohmann_json
  Threads::Threads)

# --- Ejecutables ------------------------------------------------------------
# El proceso de produccion: bus MQTT real, cliente HTTP real y galeria real. Solo
# vision_transport (los adaptadores reales); el recorrido en seco se queda en
# vision_core (los dobles en memoria no necesitan red).
add_executable(vision_service src/main.cpp)
target_link_libraries(vision_service PRIVATE vision_transport vision_models)

# Recorrido en seco: la maquina de estados real con los dobles en memoria.
add_executable(vision_dry_run src/dry_run.cpp)
target_link_libraries(vision_dry_run PRIVATE vision_core)

# Herramientas de prueba en la placa.
add_executable(camara_falsa apps/camara_falsa.cpp)
target_link_libraries(camara_falsa PRIVATE PkgConfig::OPENCV PkgConfig::CPPHTTPLIB
                      Threads::Threads)

add_executable(mqtt_probe apps/mqtt_probe.cpp)
target_link_libraries(mqtt_probe PRIVATE PkgConfig::MOSQUITTO Threads::Threads)
target_include_directories(mqtt_probe PRIVATE include)

add_executable(npu_smoke apps/npu_smoke.cpp)
target_link_libraries(npu_smoke PRIVATE vision_npu)

# Diagnostico del detector: NpuFaceDetector sobre un .nb y una imagen.
add_executable(detector_prueba apps/detector_prueba.cpp)
target_link_libraries(detector_prueba PRIVATE vision_models)

# --- Pruebas ----------------------------------------------------------------
add_library(minitest_main STATIC tests/test_main.cpp)
target_include_directories(minitest_main PUBLIC tests)

add_executable(test_core
  tests/test_alignment.cpp
  tests/test_captura_firma.cpp
  tests/test_gallery.cpp
  tests/test_head_pose.cpp
  tests/test_hmac_sha256.cpp
  tests/test_image_header.cpp
  tests/test_json_payload.cpp
  tests/test_mascara_miniatura.cpp
  tests/test_npu_preprocess.cpp
  tests/test_process_config.cpp
  tests/test_reloj_guardia.cpp
  tests/test_retinaface_decode.cpp
  tests/test_sharpness.cpp
  tests/test_tensor.cpp
  tests/test_thumbnail.cpp
  tests/test_wall_clock.cpp)
target_link_libraries(test_core PRIVATE vision_core minitest_main)
# Para que las pruebas lean el vision.conf.example versionado.
target_compile_definitions(test_core PRIVATE VISION_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME core COMMAND test_core)

add_executable(test_service tests/test_service.cpp)
target_link_libraries(test_service PRIVATE vision_core minitest_main Threads::Threads)
add_test(NAME service COMMAND test_service)

# Adaptadores reales: cliente HTTP (servidor httplib en el propio proceso) y galeria.
add_executable(test_httplib_client tests/test_httplib_client.cpp)
target_link_libraries(test_httplib_client PRIVATE vision_transport minitest_main Threads::Threads)
add_test(NAME httplib_client COMMAND test_httplib_client)

add_executable(test_gallery_store tests/test_gallery_store.cpp)
target_link_libraries(test_gallery_store PRIVATE vision_transport minitest_main Threads::Threads)
add_test(NAME gallery_store COMMAND test_gallery_store)

# Bus MQTT: prueba de integracion contra un broker real. Sin broker (variable de
# entorno VISION_BROKER o -DVISION_BROKER=host:puerto) la prueba se omite con un
# mensaje claro y no falla. Se puede excluir con ctest -LE integration.
add_executable(test_mqtt_bus tests/test_mqtt_bus.cpp)
target_link_libraries(test_mqtt_bus PRIVATE vision_transport minitest_main Threads::Threads)
add_test(NAME mqtt_bus COMMAND test_mqtt_bus)
set_tests_properties(mqtt_bus PROPERTIES LABELS integration)
set(VISION_BROKER "" CACHE STRING "Broker MQTT de pruebas (host:puerto); vacio = integracion omitida")
if(VISION_BROKER)
  set_tests_properties(mqtt_bus PROPERTIES ENVIRONMENT "VISION_BROKER=${VISION_BROKER}")
endif()

# Las pruebas de la NPU requieren /dev/vipcore: solo existen en la placa y llevan la
# etiqueta `board` (ctest -L board / ctest -LE board).
add_executable(test_npu tests/test_npu.cpp)
target_link_libraries(test_npu PRIVATE vision_npu minitest_main)

# Los adaptadores del detector y el embedder sobre la NPU, con la red de ejemplo del
# fabricante. Requiere /dev/vipcore: solo existe en la placa.
add_executable(test_npu_adapters tests/test_npu_adapters.cpp)
target_link_libraries(test_npu_adapters PRIVATE vision_models minitest_main)

set(VPM_RUN_DIR /opt/vpm_run)
if(EXISTS /dev/vipcore)
  add_test(NAME npu COMMAND test_npu)
  set_tests_properties(npu PROPERTIES LABELS board)
  add_test(NAME npu_adapters COMMAND test_npu_adapters)
  set_tests_properties(npu_adapters PROPERTIES LABELS board)
  # Humo con la red de ejemplo del fabricante, si esta instalada.
  if(EXISTS ${VPM_RUN_DIR}/network_binary.nb AND EXISTS ${VPM_RUN_DIR}/input_0.dat)
    add_test(NAME npu_smoke_vendor_model
      COMMAND npu_smoke ${VPM_RUN_DIR}/network_binary.nb ${VPM_RUN_DIR}/input_0.dat 20)
    set_tests_properties(npu_smoke_vendor_model PROPERTIES LABELS board)
  endif()
endif()
