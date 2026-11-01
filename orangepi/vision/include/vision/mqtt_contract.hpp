#pragma once

// Contrato MQTT del servicio de vision: UNICO lugar con topicos y payloads.
// Los topicos espejan la seccion 7.1 de docs/HIGH-LEVEL-PLAN.md. Cada struct anota
// en comentarios las claves JSON que le corresponden; la (de)serializacion la hara el
// adaptador del bus. Para cambiar el contrato basta con editar este archivo.

#include <array>
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
inline constexpr std::string_view kSessionCmd = "aula/sesion/cmd";                 // pantalla -> Node-RED
inline constexpr std::string_view kScreenRestart = "aula/pantalla/reinicio";       // Node-RED -> pantalla
inline constexpr std::string_view kSessionStatus = "aula/sesion/estado";           // Node-RED -> todos
inline constexpr std::string_view kSystemMetrics = "aula/sistema/metricas";        // Orange Pi -> Node-RED
}  // namespace topics

// Valores del campo "motivo" (resultado y enrolamiento).
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

// no_cuenta: exactamente 9 digitos ASCII (VARCHAR(9) del esquema; el contrato lo exige).
// Todo identificador que llega por MQTT se valida con esto antes de usarlo o repetirlo.
constexpr bool is_valid_account(std::string_view account) {
  if (account.size() != 9) return false;
  for (const char c : account) {
    if (c < '0' || c > '9') return false;
  }
  return true;
}

// Valores del campo "pose" en el orden del registro (plan 5.4, paso 3).
inline constexpr std::array<std::string_view, 5> kEnrollmentPoses = {
    "frente", "izquierda", "derecha", "arriba", "frente"};

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

// "lat_ms" en milisegundos.
struct LatenciesMs {
  int cap = 0;
  int det = 0;
  int emb = 0;
  int total = 0;
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

// aula/vision/resultado  {"cuenta":"320068234","conf":0.78,"modo":"pase","lat_ms":{...}}
struct AttendanceResult {
  // El modo es obligatorio y no tiene valor por omision: un resultado nunca debe salir
  // como "pase" (que cuenta como asistencia) porque alguien se olvido de fijarlo.
  explicit AttendanceResult(AttendanceMode result_mode) : mode(result_mode) {}

  std::optional<std::string> account;  // "cuenta"; ausente si no hubo reconocimiento
  float confidence = 0.0f;             // "conf": similitud coseno del mejor candidato
  std::optional<std::string> reason;   // "motivo": ver namespace reasons
  AttendanceMode mode;                 // "modo": ver to_wire
  LatenciesMs lat_ms;
};

// aula/vision/cmd  {"accion":"enrolar","cuenta":"320068234"}
enum class VisionAction { Enroll, Cancel, Test };  // enrolar, cancelar, probar
struct VisionCommand {
  VisionAction action = VisionAction::Test;  // "accion"
  std::string account;                       // "cuenta"; vacia si el comando no la trae
};

// aula/vision/enrolamiento
//   {"cuenta":"...","paso":3,"total":5,"pose":"izquierda","estado":"rechazada","motivo":"borrosa"}
// Sin "miniatura": el esqueleto no genera miniaturas.
// Failed = el registro en curso fallo. Error = una orden se rechazo (ocupado, cuenta_invalida):
// no es parte del registro en curso, y va con paso 0 y sin pose. En el cable es "error".
enum class EnrollmentStatus { Pending, Accepted, Rejected, Cancelled, Failed, Error };
struct EnrollmentProgress {
  std::string account;                 // "cuenta"
  int step = 0;                        // "paso" (1..total; 0 si no hay registro en curso)
  int total = static_cast<int>(kEnrollmentPoses.size());
  std::string pose;                    // "pose"
  EnrollmentStatus status = EnrollmentStatus::Pending;  // "estado"
  std::optional<std::string> reason;   // "motivo"
};

// aula/vision/embedding  {"cuenta":"320068234","embedding":[...]}
struct EmbeddingMessage {
  std::string account;             // "cuenta"
  std::vector<float> embedding;    // "embedding": normalizado L2
};

// Todo lo que el servicio publica o recibe, para que el bus sea tipado.
using Payload = std::variant<PirEvent, ActuatorCommand, AttendanceResult, VisionCommand,
                             EnrollmentProgress, EmbeddingMessage>;

}  // namespace vision
