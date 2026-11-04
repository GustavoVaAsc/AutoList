#pragma once

#include <atomic>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "vision/clock.hpp"
#include "vision/face_detector.hpp"
#include "vision/face_embedder.hpp"
#include "vision/gallery.hpp"
#include "vision/http_client.hpp"
#include "vision/message_bus.hpp"
#include "vision/wall_clock.hpp"

namespace vision {

struct ServiceConfig {
  std::string capture_url;  // GET que devuelve una foto, p. ej. la /capture de la XIAO
  int burst_size = 4;       // plan 5.1: de 3 a 5 fotos
  Millis settle_time{300};  // espera tras encender la luz, para que se estabilice la exposicion
  // Umbrales sin calibrar: se fijan con las pruebas E3 y la calibracion con LFW (plan sec. 9).
  double min_sharpness = 0.0;     // varianza del Laplaciano; bajo esto, imagen_insuficiente
  // Umbrales PROVISIONALES de la decision (contrato v0.3, seccion 4): la calibracion con
  // LFW es el hito del 18 de noviembre (plan, secciones 9 y 12.1), asi que estos valores
  // son de trabajo y hay que recalibrarlos (y volver a publicarlos) con el modelo real.
  // Con coseno >= min_recognized_score la decision es "reconocido"; entre
  // min_dudoso_score y min_recognized_score es "dudoso" (zona gris del plan 5.1, LED
  // ambar); por debajo de min_dudoso_score es "desconocido".
  float min_recognized_score = 0.60f;
  float min_dudoso_score = 0.45f;
  // Similitud (coseno) minima entre una pose y la media de las demas para que la pose
  // sobreviva al descarte de outliers del registro (plan 5.4, paso 5). Las que quedan
  // por debajo se descartan; si sobreviven menos de tres, el registro es inconsistente.
  // PROVISIONAL: se recalibra con LFW (hito del 18 de noviembre, plan, secciones 9 y 12.1).
  float min_pose_similarity = 0.60f;
  // Nombre y version del modelo de reconocimiento; lo publica el servicio en el
  // embedding y en el estado. Provisional hasta que exista el .nb (plan 6.2).
  std::string model_name = "w600k_mbf-uint8-v1";
  Millis pose_delay{3000};        // tiempo para que el alumno adopte cada pose
  int max_attempts_per_pose = 5;  // intentos antes de abortar el registro
  // La foto viene de la red y no es de fiar: se acota antes de decodificarla. Las
  // dimensiones por defecto cubren UXGA (1600x1200) y el OV3660 (2048x1536).
  Millis http_timeout{2000};                // por peticion de foto
  std::size_t max_body_bytes = 2 * 1024 * 1024;
  int max_image_width = 2048;
  int max_image_height = 1536;
  // La camara real manda JPEG. El PNG solo hace falta en pruebas y diagnostico.
  bool accept_png = false;
  // Tope de eventos de MQTT en espera de poll() (minimo 4); lo que llegue con la cola llena
  // se descarta. Los cancelar usan como mucho max_inbox_events - 3, para que el PIR, el probar
  // y el enrolar siempre quepan.
  std::size_t max_inbox_events = 16;
};

struct ServiceDeps {
  MessageBus& bus;
  HttpClient& http;
  FaceDetector& detector;
  FaceEmbedder& embedder;
  Gallery& gallery;
  Clock& clock;
  // Reloj de pared: estampa "ts" en los mensajes que publica el servicio. El de
  // milisegundos (Clock) no lleva fecha.
  WallClock& wall_clock;
};

enum class ServiceState { Idle, AttendanceInProgress, Enrolling };

// Maquina de estados del servicio (plan 5.1 y 5.4). Los mensajes del bus solo se
// encolan; el trabajo ocurre en poll(), asi que todo corre en el hilo que lo llama y
// un cancelar se atiende entre dos poses. Los PIR que llegan con el servicio ocupado
// se descartan.
//
// Cola: acotada (max_inbox_events) y sin respuesta cuando descarta. Un PIR y un
// probar en cola cuentan una vez cada uno (el enrolar no se coalesce); un enrolar con cuenta invalida se descarta antes
// de ocupar lugar (Node-RED valida al alumno antes de publicar, plan 5.4 paso 2); y los cancelar
// (vacios o con cuenta valida) se acotan a max_inbox_events - 3. Un enrolar o probar que llega con un registro en curso se contesta
// con un estado "error" y motivo "ocupado" y el registro sigue.
//
// Fallos: ninguna publicacion fallida deja el servicio atascado (estado ocupado, luz
// encendida o registro a medias); se registran en stderr. Si un evento lanza, poll() lo
// registra y sigue con el resto del lote.
class VisionService {
 public:
  // Lanza std::invalid_argument si la configuracion no es valida. Se suscribe a
  // aula/camara/pir y aula/vision/cmd; el bus debe vivir mas que el servicio, y al
  // destruirse (o si el constructor falla) se da de baja antes de liberar nada.
  VisionService(ServiceDeps deps, ServiceConfig config);
  VisionService(const VisionService&) = delete;
  VisionService& operator=(const VisionService&) = delete;

  // Atiende los mensajes pendientes y avanza el registro si ya toca la siguiente pose.
  void poll();

  // Salida ordenada: cancela el enrolamiento en curso (publicando "cancelado"), apaga
  // la luz (publicando aula/actuador/cmd con luz 0) y deja el estado en inactivo.
  //
  // Es una operacion explicita y no del destructor: cuando llega un SIGTERM el bus
  // puede estar ya caido y no serviria de nada. La llama el proceso antes de publicar
  // el estado final (aula/vision/estado con en_linea false).
  void shutdown();

  ServiceState state() const { return state_; }

 private:
  // `received` es el instante en que el evento llego al servicio (hilo del bus).
  struct Event {
    TimePoint received;
    std::variant<PirEvent, VisionCommand> payload;
  };

  struct FaceOutcome {
    std::vector<float> embedding;
    std::string_view reason;  // vacio si hubo rostro
    int det_ms = 0;
    int emb_ms = 0;
  };

  struct Enrollment {
    std::string account;
    std::optional<std::string> name;  // "nombre" del comando enrolar (opcional)
    std::size_t step = 0;  // indice 0-based de la pose en curso
    int attempts = 0;
    std::vector<std::vector<float>> embeddings;
    TimePoint due;  // cuando toca capturar la pose en curso
  };

  friend struct ServiceTestAccess;  // pruebas: llegar a lo privado sin pasar por la cola

  void on_message(const Payload& payload) noexcept;
  void enqueue(const Payload& payload, std::atomic<bool>*& claimed);
  void handle(const Event& event);
  void handle(const PirEvent& pir, TimePoint received);
  void handle(const VisionCommand& command, TimePoint received);

  void run_attendance(TimePoint received, AttendanceMode mode);
  AttendanceResult evaluate_attendance(TimePoint start, AttendanceMode mode);
  std::vector<cv::Mat> capture_burst();
  cv::Mat capture_frame();
  FaceOutcome analyze(const cv::Mat& frame);
  // Las publicaciones del servicio no lanzan: un fallo del bus se registra y se sigue.
  void publish_light(bool on);
  // El estado del servicio (aula/vision/estado): modo, galeria y modelo.
  void publish_status();
  // La hora del contrato (ISO 8601 con zona) para estampar "ts".
  std::string stamp_now() const;
  bool try_publish(std::string_view topic, const Payload& payload, const char* what);
  cv::Mat decode_capture(const HttpResponse& response) const;

  void start_enrollment(const std::string& account,
                        const std::optional<std::string>& name);
  void advance_enrollment();
  void finish_enrollment();
  void cancel_enrollment(const std::string& account);
  void publish_progress(const Enrollment& e, EnrollmentStatus status,
                        std::optional<std::string_view> reason = std::nullopt);
  void publish_refusal(const std::string& account, std::string_view reason);

  int elapsed_ms(TimePoint since) const;

  ServiceDeps deps_;
  ServiceConfig config_;
  std::atomic<ServiceState> state_{ServiceState::Idle};
  std::optional<Enrollment> enrollment_;

  std::mutex inbox_mutex_;
  std::deque<Event> inbox_;
  std::atomic<bool> pir_pending_{false};   // hay un PIR en cola o en proceso
  std::atomic<bool> test_pending_{false};    // idem para un probar

  // Lineas de log de la cola que se escriben una sola vez por poll(), sin el contenido de los
  // mensajes. Atomico y sin excepciones: se usa desde el hilo del bus, incluso al fallar.
  enum LogOnce : unsigned { kLogOverflow = 1, kLogCoalesced = 2, kLogInvalidEnroll = 4, kLogEnqueueFailed = 8, kLogInvalidCancel = 16 };
  std::atomic<unsigned> logged_{0};
  bool first_log(LogOnce which) noexcept { return (logged_.fetch_or(which) & which) == 0; }

  // Siempre los ultimos miembros: se destruyen primero, y al retornar su baja ningun
  // handler (que captura this) esta en curso ni volvera a ejecutarse.
  Subscription pir_subscription_;
  Subscription command_subscription_;
};

}  // namespace vision
