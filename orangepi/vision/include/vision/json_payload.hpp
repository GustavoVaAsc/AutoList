#pragma once

// Capa JSON del contrato MQTT: convierte los payloads tipados de mqtt_contract.hpp a
// documentos JSON y de vuelta. Es la unica pieza que conoce el formato; el adaptador
// del bus (tarea 2) solo llama a estas dos funciones.
//
// Reglas del contrato (docs/contrato-mqtt.md, seccion 1):
//   - los campos desconocidos se ignoran;
//   - los campos obligatorios que falten, o que tengan el tipo equivocado, hacen
//     fallar la conversion: no se lanza excepcion y el mensaje no se vuelca en el log;
//   - "cuenta" es siempre un string (nunca un numero: se perderian los ceros);
//   - "conf" y "margen" son flotantes, "lat_ms" son enteros (o null si la etapa no
//     corrio) y "embedding" es un arreglo de flotantes.

#include <optional>
#include <string>
#include <string_view>

#include "vision/mqtt_contract.hpp"

namespace vision::json {

enum class Error {
  kNone,
  kInvalidJson,   // el documento no es JSON valido, o no es un objeto
  kUnknownTopic,  // el topico no pertenece al contrato
  kMissingField,  // falta un campo obligatorio
  kWrongType,     // un campo tiene un tipo que el contrato no admite
  kInvalidValue,  // el valor esta fuera del vocabulario o es contradictorio
};

struct ParseResult {
  std::optional<Payload> payload;
  Error error = Error::kNone;
  // Nombre del campo que hizo fallar la conversion. Nunca el contenido del mensaje.
  std::string field;

  [[nodiscard]] bool ok() const { return payload.has_value(); }
};

// Documento JSON compacto (una linea, UTF-8) del payload. Vacio si el payload no se
// puede representar (por ejemplo, si la hora no tiene el formato del contrato).
std::optional<std::string> serialize(const Payload& payload);

// Payload del topico indicado a partir del documento JSON. Los topicos que el
// servicio no publica ni consume ("aula/sesion/cmd", "aula/pantalla/reinicio",
// "aula/sistema/metricas"...) no tienen payload en el contrato de la vision y
// devuelven kUnknownTopic.
ParseResult deserialize(std::string_view topic, std::string_view document);

}  // namespace vision::json
