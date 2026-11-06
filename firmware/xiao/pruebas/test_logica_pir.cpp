// Pruebas de host de logica_pir (sin dependencias). Compilar y correr desde
// firmware/xiao/ con los comandos del README, sección "Pruebas de host".
#include <stdio.h>
#include <string.h>

#include "../logica_pir.h"

using namespace logica_pir;

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

// Control sin calentamiento (calentamiento_ms = 0), para poder probar el
// antirrebote y los flances sin pelearse con la ventana de arranque.
static ControlPir sinCalentamiento(uint32_t antirrebote_ms = kAntirreboteMs) {
  return ControlPir(0, antirrebote_ms, 0);
}

static void nivel_alto_es_movimiento() {
  VERIFICAR(esMovimiento(1));   // HIGH
  VERIFICAR(!esMovimiento(0));  // LOW
}

static void flanco_de_subida_publica() {
  ControlPir p = sinCalentamiento();
  VERIFICAR(!p.procesarNivel(false, 1000));  // nivel bajo: nada
  VERIFICAR(p.procesarNivel(true, 1100));    // flanco de subida: hay que publicar
  VERIFICAR(!p.procesarNivel(true, 1200));   // sigue en alto: no hay flanco
  VERIFICAR(!p.procesarNivel(false, 1300));  // flanco de bajada: no se publica
  VERIFICAR(!p.procesarNivel(true, 1400));   // otra subida a 300 ms: antirrebote
  VERIFICAR(!p.procesarNivel(false, 1500));
  VERIFICAR(p.procesarNivel(true, 3100));    // a 2000 ms del evento: sí
}

static void flanco_de_bajada_no_publica() {
  ControlPir p = sinCalentamiento();
  VERIFICAR(!p.procesarNivel(false, 0));
  VERIFICAR(!p.procesarNivel(false, 10));
  VERIFICAR(!p.procesarNivel(false, 20));
  // Tampoco al bajar después de un evento publicado.
  p.procesarNivel(true, 100);
  VERIFICAR(!p.procesarNivel(false, 200));
  VERIFICAR(!p.procesarNivel(false, 300));
}

static void antirrebote_descarta_los_flanches_cercanos() {
  ControlPir p = sinCalentamiento(2000);
  VERIFICAR(!p.procesarNivel(false, 0));
  VERIFICAR(p.procesarNivel(true, 1000));    // evento
  VERIFICAR(!p.procesarNivel(false, 1100));
  VERIFICAR(!p.procesarNivel(true, 1500));   // dentro de la ventana: no
  VERIFICAR(!p.procesarNivel(false, 1600));
  VERIFICAR(!p.procesarNivel(true, 1999));   // a 999 ms del evento: no
  VERIFICAR(!p.procesarNivel(false, 2100));
  VERIFICAR(p.procesarNivel(true, 3000));    // 2000 ms exactos del evento: sí
}

static void antirrebote_por_defecto_es_2_s() {
  VERIFICAR(kAntirreboteMs == 2000);
  ControlPir p = sinCalentamiento();
  VERIFICAR(!p.procesarNivel(false, 0));
  VERIFICAR(p.procesarNivel(true, 0));      // evento
  VERIFICAR(!p.procesarNivel(false, 10));
  VERIFICAR(!p.procesarNivel(true, 1999));  // a 1999 ms: todavía dentro
  VERIFICAR(!p.procesarNivel(false, 2000));
  VERIFICAR(p.procesarNivel(true, 2000));   // a 2000 ms: fuera, se publica
}

static void el_antirrebote_no_lo_alarga_un_flanco_descartado() {
  // La ventana se mide desde el último evento publicado: un flanco descartado
  // no la reinicia, así que dos eventos nunca están a menos de 2 s.
  ControlPir p = sinCalentamiento(2000);
  VERIFICAR(!p.procesarNivel(false, 0));
  VERIFICAR(p.procesarNivel(true, 0));      // evento
  VERIFICAR(!p.procesarNivel(false, 500));
  VERIFICAR(!p.procesarNivel(true, 1000));  // descartado (a 1000 ms del evento)
  VERIFICAR(!p.procesarNivel(false, 1500));
  VERIFICAR(p.procesarNivel(true, 2000));   // a 2000 ms del evento: se publica
}

static void el_calentamiento_ignora_el_primer_flanco() {
  VERIFICAR(kCalentamientoMs == 60000);
  ControlPir p(1000);  // la placa arrancó en el milisegundo 1000
  VERIFICAR(p.enCalentamiento(1000));
  VERIFICAR(p.enCalentamiento(60999));
  VERIFICAR(!p.enCalentamiento(61000));
  VERIFICAR(!p.enCalentamiento(2000000));  // pasado el calentamiento, no vuelve

  VERIFICAR(!p.procesarNivel(false, 2000));
  VERIFICAR(!p.procesarNivel(true, 3000));   // calentamiento: no es una persona
  VERIFICAR(!p.procesarNivel(false, 4000));
  VERIFICAR(!p.procesarNivel(true, 5000));   // sigue en calentamiento
  VERIFICAR(!p.procesarNivel(false, 61000));
  VERIFICAR(p.procesarNivel(true, 62000));   // ya calentado: se publica
}

static void el_primer_flanco_caliente_no_es_movimiento() {
  // Caso típico: el PIR dispara al powering y se queda en alto un rato.
  ControlPir p(0);
  VERIFICAR(!p.procesarNivel(true, 100));    // calentamiento: ignorado
  VERIFICAR(!p.procesarNivel(true, 5000));   // sigue en alto: nada
  VERIFICAR(!p.procesarNivel(false, 60000));
  VERIFICAR(p.procesarNivel(true, 61000));   // movimiento real: se publica
}

static void el_antirrebote_corre_con_el_desbordamiento_de_millis() {
  ControlPir p = sinCalentamiento(2000);
  const uint32_t desde = 0xFFFFFF00u;       // 256 ms antes del desbordamiento
  VERIFICAR(!p.procesarNivel(false, desde));
  VERIFICAR(p.procesarNivel(true, desde));   // evento, justo antes de la vuelta
  VERIFICAR(!p.procesarNivel(false, desde + 1));
  VERIFICAR(!p.procesarNivel(true, 0xFFFFFFFFu));  // 255 ms: dentro
  VERIFICAR(!p.procesarNivel(false, 0xFFFFFFFFu));
  VERIFICAR(!p.procesarNivel(true, 500));         // 756 ms: dentro
  VERIFICAR(!p.procesarNivel(false, 500));
  VERIFICAR(!p.procesarNivel(true, 743));         // 999 ms: dentro
  VERIFICAR(!p.procesarNivel(false, 743));
  VERIFICAR(p.procesarNivel(true, 1744));         // 2000 ms: se publica
}

static void el_calentamiento_corre_con_el_desbordamiento_de_millis() {
  ControlPir p(0xFFFFFF00u);  // la placa arrancó 256 ms antes de la vuelta
  VERIFICAR(p.enCalentamiento(0xFFFFFFFFu));   // sigue calentándose
  VERIFICAR(p.enCalentamiento(30000));         // yameatrás de la vuelta
  VERIFICAR(!p.enCalentamiento(0xFFFFFF00u + kCalentamientoMs));
}

static void consulta_la_ventana_de_antirrebote() {
  ControlPir p = sinCalentamiento(2000);
  VERIFICAR(!p.dentroDeAntirrebote(1000));  // aún no hay evento
  VERIFICAR(!p.procesarNivel(false, 0));
  VERIFICAR(p.procesarNivel(true, 0));
  VERIFICAR(p.dentroDeAntirrebote(0));
  VERIFICAR(p.dentroDeAntirrebote(1999));
  VERIFICAR(!p.dentroDeAntirrebote(2000));
}

static void construye_el_payload() {
  char buf[16];
  VERIFICAR(kMovimientoBytes == 9);
  VERIFICAR(construirMovimiento(buf, sizeof buf) == 9);
  VERIFICAR(strcmp(buf, "{\"mov\":1}") == 0);
  VERIFICAR(construirMovimiento(nullptr, 0) == 0);
}

static void el_payload_no_cabe() {
  char buf[9];
  memset(buf, 'x', sizeof buf);
  VERIFICAR(construirMovimiento(buf, 9) == 0);  // falta el terminador
  VERIFICAR(buf[0] == '\0');                    // no deja basura
  char justo[10];
  VERIFICAR(construirMovimiento(justo, sizeof justo) == 9);
  VERIFICAR(strcmp(justo, "{\"mov\":1}") == 0);
  char uno[1] = {'x'};
  VERIFICAR(construirMovimiento(uno, 1) == 0);
  VERIFICAR(uno[0] == '\0');
}

int main() {
  nivel_alto_es_movimiento();
  flanco_de_subida_publica();
  flanco_de_bajada_no_publica();
  antirrebote_descarta_los_flanches_cercanos();
  antirrebote_por_defecto_es_2_s();
  el_antirrebote_no_lo_alarga_un_flanco_descartado();
  el_calentamiento_ignora_el_primer_flanco();
  el_primer_flanco_caliente_no_es_movimiento();
  el_antirrebote_corre_con_el_desbordamiento_de_millis();
  el_calentamiento_corre_con_el_desbordamiento_de_millis();
  consulta_la_ventana_de_antirrebote();
  construye_el_payload();
  el_payload_no_cabe();
  printf("%d verificaciones, %d fallos\n", total, fallos);
  return fallos == 0 ? 0 : 1;
}
