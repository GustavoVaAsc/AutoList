// Pruebas de la guardia del reloj del borde (reloj_guardia.hpp): el salto hacia adelante
// medido contra CLOCK_BOOTTIME, el ancla de la primera firma (RTC o archivo de estado) y las
// anclas reales con archivos temporales.

#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "minitest.hpp"
#include "vision/fakes.hpp"
#include "vision/reloj_guardia.hpp"

namespace {

using namespace vision;

constexpr std::int64_t kT = 1793805153;  // 2026-11-04T15:12:33Z, un reloj de pared sano
constexpr std::int64_t kBoot0 = 1000;    // CLOCK_BOOTTIME al evaluar la primera vez

bool contiene(const std::string& texto, const std::string& aguja) {
  return texto.find(aguja) != std::string::npos;
}

}  // namespace

// --- Dentro del proceso: el salto se mide contra BOOTTIME ----------------------------------

TEST(sin_anclas_la_primera_firma_pasa_y_un_salto_de_mas_de_10_minutos_no) {
  GuardiaReloj guardia(nullptr);
  CHECK(guardia.evaluar(kT, kBoot0).permitido);
  CHECK(guardia.evaluar(kT + 5, kBoot0 + 5).permitido);

  const VeredictoReloj salto = guardia.evaluar(kT + 5 + 700, kBoot0 + 5);
  CHECK(!salto.permitido);
  CHECK(contiene(salto.mensaje, "salto hacia adelante"));
  // El mensaje manda a revisar la hora, no a reiniciar sin mas.
  CHECK(contiene(salto.mensaje, "NTP"));
  CHECK(contiene(salto.mensaje, "README"));
  CHECK(!contiene(salto.mensaje, "reinicia el servicio"));
}

// Un rechazo no mueve la referencia: la siguiente peticion se juzga contra la firma buena.
TEST(un_rechazo_no_cambia_la_referencia) {
  GuardiaReloj guardia(nullptr);
  CHECK(guardia.evaluar(kT, kBoot0).permitido);
  CHECK(!guardia.evaluar(kT + 5000, kBoot0 + 1).permitido);
  CHECK(!guardia.evaluar(kT + 5000, kBoot0 + 2).permitido);
  // Si el reloj vuelve a su sitio, retoma solo.
  CHECK(guardia.evaluar(kT + 10, kBoot0 + 10).permitido);
}

TEST(el_margen_de_10_minutos_se_tolera_y_el_borde_se_rechaza) {
  GuardiaReloj guardia(nullptr);
  CHECK(guardia.evaluar(kT, kBoot0).permitido);
  CHECK(guardia.evaluar(kT + 600, kBoot0).permitido);          // justo 600 de mas: pasa
  CHECK(!guardia.evaluar(kT + 600 + 601, kBoot0).permitido);   // 601 de mas respecto de la ultima
}

// Una suspension: BOOTTIME avanza una hora y el reloj monotonico no; la hora de pared tambien.
TEST(una_suspension_no_es_un_salto) {
  GuardiaReloj guardia(nullptr);
  CHECK(guardia.evaluar(kT, kBoot0).permitido);
  // Se midio con BOOTTIME: pared +1 h y BOOTTIME +1 h.
  CHECK(guardia.evaluar(kT + 3600, kBoot0 + 3600).permitido);
}

// --- La primera firma con RTC --------------------------------------------------------------

TEST(la_primera_firma_sin_coincidencia_con_el_rtc_se_rechaza) {
  fakes::FakeAnclasReloj anclas;
  anclas.rtc = kT - 5000;  // el RTC dice otra hora: la del reloj de pared no es de fiar
  GuardiaReloj guardia(&anclas);

  const VeredictoReloj v = guardia.evaluar(kT, kBoot0);
  CHECK(!v.permitido);
  CHECK(contiene(v.mensaje, "RTC"));
  CHECK(contiene(v.mensaje, "5000"));
  CHECK(contiene(v.mensaje, "NTP"));
  CHECK(!contiene(v.mensaje, "reinicia el servicio"));
  CHECK_EQ(anclas.escrituras, 0);  // no se persiste lo que no se firma
}

TEST(la_primera_firma_con_el_rtc_de_acuerdo_pasa_y_deja_el_estado) {
  fakes::FakeAnclasReloj anclas;
  anclas.rtc = kT + 400;  // dentro de 10 minutos
  GuardiaReloj guardia(&anclas);

  const VeredictoReloj v = guardia.evaluar(kT, kBoot0);
  CHECK(v.permitido);
  CHECK(v.mensaje.empty());
  CHECK(anclas.estado.has_value());
  const auto estado = leer_estado_reloj(*anclas.estado);
  CHECK(estado.has_value());
  CHECK_EQ(estado->ts, kT);
  CHECK_EQ(estado->boottime_s, kBoot0);
  CHECK_EQ(estado->boot_id, *anclas.boot);
}

// Un RTC sin pila (1970) o imposible no es una referencia: se cae al archivo de estado.
TEST(un_rtc_implausible_se_ignora) {
  fakes::FakeAnclasReloj anclas;
  anclas.rtc = 12;
  GuardiaReloj guardia(&anclas);
  const VeredictoReloj v = guardia.evaluar(kT, kBoot0);
  CHECK(v.permitido);
  CHECK(contiene(v.mensaje, "AVISO"));  // sin RTC ni estado
}

// Un salto hacia adelante que el RTC confirma (NTP que corrige) se acepta sin reiniciar.
TEST(un_salto_que_el_rtc_confirma_se_acepta) {
  fakes::FakeAnclasReloj anclas;
  anclas.rtc = kT;
  GuardiaReloj guardia(&anclas);
  CHECK(guardia.evaluar(kT, kBoot0).permitido);

  anclas.rtc = kT + 3000;  // el RTC tambien se corrigio
  CHECK(guardia.evaluar(kT + 3000, kBoot0 + 1).permitido);
  // Y la referencia queda en el nuevo valor.
  CHECK(guardia.evaluar(kT + 3005, kBoot0 + 6).permitido);
}

TEST(un_salto_que_el_rtc_no_confirma_se_rechaza_aunque_haya_rtc) {
  fakes::FakeAnclasReloj anclas;
  anclas.rtc = kT;
  GuardiaReloj guardia(&anclas);
  CHECK(guardia.evaluar(kT, kBoot0).permitido);

  const VeredictoReloj v = guardia.evaluar(kT + 3000, kBoot0 + 1);  // el RTC sigue en kT
  CHECK(!v.permitido);
  CHECK(contiene(v.mensaje, "RTC de hardware no lo confirma"));
}

// --- Sin RTC: el archivo de estado ---------------------------------------------------------

TEST(sin_rtc_ni_estado_se_permite_con_un_aviso_fuerte_y_se_crea_el_estado) {
  fakes::FakeAnclasReloj anclas;  // sin RTC ni estado: primera instalacion
  GuardiaReloj guardia(&anclas);
  const VeredictoReloj v = guardia.evaluar(kT, kBoot0);
  CHECK(v.permitido);
  CHECK(contiene(v.mensaje, "AVISO"));
  CHECK(contiene(v.mensaje, "primera instalacion"));
  CHECK(contiene(v.mensaje, "403"));
  CHECK(anclas.estado.has_value());
  // La segunda firma ya no avisa.
  CHECK(guardia.evaluar(kT + 1, kBoot0 + 1).mensaje.empty());
}

TEST(un_estado_corrupto_se_permite_con_un_aviso_y_se_reescribe_bien) {
  const char* corruptos[] = {"basura", "", "v1 123", "v1 123 aaaa 5 extra", "v2 123 aaaa 5",
                             "v1 -5 aaaa 5", "v1 123 AAAA 5", "v1 12x aaaa 5"};
  for (const char* contenido : corruptos) {
    fakes::FakeAnclasReloj anclas;
    anclas.estado = contenido;
    GuardiaReloj guardia(&anclas);
    const VeredictoReloj v = guardia.evaluar(kT, kBoot0);
    CHECK(v.permitido);
    CHECK(contiene(v.mensaje, "corrupto"));
    CHECK(leer_estado_reloj(*anclas.estado).has_value());  // quedo un estado valido
  }
}

// Reinicio del proceso en el MISMO arranque: BOOTTIME sigue corriendo y el estado lo ancla.
TEST(el_estado_ancla_un_reinicio_del_servicio_en_el_mismo_arranque) {
  fakes::FakeAnclasReloj anclas;
  {
    GuardiaReloj guardia(&anclas);
    CHECK(guardia.evaluar(kT, kBoot0).permitido);
  }
  // El servicio se reinicia 100 s despues, con la hora normal.
  {
    GuardiaReloj guardia(&anclas);
    CHECK(guardia.evaluar(kT + 100, kBoot0 + 100).permitido);
  }
  // Otro reinicio en el que la hora salto una hora (NTP suplantado): no hay RTC que lo confirme.
  {
    GuardiaReloj guardia(&anclas);
    const VeredictoReloj v = guardia.evaluar(kT + 3700, kBoot0 + 100 + 60);
    CHECK(!v.permitido);
    CHECK(contiene(v.mensaje, "este arranque"));
    CHECK(contiene(v.mensaje, "NTP"));
  }
}

// Reinicio de la placa (otro boot_id): la hora debe ser al menos la del ts guardado.
TEST(el_estado_ancla_un_reinicio_del_equipo) {
  fakes::FakeAnclasReloj anclas;
  {
    GuardiaReloj guardia(&anclas);
    CHECK(guardia.evaluar(kT, kBoot0).permitido);
    CHECK(guardia.evaluar(kT + 600, kBoot0 + 600).permitido);
  }
  anclas.boot = "bbbbbbbb-0000-0000-0000-000000000002";  // el equipo se reinicio

  // Arranco con una hora vieja (sin NTP aun): anterior a lo firmado, se rechaza.
  {
    GuardiaReloj guardia(&anclas);
    const VeredictoReloj v = guardia.evaluar(kT - 5000, 40);
    CHECK(!v.permitido);
    CHECK(contiene(v.mensaje, "anterior a la ultima firma"));
  }
  // La hora vuelve a ser correcta (mayor que lo guardado): pasa, aunque BOOTTIME no encaje.
  {
    GuardiaReloj guardia(&anclas);
    CHECK(guardia.evaluar(kT + 7200, 90).permitido);
    // Y el estado nuevo lleva el boot_id nuevo.
    CHECK_EQ(leer_estado_reloj(*anclas.estado)->boot_id, *anclas.boot);
  }
}

// El piso persistido es el mayor ts firmado: no baja aunque una firma posterior sea menor.
TEST(el_piso_persistido_no_baja) {
  fakes::FakeAnclasReloj anclas;
  GuardiaReloj guardia(&anclas);
  CHECK(guardia.evaluar(kT + 1000, kBoot0).permitido);
  CHECK(guardia.evaluar(kT, kBoot0 + 40).permitido);  // el reloj retrocedio (la guardia solo mira adelante)
  CHECK_EQ(leer_estado_reloj(*anclas.estado)->ts, kT + 1000);
}

TEST(el_estado_se_escribe_como_mucho_cada_30_segundos) {
  fakes::FakeAnclasReloj anclas;
  GuardiaReloj guardia(&anclas);
  for (int i = 0; i < 10; ++i) CHECK(guardia.evaluar(kT + i, kBoot0 + i).permitido);
  CHECK_EQ(anclas.escrituras, 1);
  CHECK(guardia.evaluar(kT + 30, kBoot0 + 30).permitido);
  CHECK_EQ(anclas.escrituras, 2);
}

// Si el estado no se puede guardar, se firma igual y se avisa una sola vez.
TEST(si_no_se_puede_guardar_el_estado_se_avisa_una_vez) {
  fakes::FakeAnclasReloj anclas;
  anclas.se_puede_guardar = false;
  GuardiaReloj guardia(&anclas);
  const VeredictoReloj primero = guardia.evaluar(kT, kBoot0);
  CHECK(primero.permitido);
  CHECK(contiene(primero.mensaje, "no se pudo guardar el estado del reloj"));
  CHECK(guardia.evaluar(kT + 40, kBoot0 + 40).mensaje.empty());

  fakes::FakeAnclasReloj sin_boot_id;
  sin_boot_id.boot = std::nullopt;
  GuardiaReloj otra(&sin_boot_id);
  CHECK(contiene(otra.evaluar(kT, kBoot0).mensaje, "no se pudo guardar el estado del reloj"));
}

TEST(el_estado_se_serializa_y_se_lee_de_vuelta) {
  const EstadoReloj estado{kT, "aaaaaaaa-1111-2222-3333-444444444444", 987654};
  const std::string texto = serializar_estado(estado);
  CHECK_EQ(texto, std::string("v1 1793805153 aaaaaaaa-1111-2222-3333-444444444444 987654\n"));
  const auto leido = leer_estado_reloj(texto);
  CHECK(leido.has_value());
  CHECK_EQ(leido->ts, estado.ts);
  CHECK_EQ(leido->boot_id, estado.boot_id);
  CHECK_EQ(leido->boottime_s, estado.boottime_s);
}

// --- Las anclas reales, con archivos temporales ---------------------------------------------

namespace {

struct DirectorioTemporal {
  std::filesystem::path ruta;
  DirectorioTemporal() {
    char plantilla[] = "/tmp/vision-reloj-XXXXXX";
    ruta = ::mkdtemp(plantilla);
  }
  ~DirectorioTemporal() {
    std::error_code ignorado;
    std::filesystem::remove_all(ruta, ignorado);
  }
  std::string en(const std::string& nombre) const { return (ruta / nombre).string(); }
};

void escribir(const std::string& ruta, const std::string& contenido) {
  std::ofstream(ruta) << contenido;
}

}  // namespace

TEST(las_anclas_del_sistema_leen_el_rtc_y_el_boot_id) {
  DirectorioTemporal dir;
  escribir(dir.en("rtc"), "1793805153\n");
  escribir(dir.en("boot"), "be618e5d-59b5-4b48-8639-822d7f3825ef\n");
  AnclasRelojSistema anclas(dir.en("estado"), dir.en("rtc"), dir.en("boot"));
  CHECK_EQ(*anclas.rtc_epoch_seconds(), std::int64_t{1793805153});
  CHECK_EQ(*anclas.boot_id(), std::string("be618e5d-59b5-4b48-8639-822d7f3825ef"));
}

TEST(las_anclas_del_sistema_rechazan_un_rtc_o_un_boot_id_ilegibles) {
  DirectorioTemporal dir;
  AnclasRelojSistema ausentes(dir.en("estado"), dir.en("no-hay"), dir.en("no-hay"));
  CHECK(!ausentes.rtc_epoch_seconds().has_value());
  CHECK(!ausentes.boot_id().has_value());

  escribir(dir.en("rtc"), "no es un numero");
  escribir(dir.en("boot"), "con espacios y MAYUSCULAS");
  AnclasRelojSistema mal(dir.en("estado"), dir.en("rtc"), dir.en("boot"));
  CHECK(!mal.rtc_epoch_seconds().has_value());
  CHECK(!mal.boot_id().has_value());
  escribir(dir.en("rtc"), "-5");
  CHECK(!mal.rtc_epoch_seconds().has_value());
}

TEST(el_estado_del_sistema_se_guarda_atomico_con_modo_0600_y_se_lee) {
  DirectorioTemporal dir;
  AnclasRelojSistema anclas(dir.en("estado"), dir.en("rtc"), dir.en("boot"));
  CHECK(!anclas.leer_estado().has_value());  // no existe
  CHECK(anclas.guardar_estado("v1 1 aaaa 2\n"));
  CHECK_EQ(*anclas.leer_estado(), std::string("v1 1 aaaa 2\n"));
  CHECK(anclas.guardar_estado("v1 3 bbbb 4\n"));  // reemplaza
  CHECK_EQ(*anclas.leer_estado(), std::string("v1 3 bbbb 4\n"));

  struct stat info {};
  CHECK_EQ(::stat(dir.en("estado").c_str(), &info), 0);
  CHECK_EQ(static_cast<int>(info.st_mode & 0777), 0600);
  CHECK(!std::filesystem::exists(dir.en("estado.tmp")));  // sin temporales sobrantes
}

TEST(guardar_el_estado_falla_sin_escritura_o_sin_ruta) {
  DirectorioTemporal dir;
  AnclasRelojSistema sin_directorio(dir.en("no/existe/estado"));
  CHECK(!sin_directorio.guardar_estado("x"));
  AnclasRelojSistema sin_ruta("");
  CHECK(!sin_ruta.guardar_estado("x"));
  CHECK(!sin_ruta.leer_estado().has_value());
}

// De punta a punta con archivos reales: dos "procesos" y un "reinicio" del equipo.
TEST(la_guardia_con_archivos_reales_sobrevive_a_un_reinicio) {
  DirectorioTemporal dir;
  escribir(dir.en("boot"), "aaaaaaaa-0000-0000-0000-000000000001\n");
  {
    AnclasRelojSistema anclas(dir.en("estado"), dir.en("sin-rtc"), dir.en("boot"));
    GuardiaReloj guardia(&anclas);
    CHECK(guardia.evaluar(kT, kBoot0).permitido);
  }
  escribir(dir.en("boot"), "bbbbbbbb-0000-0000-0000-000000000002\n");  // otro arranque
  AnclasRelojSistema anclas(dir.en("estado"), dir.en("sin-rtc"), dir.en("boot"));
  GuardiaReloj vieja(&anclas);
  CHECK(!vieja.evaluar(kT - 3600, 30).permitido);
  GuardiaReloj buena(&anclas);
  CHECK(buena.evaluar(kT + 3600, 31).permitido);
}
