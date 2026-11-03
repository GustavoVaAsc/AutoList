// Matemática pura de las cifras que cuentan hasta su valor. Sin DOM ni imports:
// la prueba `node --test web/pruebas/*.test.js` la carga directo.

/** Duración de la cuenta la primera vez que aparece una cifra. */
export const CUENTA_INICIAL_MS = 800
/** Duración cuando el valor ya estaba en pantalla y cambia (cada consulta). */
export const CUENTA_CAMBIO_MS = 300

/** Ease-out de cuarto grado: arranca rápido y se asienta, como las demás entradas. */
export function suavizar(progreso: number): number {
  const t = Math.min(1, Math.max(0, progreso))
  return 1 - (1 - t) ** 4
}

/** Valor entre `desde` y `hasta` para un avance ya suavizado (0 a 1). */
export function valorIntermedio(desde: number, hasta: number, avance: number): number {
  return desde + (hasta - desde) * avance
}
