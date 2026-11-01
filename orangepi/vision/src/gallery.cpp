#include "vision/gallery.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "vision/mqtt_contract.hpp"

namespace vision {

std::vector<float> l2_normalized(std::span<const float> embedding) {
  if (embedding.empty()) {
    throw EmbeddingError("embedding vacio");
  }
  double sum_squares = 0.0;
  for (const float x : embedding) {
    sum_squares += static_cast<double>(x) * x;
  }
  const double norm = std::sqrt(sum_squares);
  if (!(norm > 0.0) || !std::isfinite(norm)) {
    throw EmbeddingError("embedding con norma cero o no finita");
  }
  std::vector<float> out(embedding.size());
  std::transform(embedding.begin(), embedding.end(), out.begin(),
                 [norm](float x) { return static_cast<float>(x / norm); });
  return out;
}

std::size_t Gallery::dimension() const {
  return entries_.empty() ? 0 : entries_.front().embedding.size();
}

void Gallery::upsert(const std::string& account, std::span<const float> embedding) {
  if (!is_valid_account(account)) {
    throw std::invalid_argument("Gallery::upsert: la cuenta debe tener exactamente 9 digitos");
  }
  if (!entries_.empty() && embedding.size() != dimension()) {
    throw EmbeddingError("dimension " + std::to_string(embedding.size()) +
                         " distinta de la de la galeria (" + std::to_string(dimension()) + ")");
  }
  std::vector<float> normalized = l2_normalized(embedding);

  const auto it = std::find_if(entries_.begin(), entries_.end(),
                               [&](const Entry& e) { return e.account == account; });
  if (it != entries_.end()) {
    it->embedding = std::move(normalized);
  } else {
    entries_.push_back({account, std::move(normalized)});
  }
}

bool Gallery::remove(const std::string& account) {
  const auto it = std::find_if(entries_.begin(), entries_.end(),
                               [&](const Entry& e) { return e.account == account; });
  if (it == entries_.end()) {
    return false;
  }
  entries_.erase(it);
  return true;
}

std::optional<GalleryMatch> Gallery::match(std::span<const float> query) const {
  if (entries_.empty()) {
    return std::nullopt;
  }
  if (query.size() != dimension()) {
    throw EmbeddingError("dimension " + std::to_string(query.size()) +
                         " distinta de la de la galeria (" + std::to_string(dimension()) + ")");
  }
  const std::vector<float> q = l2_normalized(query);

  const Entry* best = nullptr;
  float best_score = -1.0f;
  std::optional<float> second_score;
  for (const Entry& entry : entries_) {
    float score = 0.0f;
    for (std::size_t i = 0; i < q.size(); ++i) {
      score += q[i] * entry.embedding[i];
    }
    if (best == nullptr || score > best_score) {
      if (best != nullptr) second_score = best_score;
      best = &entry;
      best_score = score;
    } else if (!second_score || score > *second_score) {
      second_score = score;
    }
  }
  std::optional<float> margin;
  if (second_score) margin = best_score - *second_score;
  return GalleryMatch{best->account, best_score, margin};
}

}  // namespace vision
