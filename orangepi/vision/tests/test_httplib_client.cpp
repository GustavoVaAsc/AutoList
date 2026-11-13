// Pruebas del cliente HTTP real (cpp-httplib): el status, el tope del cuerpo y el
// plazo TOTAL de la peticion. El servidor es el propio cpp-httplib, en el mismo
// proceso: asi se puede hacer un servidor que gotea bytes.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <httplib.h>

#include "gcm_apoyo.hpp"
#include "minitest.hpp"
#include "vision/captura_firma.hpp"
#include "vision/httplib_client.hpp"

namespace {

using namespace vision;
using SteadyClock = std::chrono::steady_clock;

const char* kSecretoDelServidor = "secreto-de-prueba-con-mas-de-32-caracteres";

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
    // Devuelve en el cuerpo lo que llego: las tres cabeceras de la firma y el parametro
    // "res" de la query. El camino /capture es el de la placa.
    server_.Get("/capture", [](const httplib::Request& req, httplib::Response& res) {
      const std::string visto = req.get_header_value("X-Captura-TS") + "|" +
                                req.get_header_value("X-Captura-Nonce") + "|" +
                                req.get_header_value("X-Captura-HMAC") + "|" +
                                req.get_param_value("res");
      res.set_content(visto, "text/plain");
    });
    // Responde con el codigo que pide la query, como las respuestas de la placa.
    server_.Get("/rechazo", [](const httplib::Request& req, httplib::Response& res) {
      res.status = std::atoi(req.get_param_value("codigo").c_str());
      res.set_content("cuerpo que no se debe leer", "text/plain");
    });
    // Hace de placa: cifra la respuesta (AES-256-GCM) con el ts y el nonce de la peticion y
    // pone X-Captura-IV; con ?sinfirma=1 contesta en claro y sin IV. Con ?chunked=1 la manda
    // en trozos de 4 KB sin Content-Length (como el firmware) y con ?grande=1 de ~20 KB.
    server_.Get("/firmada", [](const httplib::Request& req, httplib::Response& res) {
      std::string foto = "foto-de-la-placa";
      if (req.has_param("grande")) foto.assign(20000, 'f');
      if (req.has_param("sinfirma")) {
        res.set_content(foto, "application/octet-stream");
        return;
      }
      const std::string iv_hex = "0a0b0c0d0e0f101112131415";
      const std::vector<std::uint8_t> cifrado = gcm_apoyo::cifrar(
          derivar_clave_respuesta(kSecretoDelServidor), gcm_apoyo::bytes_de_hex(iv_hex),
          aad_respuesta(std::stoll(req.get_header_value(kHeaderCapturaTs)),
                        std::stoull(req.get_header_value(kHeaderCapturaNonce))),
          std::vector<std::uint8_t>(foto.begin(), foto.end()));
      res.set_header(kHeaderCapturaIv, iv_hex);
      const std::string cuerpo(cifrado.begin(), cifrado.end());
      if (!req.has_param("chunked")) {
        res.set_content(cuerpo, "application/octet-stream");
        return;
      }
      res.set_chunked_content_provider(
          "application/octet-stream", [cuerpo](std::size_t offset, httplib::DataSink& sink) {
            const std::size_t n = std::min<std::size_t>(4096, cuerpo.size() - offset);
            sink.write(cuerpo.data() + offset, n);
            if (offset + n >= cuerpo.size()) sink.done();
            return true;
          });
    });
    // Un rechazo con Retry-After (el valor sale de la query).
    server_.Get("/reintentar", [](const httplib::Request& req, httplib::Response& res) {
      res.status = 429;
      if (req.has_param("despues")) res.set_header("Retry-After", req.get_param_value("despues"));
      res.set_content("espera", "text/plain");
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

// Numero de hilos del proceso (los que estan en /proc/self/task).
std::size_t num_hilos() {
  std::size_t count = 0;
  for (const auto& entrada : std::filesystem::directory_iterator("/proc/self/task")) {
    (void)entrada;
    ++count;
  }
  return count;
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
  CHECK(ms < 800);
}

// Al vencer el plazo, el hilo de la peticion se cierra (stop) y se espera (join): no
// queda ningun hilo huerfano. Antes el hilo se desacoplaba (detach) y se quedaba
// colgado para siempre con un servidor que gotea bytes.
TEST(httplib_plazo_vencido_no_deja_hilos_colgados) {
  TestServer server;
  HttplibClient client;
  // Una peticion que va bien deja el servidor con su grupo de hilos ya en marcha: el
  // numero de referencia no cambia por culpa del servidor.
  const HttpResponse ok = client.get(server.url("/ok"), Millis(2000), 1024);
  CHECK_EQ(ok.status, 200);
  const std::size_t antes = num_hilos();

  const SteadyClock::time_point start = SteadyClock::now();
  const HttpResponse response = client.get(server.url("/goteo"), Millis(300), 1024);
  const long long ms = elapsed_ms(start);
  CHECK_EQ(response.status, 0);
  CHECK(response.body.empty());
  CHECK(ms < 800);

  // El hilo de la peticion se espera (join) antes de volver: al salir de get() no queda
  // ningun hilo huerfano.
  CHECK(num_hilos() <= antes);
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

// El cliente real manda las tres cabeceras de la firma tal como las arma el servicio, y la
// query de la URL (?res=SVGA) viaja intacta: el servidor ve exactamente lo que firma
// la placa.
TEST(httplib_manda_las_cabeceras_de_la_firma_de_la_captura) {
  TestServer server;
  HttplibClient client;
  const std::string secreto = "secreto-de-prueba-con-mas-de-32-caracteres";
  const std::int64_t ts = 1793805153;
  const std::uint64_t nonce = 12345678901234567890ULL;

  const HttpResponse response =
      client.get(server.url("/capture?res=SVGA"), Millis(2000), 1024,
                 cabeceras_captura(secreto, ts, nonce));

  CHECK_EQ(response.status, 200);
  CHECK_EQ(body_text(response),
           std::string("1793805153|12345678901234567890|") + firma_captura(secreto, ts, nonce) +
               "|SVGA");
}

// Sin cabeceras (camara_falsa) la peticion sale igual y el servidor no ve ninguna.
TEST(httplib_sin_cabeceras_no_manda_las_de_la_firma) {
  TestServer server;
  HttplibClient client;
  const HttpResponse response = client.get(server.url("/capture?res=SVGA"), Millis(2000), 1024);
  CHECK_EQ(response.status, 200);
  CHECK_EQ(body_text(response), std::string("|||SVGA"));
}

// Un codigo distinto de 200 sigue siendo status 0 y sin cuerpo (el contrato no cambia),
// pero el codigo queda en rejected_status para el log del servicio.
TEST(httplib_deja_el_codigo_rechazado_para_el_log) {
  TestServer server;
  HttplibClient client;
  for (const int codigo : {401, 403, 429, 503, 404}) {
    const HttpResponse response =
        client.get(server.url("/rechazo?codigo=" + std::to_string(codigo)), Millis(2000), 1024);
    CHECK_EQ(response.status, 0);
    CHECK(response.body.empty());
    CHECK_EQ(response.rejected_status, codigo);
  }
}

// Los demas fallos no se hacen pasar por un codigo de la placa.
TEST(httplib_los_fallos_que_no_son_un_codigo_dejan_rejected_status_en_0) {
  TestServer server;
  HttplibClient client;
  CHECK_EQ(client.get(server.url("/ok"), Millis(2000), 4).rejected_status, 0);  // tope del cuerpo
  CHECK_EQ(client.get("http://127.0.0.1:1/x", Millis(1000), 1024).rejected_status, 0);  // red
  CHECK_EQ(client.get("no-es-una-url", Millis(1000), 1024).rejected_status, 0);
  CHECK_EQ(client.get(server.url("/ok"), Millis(2000), 1024).rejected_status, 0);  // un 200
}

// El cliente real lee X-Captura-IV de un 200 y deja el cuerpo cifrado intacto (cifrado ||
// etiqueta) para que el servicio lo descifre con su ts y su nonce.
TEST(httplib_lee_el_iv_de_la_respuesta_cifrada) {
  TestServer server;
  HttplibClient client;
  const std::int64_t ts = 1793805153;
  const std::uint64_t nonce = 987654321012345678ULL;

  const HttpResponse response =
      client.get(server.url("/firmada"), Millis(2000), 1024,
                 cabeceras_captura(kSecretoDelServidor, ts, nonce));

  CHECK_EQ(response.status, 200);
  CHECK_EQ(response.resp_iv, std::string("0a0b0c0d0e0f101112131415"));
  CHECK_EQ(response.body.size(), std::string("foto-de-la-placa").size() + 16);
  std::vector<std::uint8_t> plano;
  CHECK(descifrar_respuesta(derivar_clave_respuesta(kSecretoDelServidor), response.resp_iv, ts,
                            nonce, response.body.data(), response.body.size(), plano) ==
        ResultadoDescifrado::Ok);
  CHECK_EQ(std::string(plano.begin(), plano.end()), std::string("foto-de-la-placa"));
  // Con otro nonce (otra peticion) la misma respuesta no verifica.
  CHECK(descifrar_respuesta(derivar_clave_respuesta(kSecretoDelServidor), response.resp_iv, ts,
                            nonce + 1, response.body.data(), response.body.size(), plano) ==
        ResultadoDescifrado::NoAutentico);
}

TEST(httplib_un_200_sin_iv_lo_deja_vacio) {
  TestServer server;
  HttplibClient client;
  const HttpResponse response =
      client.get(server.url("/firmada?sinfirma=1"), Millis(2000), 1024,
                 cabeceras_captura(kSecretoDelServidor, 1793805153, 5));
  CHECK_EQ(response.status, 200);
  CHECK(response.resp_iv.empty());
}

// Respuesta chunked (sin Content-Length, trozos de 4 KB), como la manda el firmware: se lee
// entera y max_body_bytes se aplica a cifrado || etiqueta (los 16 bytes de la etiqueta cuentan).
TEST(httplib_lee_la_respuesta_cifrada_en_chunks_y_aplica_el_tope_al_cuerpo_cifrado) {
  TestServer server;
  HttplibClient client;
  const std::size_t total = 20000 + 16;
  const std::string url = server.url("/firmada?grande=1&chunked=1");

  const HttpResponse justo = client.get(url, Millis(3000), total,
                                        cabeceras_captura(kSecretoDelServidor, 1793805153, 21));
  CHECK_EQ(justo.status, 200);
  CHECK_EQ(justo.body.size(), total);
  std::vector<std::uint8_t> plano;
  CHECK(descifrar_respuesta(derivar_clave_respuesta(kSecretoDelServidor), justo.resp_iv,
                            1793805153, 21, justo.body.data(), justo.body.size(), plano) ==
        ResultadoDescifrado::Ok);
  CHECK_EQ(plano.size(), std::size_t{20000});

  const HttpResponse pasado = client.get(url, Millis(3000), total - 1,
                                         cabeceras_captura(kSecretoDelServidor, 1793805154, 22));
  CHECK_EQ(pasado.status, 0);
  CHECK(pasado.body.empty());
}

// Retry-After en segundos enteros; cualquier otra forma (fecha, texto, vacio) se ignora.
TEST(httplib_deja_el_retry_after_de_un_429) {
  TestServer server;
  HttplibClient client;
  const HttpResponse con = client.get(server.url("/reintentar?despues=2"), Millis(2000), 1024);
  CHECK_EQ(con.status, 0);
  CHECK_EQ(con.rejected_status, 429);
  CHECK_EQ(con.retry_after_s, 2);

  CHECK_EQ(client.get(server.url("/reintentar"), Millis(2000), 1024).retry_after_s, 0);
  CHECK_EQ(client.get(server.url("/reintentar?despues=manana"), Millis(2000), 1024).retry_after_s, 0);
  CHECK_EQ(client.get(server.url("/reintentar?despues=-3"), Millis(2000), 1024).retry_after_s, 0);
  CHECK_EQ(client.get(server.url("/reintentar?despues=99999999"), Millis(2000), 1024).retry_after_s, 0);
  // Un 200 nunca lleva Retry-After.
  CHECK_EQ(client.get(server.url("/ok"), Millis(2000), 1024).retry_after_s, 0);
}
