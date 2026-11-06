// Pruebas de host de logica_luz (sin dependencias). Compilar y correr desde
// firmware/xiao/ con los comandos del README, sección "Pruebas de host".
#include <stdio.h>
#include <string.h>

#include "../logica_luz.h"

using namespace logica_luz;

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

static void descarta_retenido_y_vacio() {
  VERIFICAR(!debeDescartar(false, 10));
  VERIFICAR(debeDescartar(true, 10));
  VERIFICAR(debeDescartar(false, 0));
  VERIFICAR(debeDescartar(true, 0));
}

static void interpreta_luz() {
  VERIFICAR(interpretarLuz(false, false, 0) == PedidoLuz::Ausente);
  VERIFICAR(interpretarLuz(false, true, 1) == PedidoLuz::Ausente);
  VERIFICAR(interpretarLuz(true, true, 0) == PedidoLuz::Apagar);
  VERIFICAR(interpretarLuz(true, true, 1) == PedidoLuz::Encender);
  // Presente pero no entero (true, "1", 1.5, un objeto): inválido aunque el valor sea 0 o 1.
  VERIFICAR(interpretarLuz(true, false, 1) == PedidoLuz::Invalido);
  VERIFICAR(interpretarLuz(true, false, 0) == PedidoLuz::Invalido);
  // Enteros fuera de 0 y 1.
  VERIFICAR(interpretarLuz(true, true, 2) == PedidoLuz::Invalido);
  VERIFICAR(interpretarLuz(true, true, -1) == PedidoLuz::Invalido);
  VERIFICAR(interpretarLuz(true, true, 256) == PedidoLuz::Invalido);
}

static void es_orden() {
  VERIFICAR(esOrden(PedidoLuz::Apagar));
  VERIFICAR(esOrden(PedidoLuz::Encender));
  VERIFICAR(!esOrden(PedidoLuz::Ausente));
  VERIFICAR(!esOrden(PedidoLuz::Invalido));
}

static void control_luz_cambia_solo_si_cambia_el_estado() {
  ControlLuz c;
  VERIFICAR(!c.encendida());                       // arranca apagada
  VERIFICAR(!c.aplicar(PedidoLuz::Apagar, 0));     // ya estaba apagada
  VERIFICAR(c.aplicar(PedidoLuz::Encender, 0));
  VERIFICAR(c.encendida());
  VERIFICAR(!c.aplicar(PedidoLuz::Encender, 10));  // repetir no cambia
  VERIFICAR(c.encendida());
  VERIFICAR(!c.aplicar(PedidoLuz::Ausente, 20));
  VERIFICAR(!c.aplicar(PedidoLuz::Invalido, 30));
  VERIFICAR(c.encendida());                        // lo inválido no apaga ni enciende
  VERIFICAR(c.aplicar(PedidoLuz::Apagar, 40));     // también es la vía del apagado de seguridad
  VERIFICAR(!c.encendida());
}

static void apagado_automatico_vence() {
  ControlLuz c(1000);
  VERIFICAR(!c.revisarVencimiento(5000));          // apagada: nada que vencer
  c.aplicar(PedidoLuz::Encender, 100);
  VERIFICAR(!c.revisarVencimiento(100));
  VERIFICAR(!c.revisarVencimiento(1099));          // un ms antes del límite
  VERIFICAR(c.encendida());
  VERIFICAR(c.revisarVencimiento(1100));           // justo en el límite
  VERIFICAR(!c.encendida());
  VERIFICAR(!c.revisarVencimiento(9999));          // ya apagada: no vuelve a avisar
}

static void apagado_automatico_por_defecto_es_360_s() {
  // Cubre el vencimiento de 5 min del registro en Node-RED con margen.
  VERIFICAR(kApagadoAutomaticoMs == 360000);
  ControlLuz c;
  c.aplicar(PedidoLuz::Encender, 0);
  VERIFICAR(!c.revisarVencimiento(359999));
  VERIFICAR(c.revisarVencimiento(360000));
}

static void cabe_cmd() {
  VERIFICAR(kMaxCmdBytes == 256);
  VERIFICAR(cabeCmd(0));
  VERIFICAR(cabeCmd(60));
  VERIFICAR(cabeCmd(255));
  VERIFICAR(!cabeCmd(256));
  VERIFICAR(!cabeCmd(4096));
}

static void otro_encender_renueva_el_plazo() {
  ControlLuz c(1000);
  c.aplicar(PedidoLuz::Encender, 0);
  VERIFICAR(!c.aplicar(PedidoLuz::Encender, 900));  // renovar no es un cambio de estado
  VERIFICAR(!c.revisarVencimiento(1000));           // el plazo original ya no rige
  VERIFICAR(!c.revisarVencimiento(1899));
  VERIFICAR(c.revisarVencimiento(1900));            // 1000 ms después de la renovación
  // Ausente e Invalido no renuevan.
  c.aplicar(PedidoLuz::Encender, 5000);
  c.aplicar(PedidoLuz::Ausente, 5900);
  c.aplicar(PedidoLuz::Invalido, 5900);
  VERIFICAR(c.revisarVencimiento(6000));
}

static void apagar_antes_cancela_el_vencimiento() {
  ControlLuz c(1000);
  c.aplicar(PedidoLuz::Encender, 0);
  VERIFICAR(c.aplicar(PedidoLuz::Apagar, 500));
  VERIFICAR(!c.revisarVencimiento(1000));
  VERIFICAR(!c.revisarVencimiento(2000));
  // Un Encender posterior empieza un plazo nuevo.
  VERIFICAR(c.aplicar(PedidoLuz::Encender, 3000));
  VERIFICAR(!c.revisarVencimiento(3999));
  VERIFICAR(c.revisarVencimiento(4000));
}

static void apagado_automatico_con_desbordamiento_de_millis() {
  ControlLuz c(1000);
  const uint32_t desde = 0xFFFFFF00u;               // 256 ms antes del desbordamiento
  c.aplicar(PedidoLuz::Encender, desde);
  VERIFICAR(!c.revisarVencimiento(0xFFFFFFFFu));
  VERIFICAR(!c.revisarVencimiento(500));            // ya desbordó: han pasado 757 ms
  VERIFICAR(!c.revisarVencimiento(743));            // 999 ms
  VERIFICAR(c.revisarVencimiento(744));             // 1000 ms
  VERIFICAR(!c.encendida());
}

int main() {
  descarta_retenido_y_vacio();
  interpreta_luz();
  es_orden();
  control_luz_cambia_solo_si_cambia_el_estado();
  apagado_automatico_vence();
  apagado_automatico_por_defecto_es_360_s();
  cabe_cmd();
  otro_encender_renueva_el_plazo();
  apagar_antes_cancela_el_vencimiento();
  apagado_automatico_con_desbordamiento_de_millis();
  printf("%d verificaciones, %d fallos\n", total, fallos);
  return fallos == 0 ? 0 : 1;
}
