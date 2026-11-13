#include "logica_proteccion.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace logica_proteccion {

namespace {

// Orden del enum: Hvga, Vga, Svga, Xga.
const Perfil kPerfiles[] = {
    {Resolucion::Hvga, "HVGA", 480, 320, 20},
    {Resolucion::Vga, "VGA", 640, 480, 12},
    {Resolucion::Svga, "SVGA", 800, 600, 12},
    {Resolucion::Xga, "XGA", 1024, 768, 12},
};

}  // namespace

const Perfil& perfil(Resolucion resolucion) {
  const size_t i = static_cast<size_t>(resolucion);
  if (i >= sizeof kPerfiles / sizeof kPerfiles[0]) {
    return kPerfiles[static_cast<size_t>(kResolucionPorDefecto)];
  }
  return kPerfiles[i];
}

Resolucion parsearResolucion(const char* valor) {
  if (valor == nullptr) return kResolucionPorDefecto;
  for (const Perfil& p : kPerfiles) {
    if (strcmp(valor, p.nombre) == 0) return p.resolucion;
  }
  return kResolucionPorDefecto;
}

Resolucion resolucionDeQuery(const char* query) {
  if (query == nullptr) return kResolucionPorDefecto;
  const char* p = query;
  while (*p != '\0' && *p != '#') {
    // Un parámetro va de aquí al siguiente "&" (o al final).
    const char* fin = p;
    while (*fin != '\0' && *fin != '&' && *fin != '#') ++fin;
    if (static_cast<size_t>(fin - p) >= 4 && strncmp(p, "res=", 4) == 0) {
      // El valor más largo que importa es "SVGA"; uno mayor no puede ser válido.
      char valor[8];
      const size_t largo = static_cast<size_t>(fin - (p + 4));
      if (largo >= sizeof valor) return kResolucionPorDefecto;
      memcpy(valor, p + 4, largo);
      valor[largo] = '\0';
      return parsearResolucion(valor);
    }
    p = (*fin == '&') ? fin + 1 : fin;
  }
  return kResolucionPorDefecto;
}

DecisionLimite LimitadorCapturas::intentar(uint32_t ahora_ms) {
  // La pausa tras una ráfaga completa (incluye lo que falte de la ventana).
  if (bloqueado_) {
    const uint32_t transcurrido = static_cast<uint32_t>(ahora_ms - bloqueo_inicio_);
    if (transcurrido < bloqueo_duracion_) {
      return {false, bloqueo_duracion_ - transcurrido};
    }
    bloqueado_ = false;
  }

  // Sale de la ventana lo que tenga ventana_ms_ o más (los más viejos van primero).
  size_t vencidas = 0;
  while (vencidas < cuenta_ &&
         static_cast<uint32_t>(ahora_ms - instantes_[vencidas]) >= ventana_ms_) {
    ++vencidas;
  }
  if (vencidas > 0) {
    for (size_t i = vencidas; i < cuenta_; ++i) instantes_[i - vencidas] = instantes_[i];
    cuenta_ -= vencidas;
  }

  if (cuenta_ >= kCapturasPorVentana) {
    // Ventana llena (solo ocurre si no hay pausa, p. ej. pausa_ms_ = 0 en pruebas).
    return {false, ventana_ms_ - static_cast<uint32_t>(ahora_ms - instantes_[0])};
  }

  instantes_[cuenta_++] = ahora_ms;
  if (cuenta_ == kCapturasPorVentana) {
    // Ráfaga completa: cerrado hasta que la ventana se vacíe y pase la pausa,
    // contado desde el primer instante de la ráfaga.
    const uint32_t desde_el_primero = static_cast<uint32_t>(ahora_ms - instantes_[0]);
    bloqueado_ = true;
    bloqueo_inicio_ = ahora_ms;
    bloqueo_duracion_ = ventana_ms_ + pausa_ms_ - desde_el_primero;
  }
  return {true, 0};
}

void LimitadorCapturas::reiniciar() {
  cuenta_ = 0;
  bloqueado_ = false;
  bloqueo_inicio_ = 0;
  bloqueo_duracion_ = 0;
}

Transicion GuardaTermica::primeraLectura(float temperatura_c, bool corte_previo) {
  if (corte_previo) estado_ = EstadoTermico::Corte;
  const bool valida = !isnan(temperatura_c) && temperatura_c >= kTempMinimaValidaC &&
                      temperatura_c <= kTempMaximaValidaC;
  if (valida && temperatura_c >= kTempAvisoC && estado_ != EstadoTermico::Corte) {
    estado_ = EstadoTermico::Corte;
    lectura_valida_ = true;
    return Transicion::EntraCorte;
  }
  return actualizar(temperatura_c);
}

Transicion GuardaTermica::actualizar(float temperatura_c) {
  if (isnan(temperatura_c) || temperatura_c < kTempMinimaValidaC ||
      temperatura_c > kTempMaximaValidaC) {
    lectura_valida_ = false;
    return Transicion::Ninguna;
  }
  lectura_valida_ = true;

  switch (estado_) {
    case EstadoTermico::Normal:
      if (temperatura_c >= kTempCorteC) {
        estado_ = EstadoTermico::Corte;
        return Transicion::EntraCorte;
      }
      if (temperatura_c >= kTempAvisoC) {
        estado_ = EstadoTermico::Aviso;
        return Transicion::EntraAviso;
      }
      return Transicion::Ninguna;
    case EstadoTermico::Aviso:
      if (temperatura_c >= kTempCorteC) {
        estado_ = EstadoTermico::Corte;
        return Transicion::EntraCorte;
      }
      if (temperatura_c < kTempAvisoC) {
        estado_ = EstadoTermico::Normal;
        return Transicion::SaleAviso;
      }
      return Transicion::Ninguna;
    case EstadoTermico::Corte:
      // Histéresis: de 65 a 75 °C se sigue cortado; solo por debajo de 65 se sale.
      if (temperatura_c < kTempAvisoC) {
        estado_ = EstadoTermico::Normal;
        return Transicion::SaleCorte;
      }
      return Transicion::Ninguna;
  }
  return Transicion::Ninguna;
}

bool intervaloCumplido(uint32_t ahora_ms, uint32_t ultimo_ms, uint32_t intervalo_ms) {
  return static_cast<uint32_t>(ahora_ms - ultimo_ms) >= intervalo_ms;
}

bool debeApagarPorInactividad(bool camara_encendida, uint32_t ahora_ms, uint32_t ultima_actividad_ms,
                              uint32_t inactividad_ms) {
  if (!camara_encendida) return false;
  return intervaloCumplido(ahora_ms, ultima_actividad_ms, inactividad_ms);
}

int cuadrosADescartar(bool arranque_en_frio, bool cambio_resolucion) {
  if (arranque_en_frio) return kCuadrosArranqueEnFrio;
  if (cambio_resolucion) return kCuadrosCambioResolucion;
  return kCuadrosEnCaliente;
}

void PresupuestoCamara::podar(uint32_t ahora_ms) {
  // Fuera lo que terminó hace ventana_ms_ o más: ya no cuenta.
  size_t vencidos = 0;
  while (vencidos < cuenta_ &&
         static_cast<uint32_t>(ahora_ms - cerrados_[vencidos].fin_ms) >= ventana_ms_) {
    ++vencidos;
  }
  if (vencidos > 0) {
    for (size_t i = vencidos; i < cuenta_; ++i) cerrados_[i - vencidos] = cerrados_[i];
    cuenta_ -= vencidos;
  }
}

void PresupuestoCamara::encendida(uint32_t ahora_ms) {
  if (encendida_) return;
  podar(ahora_ms);
  encendida_ = true;
  inicio_actual_ms_ = ahora_ms;
}

void PresupuestoCamara::apagada(uint32_t ahora_ms) {
  if (!encendida_) return;
  encendida_ = false;
  podar(ahora_ms);
  agregarCerrado(inicio_actual_ms_, ahora_ms);
}

void PresupuestoCamara::agregarCerrado(uint32_t inicio_ms, uint32_t fin_ms) {
  if (cuenta_ == kIntervalosPresupuesto) {
    for (size_t i = 1; i < cuenta_; ++i) cerrados_[i - 1] = cerrados_[i];
    --cuenta_;
  }
  cerrados_[cuenta_++] = {inicio_ms, fin_ms};
}

void PresupuestoCamara::restaurarEncendido(uint32_t ahora_ms, uint32_t usado_ms) {
  if (usado_ms == 0) return;
  if (usado_ms > ventana_ms_) usado_ms = ventana_ms_;
  agregarCerrado(static_cast<uint32_t>(ahora_ms - usado_ms), ahora_ms);
}

uint32_t PresupuestoCamara::encendidoEnVentanaMs(uint32_t ahora_ms) {
  podar(ahora_ms);
  // Cada tramo [inicio, fin) se recorta a la ventana [ahora - ventana, ahora]
  // midiendo la edad de sus extremos (restas sin signo, correctas con el
  // desbordamiento de millis()).
  uint64_t total = 0;
  auto sumar = [&](uint32_t inicio_ms, uint32_t fin_ms) {
    uint32_t edad_inicio = static_cast<uint32_t>(ahora_ms - inicio_ms);
    uint32_t edad_fin = static_cast<uint32_t>(ahora_ms - fin_ms);
    if (edad_inicio > ventana_ms_) edad_inicio = ventana_ms_;
    if (edad_fin > ventana_ms_) edad_fin = ventana_ms_;
    if (edad_inicio > edad_fin) total += edad_inicio - edad_fin;
  };
  for (size_t i = 0; i < cuenta_; ++i) sumar(cerrados_[i].inicio_ms, cerrados_[i].fin_ms);
  if (encendida_) sumar(inicio_actual_ms_, ahora_ms);
  return total > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(total);
}

bool PresupuestoCamara::excedido(uint32_t ahora_ms) {
  return encendidoEnVentanaMs(ahora_ms) >= presupuesto_ms_;
}

void PresupuestoCamara::iniciarEnfriamiento(uint32_t ahora_ms) {
  enfriando_ = true;
  enfriamiento_inicio_ms_ = ahora_ms;
  enfriamiento_duracion_ms_ = enfriamiento_ms_;
}

void PresupuestoCamara::restaurarEnfriamiento(uint32_t ahora_ms, uint32_t restante_ms) {
  if (restante_ms == 0) return;
  enfriando_ = true;
  enfriamiento_inicio_ms_ = ahora_ms;
  enfriamiento_duracion_ms_ = restante_ms < enfriamiento_ms_ ? restante_ms : enfriamiento_ms_;
}

bool PresupuestoCamara::enEnfriamiento(uint32_t ahora_ms) const {
  return enfriando_ &&
         static_cast<uint32_t>(ahora_ms - enfriamiento_inicio_ms_) < enfriamiento_duracion_ms_;
}

uint32_t PresupuestoCamara::esperaEnfriamientoMs(uint32_t ahora_ms) const {
  if (!enEnfriamiento(ahora_ms)) return 0;
  return enfriamiento_duracion_ms_ - static_cast<uint32_t>(ahora_ms - enfriamiento_inicio_ms_);
}

void PresupuestoCamara::reiniciar() {
  cuenta_ = 0;
  encendida_ = false;
  enfriando_ = false;
}

namespace {
constexpr uint32_t kSalRtc = 0xA5A5A5A5u;
uint32_t verificacionRtc(const EstadoSeguridadRtc& e) {
  return e.magia ^ e.enfriamiento_restante_ms ^ (e.corte_termico * 0x9E3779B1u) ^
         (e.encendido_usado_ms * 0x85EBCA6Bu) ^ kSalRtc;
}
}  // namespace

void sellarEstadoRtc(EstadoSeguridadRtc& estado) {
  estado.magia = kMagiaEstadoRtc;
  estado.verificacion = verificacionRtc(estado);
}

bool estadoRtcValido(const EstadoSeguridadRtc& estado) {
  return estado.magia == kMagiaEstadoRtc && estado.corte_termico <= 1 &&
         estado.enfriamiento_restante_ms <= kEnfriamientoMs &&
         estado.encendido_usado_ms <= kVentanaPresupuestoMs &&
         estado.verificacion == verificacionRtc(estado);
}

bool LimitadorMensajes::intentar(uint32_t ahora_ms, uint32_t* suprimidos) {
  if (hubo_ && static_cast<uint32_t>(ahora_ms - ultimo_ms_) < intervalo_ms_) {
    ++suprimidos_;
    return false;
  }
  hubo_ = true;
  ultimo_ms_ = ahora_ms;
  if (suprimidos != nullptr) *suprimidos = suprimidos_;
  suprimidos_ = 0;
  return true;
}

MotivoSecreto evaluarSecreto(const char* secreto) {
  if (secreto == nullptr || secreto[0] == '\0') return MotivoSecreto::Vacio;
  const size_t largo = strlen(secreto);
  if (largo < kSecretoLongitudMin) return MotivoSecreto::Corto;
  if (strstr(secreto, "cambiar_") != nullptr) return MotivoSecreto::Plantilla;
  bool vistos[256] = {false};
  size_t distintos = 0;
  for (size_t i = 0; i < largo; ++i) {
    const unsigned char c = static_cast<unsigned char>(secreto[i]);
    if (!vistos[c]) {
      vistos[c] = true;
      ++distintos;
    }
  }
  if (distintos < kSecretoDistintosMin) return MotivoSecreto::PocaVariedad;
  return MotivoSecreto::Valido;
}

const char* aTexto(MotivoSecreto motivo) {
  switch (motivo) {
    case MotivoSecreto::Valido:
      return "valido";
    case MotivoSecreto::Vacio:
      return "falta el secreto de la firma";
    case MotivoSecreto::Corto:
      return "el secreto de la firma es demasiado corto (minimo 32 caracteres)";
    case MotivoSecreto::PocaVariedad:
      return "el secreto de la firma tiene pocos caracteres distintos (minimo 8)";
    case MotivoSecreto::Plantilla:
      return "el secreto de la firma es el valor de ejemplo de la plantilla: no vale";
  }
  return "desconocido";
}

bool parsearIpv4(const char* texto, uint8_t salida[4]) {
  if (texto == nullptr) return false;
  uint8_t octetos[4];
  const char* p = texto;
  for (int i = 0; i < 4; ++i) {
    if (*p < '0' || *p > '9') return false;
    const char* inicio = p;
    unsigned valor = 0;
    while (*p >= '0' && *p <= '9') {
      valor = valor * 10 + static_cast<unsigned>(*p - '0');
      if (p - inicio >= 3) return false;  // más de 3 dígitos
      ++p;
    }
    if (p - inicio > 1 && *inicio == '0') return false;  // ceros a la izquierda
    if (valor > 255) return false;
    octetos[i] = static_cast<uint8_t>(valor);
    if (i < 3) {
      if (*p != '.') return false;
      ++p;
    }
  }
  if (*p != '\0') return false;
  memcpy(salida, octetos, 4);
  return true;
}

bool ipv4DeMapeada(const uint8_t v6[16], uint8_t salida[4]) {
  if (v6 == nullptr) return false;
  for (int i = 0; i < 10; ++i) {
    if (v6[i] != 0) return false;
  }
  if (v6[10] != 0xff || v6[11] != 0xff) return false;
  memcpy(salida, v6 + 12, 4);
  return true;
}

size_t construirLatido(char* destino, size_t capacidad, int rssi, uint32_t heap, float temperatura_c,
                       bool camara_encendida, bool en_corte, bool nvs_ok) {
  if (destino == nullptr || capacidad == 0) return 0;
  const bool hay_temp = !isnan(temperatura_c) && temperatura_c >= kTempMinimaValidaC &&
                        temperatura_c <= kTempMaximaValidaC;
  char temp[16] = "";
  if (hay_temp) {
    snprintf(temp, sizeof temp, ",\"temp\":%.1f", static_cast<double>(temperatura_c));
  }
  const int n = snprintf(destino, capacidad,
                         "{\"en_linea\":true,\"rssi\":%d,\"heap\":%lu%s,\"camara\":%s,\"corte\":%s,\"nvs\":%s}",
                         rssi, static_cast<unsigned long>(heap), temp, camara_encendida ? "true" : "false",
                         en_corte ? "true" : "false", nvs_ok ? "true" : "false");
  if (n < 0 || static_cast<size_t>(n) >= capacidad) {
    destino[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n);
}

}  // namespace logica_proteccion
