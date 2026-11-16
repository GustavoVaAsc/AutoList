#include "logica_energia.h"

#include <math.h>
#include <stdio.h>

namespace logica_energia {

Resultado interpretar(int16_t shunt_raw, uint16_t bus_raw, Lectura* lectura) {
  // int32_t: el valor absoluto de INT16_MIN no cabe en un int16_t.
  const int32_t shunt = shunt_raw;
  if (shunt >= kShuntRawMax || shunt <= -kShuntRawMax) return Resultado::Saturada;

  // El dato del bus está en los bits 15 a 3; los bits 1 (CNVR) y 0 (OVF) son
  // de estado y no forman parte de la tensión.
  //
  // Los límites se comparan en enteros (mV y uA) y no en float: 7500 cuentas
  // del bus son 30 V exactos, y en float salen 30.000002 y se descartarían.
  const int32_t v_mv = static_cast<int32_t>(bus_raw >> 3) * kLsbBusMv;
  int32_t i_ua = static_cast<int32_t>(lround(shunt * static_cast<double>(kLsbShuntUv) / kShuntOhm));
  if (i_ua < 0) {
    if (i_ua < -lround(kToleranciaNegativaA * 1e6)) return Resultado::Negativa;
    i_ua = 0;  // desvío del shunt sin carga: no es una corriente inversa
  }
  const float v = static_cast<float>(v_mv) / 1000.0f;
  const float i = static_cast<float>(i_ua) / 1e6f;
  const float p = v * i;

  // Con el shunt de 0.1 ohm la saturación (3.2 A) ya deja i por debajo de
  // kIMaxA y p por debajo de kPMaxW (30 V * 3.2 A = 96 W): las guardas de i y
  // de p solo se alcanzan si se cambia el shunt o el rango (logica_energia.h).
  // La de v sí es alcanzable: 0xFFFF en el bus son 32.76 V.
  if (v_mv > lround(kVMaxV * 1000.0) || i_ua > lround(kIMaxA * 1e6) || p > kPMaxW) {
    return Resultado::FueraDeRango;
  }
  if (lectura != nullptr) {
    lectura->v = v;
    lectura->i = i;
    lectura->p = p;
  }
  return Resultado::Ok;
}

const char* describir(Resultado resultado) {
  switch (resultado) {
    case Resultado::Ok:
      return "lectura válida";
    case Resultado::Saturada:
      return "el shunt saturó (corriente fuera del rango del módulo)";
    case Resultado::Negativa:
      return "corriente negativa (¿shunt conectado al revés?)";
    case Resultado::FueraDeRango:
      return "valor fuera del rango que acepta Node-RED";
  }
  return "resultado desconocido";
}

static bool enRango(float x, float maximo) {
  return isfinite(x) && x >= 0.0f && x <= maximo;
}

size_t construirEnergia(char* destino, size_t capacidad, const Lectura* lectura) {
  if (destino == nullptr || capacidad == 0) return 0;
  destino[0] = '\0';
  if (lectura == nullptr || !enRango(lectura->v, kVMaxV) || !enRango(lectura->i, kIMaxA) ||
      !enRango(lectura->p, kPMaxW)) {
    return 0;
  }
  const int n = snprintf(destino, capacidad, "{\"v\":%.2f,\"i\":%.3f,\"p\":%.2f}",
                         static_cast<double>(lectura->v), static_cast<double>(lectura->i),
                         static_cast<double>(lectura->p));
  if (n < 0 || static_cast<size_t>(n) >= capacidad) {
    destino[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n);
}

Tarea ControlEnergia::siguiente(uint32_t ahora_ms) {
  const uint32_t plazo = activo_ ? periodo_ms_ : reintento_ms_;
  // Resta sin signo: correcta aunque millis() haya dado la vuelta.
  if (tiene_ultimo_ && static_cast<uint32_t>(ahora_ms - ultimo_ms_) < plazo) return Tarea::Nada;
  tiene_ultimo_ = true;
  ultimo_ms_ = ahora_ms;
  return activo_ ? Tarea::Leer : Tarea::Iniciar;
}

bool ControlEnergia::registrarInicio(bool detectado) {
  bus_a_reiniciar_ = !detectado;
  if (!detectado) return false;
  const bool nuevo = !activo_;
  activo_ = true;
  fallos_ = 0;
  return nuevo;
}

bool ControlEnergia::registrarLectura(bool respondio) {
  if (respondio) {
    fallos_ = 0;
    return false;
  }
  if (++fallos_ < fallos_max_) return false;
  fallos_ = 0;
  activo_ = false;
  bus_a_reiniciar_ = true;
  return true;
}

bool ControlEnergia::avisarLecturaInvalida(uint32_t ahora_ms) {
  if (tiene_aviso_ && static_cast<uint32_t>(ahora_ms - ultimo_aviso_ms_) < aviso_ms_) {
    return false;
  }
  tiene_aviso_ = true;
  ultimo_aviso_ms_ = ahora_ms;
  return true;
}

}  // namespace logica_energia
