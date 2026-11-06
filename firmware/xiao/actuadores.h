// Actuadores de la XIAO: servo, LED RGB y buzzer.
//
// La parte con Arduino: pines, LEDC y temporizado. La lógica (qué hacer con
// cada orden, cuándo se cierra el servo, qué color se enciende) está en
// logica_actuadores.h, que es C++ puro y se prueba en el host.
//
// Cableado (plan, sección 8): servo en D1 (GPIO2), buzzer en D3 (GPIO4) y
// LEDs RGB en D8, D9 y D10 (GPIO 7, 8 y 9), que son los pines de la microSD
// de la placa Sense, que el proyecto no usa. La tira LED de 12 V ya ocupa D2
// (GPIO3). El servo se alimenta del buck de 5 V, nunca del pin de 3.3 V, y
// lleva un capacitor de 470 µF: un brownout del servo reinicia la cámara.
#pragma once

#include <Arduino.h>

#include "logica_actuadores.h"

namespace actuadores {

// --- Pines ------------------------------------------------------------------

static const uint8_t PIN_SERVO = D1;    // GPIO2
static const uint8_t PIN_BUZZER = D3;   // GPIO4
static const uint8_t PIN_LED_ROJO = D8;  // GPIO7
static const uint8_t PIN_LED_VERDE = D9;  // GPIO8
static const uint8_t PIN_LED_AZUL = D10;  // GPIO9

// Si los LEDs rescatados son WS2812 (un solo pin de datos) en vez de tres
// discretos, este módulo no sirve: hay que cambiar PIN_LED_ROJO, PIN_LED_VERDE
// y PIN_LED_AZUL por un único pin de datos (por ejemplo D8, GPIO7) y escribir
// el color con una biblioteca WS2812 (Adafruit NeoPixel) en vez de con LEDC.
// Los tres pines de abajo dejan de usarse.

// --- LEDC -------------------------------------------------------------------
//
// El ESP32-S3 tiene 4 timers LEDC y 8 canales: cada timer fija la frecuencia
// de sus dos canales. El servo necesita 50 Hz y no puede compartir timer con
// nadie, el buzzer necesita otro, y los tres LEDs RGB se reparten los que
// quedan (plan, secciones 6.1 y 13).
//
// El núcleo 3.3.12 (ledcAttachChannel) reutiliza un timer que ya tenga esa
// frecuencia y resolución, así que el reparto sale del ORDEN de los attaches:
// por eso se hacen en el orden de abajo y no en otro.
//
//   timer 0   50 Hz    canal 0    servo (D1)      exclusivo
//   timer 1   2 kHz    canal 2    buzzer (D3)
//   timer 2   5 kHz    canales 4 y 5   LED rojo (D8) y LED verde (D9)
//   timer 3   5 kHz    canal 6    LED azul (D10)
//
// Los tres canales RGB van a la misma frecuencia (5 kHz): el azul cae en el
// timer 3 porque el 2 ya está lleno (cada timer tiene dos canales), no porque
// su frecuencia sea distinta. La cámara no gasta canal LEDC: el XCLK lo genera
// el periférico LCD_CAM. La tira LED de 12 V tampoco: es todo o nada con
// digitalWrite.

static const uint8_t CANAL_SERVO = 0;      // timer 0
static const uint8_t CANAL_BUZZER = 2;     // timer 1
static const uint8_t CANAL_LED_ROJO = 4;   // timer 2
static const uint8_t CANAL_LED_VERDE = 5;  // timer 2
static const uint8_t CANAL_LED_AZUL = 6;   // timer 3

static const uint32_t FREQ_SERVO_HZ = 50;
static const uint32_t FREQ_BUZZER_HZ = 2000;
static const uint32_t FREQ_LED_ROJO_HZ = 5000;
static const uint32_t FREQ_LED_VERDE_HZ = 5000;
static const uint32_t FREQ_LED_AZUL_HZ = 5000;

// 12 bits: el ESP32-S3 admite hasta 14 (SOC_LEDC_TIMER_BIT_WIDTH) y con 12
// el escalón del servo es de 4.9 µS, de sobra para un SG90.
static const uint8_t RESOLUCION_LEDC = 12;
static const uint32_t MAX_NIVEL_LEDC = (1UL << RESOLUCION_LEDC) - 1;  // 4095

// --- Pulso del servo --------------------------------------------------------
//
// Ancho de pulso a 50 Hz (periodo de 20 ms). El SG90 y el MG90S mapean el
// ancho de pulso al ángulo de la siguiente manera (hoja de datos del SG90):
//
//   500 µs  ->  0°   (el extremo de un lado)
//   1500 µs -> 90°   (la mitad del recorrido)
//   2400 µs -> 180°  (el extremo del otro lado)
//
// El contrato dice que la puerta "abre 90°, espera 3 s y cierra solo": 90° son
// grados DESDE la posición de reposo, no un extremo del recorrido. Así que la
// puerta cerrada es el extremo de 0° (500 µs) y la abierta es la mitad del
// recorrido (1500 µs). El valor de 2400 µs (180°) no se usa: la puerta solo
// abre 90°.
//
// OJO: el MG90S tiene un trimpot de ajuste y algunos lotes vienen con el
// recorrido desplazado. Si en la placa la puerta abre hacia el lado contrario o
// no llega a 90°, se ajustan estas dos constantes (y nada más): con el
// monitor serie se ve el ancho de pulso que se está escribiendo.

static const uint32_t PULSO_SERVO_CERRADO_US = 500;   // 0°: puerta cerrada
static const uint32_t PULSO_SERVO_ABIERTO_US = 1500;  // 90°: puerta abierta
static const uint32_t PULSO_SERVO_MEDIO_US = 1000;    // posición intermedia de
                                                        // referencia: el firmware
                                                        // solo va de 0° a 90°, pero
                                                        // sirve para calibrar a mano

// --- API --------------------------------------------------------------------

// Configura los pines y los canales LEDC, y deja el servo cerrado, el LED
// apagado y el buzzer en silencio. Se llama una vez, desde setup().
void iniciar();

// Escribe el estado de los tres actuadores. Se puede llamar en cada vuelta de
// loop(): solo escribe un pin cuando algo cambia de verdad. El servo abre y
// cierra sin bloquear (el tiempo lo pasa el sketch).
void aplicar(const logica_actuadores::ControlActuadores& control);

// Escribe el pulso de una posición del servo.
void escribirServo(logica_actuadores::EstadoServo estado);

// Enciende el color pedido en el LED RGB.
void escribirLed(logica_actuadores::ColorLed color);

// Enciende o apaga el tono del buzzer.
void escribirBuzzer(bool encendido);

}  // namespace actuadores
