// Lógica pura del control manual de los actuadores: el catálogo de órdenes, la traducción de las respuestas de
// POST /api/actuadores (pestaña Actuadores de nodered/flows/flows.json), el estado de los actuadores en palabras y
// cuándo se pueden mandar órdenes.
// Los valores de cada orden son los que acepta el firmware de la XIAO (firmware/xiao/logica_actuadores.cpp):
// una prueba los compara con ese archivo. No hay orden de cerrar la puerta: el servo solo acepta "abrir" y se
// cierra solo a los 3 s.
// Sin DOM, sin fetch y sin importar nada: `node --test web/pruebas/*.test.js` carga este archivo
// directo en Node 22.18+ quitando los tipos.

export type GrupoOrden = 'puerta' | 'led' | 'buzzer' | 'luz'

/** El cuerpo de POST /api/actuadores: un solo campo por orden. */
export type CuerpoOrden = { servo: 'abrir' } | { led: string } | { buzzer: 0 | 1 | 2 } | { luz: 0 | 1 }

export interface Orden {
  id: string
  grupo: GrupoOrden
  /** El texto del botón: nombra la acción. */
  etiqueta: string
  cuerpo: CuerpoOrden
  /** La orden en pocas palabras; el aviso dice "Orden enviada: {hecho}". Enviada no es ejecutada: la XIAO puede no confirmarla. */
  hecho: string
}

export const ORDENES: readonly Orden[] = [
  { id: 'puerta-abrir', grupo: 'puerta', etiqueta: 'Abrir puerta', cuerpo: { servo: 'abrir' }, hecho: 'abrir puerta' },
  { id: 'led-azul', grupo: 'led', etiqueta: 'LED azul', cuerpo: { led: 'azul' }, hecho: 'LED azul' },
  { id: 'led-verde', grupo: 'led', etiqueta: 'LED verde', cuerpo: { led: 'verde' }, hecho: 'LED verde' },
  { id: 'led-rojo', grupo: 'led', etiqueta: 'LED rojo', cuerpo: { led: 'rojo' }, hecho: 'LED rojo' },
  { id: 'led-ambar', grupo: 'led', etiqueta: 'LED ámbar', cuerpo: { led: 'ambar' }, hecho: 'LED ámbar' },
  { id: 'led-apagado', grupo: 'led', etiqueta: 'Apagar LED', cuerpo: { led: 'apagado' }, hecho: 'apagar LED' },
  { id: 'buzzer-corto', grupo: 'buzzer', etiqueta: 'Pitido corto', cuerpo: { buzzer: 1 }, hecho: 'pitido corto' },
  { id: 'buzzer-doble', grupo: 'buzzer', etiqueta: 'Doble pitido', cuerpo: { buzzer: 2 }, hecho: 'doble pitido' },
  { id: 'buzzer-silencio', grupo: 'buzzer', etiqueta: 'Silenciar', cuerpo: { buzzer: 0 }, hecho: 'silenciar el buzzer' },
  { id: 'luz-encender', grupo: 'luz', etiqueta: 'Encender luz', cuerpo: { luz: 1 }, hecho: 'encender luz' },
  { id: 'luz-apagar', grupo: 'luz', etiqueta: 'Apagar luz', cuerpo: { luz: 0 }, hecho: 'apagar luz' },
]

/** El aviso de una orden que salió: dice que se envió, no que la XIAO la aplicó. */
export function avisoOrdenEnviada(orden: Orden): string {
  return `Orden enviada: ${orden.hecho}`
}

/** Solo la puerta pide confirmación: abrirla da paso al aula. El LED, el buzzer y la luz se mandan con un clic. */
export function pideConfirmacion(orden: Orden): boolean {
  return orden.grupo === 'puerta'
}

/** El texto del diálogo de confirmación de la puerta (solo abre y se cierra sola a los 3 s). */
export const CONFIRMACION_PUERTA = {
  titulo: 'Abrir la puerta',
  descripcion: 'Se cierra sola a los 3 s.',
  cancelar: 'Cancelar',
  confirmar: 'Abrir puerta',
  enviando: 'Enviando la orden…',
} as const

/**
 * Lo que dura inactivo el panel después de cada orden. La API acepta una por segundo (Node-RED, "Validar y
 * limitar orden a los actuadores") y cuenta desde que la recibe: el panel espera lo mismo desde que le responde,
 * así que un uso normal no choca con el límite.
 */
export const ENFRIAMIENTO_MS = 1000

export type ResultadoOrden =
  /** `publicado` es lo que Node-RED entregó al broker (no que la XIAO lo aplicara). */
  | { tipo: 'enviada'; publicado: Record<string, unknown> }
  /** 429: la API recibió otra orden hace menos de un segundo; esta no salió. */
  | { tipo: 'demasiado-rapido' }
  | { tipo: 'fallo'; status: number | null; mensaje: string }

/** Cuando no hubo respuesta (sin red o plazo agotado): puede que la API sí publicara. */
export const SIN_RESPUESTA =
  'La placa no respondió. No se sabe si la orden salió: revisa el estado de los actuadores antes de reintentar.'

function objeto(x: unknown): Record<string, unknown> | null {
  return typeof x === 'object' && x !== null && !Array.isArray(x) ? (x as Record<string, unknown>) : null
}

function fallo(status: number, mensaje: string): ResultadoOrden {
  return { tipo: 'fallo', status, mensaje }
}

/**
 * Traduce la respuesta de POST /api/actuadores. El texto técnico que traigan el 400 y el 415 no se muestra: el
 * cliente lo deja en la consola.
 */
export function interpretarOrden(status: number, cuerpo: unknown): ResultadoOrden {
  const o = objeto(cuerpo)
  if (status === 200) {
    const publicado = objeto(o?.publicado)
    return publicado !== null && Object.keys(publicado).length > 0
      ? { tipo: 'enviada', publicado }
      : fallo(status, 'La placa respondió algo inesperado. Revisa el estado de los actuadores antes de reintentar.')
  }
  if (status === 429) {
    return { tipo: 'demasiado-rapido' }
  }
  if (status === 400 || status === 415) {
    return fallo(status, 'La placa rechazó la orden.')
  }
  if (status === 401) {
    return fallo(status, 'Falta iniciar sesión.')
  }
  if (status === 403) {
    return fallo(status, 'La placa rechazó la petición. Recarga la página e intenta de nuevo.')
  }
  return fallo(status, 'La placa no pudo atender la orden. Intenta de nuevo en unos segundos.')
}

// ---- Estado de los actuadores en palabras ---------------------------------------------------------

/** Lo que trae aula/actuador/estado, como lo lee sistemaRespuestas.ts (`luz` es true si está encendida). */
export interface DatosActuadores {
  servo: 'abierto' | 'cerrado' | null
  led: 'azul' | 'verde' | 'rojo' | 'ambar' | 'apagado' | null
  luz: boolean | null
}

export interface EstadoEnPalabras {
  puerta: string
  led: string
  luz: string
}

/** El estado actual de la puerta, el LED y la luz con palabras; lo que no llegó dice "sin dato". */
export function estadoEnPalabras(datos: DatosActuadores | null): EstadoEnPalabras {
  const SIN_DATO = 'sin dato'
  if (datos === null) {
    return { puerta: SIN_DATO, led: SIN_DATO, luz: SIN_DATO }
  }
  return {
    puerta: datos.servo === null ? SIN_DATO : datos.servo === 'abierto' ? 'abierta (se cierra sola a los 3 s)' : 'cerrada',
    led: datos.led === null ? SIN_DATO : datos.led === 'ambar' ? 'ámbar' : datos.led,
    luz: datos.luz === null ? SIN_DATO : datos.luz ? 'encendida (se apaga sola a los 6 min)' : 'apagada',
  }
}

// ---- Cuándo se puede mandar -----------------------------------------------------------------------

export interface Disponibilidad {
  habilitado: boolean
  /** Por qué los botones están desactivados; null si no lo están. */
  motivo: string | null
  /** Una advertencia que no impide mandar la orden; null si no hay. */
  aviso: string | null
}

/**
 * Según cómo está la XIAO (la cámara, que publica el Will de la placa; null mientras no hay respuesta del estado
 * del sistema). Fuera de línea no recibiría nada: los botones se desactivan. Sin datos no se sabe: se deja
 * mandar, con el aviso. Si el último sondeo del estado falló (`errorEstado`), también se desactivan aunque haya
 * un estado anterior: no se sabe si la XIAO responde ahora.
 */
export function disponibilidadActuadores(conexion: 'en-linea' | 'fuera-de-linea' | 'sin-datos' | null, errorEstado = false): Disponibilidad {
  // Con el sondeo del estado fallando, `conexion` es lo último que se supo, no lo de ahora: no se manda nada.
  if (errorEstado) {
    return {
      habilitado: false,
      motivo: 'No se pudo consultar el nodo del aula: las órdenes se activan al volver.',
      aviso: null,
    }
  }
  switch (conexion) {
    case 'en-linea':
      return { habilitado: true, motivo: null, aviso: null }
    case 'fuera-de-linea':
      return {
        habilitado: false,
        motivo: 'Sin conexión con el nodo del aula: las órdenes se activan al volver.',
        aviso: null,
      }
    case 'sin-datos':
      return {
        habilitado: true,
        motivo: null,
        aviso: 'Aún no hay noticias del nodo del aula: la orden puede no llegar.',
      }
    default:
      return { habilitado: false, motivo: 'Consultando el estado del nodo del aula…', aviso: null }
  }
}

/**
 * Cómo se llama el estado que se muestra: "Ahora" solo si la XIAO está en línea y el último sondeo salió bien; con el
 * sondeo fallando, o la XIAO fuera de línea o sin datos, es el último estado conocido.
 */
export function etiquetaEstadoActuadores(conexion: 'en-linea' | 'fuera-de-linea' | 'sin-datos' | null, errorEstado: boolean): 'Ahora' | 'Último estado conocido' {
  return conexion === 'en-linea' && !errorEstado ? 'Ahora' : 'Último estado conocido'
}
