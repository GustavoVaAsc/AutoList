// Pruebas de la galeria en RAM: el parser tolerante de GET /api/galeria, el descarte
// de lo que no cuadra y el efecto de las altas y bajas del contrato.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "minitest.hpp"
#include "vision/fakes.hpp"
#include "vision/gallery.hpp"
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
