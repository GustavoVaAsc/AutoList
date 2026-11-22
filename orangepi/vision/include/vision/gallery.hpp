#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace vision {

class EmbeddingError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// Copia normalizada a norma L2 = 1. Lanza EmbeddingError si esta vacio o su norma es 0.
std::vector<float> l2_normalized(std::span<const float> embedding);

struct GalleryMatch {
  std::string account;  // no_cuenta del mejor candidato
  float score = 0.0f;   // similitud coseno, en [-1, 1]
  // Diferencia con el segundo mejor; nullopt si la galeria tiene menos de 2 alumnos
  // (no hay con quien comparar).
  std::optional<float> margin;
};

// Galeria en RAM (unas decenas de alumnos): busqueda lineal. Los embeddings se guardan
// normalizados. La dimension la fija el primer alta y se libera cuando la galeria queda
// vacia; cualquier embedding de otra dimension se rechaza con EmbeddingError.
// No es segura entre hilos.
class Gallery {
 public:
  // Agrega al alumno o reemplaza su embedding si ya existe. Lanza std::invalid_argument si
  // `account` no es un no_cuenta valido (9 digitos, ver is_valid_account).
  void upsert(const std::string& account, std::span<const float> embedding);

  // true si el alumno existia.
  bool remove(const std::string& account);

  // Mejor candidato por coseno; nullopt si la galeria esta vacia.
  std::optional<GalleryMatch> match(std::span<const float> query) const;

  // Mejor candidato por coseno SIN contar la cuenta `exclude` (para saber si el rostro de un
  // registro ya pertenece a otra cuenta: la propia puede re-registrarse). nullopt si no hay
  // otra cuenta. Sin margen: no hay con quien compararlo. Lanza EmbeddingError si la
  // dimension no coincide con la de la galeria.
  std::optional<GalleryMatch> match_excluding(std::span<const float> query,
                                              const std::string& exclude) const;

  std::size_t size() const { return entries_.size(); }
  std::size_t dimension() const;  // 0 si la galeria esta vacia

 private:
  struct Entry {
    std::string account;
    std::vector<float> embedding;
  };
  std::vector<Entry> entries_;
};

}  // namespace vision
