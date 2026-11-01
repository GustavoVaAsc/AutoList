#include <cmath>
#include <numeric>

#include "minitest.hpp"
#include "vision/gallery.hpp"

namespace {

using namespace vision;

const std::string kS1 = "100000001";
const std::string kA = "111111111";
const std::string kB = "222222222";
const std::string kC = "333333333";

float norm(const std::vector<float>& v) {
  return std::sqrt(std::inner_product(v.begin(), v.end(), v.begin(), 0.0f));
}

}  // namespace

TEST(l2_normalized_has_unit_norm_and_keeps_direction) {
  const std::vector<float> v = {3.0f, 4.0f};
  const std::vector<float> n = l2_normalized(v);
  CHECK_NEAR(norm(n), 1.0, 1e-6);
  CHECK_NEAR(n[0], 0.6, 1e-6);
  CHECK_NEAR(n[1], 0.8, 1e-6);
}

TEST(l2_normalized_rejects_empty_and_zero_vectors) {
  CHECK_THROWS(l2_normalized(std::vector<float>{}), EmbeddingError);
  CHECK_THROWS(l2_normalized(std::vector<float>{0.0f, 0.0f}), EmbeddingError);
}

TEST(gallery_match_on_empty_gallery_is_nullopt) {
  Gallery gallery;
  CHECK_EQ(gallery.size(), std::size_t{0});
  CHECK_EQ(gallery.dimension(), std::size_t{0});
  CHECK(!gallery.match(std::vector<float>{1.0f, 0.0f}).has_value());
}

TEST(gallery_score_ignores_embedding_magnitude) {
  Gallery gallery;
  gallery.upsert(kS1, std::vector<float>{3.0f, 4.0f});
  const auto match = gallery.match(std::vector<float>{30.0f, 40.0f});
  CHECK(match.has_value());
  CHECK_NEAR(match->score, 1.0, 1e-6);
}

TEST(gallery_top1_and_margin_with_several_students) {
  Gallery gallery;
  gallery.upsert(kA, std::vector<float>{1.0f, 0.0f});
  gallery.upsert(kB, std::vector<float>{0.0f, 1.0f});
  gallery.upsert(kC, std::vector<float>{-1.0f, 0.0f});

  // Consulta a 30 grados del eje de A: cos(30)=0.866 con A, cos(60)=0.5 con B, -0.866 con C.
  const std::vector<float> query = {std::cos(0.5236f), std::sin(0.5236f)};
  const auto match = gallery.match(query);
  CHECK(match.has_value());
  CHECK_EQ(match->account, std::string(kA));
  CHECK_NEAR(match->score, 0.866, 1e-3);
  CHECK(match->margin.has_value());
  CHECK_NEAR(*match->margin, 0.866 - 0.5, 1e-3);
}

TEST(gallery_margin_when_the_best_candidate_comes_last) {
  Gallery gallery;
  gallery.upsert(kB, std::vector<float>{0.0f, 1.0f});
  gallery.upsert(kA, std::vector<float>{1.0f, 0.0f});
  const auto match = gallery.match(std::vector<float>{std::cos(0.5236f), std::sin(0.5236f)});
  CHECK(match.has_value());
  CHECK_EQ(match->account, std::string(kA));
  CHECK(match->margin.has_value());
  CHECK_NEAR(*match->margin, 0.866 - 0.5, 1e-3);
}

TEST(gallery_margin_is_absent_with_a_single_student) {
  Gallery gallery;
  gallery.upsert(kA, std::vector<float>{1.0f, 0.0f});
  const auto match = gallery.match(std::vector<float>{1.0f, 0.0f});
  CHECK(match.has_value());
  CHECK(!match->margin.has_value());
}

TEST(gallery_dimension_is_fixed_by_first_insert) {
  Gallery gallery;
  gallery.upsert(kA, std::vector<float>{1.0f, 2.0f, 3.0f});
  CHECK_EQ(gallery.dimension(), std::size_t{3});
  CHECK_THROWS(gallery.upsert(kB, std::vector<float>{1.0f, 2.0f}), EmbeddingError);
  CHECK_THROWS(gallery.upsert(kA, std::vector<float>{1.0f, 2.0f, 3.0f, 4.0f}), EmbeddingError);
  CHECK_EQ(gallery.size(), std::size_t{1});
}

TEST(gallery_match_rejects_query_of_other_dimension) {
  Gallery gallery;
  gallery.upsert(kA, std::vector<float>{1.0f, 2.0f, 3.0f});
  CHECK_THROWS(gallery.match(std::vector<float>{1.0f, 2.0f}), EmbeddingError);
}

TEST(gallery_works_with_any_dimension) {
  for (const std::size_t dim : {std::size_t{128}, std::size_t{512}}) {
    Gallery gallery;
    std::vector<float> v(dim, 0.0f);
    v[dim - 1] = 2.0f;
    gallery.upsert(kA, v);
    CHECK_EQ(gallery.dimension(), dim);
    CHECK_NEAR(gallery.match(v)->score, 1.0, 1e-6);
  }
}

TEST(gallery_upsert_replaces_existing_account) {
  Gallery gallery;
  gallery.upsert(kA, std::vector<float>{1.0f, 0.0f});
  gallery.upsert(kB, std::vector<float>{0.0f, 1.0f});
  gallery.upsert(kA, std::vector<float>{0.0f, 1.0f});
  CHECK_EQ(gallery.size(), std::size_t{2});
  // Con el embedding nuevo, A y B son identicos: A ya no coincide con el eje X.
  CHECK_NEAR(gallery.match(std::vector<float>{1.0f, 0.0f})->score, 0.0, 1e-6);
}

TEST(gallery_remove_and_dimension_release) {
  Gallery gallery;
  gallery.upsert(kA, std::vector<float>{1.0f, 0.0f});
  CHECK(!gallery.remove("nope"));
  CHECK(gallery.remove(kA));
  CHECK_EQ(gallery.size(), std::size_t{0});
  CHECK(!gallery.match(std::vector<float>{1.0f, 0.0f}).has_value());
  gallery.upsert(kB, std::vector<float>{1.0f, 0.0f, 0.0f});  // otra dimension tras vaciar
  CHECK_EQ(gallery.dimension(), std::size_t{3});
}

TEST(gallery_rejects_zero_embedding) {
  Gallery gallery;
  CHECK_THROWS(gallery.upsert(kA, std::vector<float>{0.0f, 0.0f}), EmbeddingError);
  CHECK_EQ(gallery.size(), std::size_t{0});
}

TEST(gallery_upsert_rejects_accounts_that_are_not_nine_digits) {
  Gallery gallery;
  const std::vector<float> e = {1.0f, 0.0f};
  for (const char* bad : {"", "1", "12345678", "1234567890", "12345678a", "12345 789", "-12345678", "12345678\xC3",
                          "../../etc", "12345678\n"}) {
    CHECK_THROWS(gallery.upsert(bad, e), std::invalid_argument);
  }
  CHECK_EQ(gallery.size(), std::size_t{0});
  gallery.upsert("000000000", e);  // los ceros a la izquierda son validos
  CHECK_EQ(gallery.size(), std::size_t{1});
}
