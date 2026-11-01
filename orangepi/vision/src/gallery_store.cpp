#include "vision/gallery_store.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "vision/mqtt_contract.hpp"

namespace vision {

namespace {

using nlohmann::json;

struct Entry {
  std::string account;
  std::vector<float> embedding;
};

// Resultado de entender la respuesta del servidor.
enum class ParseOutcome {
  kOk,             // la respuesta se entendio (puede estar vacia: galeria vacia)
  kMalformed,      // no es JSON o no tiene la forma esperada
  kNothingUsable,  // la forma es la correcta, pero ninguna entrada se puede cargar
};

// Extrae las entradas de la respuesta de GET /api/galeria.
//
// El FORMATO no lo fija el contrato: se aceptan, en este orden de preferencia,
// {"embeddings":[...]}, {"alumnos":[...]} y un arreglo suelto. Cada entrada trae la
// cuenta ("cuenta"; se acepta tambien "no_cuenta", el nombre de la columna) y el
// embedding. Una entrada con "embedding": null es un alumno pendiente de rostro: se
// omite. Una cuenta que no es un no_cuenta valido, o un embedding que no es un
// arreglo de numeros, se descarta con una linea de log (sin el contenido).
//
// Si la forma es la correcta pero NINGUNA entrada se puede cargar, se devuelve
// kNothingUsable: la galeria en RAM no se toca (no se pierde lo que ya habia).
ParseOutcome parse_entries(const std::vector<std::uint8_t>& body, std::vector<Entry>& entries) {
  json document;
  try {
    document = json::parse(body.begin(), body.end());
  } catch (const json::exception&) {
    std::cerr << "[vision] la respuesta de la galeria no es JSON valido\n";
    return ParseOutcome::kMalformed;
  }

  const json* array = nullptr;
  if (document.is_array()) {
    array = &document;
  } else if (document.is_object()) {
    for (const char* key : {"embeddings", "alumnos"}) {
      const auto it = document.find(key);
      if (it != document.end() && it->is_array()) {
        array = &*it;
        break;
      }
    }
  }
  if (array == nullptr) {
    std::cerr << "[vision] la respuesta de la galeria no tiene la forma esperada\n";
    return ParseOutcome::kMalformed;
  }

  std::size_t descartadas = 0;
  for (const json& item : *array) {
    if (!item.is_object()) {
      ++descartadas;
      continue;
    }

    std::optional<std::string> account;
    for (const char* key : {"cuenta", "no_cuenta"}) {
      const auto it = item.find(key);
      if (it == item.end()) continue;
      if (it->is_string()) {
        account = it->get<std::string>();
        break;
      }
      if (it->is_number_integer()) {
        account = std::to_string(it->get<long long>());
        break;
      }
    }
    if (!account.has_value() || !is_valid_account(*account)) {
      ++descartadas;
      continue;
    }

    const auto embedding_it = item.find("embedding");
    if (embedding_it == item.end()) {
      ++descartadas;
      continue;
    }
    if (embedding_it->is_null()) {
      continue;  // alumno pendiente de rostro: no hay embedding que cargar
    }
    if (!embedding_it->is_array()) {
      ++descartadas;
      continue;
    }

    std::vector<float> values;
    values.reserve(embedding_it->size());
    bool valido = true;
    for (const json& value : *embedding_it) {
      if (!value.is_number()) {
        valido = false;
        break;
      }
      values.push_back(value.get<float>());
    }
    if (!valido || values.empty()) {
      ++descartadas;
      continue;
    }
    entries.push_back(Entry{*account, std::move(values)});
  }

  if (descartadas > 0) {
    std::cerr << "[vision] entradas de la galeria descartadas: " << descartadas << '\n';
  }
  if (entries.empty() && descartadas > 0) {
    // La respuesta tiene la forma esperada, pero no hay nada que cargar: se conserva
    // la galeria en RAM.
    std::cerr << "[vision] la respuesta de la galeria no traia ninguna entrada utilizable\n";
    return ParseOutcome::kNothingUsable;
  }
  return ParseOutcome::kOk;
}

}  // namespace

GalleryStore::GalleryStore(Gallery& gallery, HttpClient& http, GalleryStoreConfig config)
    : gallery_(gallery), http_(http), config_(std::move(config)) {}

bool GalleryStore::reload() {
  // Las recargas se serializan: si dos se cruzan (por ejemplo, un alta del broker
  // mientras se recarga al arrancar), la ultima que termina es la que se queda, y no
  // una respuesta vieja que llego antes.
  std::lock_guard reload_lock(reload_mutex_);

  std::vector<std::string> headers;
  if (!config_.token.empty()) {
    if (!http_.supports_request_headers()) {
      std::cerr << "[vision] el cliente HTTP no admite cabeceras: el token de la galeria "
                   "no se envia\n";
    }
    headers.push_back("X-Galeria-Token: " + config_.token);
  }

  // La peticion va sin el mutex: mientras se descarga, la galeria en RAM sigue en uso.
  const HttpResponse response =
      http_.get(config_.url, config_.timeout, kMaxBodyBytes, headers);
  if (response.status != 200) {
    std::cerr << "[vision] no se pudo cargar la galeria (status " << response.status << ")\n";
    return false;
  }

  std::vector<Entry> entries;
  const ParseOutcome outcome = parse_entries(response.body, entries);
  if (outcome != ParseOutcome::kOk) return false;

  // La galeria nueva se arma aparte: si una entrada no cuadra (por ejemplo, una
  // dimension distinta de la del primer alumno), se descarta y el resto se carga igual.
  Gallery galeria_nueva;
  for (const Entry& entry : entries) {
    try {
      galeria_nueva.upsert(entry.account, entry.embedding);
    } catch (const EmbeddingError& error) {
      std::cerr << "[vision] embedding descartado al cargar la galeria: " << error.what()
                << '\n';
    } catch (const std::invalid_argument&) {
      std::cerr << "[vision] cuenta descartada al cargar la galeria: no es valida\n";
    }
  }

  std::size_t cargadas = 0;
  {
    std::lock_guard lock(mutex_);
    gallery_ = std::move(galeria_nueva);
    cargadas = gallery_.size();
  }
  std::cerr << "[vision] galeria cargada: " << cargadas << " alumnos\n";
  return true;
}

void GalleryStore::apply_add(const std::string& account) {
  if (!is_valid_account(account)) {
    // El mensaje solo trae la cuenta y no se usa: la galeria se recarga igual. Se avisa
    // porque el broker deberia mandar cuentas validas.
    std::cerr << "[vision] alta de galeria con cuenta invalida: se recarga igual\n";
  }
  // El mensaje solo trae la cuenta: la galeria se recarga completa (contrato v0.3).
  reload();
}

void GalleryStore::apply_remove(const std::string& account) {
  if (!is_valid_account(account)) {
    std::cerr << "[vision] baja de galeria con cuenta invalida: se ignora\n";
    return;
  }
  std::lock_guard lock(mutex_);
  gallery_.remove(account);
}

std::size_t GalleryStore::size() const {
  std::lock_guard lock(mutex_);
  return gallery_.size();
}

}  // namespace vision
