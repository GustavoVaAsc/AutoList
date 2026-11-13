#include "puente_aula/secreto.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace puente_aula {

namespace {

constexpr std::size_t kTamanoMax = 4096;

}  // namespace

std::optional<std::string> motivo_permisos_invalidos(mode_t modo, uid_t dueno, uid_t uid_actual) {
  if (dueno != uid_actual) return "el archivo no es del usuario que ejecuta";
  if ((modo & (S_IRWXG | S_IRWXO)) != 0) {
    return "el archivo tiene permisos para el grupo o para otros (usa chmod 600)";
  }
  return std::nullopt;
}

std::string leer_secreto(const std::filesystem::path& ruta) {
  const std::string nombre = ruta.string();
  // O_NOFOLLOW: ni un enlace simbolico (apuntaria a un archivo que no se reviso); O_NONBLOCK y
  // O_NOCTTY: un FIFO o un dispositivo no pueden dejar colgado el arranque.
  const int fd = ::open(nombre.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK | O_NOCTTY);
  if (fd < 0) {
    if (errno == ELOOP) {
      throw ErrorSecreto(nombre + ": es un enlace simbolico; usa la ruta del archivo real");
    }
    throw ErrorSecreto(nombre + ": no se puede abrir el archivo del secreto: " +
                       std::strerror(errno));
  }
  struct Cierre {
    int fd;
    ~Cierre() { ::close(fd); }
  } cierre{fd};

  struct stat info {};
  if (::fstat(fd, &info) != 0) {
    throw ErrorSecreto(nombre + ": fstat fallo: " + std::strerror(errno));
  }
  if (!S_ISREG(info.st_mode)) throw ErrorSecreto(nombre + ": no es un archivo regular");
  if (const auto motivo = motivo_permisos_invalidos(info.st_mode, info.st_uid, ::geteuid())) {
    throw ErrorSecreto(nombre + ": " + *motivo);
  }
  if (info.st_size <= 0 || static_cast<std::size_t>(info.st_size) > kTamanoMax) {
    throw ErrorSecreto(nombre + ": el secreto debe tener entre 1 y " + std::to_string(kTamanoMax) +
                       " bytes");
  }

  std::string secreto(static_cast<std::size_t>(info.st_size), '\0');
  std::size_t leidos = 0;
  while (leidos < secreto.size()) {
    const ssize_t n = ::read(fd, secreto.data() + leidos, secreto.size() - leidos);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;
    leidos += static_cast<std::size_t>(n);
  }
  secreto.resize(leidos);
  if (!secreto.empty() && secreto.back() == '\n') secreto.pop_back();
  if (!secreto.empty() && secreto.back() == '\r') secreto.pop_back();
  if (secreto.empty()) throw ErrorSecreto(nombre + ": el archivo del secreto esta vacio");
  return secreto;
}

}  // namespace puente_aula
