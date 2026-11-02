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
//
// Dos formas del estado (aula/vision/estado): el estado que publica el servicio lleva
// el esquema completo ("ts", "en_linea", "modo", "galeria" y "modelo") y el Will del
// broker lleva solo {"en_linea":false}. Por eso, en el estado, "en_linea" es el unico
// campo obligatorio: los demas son opcionales y se aceptan ausentes. El formato del
// cable no cambia: es el mismo topico con el mismo JSON.
//
// Tamano y profundidad: antes de parsear se comprueban los topes kMaxDocumentBytes y
// kMaxDocumentDepth (ver exceeds_depth_limit). Un documento que los supera se rechaza
// sin lanzar y sin tumbar el proceso.

#include <cstddef>
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
  kTooLarge,      // el documento es mas largo que kMaxDocumentBytes
  kTooDeep,       // el documento tiene mas de kMaxDocumentDepth niveles de anidamiento
};

// Topes del texto que se va a parsear. Son una barrera propia de la capa JSON, por
// debajo del tope de paquete del broker (que permite 256 KiB).
//
// Tamano: un mensaje MQTT no tiene por que pasar de 64 KiB (el cuerpo mas grande del
// contrato es el embedding, de unos pocos KiB).
//
// Profundidad: el parser de nlohmann es descendente y recursivo, asi que un documento
// con miles de niveles puede agotar la pila y tumbar el proceso. El limite esta en 32
// niveles: ningun documento del contrato llega a tres.
constexpr std::size_t kMaxDocumentBytes = 64u * 1024u;
constexpr int kMaxDocumentDepth = 32;

// true si el documento tiene mas de kMaxDocumentDepth niveles de llaves o corchetes
// anidados (las llaves y los corchetes dentro de cadenas no cuentan). No lanza.
bool exceeds_depth_limit(std::string_view document) noexcept;

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
