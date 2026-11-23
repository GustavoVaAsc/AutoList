#pragma once

// La miniatura del rostro: un JPEG pequeno del rostro detectado, en base64, que viaja
// en el campo "miniatura" del progreso del registro y del reconocido del pase de lista
// (contrato MQTT, seccion 4).
//
// Es efimera por diseno: la vision la calcula, la publica en el mensaje (el de la pose o
// el del reconocido) y no la guarda en ningun sitio (ni disco, ni base de datos, ni
// galeria). Del mensaje la toma Node-RED, que solo la mantiene en memoria (la del
// registro mientras el registro esta abierto, la del pase mientras la sesion sigue
// abierta), y de ahi sale al navegador. El rostro del alumno no se persiste.

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

namespace vision {

// Tamano maximo de la miniatura (contrato MQTT): 160x120.
inline constexpr int kThumbnailWidth = 160;
inline constexpr int kThumbnailHeight = 120;
// Tope del JPEG antes de codificarlo en base64 (el contrato dice "unos 8 KB").
inline constexpr std::size_t kThumbnailMaxBytes = 8 * 1024;

// Base64 estandar (RFC 4648, con relleno '='). Escribe el bloque en base64.
std::string base64_encode(const std::vector<unsigned char>& datos);

// Recorta el rostro de la foto, lo reduce a 160x120 guardando la proporcion y lo
// devuelve como JPEG en base64.
//
// Devuelve nullopt si no hay foto, no hay rostro (un recuadro vacio o no finito) o si
// el recorte se sale de la foto. Si el JPEG no cabe en el tope, se baja la calidad
// hasta que cabe; si aun asi no cabe, no hay miniatura: es mejor no mandar una que
// el contrato no admita.
std::optional<std::string> encode_thumbnail(const cv::Mat& frame, const cv::Rect2f& rostro);

}  // namespace vision
