#pragma once

// El codigo de salida de probar_captura, como funcion pura (se prueba sin red).
//
//   0   todo salio como debia: todas las respuestas fueron 200 descifrados (o, con --sin-firma,
//       todas fueron 401, que es lo que debe contestar la placa a una peticion sin firma)
//   1   alguna peticion no obtuvo respuesta (red, tiempo, cuerpo demasiado grande)
//   2   uso incorrecto
//   4   hubo respuestas, pero alguna no fue 200 (esperado con --reusar-nonce: 403)
//   5   un 200 no es autentico: no hay X-Captura-IV, o el descifrado AES-256-GCM falla
//   6   --sin-firma y la placa CONTESTO 200: acepto una peticion sin firma (fallo grave de la
//       placa o del camino: ACEPTO SIN FIRMA)
//   78  el archivo del secreto no vale o falta el reloj
//
// Prioridad: 6 > 1 > 5 > 4 > 0 (lo mas grave primero).

namespace puente_aula {

inline constexpr int kSalidaTransporte = 1;
inline constexpr int kSalidaUso = 2;
inline constexpr int kSalidaNo200 = 4;
inline constexpr int kSalidaNoAutentica = 5;
inline constexpr int kSalidaAceptoSinFirma = 6;
inline constexpr int kSalidaConfig = 78;

struct ResultadosPrueba {
  bool transporte_fallido = false;   // alguna peticion no obtuvo respuesta
  bool acepto_sin_firma = false;     // con --sin-firma, algun 200
  bool no_autentica = false;         // algun 200 que no descifra
  bool no_200 = false;               // algun estado inesperado distinto de 200
};

inline int codigo_de_salida(const ResultadosPrueba& r) {
  if (r.acepto_sin_firma) return kSalidaAceptoSinFirma;
  if (r.transporte_fallido) return kSalidaTransporte;
  if (r.no_autentica) return kSalidaNoAutentica;
  if (r.no_200) return kSalidaNo200;
  return 0;
}

// Con --sin-firma el estado esperado es 401; con el resto de las opciones, 200.
inline bool estado_esperado(bool sin_firma, int estado) {
  return sin_firma ? estado == 401 : estado == 200;
}

}  // namespace puente_aula
