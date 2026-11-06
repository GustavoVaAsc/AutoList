// Pruebas de host de logica_actuadores (sin dependencias). Compilar y correr
// desde firmware/xiao/ con los comandos del README, sección "Pruebas de host".
#include <stdio.h>
#include <string.h>

#include "../logica_actuadores.h"

using namespace logica_actuadores;

static int fallos = 0;
static int total = 0;

#define VERIFICAR(cond)                                                   \
  do {                                                                    \
    ++total;                                                              \
    if (!(cond)) {                                                        \
      ++fallos;                                                           \
      printf("FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
    }                                                                     \
  } while (0)

static void interpreta_servo() {
  VERIFICAR(interpretarServo(false, false, nullptr) == PedidoServo::Ausente);
  VERIFICAR(interpretarServo(false, true, "abrir") == PedidoServo::Ausente);
  VERIFICAR(interpretarServo(true, true, "abrir") == PedidoServo::Abrir);
  // "cerrar" no existe en el contrato: el servo se cierra solo.
  VERIFICAR(interpretarServo(true, true, "cerrar") == PedidoServo::Invalido);
  VERIFICAR(interpretarServo(true, true, "") == PedidoServo::Invalido);
  VERIFICAR(interpretarServo(true, true, "Abrir") == PedidoServo::Invalido);
  VERIFICAR(interpretarServo(true, true, "abrir ") == PedidoServo::Invalido);
  // Presente pero no es una cadena (número, booleano, objeto): inválido.
  VERIFICAR(interpretarServo(true, false, nullptr) == PedidoServo::Invalido);
  VERIFICAR(interpretarServo(true, true, nullptr) == PedidoServo::Invalido);
}

static void interpreta_led() {
  VERIFICAR(interpretarLed(false, false, nullptr) == PedidoLed::Ausente);
  VERIFICAR(interpretarLed(true, true, "azul") == PedidoLed::Azul);
  VERIFICAR(interpretarLed(true, true, "verde") == PedidoLed::Verde);
  VERIFICAR(interpretarLed(true, true, "rojo") == PedidoLed::Rojo);
  VERIFICAR(interpretarLed(true, true, "ambar") == PedidoLed::Ambar);
  VERIFICAR(interpretarLed(true, true, "apagado") == PedidoLed::Apagado);
  // Cualquier otro valor se ignora.
  VERIFICAR(interpretarLed(true, true, "amarillo") == PedidoLed::Invalido);
  VERIFICAR(interpretarLed(true, true, "AZUL") == PedidoLed::Invalido);
  VERIFICAR(interpretarLed(true, true, "") == PedidoLed::Invalido);
  VERIFICAR(interpretarLed(true, false, nullptr) == PedidoLed::Invalido);
  VERIFICAR(interpretarLed(true, true, nullptr) == PedidoLed::Invalido);
}

static void interpreta_buzzer() {
  VERIFICAR(interpretarBuzzer(false, false, 0) == PedidoBuzzer::Ausente);
  VERIFICAR(interpretarBuzzer(true, true, 0) == PedidoBuzzer::Silencio);
  VERIFICAR(interpretarBuzzer(true, true, 1) == PedidoBuzzer::Exito);
  VERIFICAR(interpretarBuzzer(true, true, 2) == PedidoBuzzer::Error);
  // Fuera de rango.
  VERIFICAR(interpretarBuzzer(true, true, 3) == PedidoBuzzer::Invalido);
  VERIFICAR(interpretarBuzzer(true, true, -1) == PedidoBuzzer::Invalido);
  VERIFICAR(interpretarBuzzer(true, true, 255) == PedidoBuzzer::Invalido);
  // Presente pero no es un entero (cadena, booleano, decimal).
  VERIFICAR(interpretarBuzzer(true, false, 1) == PedidoBuzzer::Invalido);
}

static void es_orden() {
  VERIFICAR(esOrden(PedidoServo::Abrir));
  VERIFICAR(!esOrden(PedidoServo::Ausente));
  VERIFICAR(!esOrden(PedidoServo::Invalido));

  VERIFICAR(esOrden(PedidoLed::Azul));
  VERIFICAR(esOrden(PedidoLed::Verde));
  VERIFICAR(esOrden(PedidoLed::Rojo));
  VERIFICAR(esOrden(PedidoLed::Ambar));
  VERIFICAR(esOrden(PedidoLed::Apagado));
  VERIFICAR(!esOrden(PedidoLed::Ausente));
  VERIFICAR(!esOrden(PedidoLed::Invalido));

  VERIFICAR(esOrden(PedidoBuzzer::Silencio));
  VERIFICAR(esOrden(PedidoBuzzer::Exito));
  VERIFICAR(esOrden(PedidoBuzzer::Error));
  VERIFICAR(!esOrden(PedidoBuzzer::Ausente));
  VERIFICAR(!esOrden(PedidoBuzzer::Invalido));
}

static void nombres_del_contrato() {
  VERIFICAR(strcmp(nombreEstadoServo(EstadoServo::Cerrado), "cerrado") == 0);
  VERIFICAR(strcmp(nombreEstadoServo(EstadoServo::Abierto), "abierto") == 0);
  VERIFICAR(strcmp(nombreColor(ColorLed::Azul), "azul") == 0);
  VERIFICAR(strcmp(nombreColor(ColorLed::Verde), "verde") == 0);
  VERIFICAR(strcmp(nombreColor(ColorLed::Rojo), "rojo") == 0);
  VERIFICAR(strcmp(nombreColor(ColorLed::Ambar), "ambar") == 0);
  VERIFICAR(strcmp(nombreColor(ColorLed::Apagado), "apagado") == 0);
}

static void servo_se_abre_y_se_cierra_solo() {
  ControlActuadores c;
  VERIFICAR(c.estadoServo() == EstadoServo::Cerrado);   // arranca cerrado
  VERIFICAR(!c.servoAbierto());
  VERIFICAR(c.aplicarServo(PedidoServo::Abrir, 1000));  // sí cambia el estado
  VERIFICAR(c.estadoServo() == EstadoServo::Abierto);
  VERIFICAR(c.servoAbierto());
  // Sigue abierto durante los 3 s.
  VERIFICAR(!c.revisarServo(1000));
  VERIFICAR(!c.revisarServo(3999));
  VERIFICAR(c.estadoServo() == EstadoServo::Abierto);
  // A los 3 s exactos se cierra solo.
  VERIFICAR(c.revisarServo(4000));
  VERIFICAR(c.estadoServo() == EstadoServo::Cerrado);
  VERIFICAR(!c.revisarServo(4001));  // ya cerrado: no vuelve a avisar
  // Un pedido inválido o ausente no abre el servo.
  VERIFICAR(!c.aplicarServo(PedidoServo::Invalido, 5000));
  VERIFICAR(!c.aplicarServo(PedidoServo::Ausente, 5000));
  VERIFICAR(c.estadoServo() == EstadoServo::Cerrado);
}

static void servo_repetido_alarga_la_ventana() {
  ControlActuadores c;
  VERIFICAR(c.aplicarServo(PedidoServo::Abrir, 0));
  VERIFICAR(!c.aplicarServo(PedidoServo::Abrir, 2000));  // el estado no cambia
  VERIFICAR(c.estadoServo() == EstadoServo::Abierto);
  VERIFICAR(!c.revisarServo(3000));   // la ventana se reinició en el segundo abrir
  VERIFICAR(c.revisarServo(5000));
  VERIFICAR(c.estadoServo() == EstadoServo::Cerrado);
}

static void servo_con_desbordamiento_de_millis() {
  ControlActuadores c;
  const uint32_t desde = 0xFFFFFF00u;  // 256 ms antes del desbordamiento
  VERIFICAR(c.aplicarServo(PedidoServo::Abrir, desde));
  VERIFICAR(!c.revisarServo(0xFFFFFFFFu));  // 255 ms
  VERIFICAR(!c.revisarServo(500));  // ya desbordó: han pasado 756 ms
  VERIFICAR(!c.revisarServo(744));  // 1000 ms: todavía no se cierra
  VERIFICAR(c.estadoServo() == EstadoServo::Abierto);
  VERIFICAR(c.revisarServo(3044));  // 3000 ms
  VERIFICAR(c.estadoServo() == EstadoServo::Cerrado);
}

static void led_solo_cambia_si_cambia_de_verdad() {
  ControlActuadores c;
  VERIFICAR(c.colorLed() == ColorLed::Apagado);  // arranca apagado
  VERIFICAR(c.aplicarLed(PedidoLed::Verde));
  VERIFICAR(c.colorLed() == ColorLed::Verde);
  VERIFICAR(!c.aplicarLed(PedidoLed::Verde));   // repetir no cambia
  VERIFICAR(c.colorLed() == ColorLed::Verde);
  VERIFICAR(!c.aplicarLed(PedidoLed::Ausente));  // lo ausente no cambia
  VERIFICAR(!c.aplicarLed(PedidoLed::Invalido));  // lo inválido no cambia
  VERIFICAR(c.colorLed() == ColorLed::Verde);
  VERIFICAR(c.aplicarLed(PedidoLed::Rojo));
  VERIFICAR(c.colorLed() == ColorLed::Rojo);
  VERIFICAR(c.aplicarLed(PedidoLed::Apagado));
  VERIFICAR(c.colorLed() == ColorLed::Apagado);
}

static void el_azul_de_la_sesion_manda() {
  ControlActuadores c;
  // Sesión activa: el LED pasa a azul aunque no haya orden de color.
  VERIFICAR(c.aplicarSesionActiva(true));
  VERIFICAR(c.sesionActiva());
  VERIFICAR(c.colorLed() == ColorLed::Azul);
  VERIFICAR(c.colorRetroalimentacion() == ColorLed::Apagado);
  // Con la sesión activa, el azul manda sobre el color de retroalimentación:
  // el color pedido se recuerda, pero no se ve.
  VERIFICAR(!c.aplicarLed(PedidoLed::Verde));
  VERIFICAR(c.colorLed() == ColorLed::Azul);
  VERIFICAR(c.colorRetroalimentacion() == ColorLed::Verde);
  VERIFICAR(!c.aplicarLed(PedidoLed::Rojo));
  VERIFICAR(c.colorLed() == ColorLed::Azul);
  VERIFICAR(c.colorRetroalimentacion() == ColorLed::Rojo);
  // Al terminar la sesión vuelve el último color de retroalimentación.
  VERIFICAR(c.aplicarSesionActiva(false));
  VERIFICAR(!c.sesionActiva());
  VERIFICAR(c.colorLed() == ColorLed::Rojo);
  // Y si no hubo ningún color de retroalimentación, se apaga.
  ControlActuadores d;
  VERIFICAR(d.aplicarSesionActiva(true));
  VERIFICAR(d.colorLed() == ColorLed::Azul);
  VERIFICAR(d.aplicarSesionActiva(false));
  VERIFICAR(d.colorLed() == ColorLed::Apagado);
  // Repetir el mismo estado de sesión no cambia nada.
  VERIFICAR(!d.aplicarSesionActiva(false));
}

static void buzzer_pitido_de_exito() {
  ControlActuadores c;
  VERIFICAR(!c.buzzerEncendido());
  VERIFICAR(c.aplicarBuzzer(PedidoBuzzer::Exito, 0));
  VERIFICAR(c.buzzerEncendido());
  VERIFICAR(!c.revisarBuzzer(99));   // sigue sonando
  VERIFICAR(c.buzzerEncendido());
  VERIFICAR(c.revisarBuzzer(100));   // se acaba el pitido
  VERIFICAR(!c.buzzerEncendido());
  VERIFICAR(!c.revisarBuzzer(1000));  // no vuelve a avisar
  VERIFICAR(!c.buzzerEncendido());
  // Un pedido ausente o inválido no suena.
  VERIFICAR(!c.aplicarBuzzer(PedidoBuzzer::Ausente, 2000));
  VERIFICAR(!c.aplicarBuzzer(PedidoBuzzer::Invalido, 2000));
  VERIFICAR(!c.buzzerEncendido());
}

static void buzzer_doble_pitido_de_error() {
  ControlActuadores c;
  VERIFICAR(c.aplicarBuzzer(PedidoBuzzer::Error, 0));
  VERIFICAR(c.buzzerEncendido());
  VERIFICAR(!c.revisarBuzzer(99));
  VERIFICAR(c.revisarBuzzer(100));  // fin del primer pitido
  VERIFICAR(!c.buzzerEncendido());
  VERIFICAR(!c.revisarBuzzer(219));  // sigue en la pausa
  VERIFICAR(!c.buzzerEncendido());
  VERIFICAR(c.revisarBuzzer(220));  // segundo pitido
  VERIFICAR(c.buzzerEncendido());
  VERIFICAR(!c.revisarBuzzer(319));
  VERIFICAR(c.revisarBuzzer(320));  // se acaba
  VERIFICAR(!c.buzzerEncendido());
  VERIFICAR(!c.revisarBuzzer(1000));
}

static void buzzer_silencio_corta_el_pitido() {
  ControlActuadores c;
  VERIFICAR(c.aplicarBuzzer(PedidoBuzzer::Error, 0));
  VERIFICAR(c.buzzerEncendido());
  VERIFICAR(c.aplicarBuzzer(PedidoBuzzer::Silencio, 50));
  VERIFICAR(!c.buzzerEncendido());
  VERIFICAR(!c.revisarBuzzer(1000));  // ya no sigue el patrón
  VERIFICAR(!c.buzzerEncendido());
  // Silencio con el buzzer ya callado no cambia nada.
  VERIFICAR(!c.aplicarBuzzer(PedidoBuzzer::Silencio, 1100));
}

static void buzzer_con_desbordamiento_de_millis() {
  ControlActuadores c;
  const uint32_t desde = 0xFFFFFFC0u;  // 64 ms antes del desbordamiento
  VERIFICAR(c.aplicarBuzzer(PedidoBuzzer::Exito, desde));
  VERIFICAR(!c.revisarBuzzer(0xFFFFFFFFu));  // 63 ms: sigue sonando
  VERIFICAR(c.buzzerEncendido());
  VERIFICAR(!c.revisarBuzzer(35));  // 99 ms
  VERIFICAR(c.revisarBuzzer(36));   // 100 ms: se acaba
  VERIFICAR(!c.buzzerEncendido());
}

// Comprueba el payload exacto: el texto y su longitud (sin el terminador nulo).
static void verifica_estado(const char* buf, size_t n, const char* esperado) {
  VERIFICAR(n == strlen(esperado));
  VERIFICAR(strcmp(buf, esperado) == 0);
}

static void construye_estado() {
  char buf[64];
  verifica_estado(buf, construirEstado(buf, sizeof buf, EstadoServo::Cerrado, ColorLed::Azul, false),
                  "{\"servo\":\"cerrado\",\"led\":\"azul\",\"luz\":0}");
  verifica_estado(buf, construirEstado(buf, sizeof buf, EstadoServo::Abierto, ColorLed::Verde, true),
                  "{\"servo\":\"abierto\",\"led\":\"verde\",\"luz\":1}");
  verifica_estado(buf, construirEstado(buf, sizeof buf, EstadoServo::Cerrado, ColorLed::Rojo, true),
                  "{\"servo\":\"cerrado\",\"led\":\"rojo\",\"luz\":1}");
  verifica_estado(buf, construirEstado(buf, sizeof buf, EstadoServo::Cerrado, ColorLed::Ambar, false),
                  "{\"servo\":\"cerrado\",\"led\":\"ambar\",\"luz\":0}");
  verifica_estado(buf, construirEstado(buf, sizeof buf, EstadoServo::Cerrado, ColorLed::Apagado, false),
                  "{\"servo\":\"cerrado\",\"led\":\"apagado\",\"luz\":0}");
}

static void construye_estado_desde_el_control() {
  ControlActuadores c;
  char buf[64];
  c.aplicarSesionActiva(true);
  c.aplicarServo(PedidoServo::Abrir, 0);
  verifica_estado(buf, construirEstado(buf, sizeof buf, c, true),
                  "{\"servo\":\"abierto\",\"led\":\"azul\",\"luz\":1}");
}

static void estado_no_cabe() {
  char buf[40];
  memset(buf, 'x', sizeof buf);
  VERIFICAR(construirEstado(buf, 40, EstadoServo::Cerrado, ColorLed::Azul, false) == 0);
  VERIFICAR(buf[0] == '\0');  // no deja basura
  char justo[41];
  VERIFICAR(construirEstado(justo, sizeof justo, EstadoServo::Cerrado, ColorLed::Azul, false) == 40);
  VERIFICAR(strcmp(justo, "{\"servo\":\"cerrado\",\"led\":\"azul\",\"luz\":0}") == 0);
  VERIFICAR(construirEstado(nullptr, 0, EstadoServo::Cerrado, ColorLed::Azul, false) == 0);
  char uno[1] = {'x'};
  VERIFICAR(construirEstado(uno, 1, EstadoServo::Cerrado, ColorLed::Azul, false) == 0);
  VERIFICAR(uno[0] == '\0');
}

int main() {
  interpreta_servo();
  interpreta_led();
  interpreta_buzzer();
  es_orden();
  nombres_del_contrato();
  servo_se_abre_y_se_cierra_solo();
  servo_repetido_alarga_la_ventana();
  servo_con_desbordamiento_de_millis();
  led_solo_cambia_si_cambia_de_verdad();
  el_azul_de_la_sesion_manda();
  buzzer_pitido_de_exito();
  buzzer_doble_pitido_de_error();
  buzzer_silencio_corta_el_pitido();
  buzzer_con_desbordamiento_de_millis();
  construye_estado();
  construye_estado_desde_el_control();
  estado_no_cabe();
  printf("%d verificaciones, %d fallos\n", total, fallos);
  return fallos == 0 ? 0 : 1;
}
