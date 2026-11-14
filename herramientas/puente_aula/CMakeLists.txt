cmake_minimum_required(VERSION 3.16)
project(puente_aula LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(CheckPIESupported)
check_pie_supported(LANGUAGES CXX)

add_compile_options(-Wall -Wextra -Wpedantic -Werror=format-security
                    -fstack-protector-strong -fstack-clash-protection
                    -D_GLIBCXX_ASSERTIONS)
# -fcf-protection (CET) solo existe en x86: en aarch64 el compilador la rechaza.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-fcf-protection" TIENE_CF_PROTECTION)
if(TIENE_CF_PROTECTION)
  add_compile_options(-fcf-protection)
endif()
# _FORTIFY_SOURCE necesita optimizacion: solo en las configuraciones optimizadas.
add_compile_options($<$<NOT:$<CONFIG:Debug>>:-U_FORTIFY_SOURCE>
                    $<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=3>)
add_link_options(-Wl,-z,relro,-z,now)
enable_testing()

# --- Dependencias ------------------------------------------------------------
# cpp-httplib va incluido (third_party/cpp-httplib, ver VERSION): el paquete del sistema
# (0.14.3) tiene CVE-2025-52887 (cabeceras sin limite antes de decidir quien es el origen).
# El hash esperado esta aqui y en VERSION; si el archivo cambia, no se configura.
set(EXPECTED_HTTPLIB_SHA256 "805169049e6a6e9ba7729d7e01d14ced825029c88a277586602a6b38df4d5357")
set(HTTPLIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cpp-httplib)
file(SHA256 ${HTTPLIB_DIR}/httplib.h HTTPLIB_SHA256)
if(NOT HTTPLIB_SHA256 STREQUAL EXPECTED_HTTPLIB_SHA256)
  message(FATAL_ERROR "third_party/cpp-httplib/httplib.h no coincide con el sha256 esperado "
                      "(${EXPECTED_HTTPLIB_SHA256}); encontrado ${HTTPLIB_SHA256}")
endif()
add_library(httplib INTERFACE)
# SYSTEM: los avisos de un archivo de terceros no son nuestros.
target_include_directories(httplib SYSTEM INTERFACE ${HTTPLIB_DIR})
# Limites de lo que un cliente no autenticado puede obligar al servidor a leer. Van aqui, y no
# antes de cada #include, para que todas las unidades de compilacion vean lo mismo (el header
# es todo "inline": valores distintos serian una violacion de ODR).
target_compile_definitions(httplib INTERFACE
  CPPHTTPLIB_HEADER_MAX_COUNT=32
  CPPHTTPLIB_HEADER_MAX_LENGTH=2048
  CPPHTTPLIB_REQUEST_URI_MAX_LENGTH=1024)

find_package(nlohmann_json 3.11 REQUIRED)
find_package(Threads REQUIRED)
# OpenSSL (libcrypto 3.x del sistema): solo para descifrar la foto en probar_captura y en las
# pruebas (AES-256-GCM). El daemon no lo usa y no lo enlaza.
find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)

# --- El HMAC de la placa, compilado desde su fuente (sin copiarlo) -----------
# Es el codigo con el que la XIAO verifica la firma: probar_captura firma con el mismo.
set(XIAO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/xiao)
add_library(hmac_xiao STATIC ${XIAO_DIR}/hmac_sha256.cpp)
target_include_directories(hmac_xiao PUBLIC ${XIAO_DIR})

# --- Nucleo -----------------------------------------------------------------
add_library(puente_nucleo STATIC
  src/cliente_http.cpp
  src/config.cpp
  src/firma_captura.cpp
  src/ip.cpp
  src/jpeg.cpp
  src/reenviador_tcp.cpp
  src/registro.cpp
  src/relay_http.cpp
  src/secreto.cpp
  src/sistema.cpp
  src/url_placa.cpp)
target_include_directories(puente_nucleo PUBLIC include)
target_link_libraries(puente_nucleo PUBLIC
  hmac_xiao httplib nlohmann_json::nlohmann_json Threads::Threads)

# La parte que descifra la respuesta de la placa: separada para que puente_aula no dependa de OpenSSL.
add_library(puente_cripto STATIC src/respuesta_cifrada.cpp)
target_link_libraries(puente_cripto PUBLIC puente_nucleo OpenSSL::Crypto)

# --- Ejecutables ------------------------------------------------------------
add_executable(puente_aula src/main.cpp)
target_link_libraries(puente_aula PRIVATE puente_nucleo)

add_executable(probar_captura src/probar_captura.cpp)
target_link_libraries(probar_captura PRIVATE puente_cripto)

# --- Pruebas ----------------------------------------------------------------
# Todas sobre loopback: ninguna sale a la red.
add_library(minitest_main STATIC tests/test_main.cpp)
target_include_directories(minitest_main PUBLIC tests)

# logica_camara.cpp es el validador REAL de la placa: la prueba de firma comprueba que lo que
# firma probar_captura lo acepta (interoperabilidad, no solo vectores).
add_executable(test_puente
  tests/test_cliente_http.cpp
  tests/test_config.cpp
  tests/test_firma.cpp
  tests/test_ip.cpp
  tests/test_jpeg.cpp
  tests/test_relay_http.cpp
  tests/test_respuesta_cifrada.cpp
  tests/test_reenviador_tcp.cpp
  tests/test_salida_probar.cpp
  tests/test_secreto.cpp
  tests/test_sistema.cpp
  tests/test_url_placa.cpp
  ${XIAO_DIR}/logica_camara.cpp)
target_link_libraries(test_puente PRIVATE puente_cripto minitest_main)
target_compile_definitions(test_puente PRIVATE PUENTE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME puente COMMAND test_puente)
set_tests_properties(puente PROPERTIES TIMEOUT 120)
