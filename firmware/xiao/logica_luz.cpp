#include "logica_luz.h"

#include <stdio.h>

namespace logica_luz {

bool debeDescartar(bool posibleRetenido, size_t longitud) {
  return posibleRetenido || longitud == 0;
}

PedidoLuz interpretarLuz(bool presente, bool esEntero, long valor) {
  if (!presente) return PedidoLuz::Ausente;
  if (!esEntero) return PedidoLuz::Invalido;
  if (valor == 0) return PedidoLuz::Apagar;
  if (valor == 1) return PedidoLuz::Encender;
  return PedidoLuz::Invalido;
}

bool cabeCmd(size_t longitud) { return longitud < kMaxCmdBytes; }

bool esOrden(PedidoLuz pedido) {
  return pedido == PedidoLuz::Apagar || pedido == PedidoLuz::Encender;
}

bool ControlLuz::aplicar(PedidoLuz pedido, uint32_t ahora_ms) {
  bool nueva = encendida_;
  if (pedido == PedidoLuz::Encender) {
    nueva = true;
    encendida_desde_ms_ = ahora_ms;  // también renueva el plazo si ya estaba encendida
  }
  if (pedido == PedidoLuz::Apagar) nueva = false;
  const bool cambio = nueva != encendida_;
  encendida_ = nueva;
  return cambio;
}

bool ControlLuz::revisarVencimiento(uint32_t ahora_ms) {
  if (!encendida_) return false;
  // Resta sin signo: correcta aunque millis() haya dado la vuelta.
  if (static_cast<uint32_t>(ahora_ms - encendida_desde_ms_) < limite_ms_) return false;
  encendida_ = false;
  return true;
}

size_t construirEstado(char* destino, size_t capacidad, bool encendida) {
  if (destino == nullptr || capacidad == 0) return 0;
  const int n = snprintf(destino, capacidad, "{\"luz\":%d}", encendida ? 1 : 0);
  if (n < 0 || static_cast<size_t>(n) >= capacidad) {
    destino[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n);
}

}  // namespace logica_luz
