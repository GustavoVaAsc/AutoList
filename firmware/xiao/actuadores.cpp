#include "actuadores.h"

namespace actuadores {

namespace {

// Estado de lo último que se escribió en los pines, para no escribir de más.
logica_actuadores::EstadoServo servo_estado_ = logica_actuadores::EstadoServo::Cerrado;
logica_actuadores::ColorLed led_color_ = logica_actuadores::ColorLed::Apagado;
bool buzzer_salida_ = false;

// Periodo del servo a 50 Hz, en microsegundos.
const uint32_t kPeriodoServoUs = 20000;

// Convierte un ancho de pulso en microsegundos al nivel del LEDC.
uint32_t nivelDesdeAnchoUs(uint32_t ancho_us) {
  return (ancho_us * (1UL << RESOLUCION_LEDC)) / kPeriodoServoUs;
}

// Convierte un nivel de 0 a 255 al nivel del LEDC.
uint32_t nivelDesdeBrillo(uint8_t brillo) {
  return (static_cast<uint32_t>(brillo) * MAX_NIVEL_LEDC) / 255;
}

void adjuntarLedc(uint8_t pin, uint32_t freq, uint8_t canal, const char* nombre) {
  if (!ledcAttachChannel(pin, freq, RESOLUCION_LEDC, canal)) {
    Serial.print("LEDC: no se pudo adjuntar ");
    Serial.print(nombre);
    Serial.print(" (pin ");
    Serial.print(pin);
    Serial.println(")");
  }
}

// Comprueba que el canal quedó en la frecuencia prevista: si el núcleo asignó
// otro timer, el reparto de timers sería otro y hay que saberlo.
void avisarSiFrecuenciaDistinta(uint8_t pin, uint32_t esperada, const char* nombre) {
  const uint32_t real = ledcReadFreq(pin);
  if (real == 0) return;
  const uint32_t diferencia = real > esperada ? real - esperada : esperada - real;
  if (diferencia * 100 / esperada > 2) {  // más de un 2 % de diferencia
    Serial.print("LEDC: ");
    Serial.print(nombre);
    Serial.print(" quedó en ");
    Serial.print(real);
    Serial.print(" Hz y se esperaban ");
    Serial.print(esperada);
    Serial.println(" Hz");
  }
}

}  // namespace

void escribirServo(logica_actuadores::EstadoServo estado) {
  const uint32_t pulso = estado == logica_actuadores::EstadoServo::Abierto
                             ? PULSO_SERVO_ABIERTO_US
                             : PULSO_SERVO_CERRADO_US;
  ledcWrite(PIN_SERVO, nivelDesdeAnchoUs(pulso));
}

void escribirLed(logica_actuadores::ColorLed color) {
  uint8_t rojo = 0;
  uint8_t verde = 0;
  uint8_t azul = 0;
  switch (color) {
    case logica_actuadores::ColorLed::Azul:
      azul = 255;
      break;
    case logica_actuadores::ColorLed::Verde:
      verde = 255;
      break;
    case logica_actuadores::ColorLed::Rojo:
      rojo = 255;
      break;
    case logica_actuadores::ColorLed::Ambar:
      rojo = 255;
      verde = 165;  // ámbar: rojo y verde
      break;
    case logica_actuadores::ColorLed::Apagado:
      break;
  }
  ledcWrite(PIN_LED_ROJO, nivelDesdeBrillo(rojo));
  ledcWrite(PIN_LED_VERDE, nivelDesdeBrillo(verde));
  ledcWrite(PIN_LED_AZUL, nivelDesdeBrillo(azul));
}

void escribirBuzzer(bool encendido) {
  // 50 % de duty a 2 kHz: un tono que reproducen tanto los buzzers pasivos
  // como los activos.
  const uint32_t nivel = encendido ? (MAX_NIVEL_LEDC / 2) : 0;
  ledcWrite(PIN_BUZZER, nivel);
}

void iniciar() {
  // Primero, los pines en salida y a cero: el servo no debe recibir pulsos
  // raros mientras se configuran los canales.
  pinMode(PIN_SERVO, OUTPUT);
  digitalWrite(PIN_SERVO, LOW);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  pinMode(PIN_LED_ROJO, OUTPUT);
  digitalWrite(PIN_LED_ROJO, LOW);
  pinMode(PIN_LED_VERDE, OUTPUT);
  digitalWrite(PIN_LED_VERDE, LOW);
  pinMode(PIN_LED_AZUL, OUTPUT);
  digitalWrite(PIN_LED_AZUL, LOW);

  // El orden de los attaches fija el reparto de timers (ver actuadores.h).
  adjuntarLedc(PIN_SERVO, FREQ_SERVO_HZ, CANAL_SERVO, "el servo");
  adjuntarLedc(PIN_BUZZER, FREQ_BUZZER_HZ, CANAL_BUZZER, "el buzzer");
  adjuntarLedc(PIN_LED_ROJO, FREQ_LED_ROJO_HZ, CANAL_LED_ROJO, "el LED rojo");
  adjuntarLedc(PIN_LED_VERDE, FREQ_LED_VERDE_HZ, CANAL_LED_VERDE, "el LED verde");
  adjuntarLedc(PIN_LED_AZUL, FREQ_LED_AZUL_HZ, CANAL_LED_AZUL, "el LED azul");

  avisarSiFrecuenciaDistinta(PIN_SERVO, FREQ_SERVO_HZ, "el servo");
  avisarSiFrecuenciaDistinta(PIN_BUZZER, FREQ_BUZZER_HZ, "el buzzer");
  avisarSiFrecuenciaDistinta(PIN_LED_ROJO, FREQ_LED_ROJO_HZ, "el LED rojo");
  avisarSiFrecuenciaDistinta(PIN_LED_VERDE, FREQ_LED_VERDE_HZ, "el LED verde");
  avisarSiFrecuenciaDistinta(PIN_LED_AZUL, FREQ_LED_AZUL_HZ, "el LED azul");

  // Estado inicial: puerta cerrada, LED apagado y buzzer en silencio.
  servo_estado_ = logica_actuadores::EstadoServo::Cerrado;
  escribirServo(servo_estado_);
  led_color_ = logica_actuadores::ColorLed::Apagado;
  escribirLed(led_color_);
  buzzer_salida_ = false;
  escribirBuzzer(false);
}

void aplicar(const logica_actuadores::ControlActuadores& control) {
  // Servo: se escribe el pulso solo cuando cambia el destino. El movimiento
  // no bloquea: el servo se para solo en su destino.
  const logica_actuadores::EstadoServo estado = control.estadoServo();
  if (estado != servo_estado_) {
    servo_estado_ = estado;
    escribirServo(estado);
  }

  // LED: solo se escribe cuando el color efectivo cambia.
  const logica_actuadores::ColorLed color = control.colorLed();
  if (color != led_color_) {
    led_color_ = color;
    escribirLed(color);
  }

  // Buzzer: solo se escribe cuando cambia la salida.
  const bool sonando = control.buzzerEncendido();
  if (sonando != buzzer_salida_) {
    buzzer_salida_ = sonando;
    escribirBuzzer(sonando);
  }
}

}  // namespace actuadores
