#include "minitest.hpp"
#include "puente_aula/salida_probar.hpp"

using namespace puente_aula;

TEST(salida_todo_bien_es_0) {
  CHECK_EQ(codigo_de_salida({}), 0);
}

TEST(salida_cada_fallo_tiene_su_codigo) {
  CHECK_EQ(codigo_de_salida({true, false, false, false}), 1);
  CHECK_EQ(codigo_de_salida({false, true, false, false}), 6);
  CHECK_EQ(codigo_de_salida({false, false, true, false}), 5);
  CHECK_EQ(codigo_de_salida({false, false, false, true}), 4);
}

TEST(salida_prioridad_6_sobre_1_sobre_5_sobre_4) {
  CHECK_EQ(codigo_de_salida({true, true, true, true}), 6);
  CHECK_EQ(codigo_de_salida({true, false, true, true}), 1);
  CHECK_EQ(codigo_de_salida({false, false, true, true}), 5);
}

// Con --sin-firma un 200 es el fallo y un 401 el exito; sin esa opcion, al reves.
TEST(salida_estado_esperado_segun_el_modo) {
  CHECK(estado_esperado(true, 401));
  CHECK(!estado_esperado(true, 200));
  CHECK(!estado_esperado(true, 403));
  CHECK(estado_esperado(false, 200));
  CHECK(!estado_esperado(false, 401));
}
