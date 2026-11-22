#pragma once

#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "vision/captura_firma.hpp"
#include "vision/clock.hpp"
#include "vision/enrollment_limits.hpp"
#include "vision/face_detector.hpp"
#include "vision/face_embedder.hpp"
#include "vision/gallery.hpp"
#include "vision/head_pose.hpp"
#include "vision/http_client.hpp"
#include "vision/message_bus.hpp"
#include "vision/reloj_guardia.hpp"
#include "vision/wall_clock.hpp"

namespace vision {

struct ServiceConfig {
  std::string capture_url;  // GET que devuelve una foto, p. ej. la /capture de la XIAO
  // Secreto compartido con la placa para firmar la peticion de captura (HMAC-SHA256,
  // vision/captura_firma.hpp). Lo lee el proceso del entorno y se pasa aqui en memoria:
  // nunca va en el archivo de configuracion. Vacio = la peticion va sin firma (la
  // camara falsa de pruebas ignora las cabeceras). Con secreto, si el cliente HTTP no
  // admite cabeceras o no se puede firmar, no se pide la foto: nunca se manda sin firma
  // una peticion que se queria firmada.
  std::string capture_hmac_secret;
  // La placa limita el ritmo de capturas (4 por ventana de 2000 ms y despues una pausa de
  // 1000 ms): una rafaga no empieza antes de este tiempo despues del inicio de la anterior
  // (se espera con Clock::sleep_for). 0 = sin espaciado (las pruebas); el proceso de
  // produccion lo toma de captura.intervalo_rafagas_ms (3000).
  Millis burst_interval{0};
  // Ante un 429 se espera y se repite esa foto UNA vez: lo que diga Retry-After, o
  // retry_wait si no viene. Entre todas las fotos de una rafaga (o de una pose) la espera
  // extra no pasa de max_retry_wait: una espera que ya no cabe no se hace y la foto cuenta
  // como fallida.
  Millis retry_wait{1100};
  Millis max_retry_wait{3000};
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
  // Similitud (coseno) minima entre una pose y la media de las candidatas para que la pose
  // sobreviva al descarte de outliers del registro (plan 5.4, paso 5). El descarte es
  // iterativo: se descarta solo la peor mientras su coseno con la media quede por debajo
  // del umbral, se recalcula la media y se repite; si sobreviven menos de tres, el
  // registro es inconsistente.
  // PROVISIONAL: se recalibra con LFW (hito del 18 de noviembre, plan, secciones 9 y 12.1).
  float min_pose_similarity = 0.50f;
  // Verificacion de la pose en el registro (plan 5.4, paso 3): con los 5 landmarks se estima
  // el giro (yaw) y la inclinacion (pitch) de la cabeza, y la pose que no coincide con la
  // pedida se rechaza con pose_incorrecta (o giro_excesivo si el giro es exagerado). Los
  // limites, y el sentido izquierda/derecha, estan en PoseLimits (head_pose.hpp).
  // PROVISIONAL: se calibra con la ESP32 a partir de los yaw y pitch que se registran por
  // stderr en cada pose.
  PoseLimits pose_limits;
  // Identidad entre poses (plan 5.4, paso 5): cada pose despues de "frente" se compara
  // (coseno de embeddings normalizados) con el embedding de la pose "frente" aceptada; por
  // debajo del umbral la pose se rechaza como inconsistente y se repite (cuenta para
  // max_attempts_per_pose y para max_identity_rejections). Al cerrar, cada pose lateral
  // superviviente se compara tambien con "frente_2" con el mismo umbral.
  //
  // Los dos valores son una HIPOTESIS PROVISIONAL, sin datos de la ESP32. Lo unico medido (3
  // fotos fijas de ALTA calidad, no de la camara de la placa, docs/RESULTADOS-PRUEBAS.md) es
  // que dos personas distintas dan menos de 0.1. Que la MISMA persona de perfil de entre 0.3 y
  // 0.5 contra su frente es una ESTIMACION, sin medir. 0.20 queda entre esos dos valores, asi que
  // solo filtra un cambio BURDO de persona (no un parecido razonable) y no sustituye al
  // chequeo de duplicados ni a la presencia del profesor. "frente_2" es la misma pose que
  // "frente": se espera un coseno mucho mas alto que en perfil, y su umbral es mas alto que el
  // de las demas poses (no puede ser menor). Un umbral <= 0 desactiva la comprobacion (incluso
  // con coseno negativo); el servicio avisa por stderr al arrancar.
  float min_front_similarity = enrollment_limits::kMinFrontSimilarity;
  float min_front_2_similarity = enrollment_limits::kMinFront2Similarity;
  // Tope de rechazos por identidad (inconsistente) en TODO el registro, sin reiniciarse por
  // pose: al llegar a este numero el registro termina con error e inconsistente. Una persona
  // que cambia de cara repetidas veces no puede seguir intentando indefinidamente (cada pose
  // tiene sus propios max_attempts_per_pose). >= 1.
  int max_identity_rejections = enrollment_limits::kMaxIdentityRejections;
  // Duplicados: al cerrar el registro se busca la media final en la galeria en RAM, sin contar
  // la cuenta que se registra (que pueda re-registrarse). Si otra cuenta da un coseno >= este
  // valor, el registro cierra con error y duplicado (parecido_a lleva esa cuenta). Tiene
  // umbral propio y no puede pasar de min_dudoso_score: con min_recognized_score, una persona
  // podria tener dos cuentas con parecidos de 0.45 a 0.59 que el pase de lista ya trata como
  // "dudoso". En (0, min_dudoso_score]. PROVISIONAL.
  float min_duplicate_similarity = enrollment_limits::kMinDuplicateSimilarity;
  // Rostros de fondo durante el registro: la principal es la deteccion de mayor area (la
  // del alumno, la mas cercana a la camara) entre las de score >= other_face_min_score (un
  // falso positivo grande y de score bajo no es el principal). Las demas se ignoran si son
  // claramente pequenas (area menor que other_face_min_area_ratio de la principal) o de score
  // bajo (menor que other_face_min_score); cualquiera que quede cuenta como otro rostro y la
  // pose se rechaza con varios_rostros. Conservador a proposito: ignorar de mas deja pasar a
  // alguien cerca de la camara, y el rechazo de mas solo repite la pose. PROVISIONAL.
  float other_face_min_area_ratio = enrollment_limits::kOtherFaceMinAreaRatio;
  float other_face_min_score = enrollment_limits::kOtherFaceMinScore;
  // Nombre y version del modelo de reconocimiento; lo publica el servicio en el
  // embedding y en el estado. Provisional hasta que exista el .nb (plan 6.2).
  std::string model_name = "w600k_mbf-uint8-v1";
  // Tiempo para que el alumno adopte cada pose. El frontend cuenta este mismo plazo en su
  // contador regresivo (ESPERA_POSE_MS, web/src/api/registroRespuestas.ts): si cambia uno,
  // cambia el otro.
  Millis pose_delay{5000};
  // Intentos por pose antes de abortar el registro: el error lleva el motivo del ULTIMO
  // rechazo (pose_incorrecta, varios_rostros, sin_rostro...); "npu" es solo de fallos reales.
  int max_attempts_per_pose = 5;
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
  // Anclas absolutas del reloj (RTC, id de arranque, archivo de estado) para la guardia de la
  // firma de la captura. nullptr (pruebas, --dry-run): la primera firma no se ancla a nada
  // absoluto y solo se vigila el salto del reloj dentro del proceso.
  AnclasReloj* clock_anchors = nullptr;
  // Dice si la galeria en RAM es de fiar: la ultima recarga tuvo exito (GalleryStore::
  // loaded_ok). Sin ella (galeria sin cargar o con la recarga fallida) un registro no puede
  // saber si el rostro ya pertenece a otra cuenta: se rechaza con galeria_no_disponible (falla
  // cerrado). Una galeria VACIA tras una carga correcta si es valida. Vacia (pruebas,
  // --dry-run): no se comprueba nada, la galeria se da por buena.
  std::function<bool()> gallery_ready = nullptr;
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
    // Recuadro del rostro con mejor score, en pixeles de la foto: lo usa la
    // miniatura. Vacio si no hubo rostro.
    cv::Rect2f bbox;
    // Los 5 landmarks del rostro elegido (para estimar la pose). Solo con rostro.
    std::array<cv::Point2f, 5> landmarks{};
    // Rostros que dio el detector, y cuantos de los demas cuentan como "otro rostro"
    // (solo en el registro: ver FaceSelection::Enrollment).
    int faces_detected = 0;
    int other_faces = 0;
  };

  // Que rostro elegir cuando el detector devuelve varios. En el pase de lista, el de mayor
  // score. En el registro, el de mayor area (el alumno, el mas cercano a la camara) entre los
  // de score suficiente, y si queda otro rostro que no sea claramente pequeno o de score bajo
  // (ServiceConfig::other_face_*), no hay rostro que registrar: reason = varios_rostros.
  enum class FaceSelection { BestScore, Enrollment };

  struct Enrollment {
    std::string account;
    std::optional<std::string> name;  // "nombre" del comando enrolar (opcional)
    std::size_t step = 0;  // indice 0-based de la pose en curso
    int attempts = 0;
    std::vector<std::vector<float>> embeddings;
    // Pitch (grados) que dio la pose "frente" aceptada: las demas poses miden el cabeceo
    // contra este (la altura de la camara no cuenta).
    std::optional<double> front_pitch_deg;
    int identity_rejections = 0;  // rechazos "inconsistente" de todo el registro
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
  // Lo que paso con las fotos de una rafaga (o de una pose), para un solo log por rafaga.
  struct CaptureStats {
    int requested = 0;                      // fotos pedidas
    std::map<int, int> rejected;            // fotos rechazadas por la placa, por codigo HTTP
    // 200 que no es una respuesta autentica, por motivo (ver ResultadoDescifrado).
    int auth_iv_missing = 0;                // sin X-Captura-IV
    int auth_iv_invalid = 0;                // IV que no son 24 hex en minuscula
    int auth_short_body = 0;                // menos de 16 bytes: no cabe la etiqueta
    int auth_bad_tag = 0;                   // la etiqueta no cuadra
  };
  // Una foto (con un solo reintento ante un 429). `retry_budget` es lo que queda de la
  // espera extra permitida para la rafaga.
  cv::Mat capture_frame(Millis& retry_budget, CaptureStats& stats);
  void log_capture_stats(const CaptureStats& stats) const;
  // Detecta y elige el rostro (landmarks, recuadro y, en el registro, si hay otros), SIN calcular
  // el embedding: el registro decide primero la pose y solo despues gasta la NPU.
  FaceOutcome detect_face(const cv::Mat& frame, FaceSelection selection);
  // Alinea y calcula el embedding del rostro elegido por detect_face.
  void embed_face(const cv::Mat& frame, FaceOutcome& outcome);
  // detect_face (mayor score) + embed_face: el pase de lista.
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
  // Resultado de buscar el rostro del registro en la galeria.
  struct DuplicateCheck {
    std::optional<std::string> account;  // la cuenta a la que ya pertenece (duplicado)
    bool unavailable = false;            // la galeria no se pudo consultar (falla cerrado)
  };
  bool gallery_usable() const;
  DuplicateCheck find_duplicate(const std::string& account, const std::vector<float>& embedding);
  void cancel_enrollment(const std::string& account);
  // `miniatura` es el JPEG del rostro en base64 (contrato MQTT): viaja solo con la
  // pose aceptada o rechazada y no se guarda en ningun sitio.
  void publish_progress(const Enrollment& e, EnrollmentStatus status,
                        std::optional<std::string_view> reason = std::nullopt,
                        std::optional<std::string_view> miniatura = std::nullopt,
                        std::optional<std::string_view> similar_to = std::nullopt);
  void publish_refusal(const std::string& account, std::string_view reason);

  int elapsed_ms(TimePoint since) const;

  ServiceDeps deps_;
  ServiceConfig config_;
  std::atomic<ServiceState> state_{ServiceState::Idle};
  std::optional<Enrollment> enrollment_;
  // El inicio de la ultima rafaga (reloj monotono), para espaciar la siguiente.
  std::optional<TimePoint> last_burst_start_;
  // La clave de la respuesta cifrada (se deriva una vez, al construir, si hay secreto) y la
  // guardia del reloj de pared (reloj_guardia.hpp), que decide si el ts es de fiar antes de
  // firmarlo.
  std::optional<ClaveRespuesta> response_key_;
  GuardiaReloj clock_guard_;

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

// --- Descarte de outliers del registro (plan 5.4, paso 5) ---------------------------
//
// Estas dos funciones viven en la biblioteca (y no en el anonimo del .cpp) para que las
// pruebas puedan comprobar la regla sin montar un registro entero.

// Promedio de los embeddings indicados (misma dimension). Lanza std::invalid_argument si
// la lista de indices esta vacia.
std::vector<float> mean_embedding(const std::vector<std::vector<float>>& embeddings,
                                  const std::vector<std::size_t>& indices);

// Indices de las poses que sobreviven al descarte de outliers (regla iterativa: se
// descarta solo la peor mientras su coseno con la media de las candidatas este por debajo
// del umbral; se recalcula la media y se repite). Devuelve menos de tres indices si el
// registro es inconsistente.
std::vector<std::size_t> keep_consistent_poses(const std::vector<std::vector<float>>& embeddings,
                                                float min_pose_similarity);

}  // namespace vision
