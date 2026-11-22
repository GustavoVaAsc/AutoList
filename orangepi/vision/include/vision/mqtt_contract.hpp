#pragma once

// Contrato MQTT del servicio de vision: UNICO lugar con topicos y payloads.
// La fuente de verdad es docs/contrato-mqtt.md (v0.3); su seccion 7.1 es el resumen.
// Cada struct anota en comentarios las claves JSON que le corresponden; la
// (de)serializacion la hace la capa JSON (include/vision/json_payload.hpp). Para
// cambiar el contrato basta con editar este archivo y el documento.

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace vision {

namespace topics {
inline constexpr std::string_view kCameraPir = "aula/camara/pir";                  // XIAO -> borde
inline constexpr std::string_view kCameraEnergy = "aula/camara/energia";           // XIAO -> borde
inline constexpr std::string_view kCameraStatus = "aula/camara/estado";            // XIAO -> borde
inline constexpr std::string_view kActuatorCmd = "aula/actuador/cmd";              // borde -> XIAO
inline constexpr std::string_view kActuatorStatus = "aula/actuador/estado";        // XIAO -> borde
inline constexpr std::string_view kVisionResult = "aula/vision/resultado";         // vision -> Node-RED
inline constexpr std::string_view kVisionCmd = "aula/vision/cmd";                  // Node-RED -> vision
inline constexpr std::string_view kVisionEnrollment = "aula/vision/enrolamiento";  // vision -> frontend, LCD
inline constexpr std::string_view kVisionEmbedding = "aula/vision/embedding";      // vision -> Node-RED
inline constexpr std::string_view kVisionStatus = "aula/vision/estado";            // vision -> Node-RED, frontend
inline constexpr std::string_view kVisionGalleryAdd = "aula/vision/galeria/alta";  // Node-RED -> vision
inline constexpr std::string_view kVisionGalleryRemove = "aula/vision/galeria/baja";  // Node-RED -> vision
inline constexpr std::string_view kSessionCmd = "aula/sesion/cmd";                 // pantalla -> Node-RED
inline constexpr std::string_view kScreenRestart = "aula/pantalla/reinicio";       // Node-RED -> pantalla
inline constexpr std::string_view kSessionStatus = "aula/sesion/estado";           // Node-RED -> todos
inline constexpr std::string_view kSystemMetrics = "aula/sistema/metricas";        // Orange Pi -> Node-RED
}  // namespace topics

// Motivos del resultado: extension del contrato v0.3. "decision" dice que paso y
// "motivo" da el detalle fino (el esquema SQL intentos.motivo lo necesita).
namespace reasons {
inline constexpr std::string_view kCaptureFailed = "captura_fallida";
inline constexpr std::string_view kInsufficientImage = "imagen_insuficiente";
inline constexpr std::string_view kNoFace = "sin_rostro";
inline constexpr std::string_view kUnknown = "desconocido";
inline constexpr std::string_view kEmptyGallery = "galeria_vacia";
inline constexpr std::string_view kInternalError = "error_interno";
inline constexpr std::string_view kBusy = "ocupado";
inline constexpr std::string_view kInvalidAccount = "cuenta_invalida";
}  // namespace reasons

// Motivos del enrolamiento (contrato v0.3, seccion 4): con estado "rechazada" y con
// estado "error".
namespace enrollment_reasons {
inline constexpr std::string_view kNoFace = "sin_rostro";
inline constexpr std::string_view kSeveralFaces = "varios_rostros";
inline constexpr std::string_view kSmallFace = "rostro_chico";
inline constexpr std::string_view kBlurry = "borrosa";
inline constexpr std::string_view kExcessiveTurn = "giro_excesivo";
// Extension (v0.16): la cabeza no esta en la pose pedida (ver head_pose.hpp).
inline constexpr std::string_view kWrongPose = "pose_incorrecta";
inline constexpr std::string_view kCaptureFailure = "fallo_captura";
inline constexpr std::string_view kBusy = "ocupado";
// Con estado "error": las cinco fotos no se parecen entre si. Con "rechazada" (v0.16): la pose
// no se parece a la de "frente" (puede ser otra persona); la pose se repite.
inline constexpr std::string_view kInconsistent = "inconsistente";
inline constexpr std::string_view kNpu = "npu";
// Extension (v0.16), con estado "error": el rostro del registro ya pertenece a otra cuenta de
// la galeria (coseno >= min_recognized_score); "parecido_a" lleva esa cuenta.
inline constexpr std::string_view kDuplicate = "duplicado";
// Extension (v0.16), con estado "error": la galeria en RAM no es de fiar (la ultima recarga
// fallo o no se pudo consultar), asi que no se puede saber si el rostro ya esta registrado.
inline constexpr std::string_view kGalleryUnavailable = "galeria_no_disponible";
// Extension: la cuenta del comando no es un no_cuenta valido.
inline constexpr std::string_view kInvalidAccount = "cuenta_invalida";
}  // namespace enrollment_reasons

// no_cuenta: exactamente 9 digitos ASCII (VARCHAR(9) del esquema; el contrato lo exige).
// Todo identificador que llega por MQTT se valida con esto antes de usarlo o repetirlo.
constexpr bool is_valid_account(std::string_view account) {
  if (account.size() != 9) return false;
  for (const char c : account) {
    if (c < '0' || c > '9') {
      return false;
    }
  }
  return true;
}

// Valores del campo "pose" en el orden del registro (plan 5.4, paso 3). La quinta pose
// es "frente_2": otra toma de frente, no una repeticion de la primera.
inline constexpr std::array<std::string_view, 5> kEnrollmentPoses = {
    "frente", "izquierda", "derecha", "arriba", "frente_2"};

// aula/camara/pir  {"mov":1}
struct PirEvent {
  bool motion = true;  // "mov"
};

// aula/actuador/cmd  {"servo":"abrir","led":"verde","buzzer":1,"luz":0}
// Solo se serializan los campos presentes. La vision solo usa "luz".
struct ActuatorCommand {
  std::optional<std::string> servo;
  std::optional<std::string> led;
  std::optional<int> buzzer;
  std::optional<int> light;  // "luz"
};

// "lat_ms" en milisegundos. Cada etapa va en null si no corrio.
struct LatenciesMs {
  std::optional<int> cap;    // "cap"
  std::optional<int> det;    // "det"
  std::optional<int> emb;    // "emb"
  std::optional<int> total;  // "total"
};

// Campo "modo" del resultado: quien lo origino. Los nombres de campo y valor pueden cambiar
// con el contrato; solo se tocan aqui.
enum class AttendanceMode { Attendance, Test };  // pase (por PIR), prueba (comando probar)
namespace modes {
inline constexpr std::string_view kField = "modo";
inline constexpr std::string_view kAttendance = "pase";
inline constexpr std::string_view kTest = "prueba";
}  // namespace modes
constexpr std::string_view to_wire(AttendanceMode mode) {
  return mode == AttendanceMode::Test ? modes::kTest : modes::kAttendance;
}

// Campo "decision" del resultado: el veredicto del servicio de vision. El servicio es el
// dueno del modelo y de los umbrales, asi que es quien decide (contrato v0.3, seccion 4).
enum class Decision {
  Recognized,         // reconocido
  Doubtful,           // dudoso: zona gris, LED ambar (plan 5.1)
  Unknown,            // desconocido
  NoFace,             // sin_rostro
  InsufficientImage,  // imagen_insuficiente
  Error,              // error: fallo interno del servicio (extension del contrato)
};
namespace decisions {
inline constexpr std::string_view kRecognized = "reconocido";
inline constexpr std::string_view kDoubtful = "dudoso";
inline constexpr std::string_view kUnknown = "desconocido";
inline constexpr std::string_view kNoFace = "sin_rostro";
inline constexpr std::string_view kInsufficientImage = "imagen_insuficiente";
inline constexpr std::string_view kError = "error";
}  // namespace decisions
constexpr std::string_view to_wire(Decision decision) {
  switch (decision) {
    case Decision::Recognized:
      return decisions::kRecognized;
    case Decision::Doubtful:
      return decisions::kDoubtful;
    case Decision::Unknown:
      return decisions::kUnknown;
    case Decision::NoFace:
      return decisions::kNoFace;
    case Decision::InsufficientImage:
      return decisions::kInsufficientImage;
    case Decision::Error:
      return decisions::kError;
  }
  return decisions::kError;
}

// Campo "modo" del estado del servicio: inactivo, pase o enrolando.
enum class VisionMode { Idle, Attendance, Enrolling };
namespace vision_modes {
inline constexpr std::string_view kIdle = "inactivo";
inline constexpr std::string_view kAttendance = "pase";
inline constexpr std::string_view kEnrolling = "enrolando";
}  // namespace vision_modes
constexpr std::string_view to_wire(VisionMode mode) {
  switch (mode) {
    case VisionMode::Attendance:
      return vision_modes::kAttendance;
    case VisionMode::Enrolling:
      return vision_modes::kEnrolling;
    case VisionMode::Idle:
      break;
  }
  return vision_modes::kIdle;
}

// aula/vision/resultado
//   {"ts":"...","modo":"pase","decision":"reconocido","cuenta":"320068234","conf":0.78,
//    "margen":0.21,"lat_ms":{"cap":180,"det":12,"emb":6,"total":240}}
struct AttendanceResult {
  // El modo es obligatorio y no tiene valor por omision: un resultado nunca debe salir
  // como "pase" (que cuenta como asistencia) porque alguien se olvido de fijarlo.
  explicit AttendanceResult(AttendanceMode result_mode) : mode(result_mode) {}

  std::string timestamp;                 // "ts": ISO 8601 con zona
  Decision decision = Decision::Unknown;  // "decision"
  std::optional<std::string> account;     // "cuenta": mejor candidato; null si no hay
  std::optional<float> confidence;        // "conf": similitud coseno; null si no hubo candidato
  std::optional<float> margin;            // "margen": diferencia con el segundo candidato
  std::optional<std::string> reason;      // "motivo": detalle fino (extension del contrato)
  AttendanceMode mode;                    // "modo": ver to_wire
  LatenciesMs lat_ms;                    // "lat_ms"
};

// aula/vision/cmd  {"accion":"enrolar","cuenta":"320068234","nombre":"Ana López"}
// La accion "confirmar" se retiro del contrato v0.2: no existe.
enum class VisionAction { Enroll, Cancel, Test };  // enrolar, cancelar, probar
namespace actions {
inline constexpr std::string_view kEnroll = "enrolar";
inline constexpr std::string_view kCancel = "cancelar";
inline constexpr std::string_view kTest = "probar";
}  // namespace actions
constexpr std::string_view to_wire(VisionAction action) {
  switch (action) {
    case VisionAction::Enroll:
      return actions::kEnroll;
    case VisionAction::Cancel:
      return actions::kCancel;
    case VisionAction::Test:
      break;
  }
  return actions::kTest;
}
struct VisionCommand {
  VisionAction action = VisionAction::Test;    // "accion"
  std::string account;                         // "cuenta"; vacia si el comando no la trae
  std::optional<std::string> name;             // "nombre"; opcional, solo lo usa enrolar
};

// aula/vision/enrolamiento
//   {"ts":"...","cuenta":"...","nombre":"...","paso":3,"total":5,"pose":"izquierda",
//    "estado":"rechazada","motivo":"borrosa"}
// El estado "error" es el de una orden que no se puede atender (ocupado, cuenta_invalida):
// no es parte del registro en curso, y va con paso 0 y sin pose.
enum class EnrollmentStatus {
  WaitingPose,  // esperando_pose
  Capturing,    // capturing
  Accepted,     // aceptada
  Rejected,     // rechazada
  Warning,      // advertencia: parecido con otro alumno (todavia no se emite)
  Completed,    // completado: las 5 poses aceptadas
  Cancelled,    // cancelado
  Error,        // error
};
namespace enrollment_statuses {
inline constexpr std::string_view kWaitingPose = "esperando_pose";
inline constexpr std::string_view kCapturing = "capturando";
inline constexpr std::string_view kAccepted = "aceptada";
inline constexpr std::string_view kRejected = "rechazada";
inline constexpr std::string_view kWarning = "advertencia";
inline constexpr std::string_view kCompleted = "completado";
inline constexpr std::string_view kCancelled = "cancelado";
inline constexpr std::string_view kError = "error";
}  // namespace enrollment_statuses
constexpr std::string_view to_wire(EnrollmentStatus status) {
  switch (status) {
    case EnrollmentStatus::WaitingPose:
      return enrollment_statuses::kWaitingPose;
    case EnrollmentStatus::Capturing:
      return enrollment_statuses::kCapturing;
    case EnrollmentStatus::Accepted:
      return enrollment_statuses::kAccepted;
    case EnrollmentStatus::Rejected:
      return enrollment_statuses::kRejected;
    case EnrollmentStatus::Warning:
      return enrollment_statuses::kWarning;
    case EnrollmentStatus::Completed:
      return enrollment_statuses::kCompleted;
    case EnrollmentStatus::Cancelled:
      return enrollment_statuses::kCancelled;
    case EnrollmentStatus::Error:
      break;
  }
  return enrollment_statuses::kError;
}

struct EnrollmentProgress {
  std::string timestamp;    // "ts": ISO 8601 con zona
  std::string account;      // "cuenta"; vacia si la cuenta recibida no era valida
  std::optional<std::string> name;  // "nombre"
  int step = 0;             // "paso" (1..total; 0 si no hay registro en curso)
  int total = static_cast<int>(kEnrollmentPoses.size());  // "total"
  std::string pose;         // "pose"; vacia si no hay registro en curso
  EnrollmentStatus status = EnrollmentStatus::WaitingPose;  // "estado"
  std::optional<std::string> reason;   // "motivo": ver namespace enrollment_reasons
  std::optional<std::string> similar_to;  // "parecido_a": opcional, el esqueleto no lo emite
  std::optional<std::string> thumbnail;   // "miniatura": JPEG en base64 (160x120, ~8 KB); solo con aceptada o rechazada
};

// aula/vision/embedding  {"ts":"...","cuenta":"320068234","dim":512,"modelo":"...",
//                          "embedding":[0.0123,-0.0456]}
struct EmbeddingMessage {
  std::string timestamp;          // "ts": ISO 8601 con zona
  std::string account;            // "cuenta"
  std::string model;              // "modelo": nombre y version del .nb
  std::vector<float> embedding;   // "embedding": normalizado L2
  // "dim" no es un campo aparte: lo fija el tamano del vector (lo escribe la capa JSON).
  int dim() const { return static_cast<int>(embedding.size()); }
};

// aula/vision/estado (retenido)  {"ts":"...","en_linea":true,"modo":"inactivo","galeria":38,
//                                   "modelo":"w600k_mbf-uint8-v1"}
// El LWT {"en_linea":false} lo publica el adaptador MQTT, no el servicio.
struct VisionStatus {
  std::string timestamp;   // "ts": ISO 8601 con zona
  bool online = true;      // "en_linea"
  VisionMode mode = VisionMode::Idle;  // "modo"
  std::size_t gallery_size = 0;        // "galeria": embeddings en RAM
  std::string model;       // "modelo"
};

// aula/vision/galeria/alta y aula/vision/galeria/baja  {"cuenta":"320068234"}
// Los publica Node-RED; la vision los consume.
struct GalleryAccount {
  std::string account;  // "cuenta"
};

// Todo lo que el servicio publica o recibe, para que el bus sea tipado.
using Payload = std::variant<PirEvent, ActuatorCommand, AttendanceResult, VisionCommand,
                             EnrollmentProgress, EmbeddingMessage, VisionStatus, GalleryAccount>;

}  // namespace vision
