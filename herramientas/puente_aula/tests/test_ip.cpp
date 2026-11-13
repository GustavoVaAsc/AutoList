// La decision de la lista de permitidos es una funcion pura: se prueba sin red.
#include "apoyo.hpp"
#include "minitest.hpp"
#include "puente_aula/ip.hpp"

using namespace puente_aula;
using apoyo::ip;

TEST(ip_parsea_validas_y_rechaza_el_resto) {
  CHECK(parsear_ip("192.0.2.10").has_value());
  CHECK(parsear_ip("::1").has_value());
  CHECK(parsear_ip("fd00::5").has_value());
  CHECK(!parsear_ip("").has_value());
  CHECK(!parsear_ip("localhost").has_value());          // no se resuelven nombres
  CHECK(!parsear_ip("192.0.2.256").has_value());
  CHECK(!parsear_ip("192.0.2").has_value());
  CHECK(!parsear_ip("192.0.2.1 ").has_value());
  CHECK(!parsear_ip("192.0.2.1/24").has_value());       // sin mascaras: solo IPs exactas
  CHECK(!parsear_ip("fe80::1%eth0").has_value());       // sin zona
  CHECK(!parsear_ip(std::string_view("1.2.3.4\0x", 9)).has_value());
}

TEST(ip_ida_y_vuelta_a_texto) {
  CHECK_EQ(a_texto(ip("192.0.2.10")), std::string("192.0.2.10"));
  CHECK_EQ(a_texto(ip("::1")), std::string("::1"));
}

TEST(ip_comodines) {
  CHECK(es_comodin(ip("0.0.0.0")));
  CHECK(es_comodin(ip("::")));
  CHECK(es_comodin(ip("0:0:0:0:0:0:0:0")));
  CHECK(es_comodin(ip("::ffff:0.0.0.0")));  // se normaliza a IPv4
  CHECK(!es_comodin(ip("127.0.0.1")));
  CHECK(!es_comodin(ip("::1")));
}

TEST(permitida_decide_por_igualdad_exacta) {
  const std::vector<DireccionIp> lista = {ip("198.51.100.7"), ip("fd00::5")};
  CHECK(ip_permitida(ip("198.51.100.7"), lista));
  CHECK(ip_permitida(ip("fd00::5"), lista));
  CHECK(!ip_permitida(ip("198.51.100.8"), lista));
  CHECK(!ip_permitida(ip("198.51.100.0"), lista));
  CHECK(!ip_permitida(ip("fd00::6"), lista));
  CHECK(!ip_permitida(ip("127.0.0.1"), lista));
}

TEST(permitida_lista_vacia_no_deja_pasar_a_nadie) {
  CHECK(!ip_permitida(ip("127.0.0.1"), {}));
  CHECK(!ip_permitida(ip("0.0.0.0"), {}));
}

TEST(permitida_ipv4_mapeada_en_ipv6_equivale_a_ipv4) {
  const std::vector<DireccionIp> lista = {ip("198.51.100.7")};
  CHECK(ip_permitida(ip("::ffff:198.51.100.7"), lista));
  CHECK(!ip_permitida(ip("::ffff:198.51.100.8"), lista));
  // Y al reves: un permitido escrito como mapeado casa con el IPv4 normal.
  CHECK(ip_permitida(ip("198.51.100.7"), {ip("::ffff:198.51.100.7")}));
}

TEST(ip_de_sockaddr_y_de_vuelta) {
  sockaddr_storage almacen;
  const socklen_t largo = a_sockaddr(ip("192.0.2.10"), 1883, almacen);
  const auto de_vuelta = ip_de_sockaddr(reinterpret_cast<const sockaddr*>(&almacen), largo);
  CHECK(de_vuelta.has_value());
  CHECK(*de_vuelta == ip("192.0.2.10"));
  const socklen_t largo6 = a_sockaddr(ip("fd00::5"), 80, almacen);
  CHECK(*ip_de_sockaddr(reinterpret_cast<const sockaddr*>(&almacen), largo6) == ip("fd00::5"));
  CHECK(!ip_de_sockaddr(nullptr, 0).has_value());
}
