// Pruebas de la galeria en RAM: el parser tolerante de GET /api/galeria, el descarte
// de lo que no cuadra y el efecto de las altas y bajas del contrato.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "minitest.hpp"
#include "vision/fakes.hpp"
#include "vision/gallery.hpp"
#include "vision/gallery_retry.hpp"
#include "vision/gallery_store.hpp"

namespace {

using namespace vision;

HttpResponse json_response(const std::string& document) {
  HttpResponse response;
  response.status = 200;
  response.body.assign(document.begin(), document.end());
  return response;
}

std::vector<float> e0() { return {1.0f, 0.0f, 0.0f}; }
std::vector<float> e1() { return {0.0f, 1.0f, 0.0f}; }

bool gallery_has(const Gallery& gallery, const std::string& account,
                 std::span<const float> embedding) {
  const std::optional<GalleryMatch> match = gallery.match(embedding);
  return match.has_value() && match->account == account;
}

GalleryStoreConfig config(const std::string& url) {
  GalleryStoreConfig store_config;
  store_config.url = url;
  store_config.token = "token-de-prueba";
  store_config.timeout = Millis(1000);
  return store_config;
}

}  // namespace

TEST(galeria_forma_embeddings) {
  fakes::FakeHttpClient http;
  http.responses.push_back(json_response(
      R"({"embeddings":[{"cuenta":"320068234","embedding":[1,0,0]}]})"));
  Gallery gallery;
  GalleryStore store(gallery, http, config("http://127.0.0.1:1880/api/galeria"));

  CHECK(store.reload());
  CHECK_EQ(gallery.size(), std::size_t{1});
  CHECK(gallery_has(gallery, "320068234", e0()));
}

TEST(galeria_forma_alumnos) {
  fakes::FakeHttpClient http;
  http.responses.push_back(
      json_response(R"({"alumnos":[{"cuenta":"320068234","embedding":[1,0,0]}]})"));
  Gallery gallery;
  GalleryStore store(gallery, http, config("http://127.0.0.1:1880/api/galeria"));

  CHECK(store.reload());
  CHECK_EQ(gallery.size(), std::size_t{1});
  CHECK(gallery_has(gallery, "320068234", e0()));
}

TEST(galeria_forma_arreglo_suelto) {
  fakes::FakeHttpClient http;
  http.responses.push_back(json_response(R"([{"cuenta":"320068234","embedding":[1,0,0]}])"));
  Gallery gallery;
  GalleryStore store(gallery, http, config("http://127.0.0.1:1880/api/galeria"));

  CHECK(store.reload());
  CHECK_EQ(gallery.size(), std::size_t{1});
  CHECK(gallery_has(gallery, "320068234", e0()));
}

TEST(galeria_embedding_null_se_omite) {
  fakes::FakeHttpClient http;
  http.responses.push_back(json_response(
      R"({"embeddings":[{"cuenta":"320068234","embedding":null},{"cuenta":"320068235","embedding":[1,0,0]}]})"));
  Gallery gallery;
  GalleryStore store(gallery, http, config("http://127.0.0.1:1880/api/galeria"));

  CHECK(store.reload());
  CHECK_EQ(gallery.size(), std::size_t{1});
  CHECK(gallery_has(gallery, "320068235", e0()));
}

TEST(galeria_cuenta_invalida_se_descarta) {
  fakes::FakeHttpClient http;
  http.responses.push_back(json_response(
      R"({"embeddings":[{"cuenta":"12345","embedding":[1,0,0]},{"cuenta":"320068234","embedding":[0,1,0]}]})"));
  Gallery gallery;
  GalleryStore store(gallery, http, config("http://127.0.0.1:1880/api/galeria"));

  CHECK(store.reload());
  CHECK_EQ(gallery.size(), std::size_t{1});
  CHECK(gallery_has(gallery, "320068234", e1()));
}

TEST(galeria_dimension_distinta_se_descarta) {
  fakes::FakeHttpClient http;
  http.responses.push_back(json_response(
      R"({"embeddings":[{"cuenta":"320068234","embedding":[1,0,0]},{"cuenta":"320068235","embedding":[1,0,0,0]},{"cuenta":"320068236","embedding":[0,1,0]}]})"));
  Gallery gallery;
  GalleryStore store(gallery, http, config("http://127.0.0.1:1880/api/galeria"));

  CHECK(store.reload());
  // La primera entrada fija la dimension (3): la de 4 se descarta, las demas cargan.
  CHECK_EQ(gallery.size(), std::size_t{2});
  CHECK(gallery_has(gallery, "320068234", e0()));
  CHECK(gallery_has(gallery, "320068236", e1()));
}

TEST(galeria_reload_quita_las_cuentas_que_no_estan) {
  fakes::FakeHttpClient http;
  http.responses.push_back(
      json_response(R"({"embeddings":[{"cuenta":"320068234","embedding":[1,0,0]}]})"));
  Gallery gallery;
  gallery.upsert("320068234", e0());
  gallery.upsert("320068235", e1());
  GalleryStore store(gallery, http, config("http://127.0.0.1:1880/api/galeria"));

  CHECK(store.reload());
  CHECK_EQ(gallery.size(), std::size_t{1});
  CHECK(gallery_has(gallery, "320068234", e0()));
  // La cuenta que el servidor ya no devuelve no queda en la galeria: el unico
  // candidato es el que el servidor si devolvio.
  const std::optional<GalleryMatch> match = gallery.match(e1());
  CHECK(match.has_value());
  CHECK(match->account != "320068235");
}

TEST(galeria_apply_add_recarga) {
  fakes::FakeHttpClient http;
  http.responses.push_back(
      json_response(R"({"embeddings":[{"cuenta":"320068234","embedding":[1,0,0]}]})"));
  http.responses.push_back(json_response(
      R"({"embeddings":[{"cuenta":"320068234","embedding":[1,0,0]},{"cuenta":"320068235","embedding":[0,1,0]}]})"));
  Gallery gallery;
  GalleryStore store(gallery, http, config("http://127.0.0.1:1880/api/galeria"));

  CHECK(store.reload());
  CHECK_EQ(gallery.size(), std::size_t{1});

  store.apply_add("320068235");
  CHECK_EQ(gallery.size(), std::size_t{2});
  CHECK(gallery_has(gallery, "320068235", e1()));
}

TEST(galeria_apply_remove_borra) {
  fakes::FakeHttpClient http;
  Gallery gallery;
  gallery.upsert("320068234", e0());
  gallery.upsert("320068235", e1());
  GalleryStore store(gallery, http, config("http://127.0.0.1:1880/api/galeria"));

  store.apply_remove("320068234");
  CHECK_EQ(gallery.size(), std::size_t{1});
  CHECK(gallery_has(gallery, "320068235", e1()));
}

TEST(galeria_pide_el_token) {
  fakes::FakeHttpClient http;
  http.responses.push_back(
      json_response(R"({"embeddings":[{"cuenta":"320068234","embedding":[1,0,0]}]})"));
  Gallery gallery;
  GalleryStore store(gallery, http, config("http://127.0.0.1:1880/api/galeria"));

  CHECK(store.reload());
  CHECK_EQ(http.requested_urls.size(), std::size_t{1});
  CHECK_EQ(http.requested_urls.front(), std::string("http://127.0.0.1:1880/api/galeria"));
  CHECK_EQ(http.requested_headers.size(), std::size_t{1});
  CHECK_EQ(http.requested_headers.front().size(), std::size_t{1});
  CHECK_EQ(http.requested_headers.front().front(),
           std::string("X-Galeria-Token: token-de-prueba"));
}

TEST(galeria_error_de_red_no_toca_la_galeria) {
  fakes::FakeHttpClient http;
  // Sin respuestas en cola, el doble devuelve status 0 (error de red).
  Gallery gallery;
  gallery.upsert("320068234", e0());
  GalleryStore store(gallery, http, config("http://127.0.0.1:1880/api/galeria"));

  CHECK(!store.reload());
  CHECK_EQ(gallery.size(), std::size_t{1});
  CHECK(gallery_has(gallery, "320068234", e0()));
}

TEST(galeria_ninguna_entrada_utilizable_no_toca_la_galeria) {
  fakes::FakeHttpClient http;
  // La forma es la correcta, pero ninguna entrada sirve: la cuenta no es un no_cuenta
  // valido. La galeria en RAM no se toca: no se pierde lo que ya habia.
  http.responses.push_back(json_response(
      R"({"embeddings":[{"cuenta":"12345","embedding":[1,0,0]}]})"));
  Gallery gallery;
  gallery.upsert("320068234", e0());
  GalleryStore store(gallery, http, config("http://127.0.0.1:1880/api/galeria"));

  CHECK(!store.reload());
  CHECK_EQ(gallery.size(), std::size_t{1});
  CHECK(gallery_has(gallery, "320068234", e0()));
}

TEST(galeria_embedding_vacio_en_todas_no_toca_la_galeria) {
  fakes::FakeHttpClient http;
  // Todos los embeddings vienen vacios: no se puede cargar ninguno y la galeria en RAM
  // se conserva.
  http.responses.push_back(json_response(
      R"({"embeddings":[{"cuenta":"320068234","embedding":[]}]})"));
  Gallery gallery;
  gallery.upsert("320068234", e0());
  GalleryStore store(gallery, http, config("http://127.0.0.1:1880/api/galeria"));

  CHECK(!store.reload());
  CHECK_EQ(gallery.size(), std::size_t{1});
  CHECK(gallery_has(gallery, "320068234", e0()));
}

TEST(galeria_json_invalido_no_toca_la_galeria) {
  fakes::FakeHttpClient http;
  http.responses.push_back(json_response("esto no es json"));
  Gallery gallery;
  gallery.upsert("320068234", e0());
  GalleryStore store(gallery, http, config("http://127.0.0.1:1880/api/galeria"));

  CHECK(!store.reload());
  CHECK_EQ(gallery.size(), std::size_t{1});
}

// La primera entrada no fija la dimension: si es corrupta, la galeria se queda con una
// sola entrada y la dimension equivocada, y cada pase de lista acaba en decision
// "error". Gana la dimension mayoritaria de la respuesta.
TEST(galeria_la_dimension_mayoritaria_gana_a_la_primera_entrada) {
  fakes::FakeHttpClient http;
  // La primera entrada tiene otra dimension; las otras tres, la misma.
  http.responses.push_back(json_response(
      R"({"embeddings":[{"cuenta":"320068234","embedding":[1,0,0,0]},{"cuenta":"320068235","embedding":[0,1,0]},{"cuenta":"320068236","embedding":[1,0,0]},{"cuenta":"320068237","embedding":[0,0,1]}]})"));
  Gallery gallery;
  GalleryStore store(gallery, http, config("http://127.0.0.1:1880/api/galeria"));

  CHECK(store.reload());
  // La entrada de dimension 4 se descarta: la galeria queda con las tres de dimension 3.
  CHECK_EQ(gallery.size(), std::size_t{3});
  CHECK(gallery_has(gallery, "320068235", e1()));
  CHECK(gallery_has(gallery, "320068236", e0()));
}

// El token no puede viajar en claro: con una URL http a un host que no es el
// localhost, la peticion no se hace.
TEST(galeria_no_manda_el_token_por_http_a_un_host_lejano) {
  fakes::FakeHttpClient http;
  Gallery gallery;
  GalleryStore store(gallery, http, config("http://192.168.1.50/api/galeria"));

  CHECK(!store.reload());
  CHECK(http.requested_urls.empty());
}

// Un JSON muy anidado se rechaza sin tumbar el proceso.
TEST(galeria_rechaza_un_json_muy_anidado) {
  fakes::FakeHttpClient http;
  std::string document = R"({"embeddings":)";
  document += std::string(40, '[');
  document += "1";
  document += std::string(40, ']');
  document += "}";
  http.responses.push_back(json_response(document));

  Gallery gallery;
  gallery.upsert("320068234", e0());
  GalleryStore store(gallery, http, config("http://127.0.0.1:1880/api/galeria"));

  CHECK(!store.reload());
  // La galeria en RAM no se toca.
  CHECK_EQ(gallery.size(), std::size_t{1});
}

// --- loaded_ok: la galeria solo es de fiar tras una recarga correcta ------------------------

TEST(galeria_loaded_ok_es_false_antes_de_la_primera_recarga) {
  fakes::FakeHttpClient http;
  Gallery gallery;
  GalleryStore store(gallery, http, config("http://127.0.0.1:1880/api/galeria"));
  CHECK(!store.loaded_ok());
}

TEST(galeria_loaded_ok_es_true_tras_una_recarga_correcta_aunque_la_galeria_venga_vacia) {
  fakes::FakeHttpClient http;
  http.responses.push_back(json_response(R"({"embeddings":[]})"));  // nadie registrado todavia
  Gallery gallery;
  GalleryStore store(gallery, http, config("http://127.0.0.1:1880/api/galeria"));
  CHECK(store.reload());
  CHECK_EQ(gallery.size(), std::size_t{0});
  CHECK(store.loaded_ok());  // vacia pero cargada: es valida
}

TEST(galeria_loaded_ok_pasa_a_false_con_una_recarga_fallida_y_vuelve_con_la_siguiente_buena) {
  fakes::FakeHttpClient http;
  http.responses.push_back(
      json_response(R"({"embeddings":[{"cuenta":"320068234","embedding":[1,0,0]}]})"));
  http.responses.push_back(HttpResponse{});  // error de red (status 0)
  http.responses.push_back(json_response("{no es json"));
  http.responses.push_back(
      json_response(R"({"embeddings":[{"cuenta":"320068234","embedding":[1,0,0]}]})"));
  Gallery gallery;
  GalleryStore store(gallery, http, config("http://127.0.0.1:1880/api/galeria"));

  CHECK(store.reload());
  CHECK(store.loaded_ok());
  CHECK(!store.reload());       // la red fallo
  CHECK(!store.loaded_ok());    // la galeria puede estar desfasada
  CHECK_EQ(gallery.size(), std::size_t{1});  // pero lo que habia no se toca
  CHECK(!store.reload());       // una respuesta que no se entiende tambien
  CHECK(!store.loaded_ok());
  CHECK(store.reload());
  CHECK(store.loaded_ok());
}

TEST(galeria_loaded_ok_es_false_si_el_token_no_puede_viajar) {
  fakes::FakeHttpClient http;
  Gallery gallery;
  GalleryStore store(gallery, http, config("http://galeria.lejana/api/galeria"));
  CHECK(!store.reload());  // http a un host lejano con token: no se manda
  CHECK(!store.loaded_ok());
}

// Una fila descartada es un alumno que falta: la asistencia usa lo cargado, pero la galeria no
// es de fiar para duplicados (loaded_ok false) hasta que se corrija.
TEST(galeria_con_filas_descartadas_se_carga_pero_loaded_ok_es_false) {
  const std::string buena = R"({"cuenta":"320068234","embedding":[1,0,0]})";
  const std::vector<std::string> con_una_descartada = {
      // cuenta invalida
      R"({"embeddings":[)" + buena + R"(,{"cuenta":"123","embedding":[0,1,0]}]})",
      // embedding con un valor que no es numero
      R"({"embeddings":[)" + buena + R"(,{"cuenta":"320068235","embedding":[0,"x",0]}]})",
      // dimension minoritaria (la mayoritaria es 3)
      R"({"embeddings":[)" + buena +
          R"(,{"cuenta":"320068236","embedding":[0,1,0]},{"cuenta":"320068235","embedding":[1,0]}]})",
  };
  for (const std::string& documento : con_una_descartada) {
    fakes::FakeHttpClient http;
    http.responses.push_back(json_response(documento));
    http.responses.push_back(json_response(R"({"embeddings":[)" + buena + "]}"));
    Gallery gallery;
    GalleryStore store(gallery, http, config("http://127.0.0.1:1880/api/galeria"));

    CHECK(store.reload());  // se cargo: la asistencia usa la galeria
    CHECK(gallery.size() >= std::size_t{1});
    CHECK(gallery_has(gallery, "320068234", e0()));
    CHECK(!store.loaded_ok());  // pero falta una fila: el registro queda cerrado
    CHECK(store.reload());      // la fila corregida: vuelve a ser de fiar
    CHECK(store.loaded_ok());
  }
}

TEST(galeria_un_alumno_pendiente_de_rostro_no_cuenta_como_fila_descartada) {
  fakes::FakeHttpClient http;
  http.responses.push_back(json_response(
      R"({"embeddings":[{"cuenta":"320068234","embedding":[1,0,0]},{"cuenta":"320068235","embedding":null}]})"));
  Gallery gallery;
  GalleryStore store(gallery, http, config("http://127.0.0.1:1880/api/galeria"));
  CHECK(store.reload());
  CHECK(store.loaded_ok());
}

// Una fila cuyo embedding lanza EmbeddingError al cargarse (norma cero o no finita) es una fila
// descartada: loaded_ok queda en false.
TEST(galeria_un_embedding_de_ceros_o_no_finito_deja_loaded_ok_en_false) {
  const std::string buena = R"({"cuenta":"320068234","embedding":[1,0,0]})";
  for (const std::string mala : {R"({"cuenta":"320068235","embedding":[0,0,0]})",
                                 R"({"cuenta":"320068235","embedding":[1e39,0,0]})"}) {
    fakes::FakeHttpClient http;
    http.responses.push_back(json_response(R"({"embeddings":[)" + buena + "," + mala + "]}"));
    Gallery gallery;
    GalleryStore store(gallery, http, config("http://127.0.0.1:1880/api/galeria"));
    CHECK(store.reload());
    CHECK_EQ(gallery.size(), std::size_t{1});  // la buena se uso
    CHECK(gallery_has(gallery, "320068234", e0()));
    CHECK(!store.loaded_ok());
  }
}

// Si TODAS las filas se descartan no se carga nada: la galeria previa se conserva, la recarga
// falla y loaded_ok es false.
TEST(galeria_si_todas_las_filas_se_descartan_loaded_ok_es_false_y_se_conserva_la_previa) {
  fakes::FakeHttpClient http;
  http.responses.push_back(
      json_response(R"({"embeddings":[{"cuenta":"320068234","embedding":[1,0,0]}]})"));
  http.responses.push_back(json_response(R"({"embeddings":[{"cuenta":"123","embedding":[0,1,0]}]})"));
  Gallery gallery;
  GalleryStore store(gallery, http, config("http://127.0.0.1:1880/api/galeria"));
  CHECK(store.reload());
  CHECK(store.loaded_ok());
  CHECK(!store.reload());  // ninguna entrada utilizable
  CHECK(!store.loaded_ok());
  CHECK_EQ(gallery.size(), std::size_t{1});
  CHECK(gallery_has(gallery, "320068234", e0()));
}

// --- GalleryRetry: la decision del reintento de main.cpp, sin el proceso -------------------

TEST(el_reintento_se_programa_si_la_recarga_fallo_o_cargo_con_filas_descartadas) {
  using namespace std::chrono_literals;
  fakes::ManualClock clock;
  GalleryRetry retry(Millis(30000), 10);
  CHECK(!retry.due(clock.now()));  // nada programado

  // La recarga fallo, o cargo pero loaded_ok() quedo en false: en los dos casos no es de fiar.
  CHECK_EQ(retry.record(false, clock.now()).count(), 30000);
  CHECK(!retry.due(clock.now()));
  clock.advance(29999ms);
  CHECK(!retry.due(clock.now()));
  clock.advance(1ms);
  CHECK(retry.due(clock.now()));

  // Backoff: 30 s, 60 s, 120 s... con tope de 10x (300 s).
  CHECK_EQ(retry.record(false, clock.now()).count(), 60000);
  CHECK_EQ(retry.record(false, clock.now()).count(), 120000);
  CHECK_EQ(retry.record(false, clock.now()).count(), 240000);
  CHECK_EQ(retry.record(false, clock.now()).count(), 300000);
  CHECK_EQ(retry.record(false, clock.now()).count(), 300000);

  // Una galeria de fiar cancela el reintento y reinicia el intervalo (no queda nada programado).
  CHECK_EQ(retry.record(true, clock.now()).count(), 0);
  clock.advance(Millis(10'000'000));
  CHECK(!retry.due(clock.now()));
  CHECK_EQ(retry.record(false, clock.now()).count(), 30000);
}
