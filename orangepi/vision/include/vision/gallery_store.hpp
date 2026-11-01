#pragma once

// Galeria en RAM alimentada por el borde.
//
// El contrato (docs/contrato-mqtt.md, seccion 4) fija el endpoint y el encabezado, pero
// NO el formato de la respuesta de GET /api/galeria: el parser es tolerante a las
// formas que se listaron y el formato queda PROVISIONAL, pendiente de que el carril del
// borde lo confirme (README, seccion "La galeria en RAM").

#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

#include "vision/gallery.hpp"
#include "vision/http_client.hpp"

namespace vision {

struct GalleryStoreConfig {
  std::string url;   // GET /api/galeria
  std::string token; // encabezado X-Galeria-Token
  Millis timeout{5000};
};

class GalleryStore {
 public:
  // La galeria y el cliente HTTP son referencias: el dueño es de quien los pasa.
  GalleryStore(Gallery& gallery, HttpClient& http, GalleryStoreConfig config);

  // Descarga la galeria del servidor y deja la galeria en RAM igual que la que
  // devuelve el servidor (quita las cuentas que ya no estan). Devuelve false si la
  // peticion fallo o si la respuesta no se pudo entender: en ese caso la galeria en
  // RAM no se toca. Las recargas se serializan: la ultima que termina es la que se
  // queda.
  bool reload();

  // Ante aula/vision/galeria/alta: el mensaje solo trae la cuenta, asi que se
  // recarga la galeria completa.
  void apply_add(const std::string& account);

  // Ante aula/vision/galeria/baja: se borra esa cuenta de la galeria en RAM.
  void apply_remove(const std::string& account);

  [[nodiscard]] std::size_t size() const;

  // Tope del cuerpo de la respuesta: una galeria de 40 alumnos con 512 floats son
  // unos 200 KB; el tope esta para que un servidor equivocado no agote la RAM.
  static constexpr std::size_t kMaxBodyBytes = 8u * 1024u * 1024u;

 private:
  Gallery& gallery_;
  HttpClient& http_;
  GalleryStoreConfig config_;
  // La galeria en RAM no es segura entre hilos y los mensajes del broker llegan en el
  // hilo de red: el store serializa sus propias operaciones.
  mutable std::mutex mutex_;
  // Serializa las recargas entre si (la peticion HTTP va sin el mutex de la galeria:
  // mientras se descarga, la galeria en RAM sigue en uso).
  std::mutex reload_mutex_;
};

}  // namespace vision
