// El relevo HTTP contra una placa falsa: un servidor httplib en el propio proceso, en loopback.
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#include <httplib.h>

#include "apoyo.hpp"
#include "minitest.hpp"
#include "puente_aula/cliente_http.hpp"
#include "puente_aula/firma_captura.hpp"
#include "puente_aula/relay_http.hpp"
#include "puente_aula/respuesta_cifrada.hpp"

using namespace puente_aula;

namespace {

struct Vista {
  std::string target;
  std::string metodo;
  std::multimap<std::string, std::string> cabeceras;
};

// Hace de placa: apunta lo que recibe y contesta con `manejador`.
class FalsaPlaca {
 public:
  FalsaPlaca() {
    servidor_.set_pre_routing_handler([this](const httplib::Request& req, httplib::Response&) {
      Vista vista;
      vista.target = req.target;
      vista.metodo = req.method;
      for (const auto& cabecera : req.headers) vista.cabeceras.insert(cabecera);
      std::lock_guard<std::mutex> cerrojo(mutex_);
      vistas_.push_back(std::move(vista));
      return httplib::Server::HandlerResponse::Unhandled;
    });
    servidor_.Get("/capture", [this](const httplib::Request& req, httplib::Response& res) {
      if (manejador) manejador(req, res);
    });
    puerto_ = servidor_.bind_to_any_port("127.0.0.1");
    hilo_ = std::thread([this] { servidor_.listen_after_bind(); });
    servidor_.wait_until_ready();
  }
  ~FalsaPlaca() {
    servidor_.stop();
    hilo_.join();
  }
  int puerto() const { return puerto_; }
  std::vector<Vista> vistas() {
    std::lock_guard<std::mutex> cerrojo(mutex_);
    return vistas_;
  }

  std::function<void(const httplib::Request&, httplib::Response&)> manejador;

 private:
  httplib::Server servidor_;
  std::thread hilo_;
  int puerto_ = 0;
  std::mutex mutex_;
  std::vector<Vista> vistas_;
};

ConfigHttp config_base(int puerto_placa) {
  ConfigHttp config;
  config.escucha = {apoyo::ip("127.0.0.1"), 0, ""};
  config.permitidos = {apoyo::ip("127.0.0.1")};
  config.upstream = {apoyo::ip("127.0.0.1"), puerto_placa, ""};
  config.max_body_bytes = 1000;
  config.connect_timeout_ms = 1000;
  config.read_timeout_ms = 2000;
  config.total_timeout_ms = 4000;
  return config;
}

// Un relevo en marcha frente a una placa falsa.
struct Montaje {
  explicit Montaje(const std::function<void(ConfigHttp&)>& ajustar = nullptr)
      : relevo([&] {
          ConfigHttp config = config_base(placa.puerto());
          if (ajustar) ajustar(config);
          return config;
        }()) {
    puerto = relevo.iniciar();
  }
  FalsaPlaca placa;
  RelayHttp relevo;
  int puerto = 0;

  std::unique_ptr<httplib::Client> cliente(int segundos = 5) const {
    auto cliente = std::make_unique<httplib::Client>("127.0.0.1", puerto);
    cliente->set_read_timeout(segundos, 0);
    cliente->set_connection_timeout(segundos, 0);
    cliente->set_path_encode(false);
    return cliente;
  }
};

// Un IV valido para X-Captura-IV (12 bytes en hex).
const std::string kIv = "000102030405060708090a0b";

httplib::Headers firma() {
  return {{"X-Captura-TS", "1762344000"},
          {"X-Captura-Nonce", "12345678901234567890"},
          {"X-Captura-HMAC", "e61d70e6e5c5548dc0690515917c34dbba4f66c3c6dfec255767da6b9f7f9a04"}};
}

}  // namespace

TEST(relay_reenvia_query_y_cabeceras_de_firma_y_nada_mas) {
  Montaje montaje;
  montaje.placa.manejador = [](const httplib::Request&, httplib::Response& res) {
    res.set_content("JPEGDATA", "image/jpeg");
  };
  httplib::Headers cabeceras = firma();
  cabeceras.emplace("Authorization", "Bearer secreto-del-cliente");
  cabeceras.emplace("Cookie", "sesion=abc");
  cabeceras.emplace("X-Otro", "valor");
  cabeceras.emplace("X-Forwarded-For", "10.0.0.9");

  auto res = montaje.cliente()->Get("/capture?res=SVGA&x=%41%20b", cabeceras);
  CHECK(res);
  CHECK_EQ(res->status, 200);
  CHECK_EQ(res->body, std::string("JPEGDATA"));
  CHECK_EQ(res->get_header_value("Content-Type"), std::string("image/jpeg"));
  CHECK_EQ(res->get_header_value("Cache-Control"), std::string("no-store"));

  const auto vistas = montaje.placa.vistas();
  CHECK_EQ(vistas.size(), static_cast<std::size_t>(1));
  // La query llega byte a byte (sin decodificar ni recodificar).
  CHECK_EQ(vistas[0].target, std::string("/capture?res=SVGA&x=%41%20b"));
  const auto valor = [&](const char* nombre) {
    const auto it = vistas[0].cabeceras.find(nombre);
    return it == vistas[0].cabeceras.end() ? std::string("<ausente>") : it->second;
  };
  CHECK_EQ(valor("X-Captura-TS"), std::string("1762344000"));
  CHECK_EQ(valor("X-Captura-Nonce"), std::string("12345678901234567890"));
  CHECK_EQ(valor("X-Captura-HMAC"),
           std::string("e61d70e6e5c5548dc0690515917c34dbba4f66c3c6dfec255767da6b9f7f9a04"));
  CHECK_EQ(valor("Authorization"), std::string("<ausente>"));
  CHECK_EQ(valor("Cookie"), std::string("<ausente>"));
  CHECK_EQ(valor("X-Otro"), std::string("<ausente>"));
  CHECK_EQ(valor("X-Forwarded-For"), std::string("<ausente>"));
}

TEST(relay_sin_query_ni_firma_reenvia_lo_que_hay) {
  Montaje montaje;
  montaje.placa.manejador = [](const httplib::Request&, httplib::Response& res) {
    res.status = 401;
    res.set_content("falta firma", "text/plain");
  };
  auto res = montaje.cliente()->Get("/capture");
  CHECK(res);
  CHECK_EQ(res->status, 401);
  const auto vistas = montaje.placa.vistas();
  CHECK_EQ(vistas[0].target, std::string("/capture"));
  CHECK(vistas[0].cabeceras.find("X-Captura-HMAC") == vistas[0].cabeceras.end());
}

TEST(relay_rechaza_otros_metodos_y_caminos_sin_tocar_la_placa) {
  Montaje montaje;
  montaje.placa.manejador = [](const httplib::Request&, httplib::Response& res) {
    res.set_content("x", "text/plain");
  };
  auto cliente = montaje.cliente();
  const auto estado = [](const httplib::Result& res) { return res ? res->status : -1; };

  CHECK_EQ(estado(cliente->Post("/capture", firma(), "a=b", "text/plain")), 405);
  CHECK_EQ(estado(cliente->Put("/capture", firma(), "a=b", "text/plain")), 405);
  CHECK_EQ(estado(cliente->Delete("/capture", firma())), 405);
  CHECK_EQ(estado(cliente->Head("/capture", firma())), 405);
  CHECK_EQ(estado(cliente->Get("/", firma())), 404);
  CHECK_EQ(estado(cliente->Get("/otra", firma())), 404);
  CHECK_EQ(estado(cliente->Get("/capture/", firma())), 404);
  CHECK_EQ(estado(cliente->Get("/capture2?res=SVGA", firma())), 404);
  CHECK_EQ(estado(cliente->Get("/status", firma())), 404);
  CHECK_EQ(estado(cliente->Post("/otra", firma(), "a=b", "text/plain")), 404);
  auto res = cliente->Post("/capture", firma(), "a=b", "text/plain");
  CHECK_EQ(res->get_header_value("Allow"), std::string("GET"));
  // Ninguna de esas llego a la placa.
  CHECK_EQ(montaje.placa.vistas().size(), static_cast<std::size_t>(0));
}

TEST(relay_devuelve_el_codigo_de_la_placa) {
  Montaje montaje;
  auto cliente = montaje.cliente();
  for (const int estado : {200, 401, 403, 429, 503, 404, 500}) {
    montaje.placa.manejador = [estado](const httplib::Request&, httplib::Response& res) {
      res.status = estado;
      res.set_content("cuerpo " + std::to_string(estado), "text/plain");
    };
    auto res = cliente->Get("/capture?res=SVGA", firma());
    CHECK(res);
    CHECK_EQ(res->status, estado);
    CHECK_EQ(res->body, "cuerpo " + std::to_string(estado));
    CHECK_EQ(res->get_header_value("Content-Type"), std::string("text/plain"));
    // Un 503 de la placa no se confunde con el "ocupado" del relevo.
    CHECK(!res->has_header("X-Puente"));
  }
}

TEST(relay_pasa_retry_after_y_el_iv_y_ninguna_otra_cabecera) {
  Montaje montaje;
  montaje.placa.manejador = [](const httplib::Request&, httplib::Response& res) {
    res.status = 429;
    res.set_header("Retry-After", "7");
    res.set_header("X-Captura-IV", kIv);
    res.set_header("X-Otro", "no-debe-pasar");
    res.set_header("Set-Cookie", "a=b");
    res.set_header("Server", "esp32");
    res.set_content("demasiadas capturas", "text/plain");
  };
  auto res = montaje.cliente()->Get("/capture", firma());
  CHECK(res);
  CHECK_EQ(res->status, 429);
  CHECK_EQ(res->get_header_value("Retry-After"), std::string("7"));
  CHECK_EQ(res->get_header_value("X-Captura-IV"), kIv);
  CHECK(!res->has_header("X-Otro"));
  CHECK(!res->has_header("Set-Cookie"));
  CHECK(!res->has_header("Server"));
}

TEST(relay_sin_esas_cabeceras_no_las_inventa) {
  Montaje montaje;
  montaje.placa.manejador = [](const httplib::Request&, httplib::Response& res) {
    res.set_content("ok", "image/jpeg");
  };
  auto res = montaje.cliente()->Get("/capture", firma());
  CHECK(res);
  CHECK(!res->has_header("Retry-After"));
  CHECK(!res->has_header("X-Captura-IV"));
}

TEST(relay_cuerpo_sobre_el_tope_es_502_por_content_length) {
  Montaje montaje;  // tope: 1000 bytes
  montaje.placa.manejador = [](const httplib::Request&, httplib::Response& res) {
    res.set_content(std::string(3000, 'x'), "image/jpeg");
  };
  auto res = montaje.cliente()->Get("/capture", firma());
  CHECK(res);
  CHECK_EQ(res->status, 502);
  CHECK(res->body.find("xxxx") == std::string::npos);  // nada del cuerpo de la placa
}

TEST(relay_cuerpo_sobre_el_tope_es_502_tambien_sin_content_length) {
  Montaje montaje;
  montaje.placa.manejador = [](const httplib::Request&, httplib::Response& res) {
    res.set_chunked_content_provider("image/jpeg", [](size_t desplazamiento, httplib::DataSink& sink) {
      if (desplazamiento >= 3000) {
        sink.done();
        return true;
      }
      static const std::string trozo(500, 'y');
      sink.write(trozo.data(), trozo.size());
      return true;
    });
  };
  auto res = montaje.cliente()->Get("/capture", firma());
  CHECK(res);
  CHECK_EQ(res->status, 502);
}

TEST(relay_cuerpo_justo_en_el_tope_pasa) {
  Montaje montaje;
  montaje.placa.manejador = [](const httplib::Request&, httplib::Response& res) {
    res.set_content(std::string(1000, 'z'), "image/jpeg");
  };
  auto res = montaje.cliente()->Get("/capture", firma());
  CHECK(res);
  CHECK_EQ(res->status, 200);
  CHECK_EQ(res->body.size(), static_cast<std::size_t>(1000));
}

TEST(relay_placa_que_no_contesta_a_tiempo_es_504) {
  Montaje montaje([](ConfigHttp& config) { config.read_timeout_ms = 300; });
  montaje.placa.manejador = [](const httplib::Request&, httplib::Response& res) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    res.set_content("tarde", "text/plain");
  };
  const auto inicio = std::chrono::steady_clock::now();
  auto res = montaje.cliente()->Get("/capture", firma());
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - inicio)
                      .count();
  CHECK(res);
  CHECK_EQ(res->status, 504);
  // Esperó el plazo de lectura y no a la placa: margenes anchos para una maquina cargada.
  CHECK(ms >= 280);
  CHECK(ms < 2000);
}

TEST(relay_plazo_total_corta_a_una_placa_que_gotea_bytes_y_suelta_el_cerrojo) {
  // Cada trozo llega antes del plazo de lectura (5 s), pero la respuesta entera pasa del plazo
  // total (600 ms): sin el total, el cerrojo de la placa quedaria tomado hasta 30 x 150 ms.
  Montaje montaje([](ConfigHttp& config) {
    config.read_timeout_ms = 5000;
    config.total_timeout_ms = 600;
    config.max_body_bytes = 100000;
  });
  std::atomic<bool> gotear{true};
  montaje.placa.manejador = [&](const httplib::Request&, httplib::Response& res) {
    if (!gotear) {
      res.set_content("rapida", "text/plain");
      return;
    }
    res.set_chunked_content_provider("image/jpeg", [](size_t desplazamiento, httplib::DataSink& sink) {
      if (desplazamiento >= 300) {
        sink.done();
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(150));
      sink.write("0123456789", 10);
      return true;
    });
  };
  const auto inicio = std::chrono::steady_clock::now();
  auto res = montaje.cliente()->Get("/capture", firma());
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - inicio)
                      .count();
  CHECK(res);
  CHECK_EQ(res->status, 504);
  CHECK(ms >= 550);
  CHECK(ms < 3000);

  // El cerrojo se solto: la siguiente peticion no recibe 503.
  gotear = false;
  const auto otra = apoyo::esperar_hasta(
      [&] {
        auto r = montaje.cliente()->Get("/capture", firma());
        return r && r->status == 200;
      },
      5000);
  CHECK(otra);
}

TEST(relay_placa_caida_es_502) {
  apoyo::PuertoCerrado cerrado;
  Montaje montaje([&](ConfigHttp& config) { config.upstream.puerto = cerrado.puerto; });
  auto res = montaje.cliente()->Get("/capture", firma());
  CHECK(res);
  CHECK_EQ(res->status, 502);
}

TEST(relay_redireccion_de_la_placa_es_502_y_no_se_sigue) {
  Montaje montaje;
  montaje.placa.manejador = [](const httplib::Request&, httplib::Response& res) {
    res.status = 302;
    res.set_header("Location", "http://198.51.100.9/");
    res.set_content("mudada", "text/plain");
  };
  auto res = montaje.cliente()->Get("/capture", firma());
  CHECK(res);
  CHECK_EQ(res->status, 502);
  CHECK(!res->has_header("Location"));
}

TEST(relay_una_peticion_a_la_vez_la_concurrente_recibe_503_sin_esperar) {
  Montaje montaje;
  std::mutex mutex;
  std::condition_variable condicion;
  bool dentro = false;
  bool liberar = false;
  montaje.placa.manejador = [&](const httplib::Request&, httplib::Response& res) {
    std::unique_lock<std::mutex> cerrojo(mutex);
    dentro = true;
    condicion.notify_all();
    condicion.wait_for(cerrojo, std::chrono::seconds(3), [&] { return liberar; });
    res.set_content("foto", "image/jpeg");
  };

  int estado_primera = 0;
  std::thread primera([&] {
    auto res = montaje.cliente()->Get("/capture", firma());
    estado_primera = res ? res->status : -1;
  });
  // Si un CHECK falla, el hilo se libera y se une antes de propagar (un hilo sin unir
  // abortaria el proceso).
  struct Salvaguarda {
    std::thread& hilo;
    std::mutex& mutex;
    std::condition_variable& condicion;
    bool& liberar;
    ~Salvaguarda() {
      {
        std::lock_guard<std::mutex> cerrojo(mutex);
        liberar = true;
      }
      condicion.notify_all();
      if (hilo.joinable()) hilo.join();
    }
  } salvaguarda{primera, mutex, condicion, liberar};
  {
    std::unique_lock<std::mutex> cerrojo(mutex);
    CHECK(condicion.wait_for(cerrojo, std::chrono::seconds(3), [&] { return dentro; }));
  }

  const auto inicio = std::chrono::steady_clock::now();
  auto segunda = montaje.cliente()->Get("/capture", firma());
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - inicio)
                      .count();
  CHECK(segunda);
  CHECK_EQ(segunda->status, 503);
  CHECK_EQ(segunda->get_header_value("Retry-After"), std::string("1"));
  CHECK_EQ(segunda->get_header_value("X-Puente"), std::string("ocupado"));
  CHECK(ms < 500);  // al instante, no encolada tras la primera
  // Una peticion que ni siquiera es /capture sigue recibiendo su 404 aunque haya una en curso.
  auto otra = montaje.cliente()->Get("/otra", firma());
  CHECK(otra);
  CHECK_EQ(otra->status, 404);

  {
    std::lock_guard<std::mutex> cerrojo(mutex);
    liberar = true;
  }
  condicion.notify_all();
  primera.join();
  CHECK_EQ(estado_primera, 200);
  // Libre otra vez: la siguiente pasa.
  auto tercera = montaje.cliente()->Get("/capture", firma());
  CHECK(tercera);
  CHECK_EQ(tercera->status, 200);
  CHECK_EQ(montaje.placa.vistas().size(), static_cast<std::size_t>(2));
}

TEST(relay_rechaza_a_los_origenes_no_permitidos_sin_tocar_la_placa) {
  apoyo::CapturaRegistro registro;
  Montaje montaje([](ConfigHttp& config) { config.permitidos = {apoyo::ip("127.0.0.2")}; });
  montaje.placa.manejador = [](const httplib::Request&, httplib::Response& res) {
    res.set_content("foto", "image/jpeg");
  };
  auto res = montaje.cliente(2)->Get("/capture", firma());
  CHECK(res);
  CHECK_EQ(res->status, 403);
  CHECK(res->body.find("foto") == std::string::npos);
  // Tampoco para otros caminos y metodos: el filtro va primero.
  auto otra = montaje.cliente(2)->Post("/otra", firma(), "x", "text/plain");
  CHECK(otra);
  CHECK_EQ(otra->status, 403);
  CHECK(apoyo::esperar_hasta(
      [&] { return registro.todo().find("rechazado=no_permitida") != std::string::npos; }, 1000));
  CHECK_EQ(montaje.placa.vistas().size(), static_cast<std::size_t>(0));
}

TEST(relay_cabeceras_repetidas_o_con_basura_y_query_larga_son_400_locales) {
  Montaje montaje;
  montaje.placa.manejador = [](const httplib::Request&, httplib::Response& res) {
    res.set_content("foto", "image/jpeg");
  };
  auto cliente = montaje.cliente();
  httplib::Headers repetida = firma();
  repetida.emplace("X-Captura-TS", "1762344001");
  auto res = cliente->Get("/capture", repetida);
  CHECK(res);
  CHECK_EQ(res->status, 400);

  httplib::Headers larga = {{"X-Captura-HMAC", std::string(300, 'a')}};
  res = cliente->Get("/capture", larga);
  CHECK(res);
  CHECK_EQ(res->status, 400);

  res = cliente->Get("/capture?res=" + std::string(600, 'S'), firma());
  CHECK(res);
  CHECK_EQ(res->status, 400);
  CHECK_EQ(montaje.placa.vistas().size(), static_cast<std::size_t>(0));

  // Un '#' a mano.
  const int fd = apoyo::conectar_loopback(montaje.puerto);
  CHECK(fd >= 0);
  CHECK(apoyo::enviar_todo(fd, "GET /capture?res=SVGA#frag HTTP/1.1\r\nHost: x\r\n\r\n"));
  // httplib 0.64 ya rechaza el '#' en la linea de peticion (400); con 0.14 lo quitaba y seguia. En
  // los dos casos a la placa no llega un '#'.
  const std::string primera = apoyo::leer_hasta(fd, 12, 2000);
  ::close(fd);
  CHECK(primera == "HTTP/1.1 400" || primera == "HTTP/1.1 200");
  for (const auto& vista : montaje.placa.vistas()) {
    CHECK(vista.target.find('#') == std::string::npos);
  }
}

TEST(relay_registra_una_linea_por_peticion_sin_cuerpos_ni_valores) {
  apoyo::CapturaRegistro registro;
  Montaje montaje;
  montaje.placa.manejador = [](const httplib::Request&, httplib::Response& res) {
    res.status = 429;
    res.set_header("Retry-After", "9");
    res.set_header("X-Captura-IV", kIv);
    res.set_content("CUERPO-DE-LA-PLACA", "text/plain");
  };
  auto res = montaje.cliente()->Get("/capture?res=SVGA", firma());
  CHECK(res);
  CHECK_EQ(res->status, 429);
  CHECK(apoyo::esperar_hasta([&] { return registro.todo().find("estado=429") != std::string::npos; },
                             1000));

  std::size_t lineas_de_peticion = 0;
  for (const auto& linea : registro.lineas()) {
    if (linea.find("estado=") != std::string::npos) ++lineas_de_peticion;
  }
  CHECK_EQ(lineas_de_peticion, static_cast<std::size_t>(1));

  const std::string todo = registro.todo();
  CHECK(todo.find("origen=127.0.0.1") != std::string::npos);
  CHECK(todo.find("ruta=/capture ") != std::string::npos);
  CHECK(todo.find("bytes=18") != std::string::npos);
  CHECK(todo.find(" ms=") != std::string::npos);
  CHECK(todo.find("SVGA") == std::string::npos);                 // ni la query
  CHECK(todo.find("CUERPO-DE-LA-PLACA") == std::string::npos);   // ni el cuerpo
  CHECK(todo.find("e61d70e6e5c5548d") == std::string::npos);     // ni la firma
  CHECK(todo.find("12345678901234567890") == std::string::npos); // ni el nonce
  CHECK(todo.find("1762344000") == std::string::npos);           // ni el ts
  CHECK(todo.find(kIv) == std::string::npos); // ni cabeceras de la respuesta
}

TEST(relay_el_camino_registrado_no_puede_fabricar_lineas) {
  apoyo::CapturaRegistro registro;
  Montaje montaje;
  auto res = montaje.cliente()->Get("/otra%0A%5Bpuente_aula%5D%20falso", firma());
  CHECK(res);
  CHECK_EQ(res->status, 404);
  CHECK(apoyo::esperar_hasta([&] { return registro.todo().find("estado=404") != std::string::npos; },
                             1000));
  for (const auto& linea : registro.lineas()) {
    CHECK(linea.find('\n') == std::string::npos);
    CHECK(linea.find(' ') != std::string::npos);
  }
}

// --- Gramatica de lo que se reenvia de la respuesta -----------------------------------------

TEST(relay_descarta_cabeceras_de_respuesta_que_no_cumplen_su_gramatica) {
  Montaje montaje;
  std::string retry, iv;
  montaje.placa.manejador = [&](const httplib::Request&, httplib::Response& res) {
    res.status = 429;
    res.set_header("Retry-After", retry);
    res.set_header("X-Captura-IV", iv);
    res.set_content("x", "text/plain");
  };
  const auto pedir_con = [&](const std::string& r, const std::string& f) {
    retry = r;
    iv = f;
    auto res = montaje.cliente()->Get("/capture", firma());
    CHECK(res);
    return res;
  };
  auto res = pedir_con("12", kIv);
  CHECK_EQ(res->get_header_value("Retry-After"), std::string("12"));
  CHECK_EQ(res->get_header_value("X-Captura-IV"), kIv);
  for (const std::string& mala : {std::string("abc"), std::string("-1"), std::string("1.5"), std::string("1234567"), std::string("0x10"), std::string("1; evil")}) {
    res = pedir_con(mala, kIv);
    CHECK(!res->has_header("Retry-After"));
  }
  CHECK(pedir_con("999999", kIv)->has_header("Retry-After"));
  for (const std::string& mala :
       {std::string(23, 'a'), std::string(25, 'a'), kIv.substr(0, 23) + "G", std::string(24, 'A'),
        std::string("0x") + std::string(22, 'a'), std::string("iv malo"),
        std::string(64, 'a') /* la forma vieja de la firma HMAC */}) {
    res = pedir_con("1", mala);
    CHECK(!res->has_header("X-Captura-IV"));
    CHECK(res->has_header("Retry-After"));
  }
}

TEST(relay_normaliza_el_content_type_a_jpeg_texto_u_octet_stream) {
  Montaje montaje;
  std::string tipo;
  montaje.placa.manejador = [&](const httplib::Request&, httplib::Response& res) {
    res.set_content("x", tipo);
  };
  const auto tipo_devuelto = [&](const std::string& t) {
    tipo = t;
    auto res = montaje.cliente()->Get("/capture", firma());
    CHECK(res);
    return res->get_header_value("Content-Type");
  };
  CHECK_EQ(tipo_devuelto("application/octet-stream"), std::string("application/octet-stream"));
  CHECK_EQ(tipo_devuelto("image/jpeg"), std::string("image/jpeg"));
  CHECK_EQ(tipo_devuelto("IMAGE/JPEG"), std::string("image/jpeg"));
  CHECK_EQ(tipo_devuelto("text/plain"), std::string("text/plain"));
  CHECK_EQ(tipo_devuelto("text/plain; charset=utf-8"), std::string("text/plain"));
  CHECK_EQ(tipo_devuelto("text/html"), std::string("application/octet-stream"));
  CHECK_EQ(tipo_devuelto("application/x-evil"), std::string("application/octet-stream"));
  CHECK_EQ(tipo_devuelto("image/jpegx"), std::string("application/octet-stream"));
}

// --- La respuesta cifrada de la placa y su descifrado ----------------------------------------

namespace {

const std::string kSecreto = "secreto-de-prueba-con-mas-de-32-caracteres";

IvRespuesta iv_fijo() {
  IvRespuesta iv{};
  for (std::size_t i = 0; i < iv.size(); ++i) iv[i] = static_cast<std::uint8_t>(i);
  return iv;
}

// Una placa que cifra como el firmware: 200 octet-stream con X-Captura-IV, en trozos de 4 KB y sin
// Content-Length. `ts_aad` y `nonce_aad` permiten cifrar para OTRA peticion.
void responder_cifrado(const httplib::Request& req, httplib::Response& res, const std::string& foto,
                       bool con_iv, std::int64_t desplazar_ts = 0) {
  const std::int64_t ts = std::stoll(req.get_header_value("X-Captura-TS"));
  const std::uint64_t nonce = std::stoull(req.get_header_value("X-Captura-Nonce"));
  auto datos = std::make_shared<std::string>(cifrar_gcm(
      derivar_clave_respuesta(kSecreto), iv_fijo(), aad_respuesta(ts + desplazar_ts, nonce), foto));
  if (con_iv) res.set_header("X-Captura-IV", "000102030405060708090a0b");
  res.set_chunked_content_provider("application/octet-stream",
                                   [datos](size_t desplazamiento, httplib::DataSink& sink) {
    if (desplazamiento >= datos->size()) {
      sink.done();
      return true;
    }
    sink.write(datos->data() + desplazamiento, std::min<size_t>(4096, datos->size() - desplazamiento));
    return true;
  });
}

RespuestaCaptura pedir_por_el_relevo(const Montaje& montaje, std::int64_t ts, std::uint64_t nonce) {
  OpcionesCliente opciones;
  opciones.host = "127.0.0.1";
  opciones.puerto = montaje.puerto;
  opciones.max_body_bytes = 100000;
  return pedir(opciones, "/capture?res=SVGA", cabeceras_captura(kSecreto, ts, nonce));
}

}  // namespace

TEST(relay_la_respuesta_cifrada_llega_y_el_cliente_la_descifra) {
  Montaje montaje([](ConfigHttp& config) { config.max_body_bytes = 100000; });
  std::string foto(10000, '\0');
  for (std::size_t i = 0; i < foto.size(); ++i) foto[i] = static_cast<char>(i * 31 + 7);
  montaje.placa.manejador = [&](const httplib::Request& req, httplib::Response& res) {
    responder_cifrado(req, res, foto, true);
  };
  const RespuestaCaptura respuesta = pedir_por_el_relevo(montaje, 1762344000, 12345678901234567890ULL);
  CHECK(respuesta.falla == Falla::Ninguna);
  CHECK_EQ(respuesta.estado, 200);
  CHECK_EQ(respuesta.content_type, std::string("application/octet-stream"));
  CHECK_EQ(respuesta.cuerpo.size(), foto.size() + kLongitudEtiqueta);  // 3 trozos de 4 KB, tal cual
  CHECK(respuesta.cuerpo.substr(0, foto.size()) != foto);              // y cifrada de verdad
  const RespuestaDescifrada ok =
      descifrar_respuesta(kSecreto, 1762344000, 12345678901234567890ULL, respuesta);
  CHECK(ok.estado == EstadoRespuesta::Ok);
  CHECK(ok.texto_plano == foto);
  // Con otro ts, otro nonce u otro secreto, la misma respuesta ya no vale (no se puede repetir).
  CHECK(descifrar_respuesta(kSecreto, 1762344001, 12345678901234567890ULL, respuesta).estado ==
        EstadoRespuesta::Invalida);
  CHECK(descifrar_respuesta(kSecreto, 1762344000, 1, respuesta).estado == EstadoRespuesta::Invalida);
  CHECK(descifrar_respuesta("otro-secreto", 1762344000, 12345678901234567890ULL, respuesta).estado ==
        EstadoRespuesta::Invalida);
}

TEST(relay_respuesta_cifrada_para_otra_peticion_es_invalida) {
  Montaje montaje([](ConfigHttp& config) { config.max_body_bytes = 100000; });
  montaje.placa.manejador = [&](const httplib::Request& req, httplib::Response& res) {
    responder_cifrado(req, res, "foto capturada para otra peticion", true, /*desplazar_ts=*/-1);
  };
  const RespuestaCaptura respuesta = pedir_por_el_relevo(montaje, 1762344000, 42);
  CHECK_EQ(respuesta.estado, 200);
  const RespuestaDescifrada mala = descifrar_respuesta(kSecreto, 1762344000, 42, respuesta);
  CHECK(mala.estado == EstadoRespuesta::Invalida);
  CHECK(mala.texto_plano.empty());
}

TEST(relay_respuesta_con_cuerpo_alterado_en_el_camino_es_invalida) {
  Montaje montaje([](ConfigHttp& config) { config.max_body_bytes = 100000; });
  montaje.placa.manejador = [&](const httplib::Request& req, httplib::Response& res) {
    responder_cifrado(req, res, std::string(500, 'f'), true);
  };
  RespuestaCaptura respuesta = pedir_por_el_relevo(montaje, 1762344000, 42);
  CHECK(descifrar_respuesta(kSecreto, 1762344000, 42, respuesta).estado == EstadoRespuesta::Ok);
  respuesta.cuerpo[100] ^= 1;
  CHECK(descifrar_respuesta(kSecreto, 1762344000, 42, respuesta).estado == EstadoRespuesta::Invalida);
}

TEST(relay_respuesta_sin_iv_o_con_iv_mal_formado) {
  Montaje montaje([](ConfigHttp& config) { config.max_body_bytes = 100000; });
  bool iv_malo = false;
  montaje.placa.manejador = [&](const httplib::Request& req, httplib::Response& res) {
    responder_cifrado(req, res, "foto", false);
    if (iv_malo) res.set_header("X-Captura-IV", "corto");  // llega, pero mal formado
  };
  RespuestaCaptura respuesta = pedir_por_el_relevo(montaje, 1762344000, 42);
  CHECK(descifrar_respuesta(kSecreto, 1762344000, 42, respuesta).estado == EstadoRespuesta::Ausente);
  iv_malo = true;
  // Directo a la placa el IV mal formado se ve y cuenta como invalido...
  OpcionesCliente directo;
  directo.host = "127.0.0.1";
  directo.puerto = montaje.placa.puerto();
  directo.max_body_bytes = 100000;
  respuesta = pedir(directo, "/capture", cabeceras_captura(kSecreto, 1762344000, 43));
  CHECK(respuesta.iv_malformado);
  CHECK(descifrar_respuesta(kSecreto, 1762344000, 43, respuesta).estado == EstadoRespuesta::Invalida);
  // ... y a traves del relevo, que no reenvia lo que no cumple la gramatica, llega como ausente:
  // para quien descifra es lo mismo (no es autentico).
  respuesta = pedir_por_el_relevo(montaje, 1762344000, 44);
  CHECK(descifrar_respuesta(kSecreto, 1762344000, 44, respuesta).estado != EstadoRespuesta::Ok);
}

// --- Abuso antes de la decision por IP -------------------------------------------------------

TEST(relay_cuatro_conexiones_mudas_no_dejan_sin_servicio_a_una_peticion_permitida) {
  Montaje montaje;
  montaje.placa.manejador = [](const httplib::Request&, httplib::Response& res) {
    res.set_content("foto", "image/jpeg");
  };
  std::vector<int> mudas;
  for (int i = 0; i < 4; ++i) mudas.push_back(apoyo::conectar_loopback(montaje.puerto));
  std::this_thread::sleep_for(std::chrono::milliseconds(200));  // que los 4 hilos base las tomen

  const auto inicio = std::chrono::steady_clock::now();
  auto res = montaje.cliente(3)->Get("/capture", firma());
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - inicio)
                      .count();
  for (const int fd : mudas) ::close(fd);
  CHECK(res);
  CHECK_EQ(res->status, 200);
  CHECK(ms < 1500);  // un hilo dinamico la atendio; no espero a que vencieran las mudas (5 s)
}

TEST(relay_la_cola_de_conexiones_esta_acotada) {
  Montaje montaje;
  // 8 hilos como maximo mas 8 en cola = 16 conexiones mudas atendibles; el resto se cierra al
  // aceptarlas (no se acumulan sin limite).
  std::vector<int> mudas;
  for (int i = 0; i < 30; ++i) {
    mudas.push_back(apoyo::conectar_loopback(montaje.puerto));
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  int cerradas = 0;
  for (const int fd : mudas) {
    if (apoyo::espera_cierre(fd, 5)) ++cerradas;
  }
  for (const int fd : mudas) ::close(fd);
  const int abiertas = static_cast<int>(mudas.size()) - cerradas;
  CHECK(abiertas <= 16);
  CHECK(abiertas >= 4);
  CHECK(cerradas >= 10);
}

// --- Interfaz de escucha ---------------------------------------------------------------------

TEST(relay_escucha_atado_a_una_interfaz) {
  Montaje montaje([](ConfigHttp& config) { config.escucha.interfaz = "lo"; });
  montaje.placa.manejador = [](const httplib::Request&, httplib::Response& res) {
    res.set_content("foto", "image/jpeg");
  };
  auto res = montaje.cliente()->Get("/capture", firma());
  CHECK(res);
  CHECK_EQ(res->status, 200);
}

TEST(relay_interfaz_inexistente_impide_arrancar_con_un_mensaje_claro) {
  FalsaPlaca placa;
  ConfigHttp config = config_base(placa.puerto());
  config.escucha.interfaz = "noexiste0";
  RelayHttp relevo(config);
  try {
    relevo.iniciar();
    CHECK(false);
  } catch (const std::runtime_error& error) {
    const std::string mensaje = error.what();
    CHECK(mensaje.find("noexiste0") != std::string::npos);
    CHECK(mensaje.find("interfaz") != std::string::npos);
  }
}

// Una peticion permitida con TODOS los hilos y toda la cola ocupados por conexiones mudas: la
// degradacion esta acotada. O se atiende, o se rechaza enseguida (se cierra la conexion) o espera
// como mucho lo que tardan en vencer las mudas (2 s); el proceso sigue vivo y, al soltarse las
// mudas, el servicio vuelve.
TEST(relay_con_16_mudas_una_peticion_permitida_se_atiende_o_se_rechaza_acotada) {
  Montaje montaje;
  montaje.placa.manejador = [](const httplib::Request&, httplib::Response& res) {
    res.set_content("foto", "image/jpeg");
  };
  std::vector<int> mudas;
  for (int i = 0; i < 16; ++i) {
    mudas.push_back(apoyo::conectar_loopback(montaje.puerto));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(150));

  const auto inicio = std::chrono::steady_clock::now();
  auto res = montaje.cliente(5)->Get("/capture", firma());
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - inicio)
                      .count();
  const bool atendida = res && res->status == 200;
  const bool rechazada = !res;
  CHECK(atendida || rechazada);
  // Acotada: no queda colgada mas de lo que tardan en vencer las mudas (2 s) mas un margen.
  CHECK(ms < 4500);
  for (const int fd : mudas) ::close(fd);

  // El proceso sigue vivo y el servicio vuelve en cuanto se sueltan las mudas.
  const bool vuelve = apoyo::esperar_hasta(
      [&] {
        auto r = montaje.cliente(2)->Get("/capture", firma());
        return r && r->status == 200;
      },
      6000);
  CHECK(vuelve);
}

// SO_REUSEPORT permitiria a otro proceso del usuario enlazar el mismo ip:puerto y repartirse las
// conexiones: un segundo relevo en la misma direccion tiene que fallar.
TEST(relay_un_segundo_relevo_en_la_misma_direccion_no_arranca) {
  Montaje montaje;
  ConfigHttp config = config_base(montaje.placa.puerto());
  config.escucha.puerto = montaje.puerto;  // el mismo ip:puerto, ya ocupado
  RelayHttp segundo(config);
  try {
    segundo.iniciar();
    CHECK(false);
  } catch (const std::runtime_error& error) {
    CHECK(std::string(error.what()).find("no se pudo escuchar") != std::string::npos);
  }
  // El primero no se vio afectado.
  auto res = montaje.cliente()->Get("/otra", firma());
  CHECK(res);
  CHECK_EQ(res->status, 404);
}

TEST(relay_no_termino_inesperadamente_ni_al_detenerlo) {
  Montaje montaje;
  CHECK(!montaje.relevo.termino_inesperadamente());
  montaje.relevo.detener();
  CHECK(!montaje.relevo.termino_inesperadamente());
}
