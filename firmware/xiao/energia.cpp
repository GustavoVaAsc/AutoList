#include "energia.h"

#include <Wire.h>

#include "logica_energia.h"

namespace energia {

static bool busListo = false;
static int errorI2c = 0;

// Escribe el puntero de registro y lee sus dos bytes (el INA219 manda primero
// el más significativo).
static bool leerRegistro(uint8_t registro, uint16_t* valor) {
  Wire.beginTransmission(logica_energia::kDireccionI2c);
  Wire.write(registro);
  const uint8_t resultado = Wire.endTransmission();
  if (resultado != 0) {
    errorI2c = resultado;
    return false;
  }
  if (Wire.requestFrom(logica_energia::kDireccionI2c, static_cast<size_t>(2)) != 2) {
    errorI2c = kErrorSinRespuesta;
    return false;
  }
  const uint8_t alto = Wire.read();
  const uint8_t bajo = Wire.read();
  *valor = static_cast<uint16_t>((alto << 8) | bajo);
  return true;
}

static bool escribirRegistro(uint8_t registro, uint16_t valor) {
  Wire.beginTransmission(logica_energia::kDireccionI2c);
  Wire.write(registro);
  Wire.write(static_cast<uint8_t>(valor >> 8));
  Wire.write(static_cast<uint8_t>(valor & 0xFF));
  const uint8_t resultado = Wire.endTransmission();
  if (resultado != 0) {
    errorI2c = resultado;
    return false;
  }
  return true;
}

static bool abrirBus() {
  busListo = Wire.begin(PIN_SDA, PIN_SCL, kFrecuenciaI2cHz);
  if (busListo) {
    Wire.setTimeOut(kTimeoutI2cMs);
  } else {
    errorI2c = kErrorSinBus;
  }
  return busListo;
}

// Un esclavo que quedó a mitad de un byte puede mantener SDA en bajo y colgar el
// bus. La salida estándar: con el bus suelto, hasta 9 pulsos de SCL (el esclavo
// termina su byte y suelta SDA) y una condición de STOP. Acotado a 9 pulsos de
// unos 10 us: no espera nada del bus. Solo toca GPIO5 y GPIO6; la cámara va en
// otros pines y en el I2C1. Solo recupera un SDA retenido: si un esclavo retiene
// SCL en bajo, los pulsos no sirven y el bus sigue colgado hasta el próximo intento.
static void liberarBus() {
  pinMode(PIN_SDA, INPUT_PULLUP);
  pinMode(PIN_SCL, INPUT_PULLUP);
  delayMicroseconds(5);
  if (digitalRead(PIN_SDA) == HIGH) return;  // SDA libre: no hay nada que liberar
  for (int pulso = 0; pulso < kPulsosLiberarBus && digitalRead(PIN_SDA) == LOW; ++pulso) {
    digitalWrite(PIN_SCL, LOW);  // primero el latch en bajo: nunca se empuja la línea a alto
    pinMode(PIN_SCL, OUTPUT);
    delayMicroseconds(5);
    pinMode(PIN_SCL, INPUT_PULLUP);
    delayMicroseconds(5);
  }
  // STOP: SDA sube mientras SCL está en alto.
  digitalWrite(PIN_SDA, LOW);  // primero el latch en bajo: nunca se empuja la línea a alto
  pinMode(PIN_SDA, OUTPUT);
  delayMicroseconds(5);
  pinMode(PIN_SDA, INPUT_PULLUP);
  delayMicroseconds(5);
}

bool iniciarBus() { return abrirBus(); }

bool reiniciarBus() {
  Wire.end();
  liberarBus();
  return abrirBus();
}

bool configurar() {
  if (!busListo) {
    errorI2c = kErrorSinBus;
    return false;
  }
  errorI2c = 0;
  if (!escribirRegistro(logica_energia::kRegConfiguracion, logica_energia::kConfiguracion)) {
    return false;
  }
  uint16_t leido = 0;
  if (!leerRegistro(logica_energia::kRegConfiguracion, &leido)) return false;
  // El bit 15 (reinicio) se lee siempre como 0 y el resto debe ser lo escrito.
  if (leido != logica_energia::kConfiguracion) {
    errorI2c = kErrorConfiguracion;
    return false;
  }
  return true;
}

bool leerCrudo(int16_t* shunt_raw, uint16_t* bus_raw) {
  if (!busListo) {
    errorI2c = kErrorSinBus;
    return false;
  }
  errorI2c = 0;
  uint16_t shunt = 0;
  uint16_t bus = 0;
  if (!leerRegistro(logica_energia::kRegShunt, &shunt) ||
      !leerRegistro(logica_energia::kRegBus, &bus)) {
    return false;
  }
  *shunt_raw = static_cast<int16_t>(shunt);
  *bus_raw = bus;
  return true;
}

int ultimoError() { return errorI2c; }

}  // namespace energia
