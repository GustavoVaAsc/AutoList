#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "minitest.hpp"
#include "puente_aula/secreto.hpp"

using namespace puente_aula;
namespace fs = std::filesystem;

namespace {

// Un directorio temporal propio, que se borra al salir.
class DirectorioTemporal {
 public:
  DirectorioTemporal() {
    std::string plantilla = (fs::temp_directory_path() / "puente_aula_XXXXXX").string();
    if (mkdtemp(plantilla.data()) == nullptr) throw std::runtime_error("mkdtemp fallo");
    ruta_ = plantilla;
  }
  ~DirectorioTemporal() {
    std::error_code ignorado;
    fs::remove_all(ruta_, ignorado);
  }
  fs::path archivo(const std::string& nombre, const std::string& contenido, mode_t modo) const {
    const fs::path destino = ruta_ / nombre;
    std::ofstream(destino, std::ios::binary) << contenido;
    ::chmod(destino.c_str(), modo);
    return destino;
  }
  const fs::path& ruta() const { return ruta_; }

 private:
  fs::path ruta_;
};

std::string error_de(const fs::path& ruta) {
  try {
    leer_secreto(ruta);
  } catch (const ErrorSecreto& error) {
    return error.what();
  }
  return "";
}

}  // namespace

TEST(secreto_politica_de_permisos) {
  const uid_t yo = 1000;
  CHECK(!motivo_permisos_invalidos(S_IFREG | 0600, yo, yo).has_value());
  CHECK(!motivo_permisos_invalidos(S_IFREG | 0400, yo, yo).has_value());
  CHECK(motivo_permisos_invalidos(S_IFREG | 0600, 0, yo).has_value());      // de otro usuario
  CHECK(motivo_permisos_invalidos(S_IFREG | 0640, yo, yo).has_value());     // grupo lee
  CHECK(motivo_permisos_invalidos(S_IFREG | 0620, yo, yo).has_value());     // grupo escribe
  CHECK(motivo_permisos_invalidos(S_IFREG | 0604, yo, yo).has_value());     // otros leen
  CHECK(motivo_permisos_invalidos(S_IFREG | 0644, yo, yo).has_value());
  CHECK(motivo_permisos_invalidos(S_IFREG | 0660, yo, yo).has_value());
}

TEST(secreto_lee_y_quita_el_salto_de_linea_final) {
  DirectorioTemporal dir;
  CHECK_EQ(leer_secreto(dir.archivo("a", "clave-de-prueba", 0600)), std::string("clave-de-prueba"));
  CHECK_EQ(leer_secreto(dir.archivo("b", "clave-de-prueba\n", 0600)), std::string("clave-de-prueba"));
  CHECK_EQ(leer_secreto(dir.archivo("c", "clave-de-prueba\r\n", 0600)),
           std::string("clave-de-prueba"));
  // Solo se quita UN salto: el resto es parte del secreto.
  CHECK_EQ(leer_secreto(dir.archivo("d", "uno\n\n", 0600)), std::string("uno\n"));
}

TEST(secreto_rechaza_permisos_de_grupo_u_otros_sin_revelar_el_contenido) {
  DirectorioTemporal dir;
  for (const mode_t modo : {static_cast<mode_t>(0640), static_cast<mode_t>(0604),
                            static_cast<mode_t>(0644), static_cast<mode_t>(0660),
                            static_cast<mode_t>(0666)}) {
    const std::string mensaje = error_de(dir.archivo("s", "VALOR-SECRETO-XYZ", modo));
    CHECK(!mensaje.empty());
    CHECK(mensaje.find("permisos") != std::string::npos);
    CHECK(mensaje.find("VALOR-SECRETO-XYZ") == std::string::npos);
  }
}

TEST(secreto_rechaza_vacio_inexistente_directorio_y_enorme) {
  DirectorioTemporal dir;
  CHECK(!error_de(dir.archivo("vacio", "", 0600)).empty());
  CHECK(!error_de(dir.archivo("soloLinea", "\n", 0600)).empty());
  CHECK(!error_de(dir.ruta() / "no_existe").empty());
  CHECK(!error_de(dir.ruta()).empty());
  CHECK(!error_de(dir.archivo("enorme", std::string(5000, 'x'), 0600)).empty());
}

TEST(secreto_no_sigue_enlaces_simbolicos) {
  DirectorioTemporal dir;
  const fs::path real = dir.archivo("real", "clave-de-prueba", 0600);
  const fs::path enlace = dir.ruta() / "enlace";
  fs::create_symlink(real, enlace);
  const std::string mensaje = error_de(enlace);
  CHECK(mensaje.find("enlace simbolico") != std::string::npos);
  CHECK(mensaje.find("clave-de-prueba") == std::string::npos);
  CHECK_EQ(leer_secreto(real), std::string("clave-de-prueba"));  // el real si
}

TEST(secreto_una_fifo_no_deja_colgado_el_arranque) {
  DirectorioTemporal dir;
  const fs::path fifo = dir.ruta() / "fifo";
  CHECK_EQ(::mkfifo(fifo.c_str(), 0600), 0);
  // O_NONBLOCK: abrir un FIFO sin escritor no bloquea; se rechaza por no ser un archivo regular.
  const std::string mensaje = error_de(fifo);
  CHECK(mensaje.find("no es un archivo regular") != std::string::npos);
}
