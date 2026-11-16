// Lectura del INA219 de la XIAO por I2C.
//
// La parte con Arduino: el bus y los registros. Qué hacer con los valores
// (convertirlos, validarlos, el payload, cuándo leer y cuándo reintentar) está
// en logica_energia.h, que es C++ puro y se prueba en el host.
//
// Cableado (plan, sección 8): INA219 en D4 (SDA, GPIO5) y D5 (SCL, GPIO6), en
// serie con la entrada de 12 V. El bus es el I2C0 del core (Wire). La SCCB de
// la cámara va por GPIO39/40 en el I2C1 (captura.cpp): aquí no se usa Wire1 ni
// se toca nada de la cámara.
//
// Nada de este módulo espera de forma indefinida: cada transacción tiene el
// plazo de kTimeoutI2cMs (un NACK vuelve de inmediato; solo un bus colgado
// agota el plazo). Todo se llama desde loop(): Wire no es seguro entre tareas.
#pragma once

#include <Arduino.h>

namespace energia {

static const uint8_t PIN_SDA = D4;  // GPIO5
static const uint8_t PIN_SCL = D5;  // GPIO6

// 100 kHz: el modo estándar aguanta cables de protoboard sin problemas y a 1 ms
// por lectura sobra.
static const uint32_t kFrecuenciaI2cHz = 100000;

// Plazo de cada transacción I2C. El del core es de 50 ms.
static const uint16_t kTimeoutI2cMs = 20;

// Pulsos de SCL como máximo para soltar un SDA colgado (reiniciarBus()).
static const int kPulsosLiberarBus = 9;

// Códigos de error propios, además de los de Wire.endTransmission() (1 a 5:
// datos largos, NACK de dirección, NACK de datos, otro error, plazo agotado).
static const int kErrorSinBus = 10;       // Wire.begin() falló: no hay bus
static const int kErrorSinRespuesta = 11;  // el chip no contestó los 2 bytes
static const int kErrorConfiguracion = 12;  // lo leído de vuelta no es lo escrito

// Inicia el bus I2C0 en D4/D5. Devuelve false si el bus no arranca; el resto de
// las funciones fallan entonces sin tocar Wire.
bool iniciarBus();

// Reinicia el bus I2C0: cierra Wire, suelta SDA si un esclavo lo tiene en bajo
// (hasta kPulsosLiberarBus pulsos de SCL y un STOP) y vuelve a abrir Wire. Es
// la única forma de salir de un bus colgado (solo si es SDA el retenido). No espera nada del bus; el
// sketch la llama solo antes de reintentar la detección (cada 30 s como
// mucho). Devuelve false si Wire no arranca.
bool reiniciarBus();

// Escribe el registro de configuración y lo lee de vuelta. Devuelve true si el
// INA219 contestó y guardó la configuración: así sabemos que hay un INA219
// (u otro chip que se le parezca) en kDireccionI2c.
bool configurar();

// Lee la tensión del shunt y la del bus, tal cual salen del chip (la
// conversión es de logica_energia::interpretar). Devuelve false si el I2C
// falló, sin tocar las salidas.
bool leerCrudo(int16_t* shunt_raw, uint16_t* bus_raw);

// Código del último fallo de configurar() o leerCrudo(): 0 si salió bien, o
// uno de los de arriba.
int ultimoError();

}  // namespace energia
