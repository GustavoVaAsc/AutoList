#pragma once

// Lectura del secreto compartido (CAPTURA_HMAC_SECRETO) desde un archivo. El secreto solo
// viene de un archivo: nunca de argv (queda en /proc/<pid>/cmdline y en el historial) ni del
// entorno (lo heredan los hijos y aparece en /proc/<pid>/environ). Nada de aqui lo imprime.

#include <sys/types.h>

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>

namespace puente_aula {

class ErrorSecreto : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// Politica de permisos, pura: std::nullopt si el archivo vale; si no, el motivo. Debe ser
// del usuario que ejecuta y no tener permisos para el grupo ni para otros.
std::optional<std::string> motivo_permisos_invalidos(mode_t modo, uid_t dueno, uid_t uid_actual);

// Lee el secreto: abre sin seguir enlaces simbolicos (O_NOFOLLOW), comprueba con fstat sobre el
// mismo descriptor (sin carrera entre la comprobacion y la lectura), exige un archivo regular de hasta 4096 bytes y quita un
// salto de linea final ("\n" o "\r\n"). Lanza ErrorSecreto; los mensajes nunca llevan el
// contenido.
std::string leer_secreto(const std::filesystem::path& ruta);

}  // namespace puente_aula
