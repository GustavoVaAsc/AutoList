// Pruebas de host de logica_energia (sin dependencias). Compilar y correr desde
// firmware/xiao/ con los comandos del README, sección "Pruebas de host".
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../logica_energia.h"

using namespace logica_energia;

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

static bool cerca(float a, float b, float tolerancia = 1e-4f) {
  return fabsf(a - b) <= tolerancia;
}

// Registro del bus con el dato en los bits 15 a 3 (LSB 4 mV): 12 V son 3000.
static uint16_t busRaw(int cuentas, bool cnvr = false, bool ovf = false) {
  return static_cast<uint16_t>((cuentas << 3) | (cnvr ? 2 : 0) | (ovf ? 1 : 0));
}

// --- Constantes --------------------------------------------------------------

static void las_constantes_son_las_del_modulo_tipico() {
  VERIFICAR(kDireccionI2c == 0x40);
  VERIFICAR(cerca(kShuntOhm, 0.1f));
  VERIFICAR(cerca(kCorrienteMaxA, 3.2f));
  VERIFICAR(kShuntRawMax == 32000);
  // 0x3E67: BRNG 32 V, PGA /8, 16 muestras en bus y shunt, modo continuo.
  VERIFICAR(kConfiguracion == 0x3E67);
  VERIFICAR(kPeriodoMs == 1000);
  VERIFICAR(strcmp(kTopicoEnergia, "aula/camara/energia") == 0);
}

// --- interpretar -------------------------------------------------------------

static void convierte_los_registros_a_unidades() {
  Lectura l = {};
  // 12.000 V y 85 mV en el shunt (8500 cuentas de 10 uV): 0.85 A, 10.2 W.
  VERIFICAR(interpretar(8500, busRaw(3000), &l) == Resultado::Ok);
  VERIFICAR(cerca(l.v, 12.0f));
  VERIFICAR(cerca(l.i, 0.85f));
  VERIFICAR(cerca(l.p, 10.2f, 1e-3f));
}

static void ignora_los_bits_de_estado_del_bus() {
  Lectura l = {};
  // CNVR y OVF no son parte de la tensión: no la mueven.
  VERIFICAR(interpretar(1000, busRaw(3025, true, true), &l) == Resultado::Ok);
  VERIFICAR(cerca(l.v, 12.1f));
  VERIFICAR(cerca(l.i, 0.1f));
}

static void cero_amperes_es_valido() {
  Lectura l = {};
  VERIFICAR(interpretar(0, busRaw(3000), &l) == Resultado::Ok);
  VERIFICAR(l.i == 0.0f);
  VERIFICAR(l.p == 0.0f);
  // Sin 12 V pero con el chip vivo por sus 3.3 V: 0 V, 0 A, 0 W.
  VERIFICAR(interpretar(0, busRaw(0), &l) == Resultado::Ok);
  VERIFICAR(l.v == 0.0f);
}

static void la_saturacion_no_es_una_lectura() {
  Lectura l = {-1, -1, -1};
  VERIFICAR(interpretar(31999, busRaw(3000), &l) == Resultado::Ok);  // 3.1999 A
  VERIFICAR(cerca(l.i, 3.1999f, 1e-3f));
  l = {-1, -1, -1};
  VERIFICAR(interpretar(32000, busRaw(3000), &l) == Resultado::Saturada);
  VERIFICAR(interpretar(32767, busRaw(3000), &l) == Resultado::Saturada);
  VERIFICAR(interpretar(-32000, busRaw(3000), &l) == Resultado::Saturada);
  VERIFICAR(interpretar(INT16_MIN, busRaw(3000), &l) == Resultado::Saturada);
  VERIFICAR(l.v == -1.0f && l.i == -1.0f && l.p == -1.0f);  // no la toca
}

static void la_corriente_negativa_pequena_es_cero() {
  Lectura l = {};
  VERIFICAR(interpretar(-5, busRaw(3000), &l) == Resultado::Ok);  // -0.5 mA: desvío del shunt
  VERIFICAR(l.i == 0.0f);
  VERIFICAR(!signbit(l.i));  // que no salga "-0.000" en el payload
  VERIFICAR(l.p == 0.0f);
  VERIFICAR(interpretar(-100, busRaw(3000), &l) == Resultado::Ok);  // -0.01 A: el límite
  VERIFICAR(l.i == 0.0f);
}

static void la_corriente_negativa_grande_no_se_publica() {
  Lectura l = {-1, -1, -1};
  VERIFICAR(interpretar(-101, busRaw(3000), &l) == Resultado::Negativa);  // -0.0101 A
  VERIFICAR(interpretar(-20000, busRaw(3000), &l) == Resultado::Negativa);
  VERIFICAR(l.v == -1.0f);
}

static void lo_que_node_red_descartaria_no_se_publica() {
  Lectura l = {};
  // 0xFFFF en el bus (bus flotante): 8191 * 4 mV = 32.76 V > 30 V.
  VERIFICAR(interpretar(8500, 0xFFFF, &l) == Resultado::FueraDeRango);
  VERIFICAR(interpretar(8500, busRaw(7500), &l) == Resultado::Ok);  // 30.000 V: el tope
  VERIFICAR(cerca(l.v, 30.0f));
  VERIFICAR(interpretar(8500, busRaw(7501), &l) == Resultado::FueraDeRango);  // 30.004 V
  // 30 V a 3.1999 A serían 96 W: dentro de los 300 W. La potencia no limita
  // con este shunt, pero el chequeo existe por si se cambia el shunt.
  VERIFICAR(interpretar(31999, busRaw(7500), &l) == Resultado::Ok);
  VERIFICAR(l.p < kPMaxW);
}

static void describe_cada_resultado() {
  VERIFICAR(strlen(describir(Resultado::Ok)) > 0);
  VERIFICAR(strlen(describir(Resultado::Saturada)) > 0);
  VERIFICAR(strlen(describir(Resultado::Negativa)) > 0);
  VERIFICAR(strlen(describir(Resultado::FueraDeRango)) > 0);
  VERIFICAR(strcmp(describir(Resultado::Saturada), describir(Resultado::Negativa)) != 0);
}

// --- construirEnergia --------------------------------------------------------

static void construye_el_payload_del_contrato() {
  char buf[64];
  const Lectura l = {12.1f, 0.85f, 10.29f};
  VERIFICAR(construirEnergia(buf, sizeof buf, &l) == strlen(buf));
  VERIFICAR(strcmp(buf, "{\"v\":12.10,\"i\":0.850,\"p\":10.29}") == 0);
  const Lectura cero = {12.0f, 0.0f, 0.0f};
  VERIFICAR(construirEnergia(buf, sizeof buf, &cero) > 0);
  VERIFICAR(strcmp(buf, "{\"v\":12.00,\"i\":0.000,\"p\":0.00}") == 0);
}

static void el_payload_mas_largo_cabe_en_kEnergiaBytesMax() {
  char buf[64];
  const Lectura maxima = {kVMaxV, kIMaxA, kPMaxW};
  VERIFICAR(construirEnergia(buf, sizeof buf, &maxima) == kEnergiaBytesMax);
  VERIFICAR(strcmp(buf, "{\"v\":30.00,\"i\":10.000,\"p\":300.00}") == 0);
}

static void el_payload_no_cabe() {
  const Lectura l = {12.1f, 0.85f, 10.29f};
  char buf[kEnergiaBytesMax];  // sin sitio para el terminador
  memset(buf, 'x', sizeof buf);
  VERIFICAR(construirEnergia(buf, 20, &l) == 0);
  VERIFICAR(buf[0] == '\0');  // no deja basura
  char justo[32];  // el payload mide 31 bytes
  VERIFICAR(construirEnergia(justo, sizeof justo, &l) == 31);
  char corto[31];
  VERIFICAR(construirEnergia(corto, sizeof corto, &l) == 0);
  char uno[1] = {'x'};
  VERIFICAR(construirEnergia(uno, 1, &l) == 0);
  VERIFICAR(uno[0] == '\0');
  VERIFICAR(construirEnergia(nullptr, 0, &l) == 0);
  char otro[64];
  VERIFICAR(construirEnergia(otro, sizeof otro, nullptr) == 0);
}

static void no_arma_un_payload_con_valores_invalidos() {
  char buf[64];
  const Lectura mala_v = {30.01f, 1.0f, 1.0f};
  const Lectura mala_i = {12.0f, 10.01f, 1.0f};
  const Lectura mala_p = {12.0f, 1.0f, 300.01f};
  const Lectura negativa = {12.0f, -0.5f, 1.0f};
  const Lectura no_finita = {NAN, 1.0f, 1.0f};
  const Lectura infinita = {12.0f, INFINITY, 1.0f};
  VERIFICAR(construirEnergia(buf, sizeof buf, &mala_v) == 0 && buf[0] == '\0');
  VERIFICAR(construirEnergia(buf, sizeof buf, &mala_i) == 0 && buf[0] == '\0');
  VERIFICAR(construirEnergia(buf, sizeof buf, &mala_p) == 0 && buf[0] == '\0');
  VERIFICAR(construirEnergia(buf, sizeof buf, &negativa) == 0 && buf[0] == '\0');
  VERIFICAR(construirEnergia(buf, sizeof buf, &no_finita) == 0 && buf[0] == '\0');
  VERIFICAR(construirEnergia(buf, sizeof buf, &infinita) == 0 && buf[0] == '\0');
}

static void de_los_registros_al_payload() {
  char buf[64];
  Lectura l = {};
  VERIFICAR(interpretar(8500, busRaw(3025), &l) == Resultado::Ok);  // 12.1 V, 0.85 A
  VERIFICAR(construirEnergia(buf, sizeof buf, &l) > 0);
  VERIFICAR(strcmp(buf, "{\"v\":12.10,\"i\":0.850,\"p\":10.29}") == 0);
}

// --- ControlEnergia ----------------------------------------------------------

static void el_primer_intento_de_deteccion_es_inmediato() {
  ControlEnergia c;
  VERIFICAR(!c.activo());
  VERIFICAR(c.siguiente(0) == Tarea::Iniciar);
  VERIFICAR(c.siguiente(0) == Tarea::Nada);  // ya se hizo
}

static void sin_modulo_reintenta_cada_30_s() {
  ControlEnergia c;
  VERIFICAR(c.siguiente(100) == Tarea::Iniciar);
  VERIFICAR(!c.registrarInicio(false));
  VERIFICAR(!c.activo());
  VERIFICAR(c.siguiente(100 + 1000) == Tarea::Nada);  // no lee: no hay módulo
  VERIFICAR(c.siguiente(100 + kReintentoInicioMs - 1) == Tarea::Nada);
  VERIFICAR(c.siguiente(100 + kReintentoInicioMs) == Tarea::Iniciar);
  VERIFICAR(!c.registrarInicio(false));
  VERIFICAR(c.siguiente(100 + kReintentoInicioMs + 1) == Tarea::Nada);  // el plazo corre desde el intento
  VERIFICAR(c.siguiente(100 + 2 * kReintentoInicioMs) == Tarea::Iniciar);
}

static void detectado_lee_cada_segundo() {
  ControlEnergia c;
  VERIFICAR(c.siguiente(0) == Tarea::Iniciar);
  VERIFICAR(c.registrarInicio(true));  // pasó de ausente a activo
  VERIFICAR(c.activo());
  VERIFICAR(c.siguiente(1) == Tarea::Nada);
  VERIFICAR(c.siguiente(kPeriodoMs - 1) == Tarea::Nada);
  VERIFICAR(c.siguiente(kPeriodoMs) == Tarea::Leer);
  VERIFICAR(!c.registrarLectura(true));
  VERIFICAR(c.siguiente(kPeriodoMs + 500) == Tarea::Nada);
  VERIFICAR(c.siguiente(2 * kPeriodoMs - 1) == Tarea::Nada);
  VERIFICAR(c.siguiente(2 * kPeriodoMs) == Tarea::Leer);
}

static void repetir_el_inicio_no_anuncia_dos_veces() {
  ControlEnergia c;
  c.siguiente(0);
  VERIFICAR(c.registrarInicio(true));
  VERIFICAR(!c.registrarInicio(true));
}

static void con_retraso_en_loop_no_acumula_lecturas() {
  // loop() bloqueado 10 s por un intento de MQTT: una sola lectura al volver,
  // no diez de golpe.
  ControlEnergia c;
  c.siguiente(0);
  c.registrarInicio(true);
  VERIFICAR(c.siguiente(1000) == Tarea::Leer);
  VERIFICAR(c.siguiente(11000) == Tarea::Leer);
  VERIFICAR(c.siguiente(11001) == Tarea::Nada);
  VERIFICAR(c.siguiente(12000) == Tarea::Leer);
}

static void un_fallo_aislado_no_pierde_el_modulo() {
  ControlEnergia c;
  c.siguiente(0);
  c.registrarInicio(true);
  VERIFICAR(c.siguiente(1000) == Tarea::Leer);
  VERIFICAR(!c.registrarLectura(false));  // 1 fallo
  VERIFICAR(c.activo());
  VERIFICAR(c.siguiente(2000) == Tarea::Leer);
  VERIFICAR(!c.registrarLectura(false));  // 2 fallos
  VERIFICAR(c.siguiente(3000) == Tarea::Leer);
  VERIFICAR(!c.registrarLectura(true));   // una buena reinicia la cuenta
  VERIFICAR(c.siguiente(4000) == Tarea::Leer);
  VERIFICAR(!c.registrarLectura(false));
  VERIFICAR(c.siguiente(5000) == Tarea::Leer);
  VERIFICAR(!c.registrarLectura(false));
  VERIFICAR(c.activo());
}

static void tres_fallos_seguidos_pierden_el_modulo_y_se_reintenta() {
  VERIFICAR(kFallosSeguidosMax == 3);
  ControlEnergia c;
  c.siguiente(0);
  c.registrarInicio(true);
  VERIFICAR(c.siguiente(1000) == Tarea::Leer);
  VERIFICAR(!c.registrarLectura(false));
  VERIFICAR(c.siguiente(2000) == Tarea::Leer);
  VERIFICAR(!c.registrarLectura(false));
  VERIFICAR(c.siguiente(3000) == Tarea::Leer);
  VERIFICAR(c.registrarLectura(false));  // el tercero: perdido
  VERIFICAR(!c.activo());
  VERIFICAR(c.siguiente(4000) == Tarea::Nada);  // ya no lee
  VERIFICAR(c.siguiente(3000 + kReintentoInicioMs - 1) == Tarea::Nada);
  VERIFICAR(c.siguiente(3000 + kReintentoInicioMs) == Tarea::Iniciar);
  VERIFICAR(c.registrarInicio(true));  // volvió: se anuncia
  VERIFICAR(c.activo());
  // La cuenta de fallos empieza de cero tras recuperarse.
  VERIFICAR(c.siguiente(3000 + kReintentoInicioMs + kPeriodoMs) == Tarea::Leer);
  VERIFICAR(!c.registrarLectura(false));
  VERIFICAR(c.activo());
}

static void el_bus_se_reinicia_solo_tras_un_fallo() {
  ControlEnergia c;
  VERIFICAR(!c.reiniciarBusAntes());  // el arranque: el bus acaba de iniciarse
  VERIFICAR(c.siguiente(0) == Tarea::Iniciar);
  VERIFICAR(!c.registrarInicio(false));
  VERIFICAR(c.reiniciarBusAntes());  // no respondió: el siguiente intento reinicia el bus
  VERIFICAR(c.siguiente(kReintentoInicioMs) == Tarea::Iniciar);
  VERIFICAR(c.reiniciarBusAntes());
  VERIFICAR(!c.registrarInicio(false));
  VERIFICAR(c.reiniciarBusAntes());  // sigue sin responder: otra vez, 30 s después
  VERIFICAR(c.siguiente(kReintentoInicioMs + 1) == Tarea::Nada);  // y nunca antes
  VERIFICAR(c.siguiente(2 * kReintentoInicioMs) == Tarea::Iniciar);
  VERIFICAR(c.registrarInicio(true));
  VERIFICAR(!c.reiniciarBusAntes());  // detectado: nada que reiniciar
}

static void un_modulo_perdido_pide_reiniciar_el_bus_antes_de_reintentar() {
  ControlEnergia c;
  c.siguiente(0);
  c.registrarInicio(true);
  VERIFICAR(c.siguiente(1000) == Tarea::Leer);
  c.registrarLectura(false);
  VERIFICAR(!c.reiniciarBusAntes());  // un fallo aislado no reinicia nada
  VERIFICAR(c.siguiente(2000) == Tarea::Leer);
  c.registrarLectura(false);
  VERIFICAR(!c.reiniciarBusAntes());
  VERIFICAR(c.siguiente(3000) == Tarea::Leer);
  VERIFICAR(c.registrarLectura(false));  // perdido
  VERIFICAR(c.reiniciarBusAntes());
  VERIFICAR(c.siguiente(3000 + kReintentoInicioMs) == Tarea::Iniciar);
  VERIFICAR(c.registrarInicio(true));
  VERIFICAR(!c.reiniciarBusAntes());
}

static void una_lectura_buena_no_pide_reiniciar_el_bus() {
  ControlEnergia c;
  c.siguiente(0);
  c.registrarInicio(true);
  c.siguiente(1000);
  c.registrarLectura(true);
  VERIFICAR(!c.reiniciarBusAntes());
}

static void los_tiempos_corren_con_el_desbordamiento_de_millis() {
  ControlEnergia c;
  const uint32_t t0 = 0xFFFFFF00u;  // 256 ms antes de la vuelta
  VERIFICAR(c.siguiente(t0) == Tarea::Iniciar);
  VERIFICAR(c.registrarInicio(true));
  VERIFICAR(c.siguiente(0xFFFFFFFFu) == Tarea::Nada);   // 255 ms
  VERIFICAR(c.siguiente(500) == Tarea::Nada);           // 757 ms
  VERIFICAR(c.siguiente(743) == Tarea::Nada);           // 999 ms
  VERIFICAR(c.siguiente(744) == Tarea::Leer);           // 1000 ms
  // Y el reintento del inicio.
  ControlEnergia d;
  VERIFICAR(d.siguiente(t0) == Tarea::Iniciar);
  VERIFICAR(!d.registrarInicio(false));
  VERIFICAR(d.siguiente(t0 + kReintentoInicioMs - 1) == Tarea::Nada);
  VERIFICAR(d.siguiente(t0 + kReintentoInicioMs) == Tarea::Iniciar);
}

static void el_arranque_cerca_de_la_vuelta_no_se_confunde_con_un_plazo_vencido() {
  // Primer intento con millis() ya cerca del límite: sigue siendo inmediato y
  // el siguiente respeta el plazo.
  ControlEnergia c;
  VERIFICAR(c.siguiente(0xFFFFFFF0u) == Tarea::Iniciar);
  VERIFICAR(!c.registrarInicio(false));
  VERIFICAR(c.siguiente(0xFFFFFFF0u + 5) == Tarea::Nada);
}

static void avisa_de_la_lectura_invalida_con_limite_de_ritmo() {
  ControlEnergia c;
  VERIFICAR(c.avisarLecturaInvalida(5000));  // la primera, siempre
  VERIFICAR(!c.avisarLecturaInvalida(6000));
  VERIFICAR(!c.avisarLecturaInvalida(5000 + kAvisoLecturaInvalidaMs - 1));
  VERIFICAR(c.avisarLecturaInvalida(5000 + kAvisoLecturaInvalidaMs));
  VERIFICAR(!c.avisarLecturaInvalida(5000 + kAvisoLecturaInvalidaMs + 1));
}

static void el_aviso_corre_con_el_desbordamiento_de_millis() {
  ControlEnergia c;
  const uint32_t t0 = 0xFFFFFF00u;
  VERIFICAR(c.avisarLecturaInvalida(t0));
  VERIFICAR(!c.avisarLecturaInvalida(0));  // 256 ms
  VERIFICAR(!c.avisarLecturaInvalida(t0 + kAvisoLecturaInvalidaMs - 1));
  VERIFICAR(c.avisarLecturaInvalida(t0 + kAvisoLecturaInvalidaMs));
}

static void el_aviso_en_cero_no_se_confunde_con_no_haber_avisado() {
  ControlEnergia c;
  VERIFICAR(c.avisarLecturaInvalida(0));
  VERIFICAR(!c.avisarLecturaInvalida(1000));
}

int main() {
  las_constantes_son_las_del_modulo_tipico();
  convierte_los_registros_a_unidades();
  ignora_los_bits_de_estado_del_bus();
  cero_amperes_es_valido();
  la_saturacion_no_es_una_lectura();
  la_corriente_negativa_pequena_es_cero();
  la_corriente_negativa_grande_no_se_publica();
  lo_que_node_red_descartaria_no_se_publica();
  describe_cada_resultado();
  construye_el_payload_del_contrato();
  el_payload_mas_largo_cabe_en_kEnergiaBytesMax();
  el_payload_no_cabe();
  no_arma_un_payload_con_valores_invalidos();
  de_los_registros_al_payload();
  el_primer_intento_de_deteccion_es_inmediato();
  sin_modulo_reintenta_cada_30_s();
  detectado_lee_cada_segundo();
  repetir_el_inicio_no_anuncia_dos_veces();
  con_retraso_en_loop_no_acumula_lecturas();
  un_fallo_aislado_no_pierde_el_modulo();
  tres_fallos_seguidos_pierden_el_modulo_y_se_reintenta();
  el_bus_se_reinicia_solo_tras_un_fallo();
  un_modulo_perdido_pide_reiniciar_el_bus_antes_de_reintentar();
  una_lectura_buena_no_pide_reiniciar_el_bus();
  los_tiempos_corren_con_el_desbordamiento_de_millis();
  el_arranque_cerca_de_la_vuelta_no_se_confunde_con_un_plazo_vencido();
  avisa_de_la_lectura_invalida_con_limite_de_ritmo();
  el_aviso_corre_con_el_desbordamiento_de_millis();
  el_aviso_en_cero_no_se_confunde_con_no_haber_avisado();
  printf("%d verificaciones, %d fallos\n", total, fallos);
  return fallos == 0 ? 0 : 1;
}
