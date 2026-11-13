#pragma once

// La respuesta 200 de la placa va CIFRADA y autenticada (solo el 200; los errores van en claro):
//
//   K    = HMAC-SHA256(clave = secreto, mensaje = "AUTOLIST-CAPTURA-RESP-AES256GCM-V1")  (32 bytes)
//   IV   = 12 bytes aleatorios de la placa, en la cabecera X-Captura-IV (24 hex minuscula)
//   AAD  = "RESP\n<ts>\n<nonce>"  (decimal canonico, sin salto final; ts y nonce de la PETICION)
//   cuerpo = AES-256-GCM(K, IV, AAD, foto) || etiqueta de 16 bytes   (Content-Type:
//            application/octet-stream)
//
// El AAD ata la respuesta a su peticion: una foto capturada para otro ts/nonce no descifra. Quien
// tiene el secreto (probar_captura, el servicio de vision) descifra y verifica; el puente solo
// reenvia el IV y el cuerpo, sin conocer el secreto.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "puente_aula/cliente_http.hpp"

namespace puente_aula {

inline constexpr std::size_t kLongitudClave = 32;
inline constexpr std::size_t kLongitudIv = 12;
inline constexpr std::size_t kLongitudEtiqueta = 16;

using ClaveRespuesta = std::array<std::uint8_t, kLongitudClave>;
using IvRespuesta = std::array<std::uint8_t, kLongitudIv>;

// K = HMAC-SHA256(secreto, "AUTOLIST-CAPTURA-RESP-AES256GCM-V1"). Funcion pura.
ClaveRespuesta derivar_clave_respuesta(std::string_view secreto);

// "RESP\n<ts>\n<nonce>"
std::string aad_respuesta(std::int64_t ts, std::uint64_t nonce);

// AES-256-GCM: devuelve cifrado || etiqueta. Lo usan las pruebas (hace de placa) y nada mas.
std::string cifrar_gcm(const ClaveRespuesta& clave, const IvRespuesta& iv, std::string_view aad,
                       std::string_view texto_plano);

// AES-256-GCM: false si la etiqueta no cuadra (cuerpo, IV o AAD alterados, o clave distinta) o
// si `cifrado_y_etiqueta` es mas corto que la etiqueta. `texto_plano` solo se toca si true: se
// borra (explicit_bzero) lo que tuviera y se reemplaza; en un false queda como estaba.
bool descifrar_gcm(const ClaveRespuesta& clave, const IvRespuesta& iv, std::string_view aad,
                   std::string_view cifrado_y_etiqueta, std::string& texto_plano);

enum class EstadoRespuesta {
  Ok,        // descifra y la etiqueta cuadra: la foto es de la placa y de esta peticion
  Ausente,   // no hay X-Captura-IV (la placa no cifro, o algo en el camino lo quito)
  Invalida   // hay IV pero no cumple la forma, o la etiqueta no cuadra
};

struct RespuestaDescifrada {
  EstadoRespuesta estado = EstadoRespuesta::Ausente;
  std::string texto_plano;  // la foto; solo con Ok
};

// Descifra el cuerpo de un 200 de pedir(). El llamador debe borrar `texto_plano` cuando acabe.
RespuestaDescifrada descifrar_respuesta(std::string_view secreto, std::int64_t ts,
                                        std::uint64_t nonce, const RespuestaCaptura& respuesta);

}  // namespace puente_aula
