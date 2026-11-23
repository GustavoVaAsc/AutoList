#include "vision/json_payload.hpp"

#include <cmath>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "vision/wall_clock.hpp"

namespace vision::json {

namespace {

using nlohmann::json;

// Los flotantes se escriben con 4 decimales, como pide el contrato para el embedding.
constexpr double kDecimals = 10000.0;

double round4(double value) { return std::round(value * kDecimals) / kDecimals; }

json to_json_float(float value) { return json(round4(static_cast<double>(value))); }

json optional_string(const std::optional<std::string>& value) {
  return value.has_value() ? json(*value) : json(nullptr);
}

json optional_int(const std::optional<int>& value) {
  return value.has_value() ? json(*value) : json(nullptr);
}

// --- contexto de error ---------------------------------------------------------
// Solo guarda el nombre del campo que falla: el contenido del mensaje nunca se
// registra (puede traer datos personales).
struct Ctx {
  Error error = Error::kNone;
  std::string field;

  void fail(Error kind, const char* key) {
    if (error == Error::kNone) {
      error = kind;
      field = key != nullptr ? key : "";
    }
  }
  [[nodiscard]] bool failed() const { return error != Error::kNone; }
};

// --- lectura ------------------------------------------------------------------

// Devuelve true si el campo se leyo. Si falta y es obligatorio, falla con
// kMissingField; si el tipo no es el del contrato, falla con kWrongType.
bool read_string(const json& obj, const char* key, bool required, bool allow_null,
                 std::optional<std::string>& out, Ctx& ctx) {
  const auto it = obj.find(key);
  if (it == obj.end()) {
    if (required) ctx.fail(Error::kMissingField, key);
    return false;
  }
  if (it->is_null()) {
    if (!allow_null) {
      ctx.fail(Error::kWrongType, key);
      return false;
    }
    out.reset();
    return true;
  }
  if (!it->is_string()) {
    ctx.fail(Error::kWrongType, key);
    return false;
  }
  out = it->get<std::string>();
  return true;
}

// El mismo campo, obligatorio y sin null.
bool read_string(const json& obj, const char* key, bool required, std::string& out, Ctx& ctx) {
  std::optional<std::string> value;
  if (!read_string(obj, key, required, false, value, ctx)) return false;
  out = *value;
  return true;
}

bool read_int(const json& obj, const char* key, bool required, bool allow_null,
              std::optional<int>& out, Ctx& ctx) {
  const auto it = obj.find(key);
  if (it == obj.end()) {
    if (required) ctx.fail(Error::kMissingField, key);
    return false;
  }
  if (it->is_null()) {
    if (!allow_null) {
      ctx.fail(Error::kWrongType, key);
      return false;
    }
    out.reset();
    return true;
  }
  if (!it->is_number_integer()) {
    ctx.fail(Error::kWrongType, key);
    return false;
  }
  out = it->get<int>();
  return true;
}

bool read_float(const json& obj, const char* key, bool required, bool allow_null,
                std::optional<float>& out, Ctx& ctx) {
  const auto it = obj.find(key);
  if (it == obj.end()) {
    if (required) ctx.fail(Error::kMissingField, key);
    return false;
  }
  if (it->is_null()) {
    if (!allow_null) {
      ctx.fail(Error::kWrongType, key);
      return false;
    }
    out.reset();
    return true;
  }
  if (!it->is_number()) {
    ctx.fail(Error::kWrongType, key);
    return false;
  }
  out = it->get<double>();
  return true;
}

// El mismo campo, entero y sin null.
bool read_int(const json& obj, const char* key, bool required, int& out, Ctx& ctx) {
  std::optional<int> value;
  if (!read_int(obj, key, required, false, value, ctx)) return false;
  out = *value;
  return true;
}

bool read_bool(const json& obj, const char* key, bool required, bool& out, Ctx& ctx) {
  const auto it = obj.find(key);
  if (it == obj.end()) {
    if (required) ctx.fail(Error::kMissingField, key);
    return false;
  }
  if (!it->is_boolean()) {
    ctx.fail(Error::kWrongType, key);
    return false;
  }
  out = it->get<bool>();
  return true;
}

// Un arreglo de numeros, todos flotantes.
bool read_float_array(const json& obj, const char* key, bool required, std::vector<float>& out,
                      Ctx& ctx) {
  const auto it = obj.find(key);
  if (it == obj.end()) {
    if (required) ctx.fail(Error::kMissingField, key);
    return false;
  }
  if (!it->is_array()) {
    ctx.fail(Error::kWrongType, key);
    return false;
  }
  std::vector<float> values;
  values.reserve(it->size());
  for (const json& item : *it) {
    if (!item.is_number()) {
      ctx.fail(Error::kWrongType, key);
      return false;
    }
    values.push_back(item.get<double>());
  }
  out = std::move(values);
  return true;
}

bool is_one_of(std::string_view value, std::initializer_list<std::string_view> options) {
  for (const std::string_view option : options) {
    if (value == option) return true;
  }
  return false;
}

// El valor tiene que estar en el vocabulario del contrato.
bool check_vocabulary(std::string_view value, const char* key,
                      std::initializer_list<std::string_view> options, Ctx& ctx) {
  if (!is_one_of(value, options)) {
    ctx.fail(Error::kInvalidValue, key);
    return false;
  }
  return true;
}

// Igual, pero para un campo opcional: si no esta, no hay nada que comprobar.
bool check_optional_vocabulary(const std::optional<std::string>& value, const char* key,
                                std::initializer_list<std::string_view> options, Ctx& ctx) {
  if (!value.has_value()) return true;
  return check_vocabulary(*value, key, options, ctx);
}

// La hora del contrato: ISO 8601 con zona.
bool check_timestamp(std::string_view value, const char* key, Ctx& ctx) {
  if (!is_iso8601_with_zone(value)) {
    ctx.fail(Error::kInvalidValue, key);
    return false;
  }
  return true;
}

// La cuenta es un string de 9 digitos (o vacia, cuando el mensaje no la trae).
bool check_account(std::string_view value, const char* key, Ctx& ctx) {
  if (value.empty()) return true;
  if (!is_valid_account(value)) {
    ctx.fail(Error::kInvalidValue, key);
    return false;
  }
  return true;
}

// --- escritura ----------------------------------------------------------------

json write_latencies(const LatenciesMs& lat_ms) {
  json doc = json::object();
  doc["cap"] = optional_int(lat_ms.cap);
  doc["det"] = optional_int(lat_ms.det);
  doc["emb"] = optional_int(lat_ms.emb);
  doc["total"] = optional_int(lat_ms.total);
  return doc;
}

json to_json(const PirEvent& event) {
  json doc = json::object();
  doc["mov"] = event.motion ? 1 : 0;
  return doc;
}

json to_json(const ActuatorCommand& command) {
  json doc = json::object();
  if (command.servo.has_value()) doc["servo"] = *command.servo;
  if (command.led.has_value()) doc["led"] = *command.led;
  if (command.buzzer.has_value()) doc["buzzer"] = *command.buzzer;
  if (command.light.has_value()) doc["luz"] = *command.light;
  return doc;
}

json to_json(const AttendanceResult& result) {
  json doc = json::object();
  doc["ts"] = result.timestamp;
  doc["modo"] = std::string(to_wire(result.mode));
  doc["decision"] = std::string(to_wire(result.decision));
  doc["cuenta"] = optional_string(result.account);
  doc["conf"] = result.confidence.has_value() ? to_json_float(*result.confidence) : json(nullptr);
  if (result.margin.has_value()) doc["margen"] = to_json_float(*result.margin);
  doc["lat_ms"] = write_latencies(result.lat_ms);
  if (result.reason.has_value()) doc["motivo"] = *result.reason;
  if (result.thumbnail.has_value()) doc["miniatura"] = *result.thumbnail;
  return doc;
}

json to_json(const VisionCommand& command) {
  json doc = json::object();
  doc["accion"] = std::string(to_wire(command.action));
  if (!command.account.empty()) doc["cuenta"] = command.account;
  if (command.name.has_value()) doc["nombre"] = *command.name;
  return doc;
}

json to_json(const EnrollmentProgress& progress) {
  json doc = json::object();
  doc["ts"] = progress.timestamp;
  doc["cuenta"] = progress.account;
  if (progress.name.has_value()) doc["nombre"] = *progress.name;
  doc["paso"] = progress.step;
  doc["total"] = progress.total;
  doc["pose"] = progress.pose;
  doc["estado"] = std::string(to_wire(progress.status));
  if (progress.reason.has_value()) doc["motivo"] = *progress.reason;
  if (progress.similar_to.has_value()) doc["parecido_a"] = *progress.similar_to;
  if (progress.thumbnail.has_value()) doc["miniatura"] = *progress.thumbnail;
  return doc;
}

json to_json(const EmbeddingMessage& message) {
  json doc = json::object();
  doc["ts"] = message.timestamp;
  doc["cuenta"] = message.account;
  doc["dim"] = message.dim();
  doc["modelo"] = message.model;
  json values = json::array();
  for (const float value : message.embedding) values.push_back(to_json_float(value));
  doc["embedding"] = std::move(values);
  return doc;
}

json to_json(const VisionStatus& status) {
  json doc = json::object();
  doc["ts"] = status.timestamp;
  doc["en_linea"] = status.online;
  doc["modo"] = std::string(to_wire(status.mode));
  doc["galeria"] = status.gallery_size;
  doc["modelo"] = status.model;
  return doc;
}

json to_json(const GalleryAccount& gallery) {
  json doc = json::object();
  doc["cuenta"] = gallery.account;
  return doc;
}

// std::visit no puede deducir una funcion sobrecargada: el functor despacha.
struct Writer {
  json operator()(const PirEvent& event) const { return to_json(event); }
  json operator()(const ActuatorCommand& command) const { return to_json(command); }
  json operator()(const AttendanceResult& result) const { return to_json(result); }
  json operator()(const VisionCommand& command) const { return to_json(command); }
  json operator()(const EnrollmentProgress& progress) const { return to_json(progress); }
  json operator()(const EmbeddingMessage& message) const { return to_json(message); }
  json operator()(const VisionStatus& status) const { return to_json(status); }
  json operator()(const GalleryAccount& gallery) const { return to_json(gallery); }
};

// --- lectura por payload ---------------------------------------------------------

ParseResult parse_pir(const json& doc) {
  ParseResult result;
  Ctx ctx;
  std::optional<int> motion;
  if (!read_int(doc, "mov", true, false, motion, ctx)) {
    result.error = ctx.error;
    result.field = ctx.field;
    return result;
  }
  PirEvent event;
  event.motion = *motion != 0;
  result.payload = event;
  return result;
}

ParseResult parse_actuator(const json& doc) {
  ParseResult result;
  Ctx ctx;
  ActuatorCommand command;
  read_string(doc, "servo", false, false, command.servo, ctx);
  read_string(doc, "led", false, false, command.led, ctx);
  read_int(doc, "buzzer", false, false, command.buzzer, ctx);
  read_int(doc, "luz", false, false, command.light, ctx);
  if (ctx.failed()) {
    result.error = ctx.error;
    result.field = ctx.field;
    return result;
  }
  result.payload = command;
  return result;
}

ParseResult parse_command(const json& doc) {
  ParseResult result;
  Ctx ctx;
  std::optional<std::string> action;
  read_string(doc, "accion", true, false, action, ctx);
  if (!action.has_value()) {
    result.error = ctx.error;
    result.field = ctx.field;
    return result;
  }
  VisionCommand command;
  if (*action == actions::kEnroll) {
    command.action = VisionAction::Enroll;
  } else if (*action == actions::kCancel) {
    command.action = VisionAction::Cancel;
  } else if (*action == actions::kTest) {
    command.action = VisionAction::Test;
  } else {
    ctx.fail(Error::kInvalidValue, "accion");
    result.error = ctx.error;
    result.field = ctx.field;
    return result;
  }
  // "cuenta" es obligatoria solo para enrolar. En probar es opcional y en cancelar
  // tambien: una cuenta vacia significa "cancela el registro en curso", que es lo que
  // el servicio admite.
  std::optional<std::string> account;
  read_string(doc, "cuenta", command.action == VisionAction::Enroll, false, account, ctx);
  read_string(doc, "nombre", false, false, command.name, ctx);
  if (ctx.failed()) {
    result.error = ctx.error;
    result.field = ctx.field;
    return result;
  }
  // La cuenta no se valida aqui: la vision responde "cuenta_invalida" (el servicio
  // decide, no la capa JSON).
  command.account = account.value_or(std::string());
  result.payload = command;
  return result;
}

ParseResult parse_result(const json& doc) {
  ParseResult result;
  Ctx ctx;
  AttendanceResult payload(AttendanceMode::Attendance);
  read_string(doc, "ts", true, payload.timestamp, ctx);
  std::optional<std::string> mode;
  read_string(doc, "modo", true, false, mode, ctx);
  check_optional_vocabulary(mode, "modo", {modes::kAttendance, modes::kTest}, ctx);
  if (mode.has_value()) {
    payload.mode = *mode == modes::kTest ? AttendanceMode::Test : AttendanceMode::Attendance;
  }
  std::optional<std::string> decision;
  read_string(doc, "decision", true, false, decision, ctx);
  check_optional_vocabulary(decision, "decision",
                   {decisions::kRecognized, decisions::kDoubtful, decisions::kUnknown,
                    decisions::kNoFace, decisions::kInsufficientImage, decisions::kError}, ctx);
  if (decision.has_value()) {
    if (*decision == decisions::kRecognized) {
      payload.decision = Decision::Recognized;
    } else if (*decision == decisions::kDoubtful) {
      payload.decision = Decision::Doubtful;
    } else if (*decision == decisions::kUnknown) {
      payload.decision = Decision::Unknown;
    } else if (*decision == decisions::kNoFace) {
      payload.decision = Decision::NoFace;
    } else if (*decision == decisions::kInsufficientImage) {
      payload.decision = Decision::InsufficientImage;
    } else {
      payload.decision = Decision::Error;
    }
  }
  read_string(doc, "cuenta", true, true, payload.account, ctx);
  check_account(payload.account.value_or(std::string()), "cuenta", ctx);
  read_float(doc, "conf", true, true, payload.confidence, ctx);
  read_float(doc, "margen", false, true, payload.margin, ctx);
  read_string(doc, "motivo", false, false, payload.reason, ctx);
  read_string(doc, "miniatura", false, false, payload.thumbnail, ctx);
  check_optional_vocabulary(payload.reason, "motivo",
                   {reasons::kCaptureFailed, reasons::kInsufficientImage, reasons::kNoFace,
                    reasons::kUnknown, reasons::kEmptyGallery, reasons::kInternalError,
                    reasons::kBusy, reasons::kInvalidAccount}, ctx);

  const auto latencies = doc.find("lat_ms");
  if (latencies == doc.end()) {
    ctx.fail(Error::kMissingField, "lat_ms");
  } else if (!latencies->is_object()) {
    ctx.fail(Error::kWrongType, "lat_ms");
  } else {
    read_int(*latencies, "cap", true, true, payload.lat_ms.cap, ctx);
    read_int(*latencies, "det", true, true, payload.lat_ms.det, ctx);
    read_int(*latencies, "emb", true, true, payload.lat_ms.emb, ctx);
    read_int(*latencies, "total", true, true, payload.lat_ms.total, ctx);
  }
  check_timestamp(payload.timestamp, "ts", ctx);
  if (ctx.failed()) {
    result.error = ctx.error;
    result.field = ctx.field;
    return result;
  }
  result.payload = payload;
  return result;
}

ParseResult parse_enrollment(const json& doc) {
  ParseResult result;
  Ctx ctx;
  EnrollmentProgress payload;
  read_string(doc, "ts", true, payload.timestamp, ctx);
  read_string(doc, "cuenta", true, payload.account, ctx);
  check_account(payload.account, "cuenta", ctx);
  read_string(doc, "nombre", false, false, payload.name, ctx);
  read_int(doc, "paso", true, payload.step, ctx);
  read_int(doc, "total", true, payload.total, ctx);
  std::optional<std::string> pose;
  read_string(doc, "pose", true, false, pose, ctx);
  payload.pose = pose.value_or(std::string());
  if (pose.has_value() && !pose->empty() &&
      !is_one_of(*pose, {kEnrollmentPoses[0], kEnrollmentPoses[1], kEnrollmentPoses[2],
                          kEnrollmentPoses[3], kEnrollmentPoses[4]})) {
    ctx.fail(Error::kInvalidValue, "pose");
  }
  std::optional<std::string> status;
  read_string(doc, "estado", true, false, status, ctx);
  check_optional_vocabulary(status, "estado",
                   {enrollment_statuses::kWaitingPose, enrollment_statuses::kCapturing,
                    enrollment_statuses::kAccepted, enrollment_statuses::kRejected,
                    enrollment_statuses::kWarning, enrollment_statuses::kCompleted,
                    enrollment_statuses::kCancelled, enrollment_statuses::kError}, ctx);
  if (status.has_value()) {
    if (*status == enrollment_statuses::kWaitingPose) {
      payload.status = EnrollmentStatus::WaitingPose;
    } else if (*status == enrollment_statuses::kCapturing) {
      payload.status = EnrollmentStatus::Capturing;
    } else if (*status == enrollment_statuses::kAccepted) {
      payload.status = EnrollmentStatus::Accepted;
    } else if (*status == enrollment_statuses::kRejected) {
      payload.status = EnrollmentStatus::Rejected;
    } else if (*status == enrollment_statuses::kWarning) {
      payload.status = EnrollmentStatus::Warning;
    } else if (*status == enrollment_statuses::kCompleted) {
      payload.status = EnrollmentStatus::Completed;
    } else if (*status == enrollment_statuses::kCancelled) {
      payload.status = EnrollmentStatus::Cancelled;
    } else {
      payload.status = EnrollmentStatus::Error;
    }
  }
  read_string(doc, "motivo", false, false, payload.reason, ctx);
  check_optional_vocabulary(payload.reason, "motivo",
                   {enrollment_reasons::kNoFace, enrollment_reasons::kSeveralFaces,
                    enrollment_reasons::kSmallFace, enrollment_reasons::kBlurry,
                    enrollment_reasons::kExcessiveTurn, enrollment_reasons::kWrongPose,
                    enrollment_reasons::kCaptureFailure,
                    enrollment_reasons::kBusy, enrollment_reasons::kInconsistent,
                    enrollment_reasons::kNpu, enrollment_reasons::kDuplicate,
                    enrollment_reasons::kGalleryUnavailable,
                    enrollment_reasons::kInvalidAccount}, ctx);
  read_string(doc, "parecido_a", false, false, payload.similar_to, ctx);
  read_string(doc, "miniatura", false, false, payload.thumbnail, ctx);
  check_timestamp(payload.timestamp, "ts", ctx);
  if (ctx.failed()) {
    result.error = ctx.error;
    result.field = ctx.field;
    return result;
  }
  result.payload = payload;
  return result;
}

ParseResult parse_embedding(const json& doc) {
  ParseResult result;
  Ctx ctx;
  EmbeddingMessage payload;
  read_string(doc, "ts", true, payload.timestamp, ctx);
  read_string(doc, "cuenta", true, payload.account, ctx);
  check_account(payload.account, "cuenta", ctx);
  read_string(doc, "modelo", true, payload.model, ctx);
  std::optional<int> dim;
  read_int(doc, "dim", true, false, dim, ctx);
  read_float_array(doc, "embedding", true, payload.embedding, ctx);
  if (!ctx.failed() && dim.has_value() &&
      static_cast<int>(payload.embedding.size()) != *dim) {
    // "dim" y el vector tienen que decir lo mismo: Node-RED compara "dim" con la
    // columna vector(N).
    ctx.fail(Error::kInvalidValue, "dim");
  }
  check_timestamp(payload.timestamp, "ts", ctx);
  if (ctx.failed()) {
    result.error = ctx.error;
    result.field = ctx.field;
    return result;
  }
  result.payload = payload;
  return result;
}

ParseResult parse_status(const json& doc) {
  ParseResult result;
  Ctx ctx;
  VisionStatus payload;
  // El estado tiene dos formas (ver json_payload.hpp): el estado completo que publica
  // el servicio y el Will del broker, que solo lleva "en_linea". Por eso "en_linea" es
  // el unico campo obligatorio y los demas se aceptan ausentes.
  std::optional<std::string> timestamp;
  read_string(doc, "ts", false, true, timestamp, ctx);
  if (timestamp.has_value()) payload.timestamp = *timestamp;
  read_bool(doc, "en_linea", true, payload.online, ctx);
  std::optional<std::string> mode;
  read_string(doc, "modo", false, false, mode, ctx);
  check_optional_vocabulary(mode, "modo",
                   {vision_modes::kIdle, vision_modes::kAttendance, vision_modes::kEnrolling},
                   ctx);
  if (mode.has_value()) {
    if (*mode == vision_modes::kAttendance) {
      payload.mode = VisionMode::Attendance;
    } else if (*mode == vision_modes::kEnrolling) {
      payload.mode = VisionMode::Enrolling;
    } else {
      payload.mode = VisionMode::Idle;
    }
  }
  std::optional<int> gallery;
  read_int(doc, "galeria", false, false, gallery, ctx);
  if (gallery.has_value() && *gallery < 0) ctx.fail(Error::kInvalidValue, "galeria");
  payload.gallery_size = gallery.has_value() ? static_cast<std::size_t>(*gallery) : 0;
  std::optional<std::string> model;
  read_string(doc, "modelo", false, false, model, ctx);
  if (model.has_value()) payload.model = *model;
  if (timestamp.has_value()) check_timestamp(*timestamp, "ts", ctx);
  if (ctx.failed()) {
    result.error = ctx.error;
    result.field = ctx.field;
    return result;
  }
  result.payload = payload;
  return result;
}

ParseResult parse_gallery(const json& doc) {
  ParseResult result;
  Ctx ctx;
  GalleryAccount payload;
  read_string(doc, "cuenta", true, payload.account, ctx);
  check_account(payload.account, "cuenta", ctx);
  if (ctx.failed()) {
    result.error = ctx.error;
    result.field = ctx.field;
    return result;
  }
  result.payload = payload;
  return result;
}

}  // namespace

bool exceeds_depth_limit(std::string_view document) noexcept {
  // Cuenta los niveles de llaves y corchetes, sin contar los que estan dentro de una
  // cadena (un texto con "{" no anida nada).
  int depth = 0;
  bool en_cadena = false;
  bool escapado = false;
  for (const char caracter : document) {
    if (en_cadena) {
      if (escapado) {
        escapado = false;
      } else if (caracter == '\\') {
        escapado = true;
      } else if (caracter == '"') {
        en_cadena = false;
      }
      continue;
    }
    if (caracter == '"') {
      en_cadena = true;
    } else if (caracter == '{' || caracter == '[') {
      if (++depth > kMaxDocumentDepth) return true;
    } else if (caracter == '}' || caracter == ']') {
      // Una llave de mas no es un problema de profundidad: lo rechaza el parser.
      if (depth > 0) --depth;
    }
  }
  return false;
}

std::optional<std::string> serialize(const Payload& payload) {
  json doc = std::visit(Writer{}, payload);
  if (!doc.is_object()) {
    return std::nullopt;
  }
  // La hora del contrato tiene que tener el formato ISO 8601 con zona: si no, el
  // mensaje no sale (un `ts` mal formado rompe a los consumidores).
  const auto ts = doc.find("ts");
  if (ts != doc.end()) {
    if (!ts->is_string() || !is_iso8601_with_zone(ts->get<std::string>())) {
      return std::nullopt;
    }
  }
  try {
    return doc.dump();
  } catch (const json::exception&) {
    return std::nullopt;  // por ejemplo, texto que no es UTF-8 valido
  }
}

ParseResult deserialize(std::string_view topic, std::string_view document) {
  ParseResult result;
  // Barreras antes de parsear: el tamano y la profundidad. El parser de nlohmann es
  // descendente y recursivo, asi que un documento muy largo o muy anidado se rechaza
  // antes de tocarlo (un "[[[[..." de 256 KiB tumbaba el proceso con SIGSEGV).
  if (document.size() > kMaxDocumentBytes) {
    result.error = Error::kTooLarge;
    return result;
  }
  if (exceeds_depth_limit(document)) {
    result.error = Error::kTooDeep;
    return result;
  }
  json doc;
  try {
    doc = json::parse(std::string(document));
  } catch (const json::exception&) {
    result.error = Error::kInvalidJson;
    return result;
  }
  if (!doc.is_object()) {
    result.error = Error::kInvalidJson;
    return result;
  }

  if (topic == topics::kCameraPir) return parse_pir(doc);
  if (topic == topics::kActuatorCmd) return parse_actuator(doc);
  if (topic == topics::kVisionCmd) return parse_command(doc);
  if (topic == topics::kVisionResult) return parse_result(doc);
  if (topic == topics::kVisionEnrollment) return parse_enrollment(doc);
  if (topic == topics::kVisionEmbedding) return parse_embedding(doc);
  if (topic == topics::kVisionStatus) return parse_status(doc);
  if (topic == topics::kVisionGalleryAdd) return parse_gallery(doc);
  if (topic == topics::kVisionGalleryRemove) return parse_gallery(doc);

  result.error = Error::kUnknownTopic;
  return result;
}

}  // namespace vision::json
