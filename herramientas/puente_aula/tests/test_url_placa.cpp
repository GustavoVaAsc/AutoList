#include "minitest.hpp"
#include "puente_aula/url_placa.hpp"

using namespace puente_aula;

namespace {

bool valida(const std::string& url, std::string& host, int& puerto) {
  std::string error;
  return analizar_url_placa(url, host, puerto, error);
}

}  // namespace

TEST(url_placa_validas) {
  std::string host;
  int puerto = 0;
  CHECK(valida("http://192.0.2.50", host, puerto));
  CHECK_EQ(host, std::string("192.0.2.50"));
  CHECK_EQ(puerto, 80);
  CHECK(valida("http://192.0.2.50:8080/", host, puerto));
  CHECK_EQ(puerto, 8080);
  CHECK(valida("http://[fd00::5]:9000", host, puerto));
  CHECK_EQ(host, std::string("fd00::5"));
  CHECK_EQ(puerto, 9000);
  CHECK(valida("http://[fd00::5]", host, puerto));
  CHECK_EQ(puerto, 80);
}

TEST(url_placa_invalidas) {
  std::string host;
  int puerto = 0;
  CHECK(!valida("", host, puerto));
  CHECK(!valida("https://192.0.2.50", host, puerto));
  CHECK(!valida("192.0.2.50", host, puerto));
  CHECK(!valida("http://placa.local", host, puerto));            // sin nombres
  CHECK(!valida("http://192.0.2.50/capture", host, puerto));     // el camino es fijo
  CHECK(!valida("http://192.0.2.50/capture?res=SVGA", host, puerto));
  CHECK(!valida("http://usuario@192.0.2.50", host, puerto));
  CHECK(!valida("http://192.0.2.50:0", host, puerto));
  CHECK(!valida("http://192.0.2.50:70000", host, puerto));
  CHECK(!valida("http://192.0.2.50:abc", host, puerto));
  CHECK(!valida("http://192.0.2.50:", host, puerto));
  CHECK(!valida("http://[fd00::5", host, puerto));
}
