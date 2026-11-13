#include <sys/socket.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>

#include "minitest.hpp"
#include "puente_aula/sistema.hpp"

using namespace puente_aula;
namespace fs = std::filesystem;

namespace {

class Dir {
 public:
  Dir() {
    std::string plantilla = (fs::temp_directory_path() / "puente_aula_sis_XXXXXX").string();
    if (mkdtemp(plantilla.data()) == nullptr) throw std::runtime_error("mkdtemp fallo");
    ruta = plantilla;
  }
  ~Dir() {
    std::error_code ignorado;
    fs::remove_all(ruta, ignorado);
  }
  fs::path archivo(const std::string& nombre, const std::string& contenido) const {
    const fs::path destino = ruta / nombre;
    std::ofstream(destino) << contenido;
    return destino;
  }
  fs::path ruta;
};

bool contiene(const std::string& texto, const std::string& trozo) {
  return texto.find(trozo) != std::string::npos;
}

}  // namespace

TEST(sistema_lee_el_reenvio_de_ip) {
  Dir dir;
  const auto uno = dir.archivo("uno", "1\n");
  const auto cero = dir.archivo("cero", "0\n");
  const auto vacio = dir.archivo("vacio", "");
  const fs::path ausente = dir.ruta / "no_existe";

  EstadoReenvio estado = leer_reenvio(uno, cero);
  CHECK(estado.ipv4);
  CHECK(!estado.ipv6);
  estado = leer_reenvio(cero, uno);
  CHECK(!estado.ipv4);
  CHECK(estado.ipv6);
  estado = leer_reenvio(uno, uno);
  CHECK(estado.ipv4 && estado.ipv6);
  // Ausente o vacio: no reenvia (no se inventa un aviso).
  estado = leer_reenvio(ausente, vacio);
  CHECK(!estado.ipv4 && !estado.ipv6);
}

TEST(sistema_avisa_fuerte_solo_si_reenvia) {
  CHECK(avisos_de_reenvio({false, false}).empty());

  const auto solo4 = avisos_de_reenvio({true, false});
  CHECK_EQ(solo4.size(), static_cast<std::size_t>(1));
  CHECK(contiene(solo4[0], "AVISO"));
  CHECK(contiene(solo4[0], "ip_forward=1"));
  CHECK(contiene(solo4[0], "ENRUTA"));
  CHECK(contiene(solo4[0], "puente_aula.nft"));

  const auto ambos = avisos_de_reenvio({true, true});
  CHECK_EQ(ambos.size(), static_cast<std::size_t>(2));
  CHECK(contiene(ambos[1], "net.ipv6.conf.all.forwarding=1"));
}

TEST(sistema_atar_a_interfaz_vacia_no_hace_nada) {
  std::string error;
  CHECK(atar_a_interfaz(-1, "", error));
  CHECK(error.empty());
}

TEST(sistema_atar_a_interfaz_que_no_existe_da_un_error_claro) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  std::string error;
  CHECK(!atar_a_interfaz(fd, "noexiste0", error));
  CHECK(contiene(error, "noexiste0"));
  CHECK(contiene(error, "no existe"));
  ::close(fd);
}

TEST(sistema_atar_a_lo_funciona_sin_privilegios) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  std::string error;
  CHECK(atar_a_interfaz(fd, "lo", error));
  ::close(fd);
}
