// Pruebas del cliente HTTP real (cpp-httplib): el status, el tope del cuerpo y el
// plazo TOTAL de la peticion. El servidor es el propio cpp-httplib, en el mismo
// proceso: asi se puede hacer un servidor que gotea bytes.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include <httplib.h>

#include "minitest.hpp"
#include "vision/httplib_client.hpp"

namespace {

using namespace vision;
using SteadyClock = std::chrono::steady_clock;

// Servidor httplib en el proceso de la prueba.
class TestServer {
 public:
  TestServer() {
    server_.Get("/ok", [](const httplib::Request&, httplib::Response& res) {
      res.set_content("hola vision", "text/plain");
    });
    server_.Get("/con-token", [](const httplib::Request& req, httplib::Response& res) {
      const std::string token = req.get_header_value("X-Galeria-Token");
      if (token != "secreto") {
        res.status = 401;
        res.set_content("sin token", "text/plain");
        return;
      }
      res.set_content("con token", "text/plain");
    });
    server_.Get("/grande", [](const httplib::Request&, httplib::Response& res) {
      // Declara un cuerpo enorme y no lo envia: el cliente debe abortar al ver la
      // cabecera, sin leer el cuerpo.
      res.set_header("Content-Length", "1000000");
      res.set_content("x", "text/plain");
    });
    server_.Get("/goteo", [](const httplib::Request&, httplib::Response& res) {
      res.set_chunked_content_provider(
          "text/plain", [](std::size_t offset, httplib::DataSink& sink) {
            if (offset > 100) return false;
            if (!sink.is_writable()) return false;  // el cliente ya se fue
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            sink.write("a", 1);
            return true;
          });
    });

    port_ = server_.bind_to_any_port("127.0.0.1");
    thread_ = std::thread([this] { server_.listen_after_bind(); });
    wait_until_ready();
  }

  ~TestServer() {
    server_.stop();
    if (thread_.joinable()) thread_.join();
  }

  [[nodiscard]] std::string url(const std::string& path) const {
    return "http://127.0.0.1:" + std::to_string(port_) + path;
  }

 private:
  // Espera a que el servidor acepte conexiones (bind_to_any_port ya deja el socket
  // escuchando, pero listen_after_bind corre en otro hilo).
  void wait_until_ready() {
    for (int intento = 0; intento < 100; ++intento) {
      httplib::Client probe("127.0.0.1", port_);
      probe.set_connection_timeout(0, 100000);
      auto result = probe.Get("/ok");
      if (result) return;
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }

  httplib::Server server_;
  int port_ = 0;
  std::thread thread_;
};

std::string body_text(const HttpResponse& response) {
  return std::string(response.body.begin(), response.body.end());
}

long long elapsed_ms(SteadyClock::time_point start) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(SteadyClock::now() - start).count();
}

}  // namespace

TEST(httplib_200_con_cuerpo) {
  TestServer server;
  HttplibClient client;
  const HttpResponse response = client.get(server.url("/ok"), Millis(2000), 1024);
  CHECK_EQ(response.status, 200);
  CHECK_EQ(body_text(response), std::string("hola vision"));
}

TEST(httplib_404_devuelve_status_0) {
  TestServer server;
  HttplibClient client;
  const HttpResponse response = client.get(server.url("/no-existe"), Millis(2000), 1024);
  CHECK_EQ(response.status, 0);
  CHECK(response.body.empty());
}

TEST(httplib_cuerpo_mayor_al_tope) {
  TestServer server;
  HttplibClient client;
  const HttpResponse response = client.get(server.url("/ok"), Millis(2000), 4);
  CHECK_EQ(response.status, 0);
  CHECK(response.body.empty());
}

TEST(httplib_content_length_enorme_no_lee_el_cuerpo) {
  TestServer server;
  HttplibClient client;
  const SteadyClock::time_point start = SteadyClock::now();
  const HttpResponse response = client.get(server.url("/grande"), Millis(5000), 1024);
  const long long ms = elapsed_ms(start);
  CHECK_EQ(response.status, 0);
  CHECK(response.body.empty());
  // Aborta al ver la cabecera: no espera a que el cuerpo llegue.
  CHECK(ms < 2000);
}

TEST(httplib_plazo_total_no_lo_alarga_un_goteo) {
  TestServer server;
  HttplibClient client;
  const SteadyClock::time_point start = SteadyClock::now();
  const HttpResponse response = client.get(server.url("/goteo"), Millis(300), 1024);
  const long long ms = elapsed_ms(start);
  CHECK_EQ(response.status, 0);
  CHECK(response.body.empty());
  // El servidor gotea un byte cada 100 ms: sin el plazo total, la peticion duraria
  // mas de 10 s.
  CHECK(ms < 2000);
}

TEST(httplib_error_de_red_devuelve_status_0) {
  HttplibClient client;
  // El puerto 1 no tiene nada escuchando: error de conexion.
  const HttpResponse response = client.get("http://127.0.0.1:1/api/galeria", Millis(1000), 1024);
  CHECK_EQ(response.status, 0);
  CHECK(response.body.empty());
}

TEST(httplib_url_invalida_devuelve_status_0) {
  HttplibClient client;
  const HttpResponse response = client.get("no-es-una-url", Millis(1000), 1024);
  CHECK_EQ(response.status, 0);
  CHECK(response.body.empty());
}

TEST(httplib_manda_las_cabeceras) {
  TestServer server;
  HttplibClient client;

  const HttpResponse sin_token = client.get(server.url("/con-token"), Millis(2000), 1024);
  CHECK_EQ(sin_token.status, 0);  // 401 no es 200

  const HttpResponse con_token =
      client.get(server.url("/con-token"), Millis(2000), 1024, {"X-Galeria-Token: secreto"});
  CHECK_EQ(con_token.status, 200);
  CHECK_EQ(body_text(con_token), std::string("con token"));
}
