// Pruebas de host de logica_proteccion (sin dependencias). Compilar y correr
// desde firmware/xiao/ con los comandos del README, sección "Pruebas de host".
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../logica_proteccion.h"

using namespace logica_proteccion;

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

// --- Resolución ----------------------------------------------------------------

static void la_lista_blanca_son_cuatro_nombres_exactos() {
  VERIFICAR(parsearResolucion("HVGA") == Resolucion::Hvga);
  VERIFICAR(parsearResolucion("VGA") == Resolucion::Vga);
  VERIFICAR(parsearResolucion("SVGA") == Resolucion::Svga);
  VERIFICAR(parsearResolucion("XGA") == Resolucion::Xga);
}

static void lo_desconocido_cae_en_svga() {
  VERIFICAR(kResolucionPorDefecto == Resolucion::Svga);
  // Lo que está por encima de XGA, aunque el sensor lo soporte, no se produce.
  const char* const rechazados[] = {"UXGA", "QXGA", "SXGA", "HD", "FHD", "QVGA", "CIF", "5MP",
                                    "xga", "Xga", "svga", " XGA", "XGA ", "XGAA", "XG", "",
                                    "8", "-1", "XGA\n", "VGA;XGA", "%58GA"};
  for (const char* r : rechazados) {
    VERIFICAR(parsearResolucion(r) == Resolucion::Svga);
  }
  VERIFICAR(parsearResolucion(nullptr) == Resolucion::Svga);
}

static void los_perfiles_son_los_acordados() {
  VERIFICAR(perfil(Resolucion::Hvga).ancho == 480 && perfil(Resolucion::Hvga).alto == 320);
  VERIFICAR(perfil(Resolucion::Vga).ancho == 640 && perfil(Resolucion::Vga).alto == 480);
  VERIFICAR(perfil(Resolucion::Svga).ancho == 800 && perfil(Resolucion::Svga).alto == 600);
  VERIFICAR(perfil(Resolucion::Xga).ancho == 1024 && perfil(Resolucion::Xga).alto == 768);
  VERIFICAR(perfil(Resolucion::Hvga).calidad_jpeg == 20);
  VERIFICAR(perfil(Resolucion::Vga).calidad_jpeg == 12);
  VERIFICAR(perfil(Resolucion::Svga).calidad_jpeg == 12);
  VERIFICAR(perfil(Resolucion::Xga).calidad_jpeg == 12);
  VERIFICAR(strcmp(perfil(Resolucion::Svga).nombre, "SVGA") == 0);
  // El nombre del perfil vuelve a dar su propia resolución.
  const Resolucion todas[] = {Resolucion::Hvga, Resolucion::Vga, Resolucion::Svga, Resolucion::Xga};
  for (Resolucion r : todas) {
    VERIFICAR(perfil(r).resolucion == r);
    VERIFICAR(parsearResolucion(perfil(r).nombre) == r);
  }
}

static void ninguna_resolucion_pasa_de_xga() {
  const Perfil& max = perfil(kResolucionMaxima);
  VERIFICAR(kResolucionMaxima == Resolucion::Xga);
  const Resolucion todas[] = {Resolucion::Hvga, Resolucion::Vga, Resolucion::Svga, Resolucion::Xga};
  for (Resolucion r : todas) {
    VERIFICAR(perfil(r).ancho <= max.ancho);
    VERIFICAR(perfil(r).alto <= max.alto);
  }
  // Un valor del enum fuera de rango (no debería ocurrir) no se sale de la tabla.
  VERIFICAR(perfil(static_cast<Resolucion>(200)).resolucion == Resolucion::Svga);
}

static void la_query_se_lee_por_parametro() {
  VERIFICAR(resolucionDeQuery("res=XGA") == Resolucion::Xga);
  VERIFICAR(resolucionDeQuery("res=HVGA") == Resolucion::Hvga);
  VERIFICAR(resolucionDeQuery("a=1&res=VGA&b=2") == Resolucion::Vga);
  VERIFICAR(resolucionDeQuery("res=XGA&res=VGA") == Resolucion::Xga);  // gana la primera
  VERIFICAR(resolucionDeQuery("res=VGA#res=XGA") == Resolucion::Vga);
  VERIFICAR(resolucionDeQuery("x=1#res=XGA") == Resolucion::Svga);
}

static void la_query_mala_cae_en_svga() {
  VERIFICAR(resolucionDeQuery(nullptr) == Resolucion::Svga);
  VERIFICAR(resolucionDeQuery("") == Resolucion::Svga);
  VERIFICAR(resolucionDeQuery("res=") == Resolucion::Svga);
  VERIFICAR(resolucionDeQuery("res=UXGA") == Resolucion::Svga);
  VERIFICAR(resolucionDeQuery("res=QXGA") == Resolucion::Svga);
  VERIFICAR(resolucionDeQuery("res=xga") == Resolucion::Svga);
  VERIFICAR(resolucionDeQuery("res=XGA9") == Resolucion::Svga);
  VERIFICAR(resolucionDeQuery("res=XGAXGAXGAXGAXGA") == Resolucion::Svga);  // más largo que el búfer
  VERIFICAR(resolucionDeQuery("resolucion=XGA") == Resolucion::Svga);
  VERIFICAR(resolucionDeQuery("xres=XGA") == Resolucion::Svga);
  VERIFICAR(resolucionDeQuery("RES=XGA") == Resolucion::Svga);
  VERIFICAR(resolucionDeQuery("res") == Resolucion::Svga);
  VERIFICAR(resolucionDeQuery("res=%58GA") == Resolucion::Svga);
  VERIFICAR(resolucionDeQuery("&&&") == Resolucion::Svga);
  VERIFICAR(resolucionDeQuery("res=XGA=1") == Resolucion::Svga);
}

// --- Límite de capturas -----------------------------------------------------------------

static void cuatro_en_la_ventana_y_la_quinta_no() {
  LimitadorCapturas l;
  VERIFICAR(l.intentar(1000).permitido);
  VERIFICAR(l.intentar(1100).permitido);
  VERIFICAR(l.intentar(1200).permitido);
  VERIFICAR(l.intentar(1300).permitido);
  const DecisionLimite d = l.intentar(1400);
  VERIFICAR(!d.permitido);
  VERIFICAR(d.espera_ms > 0);
}

static void dos_capturas_separadas_no_llenan_nada() {
  LimitadorCapturas l;
  for (uint32_t t = 0; t < 20000; t += 1000) VERIFICAR(l.intentar(t).permitido);  // 1/s sostenido
}

static void la_pausa_tras_la_rafaga_cierra_hasta_la_ventana_mas_1_s() {
  LimitadorCapturas l;
  l.intentar(0);
  l.intentar(100);
  l.intentar(200);
  VERIFICAR(l.intentar(300).permitido);  // cuarta: ráfaga completa
  // La ventana se vacía a los 2000 ms del primero, pero la pausa de 1 s corre
  // después: se abre a los 3000 ms del primero.
  VERIFICAR(!l.intentar(2000).permitido);
  VERIFICAR(!l.intentar(2999).permitido);
  const DecisionLimite d = l.intentar(2500);
  VERIFICAR(!d.permitido);
  VERIFICAR(d.espera_ms == 500);
  VERIFICAR(l.intentar(3000).permitido);
}

static void el_429_no_alarga_la_espera() {
  LimitadorCapturas l;
  for (uint32_t t = 0; t < 4; ++t) l.intentar(t * 10);
  for (uint32_t t = 100; t < 3000; t += 50) VERIFICAR(!l.intentar(t).permitido);  // insistir
  VERIFICAR(l.intentar(3000).permitido);  // 3000 desde el primero (0), no desde el último intento
}

static void la_ventana_es_deslizante() {
  LimitadorCapturas l;
  VERIFICAR(l.intentar(0).permitido);
  VERIFICAR(l.intentar(1000).permitido);
  VERIFICAR(l.intentar(1500).permitido);
  VERIFICAR(l.intentar(1900).permitido);  // cuatro en 1900 ms: ráfaga completa
  VERIFICAR(!l.intentar(2000).permitido);  // cerrado hasta 0 + 2000 + 1000
  // A los 3000 ms salieron de la ventana las de 0 y 1000; quedan 1500 y 1900.
  VERIFICAR(l.intentar(3000).permitido);
  VERIFICAR(l.intentar(3010).permitido);   // otra vez cuatro en la ventana (1500, 1900, 3000, 3010)
  VERIFICAR(!l.intentar(4499).permitido);  // cerrado hasta 1500 + 2000 + 1000
  VERIFICAR(l.intentar(4500).permitido);
}

static void con_el_desbordamiento_de_millis() {
  LimitadorCapturas l;
  const uint32_t t0 = 0xFFFFFFFFu - 150;  // la ráfaga cruza el desbordamiento
  VERIFICAR(l.intentar(t0).permitido);
  VERIFICAR(l.intentar(t0 + 100).permitido);
  VERIFICAR(l.intentar(t0 + 200).permitido);  // ya desbordó
  VERIFICAR(l.intentar(t0 + 300).permitido);
  VERIFICAR(!l.intentar(t0 + 400).permitido);
  VERIFICAR(!l.intentar(t0 + 2999).permitido);
  VERIFICAR(l.intentar(t0 + 3000).permitido);
}

static void la_espera_informada_es_la_real() {
  LimitadorCapturas l;
  l.intentar(0);
  l.intentar(0);
  l.intentar(0);
  l.intentar(0);
  const DecisionLimite d = l.intentar(1000);
  VERIFICAR(!d.permitido);
  VERIFICAR(d.espera_ms == 2000);  // 3000 - 1000
}

static void sin_pausa_manda_solo_la_ventana() {
  LimitadorCapturas l(2000, 0);
  for (int i = 0; i < 4; ++i) VERIFICAR(l.intentar(static_cast<uint32_t>(i) * 100).permitido);
  const DecisionLimite d = l.intentar(500);
  VERIFICAR(!d.permitido);
  VERIFICAR(d.espera_ms == 1500);  // la ventana se vacía a los 2000 del primero
  VERIFICAR(l.intentar(2000).permitido);
}

static void reiniciar_olvida_todo() {
  LimitadorCapturas l;
  for (int i = 0; i < 4; ++i) l.intentar(0);
  VERIFICAR(!l.intentar(10).permitido);
  l.reiniciar();
  VERIFICAR(l.intentar(10).permitido);
}

// --- Guarda térmica -----------------------------------------------------------------------

static void antes_de_leer_no_se_permite_capturar() {
  GuardaTermica g;
  VERIFICAR(!g.capturaPermitida());
  VERIFICAR(!g.lecturaValida());
  VERIFICAR(g.estado() == EstadoTermico::Normal);
}

static void normal_aviso_y_corte() {
  GuardaTermica g;
  VERIFICAR(g.actualizar(40.0f) == Transicion::Ninguna);
  VERIFICAR(g.capturaPermitida());
  VERIFICAR(g.actualizar(64.9f) == Transicion::Ninguna);
  VERIFICAR(g.estado() == EstadoTermico::Normal);
  VERIFICAR(g.actualizar(65.0f) == Transicion::EntraAviso);  // el aviso es >= 65
  VERIFICAR(g.estado() == EstadoTermico::Aviso);
  VERIFICAR(g.capturaPermitida());                            // en aviso se sigue capturando
  VERIFICAR(g.actualizar(70.0f) == Transicion::Ninguna);
  VERIFICAR(g.actualizar(74.9f) == Transicion::Ninguna);
  VERIFICAR(g.capturaPermitida());
  VERIFICAR(g.actualizar(75.0f) == Transicion::EntraCorte);  // el corte es >= 75
  VERIFICAR(g.estado() == EstadoTermico::Corte);
  VERIFICAR(!g.capturaPermitida());
}

static void el_corte_no_se_levanta_hasta_bajar_de_65() {
  GuardaTermica g;
  g.actualizar(80.0f);
  VERIFICAR(g.estado() == EstadoTermico::Corte);
  VERIFICAR(g.actualizar(75.0f) == Transicion::Ninguna);
  VERIFICAR(g.actualizar(74.0f) == Transicion::Ninguna);  // bajó de 75 pero sigue cortado
  VERIFICAR(!g.capturaPermitida());
  VERIFICAR(g.actualizar(66.0f) == Transicion::Ninguna);
  VERIFICAR(!g.capturaPermitida());
  VERIFICAR(g.actualizar(65.0f) == Transicion::Ninguna);  // 65 exacto: todavía no ("por debajo")
  VERIFICAR(!g.capturaPermitida());
  VERIFICAR(g.actualizar(64.9f) == Transicion::SaleCorte);
  VERIFICAR(g.estado() == EstadoTermico::Normal);
  VERIFICAR(g.capturaPermitida());
}

static void tras_salir_del_corte_vuelve_a_cortar_y_a_avisar() {
  GuardaTermica g;
  g.actualizar(76.0f);
  g.actualizar(60.0f);
  VERIFICAR(g.capturaPermitida());
  VERIFICAR(g.actualizar(65.5f) == Transicion::EntraAviso);  // sale de Normal, no del corte
  VERIFICAR(g.actualizar(80.0f) == Transicion::EntraCorte);  // de aviso directo a corte
  VERIFICAR(!g.capturaPermitida());
}

static void salto_directo_de_normal_a_corte() {
  GuardaTermica g;
  VERIFICAR(g.actualizar(30.0f) == Transicion::Ninguna);
  VERIFICAR(g.actualizar(90.0f) == Transicion::EntraCorte);
  VERIFICAR(!g.capturaPermitida());
}

static void el_aviso_se_apaga_al_bajar_de_65() {
  GuardaTermica g;
  g.actualizar(66.0f);
  VERIFICAR(g.estado() == EstadoTermico::Aviso);
  VERIFICAR(g.actualizar(64.0f) == Transicion::SaleAviso);
  VERIFICAR(g.estado() == EstadoTermico::Normal);
}

static void una_lectura_no_valida_falla_cerrado() {
  GuardaTermica g;
  g.actualizar(40.0f);
  VERIFICAR(g.capturaPermitida());
  VERIFICAR(g.actualizar(NAN) == Transicion::Ninguna);
  VERIFICAR(!g.lecturaValida());
  VERIFICAR(!g.capturaPermitida());           // no se captura sin una lectura creíble
  VERIFICAR(g.estado() == EstadoTermico::Normal);  // pero el estado no cambió
  VERIFICAR(g.actualizar(41.0f) == Transicion::Ninguna);
  VERIFICAR(g.capturaPermitida());            // vuelve con la siguiente lectura válida
  VERIFICAR(g.actualizar(INFINITY) == Transicion::Ninguna);
  VERIFICAR(!g.capturaPermitida());
  g.actualizar(-300.0f);
  VERIFICAR(!g.capturaPermitida());
  g.actualizar(200.0f);
  VERIFICAR(!g.capturaPermitida());
}

static void una_lectura_no_valida_no_levanta_el_corte() {
  GuardaTermica g;
  g.actualizar(80.0f);
  g.actualizar(NAN);
  VERIFICAR(g.estado() == EstadoTermico::Corte);
  VERIFICAR(!g.capturaPermitida());
  g.actualizar(20.0f);
  VERIFICAR(g.capturaPermitida());
}

// --- Planificación y reposo ----------------------------------------------------------------

static void el_intervalo_cumplido_corre_con_el_desbordamiento() {
  VERIFICAR(!intervaloCumplido(1000, 0, 30000));
  VERIFICAR(!intervaloCumplido(29999, 0, 30000));
  VERIFICAR(intervaloCumplido(30000, 0, 30000));
  const uint32_t ultimo = 0xFFFFFFFFu - 10000;
  VERIFICAR(!intervaloCumplido(ultimo + 29999, ultimo, 30000));  // cruza el desbordamiento
  VERIFICAR(intervaloCumplido(ultimo + 30000, ultimo, 30000));
  VERIFICAR(kIntervaloTemperaturaMs == 30000 && kIntervaloLatidoMs == 30000);
}

static void la_inactividad_apaga_a_los_15_s() {
  VERIFICAR(kInactividadCamaraMs == 15000);
  VERIFICAR(!debeApagarPorInactividad(true, 1000, 1000));
  VERIFICAR(!debeApagarPorInactividad(true, 15999, 1000));
  VERIFICAR(debeApagarPorInactividad(true, 16000, 1000));
  VERIFICAR(debeApagarPorInactividad(true, 600000, 1000));
}

static void la_camara_apagada_no_se_vuelve_a_apagar() {
  VERIFICAR(!debeApagarPorInactividad(false, 600000, 0));
}

static void la_inactividad_corre_con_el_desbordamiento() {
  const uint32_t actividad = 0xFFFFFFFFu - 5000;
  VERIFICAR(!debeApagarPorInactividad(true, actividad + 14999, actividad));
  VERIFICAR(debeApagarPorInactividad(true, actividad + 15000, actividad));  // ahora < actividad
}

static void la_inactividad_se_puede_cambiar() {
  VERIFICAR(debeApagarPorInactividad(true, 5000, 0, 5000));
  VERIFICAR(!debeApagarPorInactividad(true, 4999, 0, 5000));
}

static void cuadros_a_descartar() {
  VERIFICAR(cuadrosADescartar(true, false) == 3);
  VERIFICAR(cuadrosADescartar(true, true) == 3);   // el arranque en frío ya incluye el cambio
  VERIFICAR(cuadrosADescartar(false, true) == 2);
  VERIFICAR(cuadrosADescartar(false, false) == 1); // en caliente siempre se tira el cuadro viejo
}

// --- Latido -----------------------------------------------------------------------------------

static void el_latido_tiene_los_campos_del_contrato() {
  char b[kLatidoBytesMax];
  const size_t n = construirLatido(b, sizeof b, -58, 120000, 48.0f, false, false, true);
  VERIFICAR(n == strlen(b));
  VERIFICAR(strcmp(b, "{\"en_linea\":true,\"rssi\":-58,\"heap\":120000,\"temp\":48.0,"
                      "\"camara\":false,\"corte\":false,\"nvs\":true}") == 0);
  construirLatido(b, sizeof b, -71, 98304, 52.34f, true, true, false);
  VERIFICAR(strcmp(b, "{\"en_linea\":true,\"rssi\":-71,\"heap\":98304,\"temp\":52.3,"
                      "\"camara\":true,\"corte\":true,\"nvs\":false}") == 0);
}

static void el_latido_omite_temp_si_no_hay_lectura() {
  char b[kLatidoBytesMax];
  const char* const esperado =
      "{\"en_linea\":true,\"rssi\":-58,\"heap\":120000,\"camara\":false,\"corte\":true,\"nvs\":true}";
  VERIFICAR(construirLatido(b, sizeof b, -58, 120000, NAN, false, true, true) > 0);
  VERIFICAR(strcmp(b, esperado) == 0);
  construirLatido(b, sizeof b, -58, 120000, 1e30f, false, true, true);
  VERIFICAR(strcmp(b, esperado) == 0);
  construirLatido(b, sizeof b, -58, 120000, -INFINITY, false, true, true);
  VERIFICAR(strcmp(b, esperado) == 0);
}

static void el_peor_caso_del_latido_cabe_en_el_bufer_de_pubsubclient() {
  char b[kLatidoBytesMax];
  // camara, corte y nvs en false son lo más largo ("false" mide 5).
  const size_t n = construirLatido(b, sizeof b, -2147483647 - 1, 4294967295u, -40.0f, false, false, false);
  VERIFICAR(n > 0);
  VERIFICAR(n == 108);
  // PubSubClient: búfer de 256 B = 5 de cabecera + 2 del largo del tópico + tópico + payload.
  const size_t topico = strlen("aula/camara/estado");
  VERIFICAR(5 + 2 + topico + n <= 256);
  VERIFICAR(n + 1 <= kLatidoBytesMax);
}

static void el_latido_que_no_cabe_devuelve_cero() {
  char b[20];
  memset(b, 'x', sizeof b);
  VERIFICAR(construirLatido(b, sizeof b, -58, 120000, 48.0f, false, false, true) == 0);
  VERIFICAR(b[0] == '\0');
  VERIFICAR(construirLatido(nullptr, 10, 0, 0, 0.0f, false, false, false) == 0);
  VERIFICAR(construirLatido(b, 0, 0, 0, 0.0f, false, false, false) == 0);
}

// --- Presupuesto de encendido -------------------------------------------------------------------

static void el_presupuesto_por_defecto_es_300_s_en_10_min() {
  VERIFICAR(kPresupuestoEncendidoMs == 300000);
  VERIFICAR(kVentanaPresupuestoMs == 600000);
  VERIFICAR(kEnfriamientoMs == 120000);
}

static void el_presupuesto_cuenta_el_tramo_abierto() {
  PresupuestoCamara p;
  p.encendida(1000);
  VERIFICAR(p.encendidoEnVentanaMs(1000) == 0);
  VERIFICAR(p.encendidoEnVentanaMs(11000) == 10000);
  VERIFICAR(!p.excedido(300999));
  VERIFICAR(p.excedido(301000));  // 300 s exactos: agotado
}

static void el_presupuesto_acumula_entre_ciclos() {
  PresupuestoCamara p;
  uint32_t t = 0;
  // 10 ciclos de 20 s encendida y 10 s apagada: 200 s en 300 s.
  for (int i = 0; i < 10; ++i) {
    p.encendida(t);
    t += 20000;
    p.apagada(t);
    t += 10000;
  }
  VERIFICAR(p.encendidoEnVentanaMs(t) == 200000);
  VERIFICAR(!p.excedido(t));
  // Otros 100 s encendida completan los 300 s.
  p.encendida(t);
  VERIFICAR(!p.excedido(t + 99999));
  VERIFICAR(p.excedido(t + 100000));
}

static void el_presupuesto_olvida_lo_que_sale_de_la_ventana() {
  PresupuestoCamara p;
  p.encendida(0);
  p.apagada(300000);  // 300 s seguidos
  VERIFICAR(p.excedido(300000));
  VERIFICAR(p.excedido(599999));
  // A los 600 s del fin del tramo sale entero; antes sale poco a poco.
  VERIFICAR(p.encendidoEnVentanaMs(400000) == 300000);  // ventana [-200 s, 400 s]: todo
  VERIFICAR(p.encendidoEnVentanaMs(700000) == 200000);  // ventana [100 s, 700 s]: 200 s
  VERIFICAR(!p.excedido(700000));
  VERIFICAR(p.encendidoEnVentanaMs(900000) == 0);       // ventana [300 s, 900 s]: nada
}

static void el_presupuesto_recorta_un_tramo_que_cruza_el_borde_de_la_ventana() {
  PresupuestoCamara p;
  p.encendida(0);
  VERIFICAR(p.encendidoEnVentanaMs(1000000) == 600000);  // abierto: solo cuenta la ventana
  p.apagada(1000000);
  VERIFICAR(p.encendidoEnVentanaMs(1000000) == 600000);
  VERIFICAR(p.encendidoEnVentanaMs(1100000) == 500000);
}

static void el_presupuesto_corre_con_el_desbordamiento_de_millis() {
  PresupuestoCamara p;
  const uint32_t t0 = 0xFFFFFFFFu - 100000;  // el tramo cruza el desbordamiento
  p.encendida(t0);
  p.apagada(t0 + 150000);                   // 150 s encendida
  VERIFICAR(p.encendidoEnVentanaMs(t0 + 150000) == 150000);
  p.encendida(t0 + 160000);                 // ya desbordó
  VERIFICAR(p.encendidoEnVentanaMs(t0 + 310000) == 300000);
  VERIFICAR(p.excedido(t0 + 310000));
  VERIFICAR(!p.excedido(t0 + 309999));
  p.apagada(t0 + 310000);
  VERIFICAR(p.encendidoEnVentanaMs(t0 + 760000) == 150000);  // salió el primer tramo
}

static void el_enfriamiento_dura_2_min_fijos() {
  PresupuestoCamara p;
  VERIFICAR(!p.enEnfriamiento(0));
  VERIFICAR(p.esperaEnfriamientoMs(0) == 0);
  p.iniciarEnfriamiento(5000);
  VERIFICAR(p.enEnfriamiento(5000));
  VERIFICAR(p.enEnfriamiento(124999));
  VERIFICAR(p.esperaEnfriamientoMs(65000) == 60000);
  VERIFICAR(!p.enEnfriamiento(125000));
  VERIFICAR(p.esperaEnfriamientoMs(125000) == 0);
  // Con el desbordamiento de millis().
  const uint32_t t = 0xFFFFFFFFu - 1000;
  p.iniciarEnfriamiento(t);
  VERIFICAR(p.enEnfriamiento(t + 119999));
  VERIFICAR(!p.enEnfriamiento(t + 120000));
}

static void el_presupuesto_se_puede_configurar() {
  PresupuestoCamara p(10000, 60000, 5000);
  p.encendida(0);
  VERIFICAR(!p.excedido(9999));
  VERIFICAR(p.excedido(10000));
  p.iniciarEnfriamiento(10000);
  VERIFICAR(!p.enEnfriamiento(15000));
}

static void el_presupuesto_ignora_llamadas_repetidas() {
  PresupuestoCamara p;
  p.apagada(100);  // apagar lo apagado: nada
  VERIFICAR(p.encendidoEnVentanaMs(200) == 0);
  p.encendida(1000);
  p.encendida(5000);  // ya encendida: no reinicia el tramo
  VERIFICAR(p.encendidoEnVentanaMs(11000) == 10000);
  p.apagada(11000);
  p.apagada(12000);
  VERIFICAR(p.encendidoEnVentanaMs(20000) == 10000);
  p.reiniciar();
  VERIFICAR(p.encendidoEnVentanaMs(20000) == 0);
}

static void el_presupuesto_aguanta_mas_ciclos_que_su_memoria() {
  PresupuestoCamara p;
  uint32_t t = 0;
  for (int i = 0; i < 200; ++i) {  // ciclos de 1 s cada 2 s: pasan de los 64 recordados
    p.encendida(t);
    t += 1000;
    p.apagada(t);
    t += 1000;
  }
  // En la ventana de 600 s caben 300 ciclos, solo se recuerdan 64: lo que se ve
  // es un mínimo (64 s), nunca un exceso falso.
  VERIFICAR(p.encendidoEnVentanaMs(t) <= 200000);
  VERIFICAR(p.encendidoEnVentanaMs(t) >= 64000);
}


// --- Primera lectura tras un arranque -------------------------------------------------------------

static void la_primera_lectura_caliente_entra_en_corte() {
  GuardaTermica g;
  VERIFICAR(g.primeraLectura(65.0f, false) == Transicion::EntraCorte);  // desde 65 °C, no 75
  VERIFICAR(g.estado() == EstadoTermico::Corte);
  VERIFICAR(!g.capturaPermitida());
  VERIFICAR(g.actualizar(66.0f) == Transicion::Ninguna);
  VERIFICAR(g.actualizar(64.9f) == Transicion::SaleCorte);  // se sale por debajo de 65
  VERIFICAR(g.capturaPermitida());
}

static void la_primera_lectura_fria_es_normal() {
  GuardaTermica g;
  VERIFICAR(g.primeraLectura(40.0f, false) == Transicion::Ninguna);
  VERIFICAR(g.estado() == EstadoTermico::Normal);
  VERIFICAR(g.capturaPermitida());
}

static void el_corte_anotado_antes_del_reinicio_se_mantiene() {
  GuardaTermica g;
  // Corte previo y 70 °C: sigue cortado, sin transición nueva.
  VERIFICAR(g.primeraLectura(70.0f, true) == Transicion::Ninguna);
  VERIFICAR(g.estado() == EstadoTermico::Corte);
  VERIFICAR(!g.capturaPermitida());
  // Corte previo y 50 °C: se levanta (por debajo de 65).
  GuardaTermica h;
  VERIFICAR(h.primeraLectura(50.0f, true) == Transicion::SaleCorte);
  VERIFICAR(h.capturaPermitida());
  // Corte previo y lectura no válida: sigue cortado y sin permiso.
  GuardaTermica i;
  VERIFICAR(i.primeraLectura(NAN, true) == Transicion::Ninguna);
  VERIFICAR(i.estado() == EstadoTermico::Corte);
  VERIFICAR(!i.capturaPermitida());
}

// --- Enfriamiento restaurado y estado del RTC ------------------------------------------------------

static void el_enfriamiento_se_restaura_con_lo_que_faltaba() {
  PresupuestoCamara p;
  p.restaurarEnfriamiento(1000, 45000);
  VERIFICAR(p.enEnfriamiento(1000));
  VERIFICAR(p.esperaEnfriamientoMs(11000) == 35000);
  VERIFICAR(p.enEnfriamiento(45999));
  VERIFICAR(!p.enEnfriamiento(46000));
  // 0 no hace nada; más del máximo se recorta al máximo.
  PresupuestoCamara q;
  q.restaurarEnfriamiento(0, 0);
  VERIFICAR(!q.enEnfriamiento(0));
  q.restaurarEnfriamiento(0, 999999999);
  VERIFICAR(q.esperaEnfriamientoMs(0) == kEnfriamientoMs);
  // Uno nuevo vuelve a la duración completa.
  p.iniciarEnfriamiento(50000);
  VERIFICAR(p.esperaEnfriamientoMs(50000) == kEnfriamientoMs);
}

static void el_encendido_usado_se_restaura_como_tramo_cerrado() {
  PresupuestoCamara p;
  p.restaurarEncendido(1000000, 200000);  // 200 s gastados antes del reinicio
  VERIFICAR(p.encendidoEnVentanaMs(1000000) == 200000);
  VERIFICAR(!p.excedido(1000000));
  // 100 s más encendida completan los 300 s.
  p.encendida(1000000);
  VERIFICAR(!p.excedido(1099999));
  VERIFICAR(p.excedido(1100000));
  // Sale de la ventana como cualquier tramo: 600 s después del arranque ya no cuenta.
  PresupuestoCamara q;
  q.restaurarEncendido(1000000, 200000);
  VERIFICAR(q.encendidoEnVentanaMs(1000000 + 599999) > 0);
  VERIFICAR(q.encendidoEnVentanaMs(1000000 + 600000) == 0);
  // 0 no hace nada; más que la ventana se recorta a la ventana.
  PresupuestoCamara r;
  r.restaurarEncendido(5000, 0);
  VERIFICAR(r.encendidoEnVentanaMs(5000) == 0);
  r.restaurarEncendido(5000000, 999999999);
  VERIFICAR(r.encendidoEnVentanaMs(5000000) == kVentanaPresupuestoMs);
  VERIFICAR(r.excedido(5000000));
  // Con el desbordamiento de millis().
  PresupuestoCamara s;
  s.restaurarEncendido(100000, 250000);  // el inicio cae antes del cero
  VERIFICAR(s.encendidoEnVentanaMs(100000) == 250000);
}

static void el_estado_rtc_se_valida() {
  EstadoSeguridadRtc e = {1, 2, 3, 4, 5};  // basura
  VERIFICAR(!estadoRtcValido(e));
  e.enfriamiento_restante_ms = 45000;
  e.corte_termico = 1;
  e.encendido_usado_ms = 123000;
  sellarEstadoRtc(e);
  VERIFICAR(e.magia == kMagiaEstadoRtc);
  VERIFICAR(estadoRtcValido(e));
  // Cualquier campo alterado lo invalida.
  EstadoSeguridadRtc a = e;
  a.enfriamiento_restante_ms ^= 1;
  VERIFICAR(!estadoRtcValido(a));
  a = e;
  a.corte_termico = 0;
  VERIFICAR(!estadoRtcValido(a));
  a = e;
  a.verificacion ^= 0x100;
  VERIFICAR(!estadoRtcValido(a));
  a = e;
  a.magia = 0;
  VERIFICAR(!estadoRtcValido(a));
  a = e;
  a.encendido_usado_ms ^= 1;
  VERIFICAR(!estadoRtcValido(a));
  // Valores fuera de lo posible no valen aunque estén sellados.
  a = e;
  a.encendido_usado_ms = kVentanaPresupuestoMs + 1;
  sellarEstadoRtc(a);
  VERIFICAR(!estadoRtcValido(a));
  a = e;
  a.enfriamiento_restante_ms = kEnfriamientoMs + 1;
  sellarEstadoRtc(a);
  VERIFICAR(!estadoRtcValido(a));
  // Un 2 en corte_termico no es un booleano.
  a = e;
  a.corte_termico = 2;
  sellarEstadoRtc(a);
  VERIFICAR(!estadoRtcValido(a));
  // Todo ceros (la RAM del RTC recién borrada) no vale.
  EstadoSeguridadRtc ceros = {0, 0, 0, 0, 0};
  VERIFICAR(!estadoRtcValido(ceros));
  sellarEstadoRtc(ceros);
  VERIFICAR(estadoRtcValido(ceros));
}

// --- Reposo tras un fallo ---------------------------------------------------------------------

static void el_reposo_tras_fallo_dura_10_s() {
  VERIFICAR(kReposoTrasFalloMs == 10000);
  ReposoTrasFallo r;
  VERIFICAR(!r.enReposo(0));
  r.fallo(1000);
  VERIFICAR(r.enReposo(1000));
  VERIFICAR(r.enReposo(10999));
  VERIFICAR(!r.enReposo(11000));
  const uint32_t t = 0xFFFFFFFFu - 500;  // desbordamiento
  r.fallo(t);
  VERIFICAR(r.enReposo(t + 9999));
  VERIFICAR(!r.enReposo(t + 10000));
  r.fallo(0);
  r.reiniciar();
  VERIFICAR(!r.enReposo(0));
}

// --- Límite de mensajes del registro ------------------------------------------------------------

static void el_registro_se_limita_a_uno_por_segundo_y_cuenta_los_callados() {
  LimitadorMensajes l;
  uint32_t suprimidos = 99;
  VERIFICAR(l.intentar(5000, &suprimidos));
  VERIFICAR(suprimidos == 0);
  VERIFICAR(!l.intentar(5100, &suprimidos));
  VERIFICAR(!l.intentar(5500, nullptr));
  VERIFICAR(!l.intentar(5999, &suprimidos));
  VERIFICAR(l.intentar(6000, &suprimidos));
  VERIFICAR(suprimidos == 3);
  VERIFICAR(l.intentar(7000, &suprimidos));
  VERIFICAR(suprimidos == 0);
  // Desbordamiento de millis().
  const uint32_t t = 0xFFFFFFFFu - 200;
  VERIFICAR(l.intentar(t, &suprimidos));
  VERIFICAR(!l.intentar(t + 999, &suprimidos));
  VERIFICAR(l.intentar(t + 1000, &suprimidos));
}

// --- Secreto ------------------------------------------------------------------------------------

static void el_secreto_tiene_que_ser_fuerte() {
  VERIFICAR(evaluarSecreto(nullptr) == MotivoSecreto::Vacio);
  VERIFICAR(evaluarSecreto("") == MotivoSecreto::Vacio);
  VERIFICAR(evaluarSecreto("abcdefgh12345678") == MotivoSecreto::Corto);  // 16
  VERIFICAR(evaluarSecreto("abcdefghijklmnopqrstuvwxyz01234") == MotivoSecreto::Corto);  // 31
  VERIFICAR(evaluarSecreto("abcdefghijklmnopqrstuvwxyz012345") == MotivoSecreto::Valido);  // 32
  VERIFICAR(evaluarSecreto("cambiar_esta_clave_por_otra_larga_y_distinta") == MotivoSecreto::Plantilla);
  VERIFICAR(evaluarSecreto("cambiar_") == MotivoSecreto::Corto);  // el largo se mira antes
  // 32 caracteres pero solo 7 distintos.
  VERIFICAR(evaluarSecreto("abcdefgabcdefgabcdefgabcdefgabcd") == MotivoSecreto::PocaVariedad);
  VERIFICAR(evaluarSecreto("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") == MotivoSecreto::PocaVariedad);
  // Con 8 distintos pasa.
  VERIFICAR(evaluarSecreto("abcdefghabcdefghabcdefghabcdefgh") == MotivoSecreto::Valido);
  // El texto del motivo no incluye el secreto (no lo recibe).
  VERIFICAR(strlen(aTexto(MotivoSecreto::Plantilla)) > 0);
  VERIFICAR(strcmp(aTexto(MotivoSecreto::Valido), "valido") == 0);
}

// --- Origen permitido ---------------------------------------------------------------------------

static void parsea_ipv4() {
  uint8_t ip[4] = {9, 9, 9, 9};
  VERIFICAR(parsearIpv4("192.168.1.7", ip));
  VERIFICAR(ip[0] == 192 && ip[1] == 168 && ip[2] == 1 && ip[3] == 7);
  VERIFICAR(parsearIpv4("0.0.0.0", ip));
  VERIFICAR(parsearIpv4("255.255.255.255", ip) && ip[0] == 255 && ip[3] == 255);
  const char* const malas[] = {"",         "1.2.3",     "1.2.3.4.5",  "1.2.3.256", "256.1.1.1", "01.2.3.4",
                               "1.2.3.04", "a.b.c.d",   "1.2.3.4 ",   " 1.2.3.4",  "1..3.4",    "1.2.3.",
                               ".1.2.3",   "1.2.3.4a",  "1.2.3.1000", "-1.2.3.4",  "0x7f.0.0.1", "::1"};
  for (const char* m : malas) {
    uint8_t salida[4] = {1, 2, 3, 4};
    VERIFICAR(!parsearIpv4(m, salida));
    VERIFICAR(salida[0] == 1 && salida[3] == 4);  // no se toca si falla
  }
  VERIFICAR(!parsearIpv4(nullptr, ip));
}

static void extrae_ipv4_de_un_v6_mapeado() {
  uint8_t v6[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 192, 168, 1, 7};
  uint8_t v4[4] = {0};
  VERIFICAR(ipv4DeMapeada(v6, v4));
  VERIFICAR(v4[0] == 192 && v4[1] == 168 && v4[2] == 1 && v4[3] == 7);
  uint8_t nativa[16] = {0x20, 0x01, 0xd, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
  VERIFICAR(!ipv4DeMapeada(nativa, v4));
  uint8_t compatible[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 168, 1, 7};  // ::a.b.c.d, no mapeada
  VERIFICAR(!ipv4DeMapeada(compatible, v4));
  uint8_t casi[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0xff, 0xff, 192, 168, 1, 7};
  VERIFICAR(!ipv4DeMapeada(casi, v4));
  VERIFICAR(!ipv4DeMapeada(nullptr, v4));
}

// --- Búfer de la foto y resolución máxima ---------------------------------------------------------

static void el_bufer_cubre_la_resolucion_maxima() {
  const Perfil& max = perfil(kResolucionMaxima);
  VERIFICAR(kResolucionMaxima == Resolucion::Xga);
  VERIFICAR(max.ancho == 1024 && max.alto == 768);
  const size_t pixeles = static_cast<size_t>(max.ancho) * max.alto;
  VERIFICAR(kBufferJpegBytes >= pixeles / 5);   // el tamaño por defecto del driver
  VERIFICAR(kBufferJpegBytes >= pixeles / 2);   // el margen que se quiere para una escena ruidosa
  VERIFICAR(kBufferJpegBytes < pixeles * 2);    // y no el cuadro en bruto: sigue siendo un JPEG
  // Ningún perfil necesita más búfer que el máximo.
  const Resolucion todas[] = {Resolucion::Hvga, Resolucion::Vga, Resolucion::Svga, Resolucion::Xga};
  for (Resolucion r : todas) {
    VERIFICAR(static_cast<size_t>(perfil(r).ancho) * perfil(r).alto <= pixeles);
  }
}

int main() {
  la_lista_blanca_son_cuatro_nombres_exactos();
  lo_desconocido_cae_en_svga();
  los_perfiles_son_los_acordados();
  ninguna_resolucion_pasa_de_xga();
  la_query_se_lee_por_parametro();
  la_query_mala_cae_en_svga();
  cuatro_en_la_ventana_y_la_quinta_no();
  dos_capturas_separadas_no_llenan_nada();
  la_pausa_tras_la_rafaga_cierra_hasta_la_ventana_mas_1_s();
  el_429_no_alarga_la_espera();
  la_ventana_es_deslizante();
  con_el_desbordamiento_de_millis();
  la_espera_informada_es_la_real();
  sin_pausa_manda_solo_la_ventana();
  reiniciar_olvida_todo();
  antes_de_leer_no_se_permite_capturar();
  normal_aviso_y_corte();
  el_corte_no_se_levanta_hasta_bajar_de_65();
  tras_salir_del_corte_vuelve_a_cortar_y_a_avisar();
  salto_directo_de_normal_a_corte();
  el_aviso_se_apaga_al_bajar_de_65();
  una_lectura_no_valida_falla_cerrado();
  una_lectura_no_valida_no_levanta_el_corte();
  el_intervalo_cumplido_corre_con_el_desbordamiento();
  la_inactividad_apaga_a_los_15_s();
  la_camara_apagada_no_se_vuelve_a_apagar();
  la_inactividad_corre_con_el_desbordamiento();
  la_inactividad_se_puede_cambiar();
  cuadros_a_descartar();
  el_latido_tiene_los_campos_del_contrato();
  el_latido_omite_temp_si_no_hay_lectura();
  el_peor_caso_del_latido_cabe_en_el_bufer_de_pubsubclient();
  el_latido_que_no_cabe_devuelve_cero();
  el_presupuesto_por_defecto_es_300_s_en_10_min();
  el_presupuesto_cuenta_el_tramo_abierto();
  el_presupuesto_acumula_entre_ciclos();
  el_presupuesto_olvida_lo_que_sale_de_la_ventana();
  el_presupuesto_recorta_un_tramo_que_cruza_el_borde_de_la_ventana();
  el_presupuesto_corre_con_el_desbordamiento_de_millis();
  el_enfriamiento_dura_2_min_fijos();
  el_presupuesto_se_puede_configurar();
  el_presupuesto_ignora_llamadas_repetidas();
  el_presupuesto_aguanta_mas_ciclos_que_su_memoria();
  la_primera_lectura_caliente_entra_en_corte();
  la_primera_lectura_fria_es_normal();
  el_corte_anotado_antes_del_reinicio_se_mantiene();
  el_enfriamiento_se_restaura_con_lo_que_faltaba();
  el_encendido_usado_se_restaura_como_tramo_cerrado();
  el_estado_rtc_se_valida();
  el_reposo_tras_fallo_dura_10_s();
  el_registro_se_limita_a_uno_por_segundo_y_cuenta_los_callados();
  el_secreto_tiene_que_ser_fuerte();
  parsea_ipv4();
  extrae_ipv4_de_un_v6_mapeado();
  el_bufer_cubre_la_resolucion_maxima();
  printf("%d verificaciones, %d fallos\n", total, fallos);
  return fallos == 0 ? 0 : 1;
}
