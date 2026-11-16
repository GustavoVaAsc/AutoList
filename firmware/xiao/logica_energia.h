// Lógica de la lectura de energía de la XIAO (INA219; nodo de campo, contrato
// MQTT sección 3, aula/camara/energia). C++ puro, sin dependencias de Arduino:
// así se prueba en el host con g++ (pruebas/test_logica_energia.cpp). energia.cpp
// hace la E/S por I2C, y el sketch (xiao.ino) decide con este módulo cuándo
// leer y publica la lectura.
//
// El INA219 va EN SERIE con la entrada de 12 V (plan, sección 8). Este módulo
// no usa los registros de corriente, potencia ni calibración del chip: lee la
// tensión del shunt y la del bus y calcula i = Vshunt / R y p = v * i. Así una
// pérdida de la calibración del chip (que se borra al resetearse) no puede dar
// lecturas de 0 A que parezcan buenas.
//
// Este módulo no consume ningún tópico: solo produce la lectura. El descarte de
// retenidos y vacíos (contrato, sección 1) es de los consumidores.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace logica_energia {

// Tópico de la lectura (contrato, sección 3): QoS 0, sin retención.
const char kTopicoEnergia[] = "aula/camara/energia";

// --- Parámetros del módulo ---------------------------------------------------

// Dirección I2C del INA219 con A0 y A1 a GND (la de fábrica de los módulos).
const uint8_t kDireccionI2c = 0x40;

// Resistencia del shunt. El módulo típico lleva 0.1 ohm (marcado R100). Si el
// módulo comprado lleva otro valor (por ejemplo, 0.01 ohm: R010), se cambia
// aquí y solo aquí.
const float kShuntOhm = 0.1f;

// Rango de la tensión del shunt (ganancia del amplificador, PGA /8): +-320 mV.
// Con 0.1 ohm eso son 3.2 A, que es donde satura el módulo típico: por encima
// de eso la lectura no es válida y no se publica. Es el rango más ancho del
// chip; para cambiarlo hay que cambiar también kConfiguracion.
const int32_t kRangoShuntMv = 320;

// Corriente a la que satura el rango anterior con el shunt de arriba.
const float kCorrienteMaxA = (kRangoShuntMv / 1000.0f) / kShuntOhm;

// El LSB del registro de la tensión del shunt es 10 uV, así que a +-320 mV el
// registro llega a +-32000. Con esa cuenta o más, el chip saturó.
const int32_t kLsbShuntUv = 10;
const int32_t kShuntRawMax = kRangoShuntMv * 1000 / kLsbShuntUv;

// El LSB del registro de la tensión del bus es 4 mV y el dato está en los
// bits 15 a 3.
const int32_t kLsbBusMv = 4;

// Registros del INA219 (hoja de datos, tabla 2).
const uint8_t kRegConfiguracion = 0x00;
const uint8_t kRegShunt = 0x01;
const uint8_t kRegBus = 0x02;

// Registro de configuración: rango del bus de 32 V (la entrada es de 12 V y
// Node-RED acepta hasta 30), PGA /8 (+-320 mV), 16 muestras promediadas en el
// bus y en el shunt (8.51 ms cada una) y modo continuo del shunt y el bus.
// Bits: BRNG (13) = 1, PGA (12:11) = 11, BADC (10:7) = 1100, SADC (6:3) = 1100,
// MODE (2:0) = 111.
const uint16_t kConfiguracion = (1u << 13) | (3u << 11) | (0xCu << 7) | (0xCu << 3) | 7u;

// --- Rangos de lo que se publica (los que valida Node-RED) -------------------

const float kVMaxV = 30.0f;
const float kIMaxA = 10.0f;
const float kPMaxW = 300.0f;

// El shunt tiene un desvío de unas decenas de uV: sin carga, la corriente
// puede salir un poco por debajo de 0. Hasta este valor se toma como 0 A; más
// negativa, el shunt está al revés o el módulo está dañado y no se publica.
const float kToleranciaNegativaA = 0.01f;

// --- Tiempos -----------------------------------------------------------------

// Cada cuánto se lee y publica (contrato: propuesta de 1 s).
const uint32_t kPeriodoMs = 1000;

// Cada cuánto se reintenta detectar el módulo si no estaba o se perdió.
const uint32_t kReintentoInicioMs = 30000;

// Lecturas seguidas que fallan por I2C antes de dar el módulo por perdido.
const uint8_t kFallosSeguidosMax = 3;

// Entre dos avisos por el serie de una lectura inválida (saturada, negativa o
// fuera de rango): con la entrada en 12 V sin carga correcta no se debe llenar
// la consola a un aviso por segundo.
const uint32_t kAvisoLecturaInvalidaMs = 30000;

// Mayor longitud del payload sin el terminador nulo, con los máximos de
// arriba: {"v":30.00,"i":10.000,"p":300.00} son 33 bytes. El búfer de
// PubSubClient son 256: cabe de sobra.
const size_t kEnergiaBytesMax = 33;

// --- Conversión de la lectura ------------------------------------------------

struct Lectura {
  float v;  // V, entrada de 12 V
  float i;  // A
  float p;  // W
};

enum class Resultado {
  Ok,
  Saturada,     // el shunt llegó al límite del rango: la corriente no es fiable
  Negativa,     // corriente por debajo de -kToleranciaNegativaA
  FueraDeRango  // v, i o p fuera de lo que acepta Node-RED
};

// Convierte los registros crudos (tensión del shunt con signo, registro del
// bus tal cual) a V, A y W. Solo rellena `lectura` si devuelve Ok.
Resultado interpretar(int16_t shunt_raw, uint16_t bus_raw, Lectura* lectura);

// Texto corto en español para el serie (nunca nulo).
const char* describir(Resultado resultado);

// Escribe en `destino` el payload de aula/camara/energia,
// {"v":12.10,"i":0.850,"p":10.29}, con terminador nulo. Devuelve su longitud,
// o 0 si no cabe en `capacidad` o `lectura` no es válida (nula, o con un valor
// fuera de rango o no finito): nunca se arma un payload que Node-RED vaya a
// descartar.
size_t construirEnergia(char* destino, size_t capacidad, const Lectura* lectura);

// --- Qué toca hacer y cuándo -------------------------------------------------

enum class Tarea {
  Nada,
  Iniciar,  // configurar el INA219 (no estaba o se perdió)
  Leer      // leer y, si es válida, publicar
};

// Reparte el tiempo: lee cada kPeriodoMs mientras el módulo responde y, si no
// está, reintenta detectarlo cada kReintentoInicioMs. Los tiempos son los de
// millis() (uint32_t, con desbordamiento cada ~49 días): las restas son sin
// signo, igual que en logica_pir y logica_luz. Nunca espera: solo contesta.
class ControlEnergia {
 public:
  // El primer intento de detección es inmediato.
  explicit ControlEnergia(uint32_t periodo_ms = kPeriodoMs,
                          uint32_t reintento_ms = kReintentoInicioMs,
                          uint8_t fallos_max = kFallosSeguidosMax,
                          uint32_t aviso_ms = kAvisoLecturaInvalidaMs)
      : periodo_ms_(periodo_ms),
        reintento_ms_(reintento_ms),
        fallos_max_(fallos_max),
        aviso_ms_(aviso_ms) {}

  // Qué hacer ahora. Si devuelve algo distinto de Nada, cuenta como hecho:
  // el siguiente plazo corre desde `ahora_ms`, haya salido bien o mal.
  Tarea siguiente(uint32_t ahora_ms);

  // true si, antes de ejecutar Tarea::Iniciar, hay que reiniciar el bus I2C:
  // el intento anterior de detectar el módulo falló, o se dio por perdido tras
  // fallar sus lecturas. Un bus colgado (un esclavo con SDA en bajo, o un
  // plazo agotado) solo se arregla reiniciándolo. El primer intento tras el
  // arranque no lo pide (el bus acaba de iniciarse en setup()), y los demás
  // llevan el ritmo de kReintentoInicioMs: nunca más de uno cada 30 s.
  bool reiniciarBusAntes() const { return bus_a_reiniciar_; }

  // Resultado de Tarea::Iniciar. Devuelve true si el módulo pasó de ausente a
  // activo (para anunciarlo una sola vez).
  bool registrarInicio(bool detectado);

  // Resultado de la lectura por I2C de Tarea::Leer (no del valor leído: eso
  // lo juzga interpretar()). Devuelve true si con este fallo se da el módulo
  // por perdido; pasa a ausente y se reintenta la detección.
  bool registrarLectura(bool respondio);

  // true si el módulo respondió en la última detección y no se ha perdido.
  bool activo() const { return activo_; }

  // true si toca avisar por el serie de una lectura inválida: la primera vez
  // y luego como mucho una por `aviso_ms`.
  bool avisarLecturaInvalida(uint32_t ahora_ms);

 private:
  uint32_t periodo_ms_;
  uint32_t reintento_ms_;
  uint8_t fallos_max_;
  uint32_t aviso_ms_;
  uint32_t ultimo_ms_ = 0;
  uint32_t ultimo_aviso_ms_ = 0;
  uint8_t fallos_ = 0;
  bool activo_ = false;
  bool bus_a_reiniciar_ = false;
  bool tiene_ultimo_ = false;  // si ya hubo una tarea
  bool tiene_aviso_ = false;   // si ya se avisó alguna vez
};

}  // namespace logica_energia
