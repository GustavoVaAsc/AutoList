// Pruebas de la validacion de la hora del reloj de pared.

#include "minitest.hpp"
#include "vision/wall_clock.hpp"

namespace {

using namespace vision;

TEST(hora_plausible_acepta_una_hora_actual) {
  CHECK(is_plausible_timestamp("2026-11-04T09:12:33.120-06:00"));
  CHECK(is_plausible_timestamp("2000-01-01T00:00:00+00:00"));
}

TEST(hora_plausible_rechaza_anos_anteriores_a_2000) {
  CHECK(!is_plausible_timestamp("1970-01-01T00:00:00.000+00:00"));
  CHECK(!is_plausible_timestamp("1999-12-31T23:59:59.999-06:00"));
}

// El año que se evalúa es el de la hora local escrita en el texto, no el UTC: no se
// convierte la zona.
TEST(hora_plausible_evalua_el_ano_escrito_sin_convertir_la_zona) {
  CHECK(is_plausible_timestamp("2000-01-01T00:00:00.000+06:00"));
  CHECK(!is_plausible_timestamp("1999-12-31T23:59:59.999-06:00"));
}

TEST(hora_plausible_rechaza_zona_sin_dos_puntos_y_longitudes_raras) {
  CHECK(!is_plausible_timestamp("2026-11-04T09:12:33+0600"));
  CHECK(!is_plausible_timestamp("2026-11-04T09:12:33+06:0"));  // 24 caracteres
  CHECK(!is_plausible_timestamp("2026-11-04T09:12:33.-06:00"));
}

TEST(hora_plausible_rechaza_digitos_no_ascii) {
  // Cifras arabes-indicas (UTF-8) en lugar de ASCII.
  CHECK(!is_plausible_timestamp("٢٠٢٦-11-04T09:12:33.120-06:00"));
  CHECK(!is_plausible_timestamp("2026-11-04T09:12:33.120-٠٦:00"));
}

TEST(hora_plausible_rechaza_vacio_y_mal_formada) {
  CHECK(!is_plausible_timestamp(""));
  CHECK(!is_plausible_timestamp("2026-11-04"));
  CHECK(!is_plausible_timestamp("2026-11-04T09:12:33.120"));
}

}  // namespace
