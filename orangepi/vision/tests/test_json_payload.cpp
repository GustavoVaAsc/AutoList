// Pruebas de la capa JSON del contrato MQTT: ida y vuelta de cada payload, nombres
// exactos de los campos, campos desconocidos ignorados y tipos erroneos rechazados
// sin lanzar.

#include <cmath>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "minitest.hpp"
#include "vision/gallery.hpp"
#include "vision/json_payload.hpp"

namespace {

using namespace vision;
namespace nj = nlohmann;

const std::string kTs = "2026-11-04T09:12:33.120-06:00";

// Serializa y devuelve el documento; si falla, la prueba falla.
std::string serialize_or_fail(const Payload& payload) {
  const std::optional<std::string> document = json::serialize(payload);
  if (!document.has_value()) throw minitest::Failure{"el payload no se pudo serializar"};
  return *document;
}

// Documento JSON del payload, ya parseado.
nj::json serialize_json(const Payload& payload) { return nj::json::parse(serialize_or_fail(payload)); }

// Deserializa y falla la prueba si la conversion no fue buena.
Payload deserialize_or_fail(std::string_view topic, const nj::json& document) {
  const json::ParseResult parsed = json::deserialize(topic, document.dump());
  if (!parsed.ok()) {
    throw minitest::Failure{"la conversion fallo: " + parsed.field};
  }
  return *parsed.payload;
}

AttendanceResult sample_result() {
  AttendanceResult result(AttendanceMode::Attendance);
  result.timestamp = kTs;
  result.decision = Decision::Recognized;
  result.account = "320068234";
  result.confidence = 0.78f;
  result.margin = 0.21f;
  result.lat_ms.cap = 180;
  result.lat_ms.det = 12;
  result.lat_ms.emb = 6;
  result.lat_ms.total = 240;
  return result;
}

EnrollmentProgress sample_enrollment() {
  EnrollmentProgress progress;
  progress.timestamp = kTs;
  progress.account = "320068234";
  progress.name = "Ana López";
  progress.step = 3;
  progress.total = 5;
  progress.pose = "izquierda";
  progress.status = EnrollmentStatus::Rejected;
  progress.reason = "borrosa";
  return progress;
}

EmbeddingMessage sample_embedding() {
  EmbeddingMessage message;
  message.timestamp = kTs;
  message.account = "320068234";
  message.model = "w600k_mbf-uint8-v1";
  message.embedding = {0.0123f, -0.0456f, 0.0f};
  return message;
}

VisionStatus sample_status() {
  VisionStatus status;
  status.timestamp = kTs;
  status.online = true;
  status.mode = VisionMode::Idle;
  status.gallery_size = 38;
  status.model = "w600k_mbf-uint8-v1";
  return status;
}

}  // namespace

// --- Resultado -----------------------------------------------------------------

TEST(resultado_tiene_los_campos_exactos_del_contrato) {
  const nj::json doc = serialize_json(sample_result());

  CHECK(doc.contains("ts"));
  CHECK(doc.contains("modo"));
  CHECK(doc.contains("decision"));
  CHECK(doc.contains("cuenta"));
  CHECK(doc.contains("conf"));
  CHECK(doc.contains("margen"));
  CHECK(doc.contains("lat_ms"));
  CHECK_EQ(doc.size(), std::size_t{7});
  CHECK_EQ(doc["ts"].get<std::string>(), kTs);
  CHECK_EQ(doc["modo"].get<std::string>(), std::string("pase"));
  CHECK_EQ(doc["decision"].get<std::string>(), std::string("reconocido"));
  CHECK_EQ(doc["cuenta"].get<std::string>(), std::string("320068234"));
  CHECK_NEAR(doc["conf"].get<double>(), 0.78, 1e-9);
  CHECK_NEAR(doc["margen"].get<double>(), 0.21, 1e-9);
  CHECK(doc["lat_ms"].contains("cap"));
  CHECK(doc["lat_ms"].contains("det"));
  CHECK(doc["lat_ms"].contains("emb"));
  CHECK(doc["lat_ms"].contains("total"));
  CHECK_EQ(doc["lat_ms"].size(), std::size_t{4});
  CHECK_EQ(doc["lat_ms"]["cap"].get<int>(), 180);
  CHECK_EQ(doc["lat_ms"]["total"].get<int>(), 240);
}

TEST(resultado_ida_y_vuelta_conserva_los_campos) {
  const Payload original = sample_result();
  const Payload parsed = deserialize_or_fail(topics::kVisionResult, serialize_json(original));

  const auto* result = std::get_if<AttendanceResult>(&parsed);
  CHECK(result != nullptr);
  CHECK_EQ(result->timestamp, kTs);
  CHECK(result->decision == Decision::Recognized);
  CHECK(result->mode == AttendanceMode::Attendance);
  CHECK_EQ(result->account.value(), std::string("320068234"));
  CHECK_NEAR(result->confidence.value(), 0.78f, 1e-6);
  CHECK_NEAR(result->margin.value(), 0.21f, 1e-6);
  CHECK_EQ(result->lat_ms.cap.value(), 180);
  CHECK_EQ(result->lat_ms.det.value(), 12);
  CHECK_EQ(result->lat_ms.emb.value(), 6);
  CHECK_EQ(result->lat_ms.total.value(), 240);
}

// Sin miniatura el resultado no cambia: el campo no aparece (ni vacio ni null).
TEST(resultado_sin_miniatura_no_la_inventa) {
  const nj::json doc = serialize_json(sample_result());
  CHECK(!doc.contains("miniatura"));
  CHECK_EQ(doc.size(), std::size_t{7});
}

// La miniatura del pase de lista viaja como un string mas del resultado (contrato MQTT
// v0.17): opcional, y los demas campos no cambian.
TEST(resultado_con_miniatura_la_publica_y_la_lee) {
  AttendanceResult result = sample_result();
  result.thumbnail = "BASE64";
  const nj::json doc = serialize_json(result);
  CHECK(doc.contains("miniatura"));
  CHECK_EQ(doc["miniatura"].get<std::string>(), std::string("BASE64"));
  CHECK_EQ(doc.size(), std::size_t{8});

  const Payload leida = deserialize_or_fail(topics::kVisionResult, doc);
  const auto& leido = std::get<AttendanceResult>(leida);
  CHECK_EQ(leido.thumbnail.value(), std::string("BASE64"));
  CHECK_EQ(leido.account.value(), std::string("320068234"));
}

// Una miniatura que no es un string es un tipo equivocado: la conversion falla sin lanzar.
TEST(resultado_rechaza_una_miniatura_que_no_es_texto) {
  nj::json doc = serialize_json(sample_result());
  doc["miniatura"] = 7;
  const json::ParseResult parsed = json::deserialize(topics::kVisionResult, doc.dump());
  CHECK(!parsed.ok());
  CHECK(parsed.error == json::Error::kWrongType);
  CHECK_EQ(parsed.field, std::string("miniatura"));
}

TEST(resultado_sin_cuenta_ni_conf_lleva_null) {
  AttendanceResult result(AttendanceMode::Test);
  result.timestamp = kTs;
  result.decision = Decision::NoFace;
  result.reason = std::string(reasons::kNoFace);
  result.lat_ms.cap = 100;

  const nj::json doc = serialize_json(result);
  CHECK(doc["cuenta"].is_null());
  CHECK(doc["conf"].is_null());
  CHECK(!doc.contains("margen"));  // opcional: no se manda si no hay segundo candidato
  CHECK_EQ(doc["modo"].get<std::string>(), std::string("prueba"));
  CHECK_EQ(doc["decision"].get<std::string>(), std::string("sin_rostro"));
  CHECK_EQ(doc["motivo"].get<std::string>(), std::string("sin_rostro"));
}

TEST(resultado_ignora_los_campos_desconocidos) {
  nj::json doc = serialize_json(sample_result());
  doc["extra"] = 42;
  doc["otro"] = nj::json::object({{"a", 1}});
  doc["lat_ms"]["extra"] = 1;

  const json::ParseResult parsed = json::deserialize(topics::kVisionResult, doc.dump());
  CHECK(parsed.ok());
  const auto* result = std::get_if<AttendanceResult>(&parsed.payload.value());
  CHECK(result != nullptr);
  CHECK_EQ(result->account.value(), std::string("320068234"));
}

TEST(resultado_rechaza_un_campo_obligatorio_que_falta) {
  nj::json doc = serialize_json(sample_result());
  doc.erase("decision");
  const json::ParseResult parsed = json::deserialize(topics::kVisionResult, doc.dump());
  CHECK(!parsed.ok());
  CHECK(parsed.error == json::Error::kMissingField);
  CHECK_EQ(parsed.field, std::string("decision"));
}

TEST(resultado_rechaza_un_tipo_incorrecto_sin_lanzar) {
  nj::json doc = serialize_json(sample_result());
  doc["conf"] = "alta";
  json::ParseResult parsed = json::deserialize(topics::kVisionResult, doc.dump());
  CHECK(!parsed.ok());
  CHECK(parsed.error == json::Error::kWrongType);
  CHECK_EQ(parsed.field, std::string("conf"));

  doc = serialize_json(sample_result());
  doc["cuenta"] = 320068234;  // un numero perderia los ceros a la izquierda
  parsed = json::deserialize(topics::kVisionResult, doc.dump());
  CHECK(!parsed.ok());
  CHECK(parsed.error == json::Error::kWrongType);
  CHECK_EQ(parsed.field, std::string("cuenta"));

  doc = serialize_json(sample_result());
  doc["lat_ms"] = 240;
  parsed = json::deserialize(topics::kVisionResult, doc.dump());
  CHECK(!parsed.ok());
  CHECK(parsed.error == json::Error::kWrongType);
  CHECK_EQ(parsed.field, std::string("lat_ms"));
}

TEST(resultado_rechaza_un_valor_fuera_del_vocabulario) {
  nj::json doc = serialize_json(sample_result());
  doc["decision"] = "tal vez";
  json::ParseResult parsed = json::deserialize(topics::kVisionResult, doc.dump());
  CHECK(!parsed.ok());
  CHECK(parsed.error == json::Error::kInvalidValue);
  CHECK_EQ(parsed.field, std::string("decision"));

  doc = serialize_json(sample_result());
  doc["modo"] = "ensayo";
  parsed = json::deserialize(topics::kVisionResult, doc.dump());
  CHECK(!parsed.ok());
  CHECK(parsed.error == json::Error::kInvalidValue);
}

TEST(resultado_rechaza_una_hora_mal_formada) {
  nj::json doc = serialize_json(sample_result());
  doc["ts"] = "2026-11-04 09:12:33";
  const json::ParseResult parsed = json::deserialize(topics::kVisionResult, doc.dump());
  CHECK(!parsed.ok());
  CHECK(parsed.error == json::Error::kInvalidValue);
  CHECK_EQ(parsed.field, std::string("ts"));
}

TEST(cuenta_conserva_los_ceros_a_la_izquierda) {
  AttendanceResult result(AttendanceMode::Attendance);
  result.timestamp = kTs;
  result.decision = Decision::Recognized;
  result.account = "012345678";
  result.confidence = 0.9f;

  const nj::json doc = serialize_json(result);
  CHECK(doc["cuenta"].is_string());
  CHECK_EQ(doc["cuenta"].get<std::string>(), std::string("012345678"));

  const Payload parsed = deserialize_or_fail(topics::kVisionResult, doc);
  const auto* back = std::get_if<AttendanceResult>(&parsed);
  CHECK(back != nullptr);
  CHECK_EQ(back->account.value(), std::string("012345678"));
}

TEST(lat_ms_admite_null_para_las_etapas_que_no_corrieron) {
  nj::json doc = serialize_json(sample_result());
  doc["lat_ms"]["det"] = nullptr;
  doc["lat_ms"]["emb"] = nullptr;

  const Payload parsed = deserialize_or_fail(topics::kVisionResult, doc);
  const auto* result = std::get_if<AttendanceResult>(&parsed);
  CHECK(result != nullptr);
  CHECK_EQ(result->lat_ms.cap.value(), 180);
  CHECK(!result->lat_ms.det.has_value());
  CHECK(!result->lat_ms.emb.has_value());
  CHECK_EQ(result->lat_ms.total.value(), 240);
}

// --- Enrolamiento ---------------------------------------------------------------

TEST(enrolamiento_tiene_los_campos_exactos_del_contrato) {
  const nj::json doc = serialize_json(sample_enrollment());

  CHECK(doc.contains("ts"));
  CHECK(doc.contains("cuenta"));
  CHECK(doc.contains("nombre"));
  CHECK(doc.contains("paso"));
  CHECK(doc.contains("total"));
  CHECK(doc.contains("pose"));
  CHECK(doc.contains("estado"));
  CHECK(doc.contains("motivo"));
  CHECK_EQ(doc.size(), std::size_t{8});
  CHECK_EQ(doc["paso"].get<int>(), 3);
  CHECK_EQ(doc["total"].get<int>(), 5);
  CHECK_EQ(doc["pose"].get<std::string>(), std::string("izquierda"));
  CHECK_EQ(doc["estado"].get<std::string>(), std::string("rechazada"));
  CHECK_EQ(doc["motivo"].get<std::string>(), std::string("borrosa"));
}

TEST(enrolamiento_ida_y_vuelta_conserva_los_campos) {
  const Payload parsed =
      deserialize_or_fail(topics::kVisionEnrollment, serialize_json(sample_enrollment()));

  const auto* progress = std::get_if<EnrollmentProgress>(&parsed);
  CHECK(progress != nullptr);
  CHECK_EQ(progress->timestamp, kTs);
  CHECK_EQ(progress->account, std::string("320068234"));
  CHECK_EQ(progress->name.value(), std::string("Ana López"));
  CHECK_EQ(progress->step, 3);
  CHECK_EQ(progress->total, 5);
  CHECK_EQ(progress->pose, std::string("izquierda"));
  CHECK(progress->status == EnrollmentStatus::Rejected);
  CHECK_EQ(progress->reason.value(), std::string("borrosa"));
}

TEST(enrolamiento_admite_todos_los_estados_del_contrato) {
  for (const std::string_view estado : {"esperando_pose", "capturando", "aceptada", "rechazada",
                                        "advertencia", "completado", "cancelado", "error"}) {
    nj::json doc = serialize_json(sample_enrollment());
    doc["estado"] = std::string(estado);
    const json::ParseResult parsed = json::deserialize(topics::kVisionEnrollment, doc.dump());
    CHECK(parsed.ok());
  }
}

// Los motivos de un registro con estado "rechazada" (v0.16: pose_incorrecta es nuevo e
// inconsistente tambien vale con "rechazada", porque la pose no se parece a "frente").
TEST(enrolamiento_admite_los_motivos_de_pose_y_de_identidad_con_rechazada) {
  for (const std::string_view motivo : {"pose_incorrecta", "giro_excesivo", "varios_rostros",
                                        "inconsistente"}) {
    EnrollmentProgress progress = sample_enrollment();
    progress.status = EnrollmentStatus::Rejected;
    progress.reason = std::string(motivo);
    const Payload parsed =
        deserialize_or_fail(topics::kVisionEnrollment, serialize_json(progress));
    const auto* back = std::get_if<EnrollmentProgress>(&parsed);
    CHECK(back != nullptr);
    CHECK(back->status == EnrollmentStatus::Rejected);
    CHECK_EQ(back->reason.value(), std::string(motivo));
  }
}

// v0.14: "duplicado" cierra el registro con "error" y lleva la cuenta parecida en "parecido_a".
TEST(enrolamiento_admite_el_error_duplicado_con_parecido_a) {
  EnrollmentProgress progress = sample_enrollment();
  progress.status = EnrollmentStatus::Error;
  progress.reason = std::string("duplicado");
  progress.similar_to = std::string("320068999");
  const Payload parsed = deserialize_or_fail(topics::kVisionEnrollment, serialize_json(progress));
  const auto* back = std::get_if<EnrollmentProgress>(&parsed);
  CHECK(back != nullptr);
  CHECK(back->status == EnrollmentStatus::Error);
  CHECK_EQ(back->reason.value(), std::string("duplicado"));
  CHECK_EQ(back->similar_to.value(), std::string("320068999"));
}

TEST(enrolamiento_admite_el_error_galeria_no_disponible) {
  EnrollmentProgress progress = sample_enrollment();
  progress.status = EnrollmentStatus::Error;
  progress.step = 0;
  progress.pose = "";
  progress.reason = std::string("galeria_no_disponible");
  const Payload parsed = deserialize_or_fail(topics::kVisionEnrollment, serialize_json(progress));
  const auto* back = std::get_if<EnrollmentProgress>(&parsed);
  CHECK(back != nullptr);
  CHECK_EQ(back->reason.value(), std::string("galeria_no_disponible"));
}

TEST(enrolamiento_rechaza_una_pose_fuera_del_contrato) {
  nj::json doc = serialize_json(sample_enrollment());
  doc["pose"] = "arriba_2";
  const json::ParseResult parsed = json::deserialize(topics::kVisionEnrollment, doc.dump());
  CHECK(!parsed.ok());
  CHECK(parsed.error == json::Error::kInvalidValue);
  CHECK_EQ(parsed.field, std::string("pose"));
}

TEST(enrolamiento_sin_miniatura_no_la_inventa) {
  const nj::json doc = serialize_json(sample_enrollment());
  CHECK(!doc.contains("miniatura"));
  CHECK(!doc.contains("parecido_a"));
}

// La miniatura viaja como un string mas del mensaje de la pose (contrato MQTT): el
// campo no aparece si no la hay.
TEST(enrolamiento_con_miniatura_la_publica) {
  EnrollmentProgress progress = sample_enrollment();
  progress.thumbnail = "BASE64";
  const nj::json doc = serialize_json(progress);
  CHECK(doc.contains("miniatura"));
  CHECK_EQ(doc["miniatura"].get<std::string>(), std::string("BASE64"));

  const Payload leida = deserialize_or_fail(topics::kVisionEnrollment, doc);
  CHECK_EQ(std::get<EnrollmentProgress>(leida).thumbnail.value(), std::string("BASE64"));
}

// --- Embedding ------------------------------------------------------------------

TEST(embedding_tiene_los_campos_exactos_del_contrato) {
  const nj::json doc = serialize_json(sample_embedding());

  CHECK(doc.contains("ts"));
  CHECK(doc.contains("cuenta"));
  CHECK(doc.contains("dim"));
  CHECK(doc.contains("modelo"));
  CHECK(doc.contains("embedding"));
  CHECK_EQ(doc.size(), std::size_t{5});
  CHECK_EQ(doc["dim"].get<int>(), 3);
  CHECK_EQ(doc["modelo"].get<std::string>(), std::string("w600k_mbf-uint8-v1"));
  CHECK(doc["embedding"].is_array());
  CHECK_EQ(doc["embedding"].size(), std::size_t{3});
  CHECK_NEAR(doc["embedding"][0].get<double>(), 0.0123, 1e-9);
  CHECK_NEAR(doc["embedding"][1].get<double>(), -0.0456, 1e-9);
}

TEST(embedding_ida_y_vuelta_conserva_los_campos) {
  const Payload parsed =
      deserialize_or_fail(topics::kVisionEmbedding, serialize_json(sample_embedding()));

  const auto* message = std::get_if<EmbeddingMessage>(&parsed);
  CHECK(message != nullptr);
  CHECK_EQ(message->timestamp, kTs);
  CHECK_EQ(message->account, std::string("320068234"));
  CHECK_EQ(message->model, std::string("w600k_mbf-uint8-v1"));
  CHECK_EQ(message->embedding.size(), std::size_t{3});
  CHECK_NEAR(message->embedding[0], 0.0123f, 1e-6);
  CHECK_EQ(message->dim(), 3);
}

TEST(embedding_de_512_dimensiones) {
  EmbeddingMessage message;
  message.timestamp = kTs;
  message.account = "320068234";
  message.model = "w600k_mbf-uint8-v1";
  message.embedding.resize(512);
  for (std::size_t i = 0; i < message.embedding.size(); ++i) {
    message.embedding[i] = static_cast<float>(i) / 512.0f;
  }

  const nj::json doc = serialize_json(message);
  CHECK_EQ(doc["dim"].get<int>(), 512);
  CHECK_EQ(doc["embedding"].size(), std::size_t{512});

  const Payload parsed = deserialize_or_fail(topics::kVisionEmbedding, doc);
  const auto* back = std::get_if<EmbeddingMessage>(&parsed);
  CHECK(back != nullptr);
  CHECK_EQ(back->embedding.size(), std::size_t{512});
  CHECK_EQ(back->dim(), 512);
  CHECK_NEAR(back->embedding[100], 100.0f / 512.0f, 1e-4);
}

// Node-RED rechaza un embedding cuya norma L2 se aleja mas de 0.01 de 1. El redondeo
// a 4 decimales de la serializacion no debe acercarse a ese limite.
TEST(embedding_normalizado_conserva_la_norma_despues_de_serializar) {
  std::vector<float> raw(512);
  for (std::size_t i = 0; i < raw.size(); ++i) {
    raw[i] = std::sin(static_cast<float>(i) * 0.37f) + 0.1f * static_cast<float>(i % 7);
  }
  EmbeddingMessage message;
  message.timestamp = kTs;
  message.account = "320068234";
  message.model = "w600k_mbf-uint8-v1";
  message.embedding = l2_normalized(raw);

  const std::optional<std::string> text = json::serialize(message);
  CHECK(text.has_value());
  const nj::json doc = nj::json::parse(*text);
  double sum = 0.0;
  for (const nj::json& value : doc["embedding"]) {
    const double x = value.get<double>();
    sum += x * x;
  }
  CHECK_NEAR(std::sqrt(sum), 1.0, 2e-3);
}

TEST(embedding_rechaza_un_dim_que_no_cuadra_con_el_vector) {
  nj::json doc = serialize_json(sample_embedding());
  doc["dim"] = 512;
  const json::ParseResult parsed = json::deserialize(topics::kVisionEmbedding, doc.dump());
  CHECK(!parsed.ok());
  CHECK(parsed.error == json::Error::kInvalidValue);
  CHECK_EQ(parsed.field, std::string("dim"));
}

TEST(embedding_rechaza_un_vector_con_valores_que_no_son_numeros) {
  nj::json doc = serialize_json(sample_embedding());
  doc["embedding"] = nj::json::array({0.1, "dos"});
  const json::ParseResult parsed = json::deserialize(topics::kVisionEmbedding, doc.dump());
  CHECK(!parsed.ok());
  CHECK(parsed.error == json::Error::kWrongType);
  CHECK_EQ(parsed.field, std::string("embedding"));
}

// --- Estado ---------------------------------------------------------------------

TEST(estado_tiene_los_campos_exactos_del_contrato) {
  const nj::json doc = serialize_json(sample_status());

  CHECK(doc.contains("ts"));
  CHECK(doc.contains("en_linea"));
  CHECK(doc.contains("modo"));
  CHECK(doc.contains("galeria"));
  CHECK(doc.contains("modelo"));
  CHECK_EQ(doc.size(), std::size_t{5});
  CHECK_EQ(doc["en_linea"].get<bool>(), true);
  CHECK_EQ(doc["modo"].get<std::string>(), std::string("inactivo"));
  CHECK_EQ(doc["galeria"].get<int>(), 38);
  CHECK_EQ(doc["modelo"].get<std::string>(), std::string("w600k_mbf-uint8-v1"));
}

TEST(estado_ida_y_vuelta_conserva_los_campos) {
  const Payload parsed = deserialize_or_fail(topics::kVisionStatus, serialize_json(sample_status()));

  const auto* status = std::get_if<VisionStatus>(&parsed);
  CHECK(status != nullptr);
  CHECK_EQ(status->timestamp, kTs);
  CHECK(status->online);
  CHECK(status->mode == VisionMode::Idle);
  CHECK_EQ(status->gallery_size, std::size_t{38});
  CHECK_EQ(status->model, std::string("w600k_mbf-uint8-v1"));
}

TEST(estado_rechaza_un_modo_fuera_del_vocabulario) {
  nj::json doc = serialize_json(sample_status());
  doc["modo"] = "dormido";
  const json::ParseResult parsed = json::deserialize(topics::kVisionStatus, doc.dump());
  CHECK(!parsed.ok());
  CHECK(parsed.error == json::Error::kInvalidValue);
  CHECK_EQ(parsed.field, std::string("modo"));
}

// --- Comandos, PIR, actuador y galeria --------------------------------------------

TEST(cmd_de_enrolar_ida_y_vuelta_con_nombre) {
  VisionCommand command;
  command.action = VisionAction::Enroll;
  command.account = "320068234";
  command.name = "Ana López";

  const nj::json doc = serialize_json(command);
  CHECK_EQ(doc["accion"].get<std::string>(), std::string("enrolar"));
  CHECK_EQ(doc["cuenta"].get<std::string>(), std::string("320068234"));
  CHECK_EQ(doc["nombre"].get<std::string>(), std::string("Ana López"));

  const Payload parsed = deserialize_or_fail(topics::kVisionCmd, doc);
  const auto* back = std::get_if<VisionCommand>(&parsed);
  CHECK(back != nullptr);
  CHECK(back->action == VisionAction::Enroll);
  CHECK_EQ(back->account, std::string("320068234"));
  CHECK_EQ(back->name.value(), std::string("Ana López"));
}

TEST(cmd_rechaza_una_accion_fuera_del_contrato) {
  const json::ParseResult parsed =
      json::deserialize(topics::kVisionCmd, R"({"accion":"confirmar","cuenta":"320068234"})");
  CHECK(!parsed.ok());
  CHECK(parsed.error == json::Error::kInvalidValue);
  CHECK_EQ(parsed.field, std::string("accion"));
}

TEST(cmd_de_enrolar_sin_cuenta_falla) {
  const json::ParseResult parsed = json::deserialize(topics::kVisionCmd, R"({"accion":"enrolar"})");
  CHECK(!parsed.ok());
  CHECK(parsed.error == json::Error::kMissingField);
  CHECK_EQ(parsed.field, std::string("cuenta"));
}

TEST(pir_ida_y_vuelta) {
  const nj::json doc = serialize_json(PirEvent{});
  CHECK_EQ(doc["mov"].get<int>(), 1);

  const Payload parsed = deserialize_or_fail(topics::kCameraPir, doc);
  const auto* event = std::get_if<PirEvent>(&parsed);
  CHECK(event != nullptr);
  CHECK(event->motion);
}

TEST(actuador_solo_manda_los_campos_presentes) {
  ActuatorCommand command;
  command.light = 1;
  const nj::json doc = serialize_json(command);
  CHECK_EQ(doc.size(), std::size_t{1});
  CHECK_EQ(doc["luz"].get<int>(), 1);

  const Payload parsed = deserialize_or_fail(topics::kActuatorCmd, doc);
  const auto* back = std::get_if<ActuatorCommand>(&parsed);
  CHECK(back != nullptr);
  CHECK_EQ(back->light.value(), 1);
  CHECK(!back->servo.has_value());
  CHECK(!back->led.has_value());
  CHECK(!back->buzzer.has_value());
}

TEST(galeria_alta_y_baja_ida_y_vuelta) {
  for (const std::string_view topic : {topics::kVisionGalleryAdd, topics::kVisionGalleryRemove}) {
    GalleryAccount gallery;
    gallery.account = "320068234";

    const nj::json doc = serialize_json(gallery);
    CHECK_EQ(doc.size(), std::size_t{1});
    CHECK_EQ(doc["cuenta"].get<std::string>(), std::string("320068234"));

    const Payload parsed = deserialize_or_fail(topic, doc);
    const auto* back = std::get_if<GalleryAccount>(&parsed);
    CHECK(back != nullptr);
    CHECK_EQ(back->account, std::string("320068234"));
  }
}

TEST(galeria_rechaza_una_cuenta_con_menos_de_nueve_digitos) {
  const json::ParseResult parsed =
      json::deserialize(topics::kVisionGalleryAdd, R"({"cuenta":"12345"})");
  CHECK(!parsed.ok());
  CHECK(parsed.error == json::Error::kInvalidValue);
  CHECK_EQ(parsed.field, std::string("cuenta"));
}

// --- Errores generales -------------------------------------------------------------

TEST(json_invalido_no_lanza_y_no_devuelve_payload) {
  const json::ParseResult parsed = json::deserialize(topics::kVisionResult, "{no es nj::json");
  CHECK(!parsed.ok());
  CHECK(parsed.error == json::Error::kInvalidJson);
  CHECK(!parsed.payload.has_value());
}

TEST(un_arreglo_no_es_un_payload) {
  const json::ParseResult parsed = json::deserialize(topics::kVisionResult, "[1,2,3]");
  CHECK(!parsed.ok());
  CHECK(parsed.error == json::Error::kInvalidJson);
}

TEST(topico_fuera_del_contrato_no_se_convierte) {
  const json::ParseResult parsed = json::deserialize("aula/sesion/cmd", R"({"accion":"iniciar"})");
  CHECK(!parsed.ok());
  CHECK(parsed.error == json::Error::kUnknownTopic);
}

TEST(el_error_no_incluye_el_contenido_del_mensaje) {
  const json::ParseResult parsed =
      json::deserialize(topics::kVisionCmd, R"({"accion":"enrolar","cuenta":320068234})");
  CHECK(!parsed.ok());
  CHECK_EQ(parsed.field, std::string("cuenta"));
  CHECK(parsed.field.find("320068234") == std::string::npos);
}

TEST(un_ts_mal_formado_no_se_serializa) {
  AttendanceResult result(AttendanceMode::Attendance);
  result.timestamp = "ayer";
  result.decision = Decision::Recognized;

  CHECK(!json::serialize(result).has_value());
}

TEST(los_mensajes_sin_hora_se_serializan) {
  CHECK(json::serialize(PirEvent{}).has_value());
  CHECK(json::serialize(GalleryAccount{"320068234"}).has_value());
}

// --- Will y forma minima del estado -----------------------------------------------

// El Will del broker es {"en_linea":false}: la forma minima del estado. La capa JSON
// la tiene que aceptar (si no, cualquier consumidor con esta capa lo descartaria).
TEST(el_will_del_broker_se_acepta_como_estado) {
  const json::ParseResult parsed = json::deserialize(topics::kVisionStatus, R"({"en_linea":false})");
  CHECK(parsed.ok());
  const auto* status = std::get_if<VisionStatus>(&*parsed.payload);
  CHECK(status != nullptr);
  CHECK(!status->online);
  // Los campos que no vienen se quedan vacios: el Will no lleva hora ni galeria.
  CHECK(status->timestamp.empty());
  CHECK_EQ(status->gallery_size, std::size_t{0});
}

TEST(el_estado_minimo_sin_en_linea_se_rechaza) {
  const json::ParseResult parsed = json::deserialize(topics::kVisionStatus, R"({"modo":"inactivo"})");
  CHECK(!parsed.ok());
  CHECK(parsed.error == json::Error::kMissingField);
  CHECK_EQ(parsed.field, std::string("en_linea"));
}

// "cancelar" sin cuenta: la cuenta vacia significa "cancela el registro en curso", que
// es lo que el servicio admite.
TEST(cancelar_sin_cuenta_se_acepta) {
  const json::ParseResult parsed =
      json::deserialize(topics::kVisionCmd, R"({"accion":"cancelar"})");
  CHECK(parsed.ok());
  const auto* command = std::get_if<VisionCommand>(&*parsed.payload);
  CHECK(command != nullptr);
  CHECK(command->action == VisionAction::Cancel);
  CHECK(command->account.empty());
}

// --- Topes de tamano y profundidad ---------------------------------------------------

TEST(un_json_muy_anidado_se_rechaza_sin_lanzar) {
  std::string document = R"({"accion":"enrolar","cuenta":"320068234","extra":)";
  document += std::string(40, '[');
  document += "1";
  document += std::string(40, ']');
  document += "}";

  const json::ParseResult parsed = json::deserialize(topics::kVisionCmd, document);
  CHECK(!parsed.ok());
  CHECK(parsed.error == json::Error::kTooDeep);
  CHECK(!parsed.payload.has_value());
}

TEST(un_json_muy_grande_se_rechaza_sin_lanzar) {
  std::string document = R"({"accion":"enrolar","cuenta":"320068234","nombre":")";
  document += std::string(json::kMaxDocumentBytes + 1, 'a');
  document += R"("})";

  const json::ParseResult parsed = json::deserialize(topics::kVisionCmd, document);
  CHECK(!parsed.ok());
  CHECK(parsed.error == json::Error::kTooLarge);
  CHECK(!parsed.payload.has_value());
}

TEST(el_calculo_de_profundidad_ignora_las_cadenas) {
  // Un texto con llaves y corchetes no anida nada.
  CHECK(!json::exceeds_depth_limit(R"({"texto":"[[[[[[[[[["} )"));
  CHECK(!json::exceeds_depth_limit(R"({"a":1})"));
  CHECK(json::exceeds_depth_limit(std::string(33, '[')));
  CHECK(!json::exceeds_depth_limit(std::string(32, '[')));
}
