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
  /** Lo que dice el aviso cuando la orden salió. */
  hecho: string
}

export const ORDENES: readonly Orden[] = [
  { id: 'puerta-abrir', grupo: 'puerta', etiqueta: 'Abrir puerta', cuerpo: { servo: 'abrir' }, hecho: 'Se pidió abrir la puerta. Se cierra sola a los 3 s.' },
  { id: 'led-azul', grupo: 'led', etiqueta: 'LED azul', cuerpo: { led: 'azul' }, hecho: 'Se pidió el LED azul.' },
  { id: 'led-verde', grupo: 'led', etiqueta: 'LED verde', cuerpo: { led: 'verde' }, hecho: 'Se pidió el LED verde.' },
  { id: 'led-rojo', grupo: 'led', etiqueta: 'LED rojo', cuerpo: { led: 'rojo' }, hecho: 'Se pidió el LED rojo.' },
  { id: 'led-ambar', grupo: 'led', etiqueta: 'LED ámbar', cuerpo: { led: 'ambar' }, hecho: 'Se pidió el LED ámbar.' },
  { id: 'led-apagado', grupo: 'led', etiqueta: 'Apagar LED', cuerpo: { led: 'apagado' }, hecho: 'Se pidió apagar el LED.' },
  { id: 'buzzer-corto', grupo: 'buzzer', etiqueta: 'Pitido corto', cuerpo: { buzzer: 1 }, hecho: 'Se pidió un pitido corto.' },
  { id: 'buzzer-doble', grupo: 'buzzer', etiqueta: 'Doble pitido', cuerpo: { buzzer: 2 }, hecho: 'Se pidió un doble pitido.' },
  { id: 'buzzer-silencio', grupo: 'buzzer', etiqueta: 'Silenciar', cuerpo: { buzzer: 0 }, hecho: 'Se pidió silenciar el buzzer.' },
  { id: 'luz-encender', grupo: 'luz', etiqueta: 'Encender luz', cuerpo: { luz: 1 }, hecho: 'Se pidió encender la luz. Se apaga sola a los 6 min.' },
  { id: 'luz-apagar', grupo: 'luz', etiqueta: 'Apagar luz', cuerpo: { luz: 0 }, hecho: 'Se pidió apagar la luz.' },
]

/** Solo la puerta pide confirmación: abrirla da paso al aula. El LED, el buzzer y la luz se mandan con un clic. */
export function pideConfirmacion(orden: Orden): boolean {
  return orden.grupo === 'puerta'
}

/** El texto del diálogo de confirmación de la puerta (el servo solo abre y se cierra solo a los 3 s). */
export const CONFIRMACION_PUERTA = {
  titulo: 'Abrir la puerta del aula',
  descripcion: 'El servo abre la puerta ahora y la cierra solo a los 3 s. No hay una orden para cerrarla antes.',
  cancelar: 'Cancelar',
  confirmar: 'Abrir puerta',
  enviando: 'Enviando la orden',
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
  'No hubo respuesta de la API (sin conexión o tardó demasiado). No se sabe si la orden salió: revisa el estado de los actuadores antes de reintentar.'

function objeto(x: unknown): Record<string, unknown> | null {
  return typeof x === 'object' && x !== null && !Array.isArray(x) ? (x as Record<string, unknown>) : null
}

function fallo(status: number, mensaje: string): ResultadoOrden {
  return { tipo: 'fallo', status, mensaje }
}

/**
 * Traduce la respuesta de POST /api/actuadores. El 400 y el 415 traen un texto fijo del flujo (nunca algo que
 * vino del cuerpo enviado), y se repite tal cual.
 */
export function interpretarOrden(status: number, cuerpo: unknown): ResultadoOrden {
  const o = objeto(cuerpo)
  if (status === 200) {
    const publicado = objeto(o?.publicado)
    return publicado !== null && Object.keys(publicado).length > 0
      ? { tipo: 'enviada', publicado }
      : fallo(status, 'La API respondió algo que la vista no reconoce. Revisa el estado de los actuadores antes de reintentar.')
  }
  if (status === 429) {
    return { tipo: 'demasiado-rapido' }
  }
  if (status === 400 || status === 415) {
    return fallo(status, typeof o?.error === 'string' ? `La API rechazó la orden: ${o.error}.` : `La API rechazó la orden (error ${status}).`)
  }
  if (status === 401) {
    return fallo(status, 'La API pide usuario y contraseña del profesor y no se completó el inicio de sesión.')
  }
  if (status === 403) {
    return fallo(status, 'La API rechazó la petición por venir de otro sitio. Recarga la página e inténtalo de nuevo.')
  }
  if (status === 500) {
    return fallo(status, 'Node-RED no pudo procesar la orden. Revisa su registro si sigue pasando.')
  }
  return fallo(status, `La API respondió con el error ${status}. Espera unos segundos; si sigue igual, avisa al responsable del borde.`)
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
      motivo: 'No se pudo consultar el estado de la XIAO, y no se mandan órdenes sin saber si responde. Se activan solas cuando vuelva la consulta.',
      aviso: null,
    }
  }
  switch (conexion) {
    case 'en-linea':
      return { habilitado: true, motivo: null, aviso: null }
    case 'fuera-de-linea':
      return {
        habilitado: false,
        motivo: 'La XIAO está fuera de línea y no recibiría las órdenes. Se activan solas cuando vuelva.',
        aviso: null,
      }
    case 'sin-datos':
      return {
        habilitado: true,
        motivo: null,
        aviso: 'Node-RED no ha recibido nada de la XIAO desde que arrancó: la orden se envía al broker, pero puede no llegar.',
      }
    default:
      return { habilitado: false, motivo: 'Consultando el estado de la XIAO.', aviso: null }
  }
}

/**
 * Cómo se llama el estado que se muestra: "Ahora" solo si la XIAO está en línea y el último sondeo salió bien; con el
 * sondeo fallando, o la XIAO fuera de línea o sin datos, es el último estado conocido.
 */
export function etiquetaEstadoActuadores(conexion: 'en-linea' | 'fuera-de-linea' | 'sin-datos' | null, errorEstado: boolean): 'Ahora' | 'Último estado conocido' {
  return conexion === 'en-linea' && !errorEstado ? 'Ahora' : 'Último estado conocido'
}
