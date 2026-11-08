// Cliente de GET /api/camara/previa y lógica pura de su respuesta.
//
// El borde sirve el último cuadro que tiene en memoria de la cámara del aula como
// image/jpeg, y pone en la cabecera X-Edad-Cuadro la antigüedad de ese cuadro en
// milisegundos. Aquí vive el fetch y la interpretación de la respuesta; lo que la
// vista dice sale de textoEstadoPrevia.
//
// El cuadro se devuelve como URL de datos (`data:image/jpeg;base64,...`), no como
// URL de objeto (`blob:`): la CSP de `/app/` es `img-src 'self' data:` y no
// admite el esquema `blob:`. La conversión está en urlDeImagen.
//
// Sin imports: la prueba `node --test web/pruebas/*.test.js` carga este archivo
// directo (Node 22.18 o posterior quita los tipos), como alumnosRespuestas.ts y
// asistenciaRespuestas.ts. Por eso lleva su propia clase de error.

const RUTA = '/api/camara/previa'
const TIEMPO_MAXIMO_MS = 8000

/** Fallo al consultar la previsualización; `mensaje` va en español y se muestra tal cual. */
export class ErrorPrevia extends Error {}

/**
 * Lo que respondió el borde:
 * - cuadro: hay imagen; `edadMs` es la antigüedad que dice la cabecera, o null si
 *   el borde no la mandó;
 * - no-configurada: la previsualización no está activa en el borde (no es un fallo);
 * - sin-cuadro: todavía no hay cuadro, o la fuente está fallando (no es un fallo);
 * - sin-sesion: la API pidió credenciales y no se completaron;
 * - error: cualquier otra respuesta.
 */
export type ResultadoPrevia =
  | { tipo: 'cuadro'; edadMs: number | null }
  | { tipo: 'no-configurada' }
  | { tipo: 'sin-cuadro' }
  | { tipo: 'sin-sesion' }
  | { tipo: 'error'; mensaje: string }

/** La antigüedad que dice la cabecera, en milisegundos; null si no la dice o no se entiende. */
function leerEdad(encabezado: string | null): number | null {
  if (encabezado === null) {
    return null
  }
  const texto = encabezado.trim()
  if (texto === '') {
    return null
  }
  const valor = Number(texto)
  if (!Number.isFinite(valor) || valor < 0) {
    return null
  }
  return valor
}

/**
 * Traduce la respuesta del borde. Un 404 y un 503 son estados normales de esta
 * función (no está activa, o no hay cuadro todavía), así que no son fallos.
 */
export function interpretarPrevia(status: number, edadEncabezado: string | null): ResultadoPrevia {
  switch (status) {
    case 200:
      return { tipo: 'cuadro', edadMs: leerEdad(edadEncabezado) }
    case 401:
      return { tipo: 'sin-sesion' }
    case 404:
      return { tipo: 'no-configurada' }
    case 503:
      return { tipo: 'sin-cuadro' }
    default:
      return {
        tipo: 'error',
        mensaje: `La API respondió con el error ${status}. Espera unos segundos; si sigue igual, avisa al responsable del borde.`,
      }
  }
}

/**
 * Lo que dice el panel, en palabras. El texto lleva el estado; el color no.
 * Un resultado null es la primera consulta, todavía sin respuesta.
 */
export function textoEstadoPrevia(resultado: ResultadoPrevia | null): { titulo: string; detalle: string } {
  if (resultado === null) {
    return {
      titulo: 'Consultando la cámara',
      detalle: 'Pidiendo a la placa el último cuadro de la cámara.',
    }
  }
  switch (resultado.tipo) {
    case 'cuadro':
      return {
        titulo: 'Vista en vivo',
        detalle: 'Es el último cuadro que la placa tiene en memoria. Sirve de guía para colocar al alumno.',
      }
    case 'no-configurada':
      return {
        titulo: 'Vista previa no configurada',
        detalle: 'El borde no tiene activa la previsualización de la cámara. No es un error: la función no está en uso.',
      }
    case 'sin-cuadro':
      return {
        titulo: 'Sin cuadro de la cámara',
        detalle: 'Todavía no hay ningún cuadro, o la fuente está fallando. La vista lo vuelve a intentar cada segundo.',
      }
    case 'sin-sesion':
      return {
        titulo: 'Sin sesión iniciada',
        detalle: 'La API pide usuario y contraseña del profesor y no se completó el inicio de sesión.',
      }
    case 'error':
      return {
        titulo: 'No se pudo consultar la vista previa',
        detalle: resultado.mensaje,
      }
  }
}

/** "12 s", "2 min 5 s". Null si el borde no dijo la antigüedad del cuadro. */
export function formatearEdadCuadro(ms: number | null): string | null {
  if (ms === null) {
    return null
  }
  const total = Number.isFinite(ms) && ms > 0 ? Math.floor(ms / 1000) : 0
  const minutos = Math.floor(total / 60)
  const segundos = total % 60
  return minutos === 0 ? `${segundos} s` : `${minutos} min ${segundos} s`
}

/**
 * Aviso de privacidad del panel. Va literal: es el texto que el proyecto exige
 * junto a la imagen en vivo.
 */
export const AVISO_PRIVACIDAD =
  'Vista en vivo: los cuadros solo se muestran aquí, en memoria. No se guarda ninguna imagen del rostro. El sistema almacena únicamente el vector matemático del rostro y el registro de asistencia.'

export interface ConsultaPrevia {
  resultado: ResultadoPrevia
  /** URL de datos del cuadro recibido; null si no hay cuadro. */
  imagenUrl: string | null
}

/**
 * Tamaño de los trozos en que se codifica el cuadro. Un cuadro de la cámara
 * pesa cientos de KiB y `String.fromCharCode(...bytes)` sobre el arreglo entero
 * revienta la pila de llamadas; troceando, no hay problema.
 */
const TAMANO_TROZO = 8192

/**
 * Base64 de los bytes de la imagen. Es puro y no toca el DOM: la prueba
 * `node --test web/pruebas/*.test.js` lo puede comprobar en Node.
 */
export function base64DeBytes(bytes: Uint8Array): string {
  let binario = ''
  for (let inicio = 0; inicio < bytes.length; inicio += TAMANO_TROZO) {
    binario += String.fromCharCode(...bytes.subarray(inicio, inicio + TAMANO_TROZO))
  }
  return btoa(binario)
}

/**
 * El tipo de medio de la cabecera `Content-Type`, sin sus parámetros:
 * `image/jpeg; charset=binary` es `image/jpeg`. Null si la cabecera no trae
 * tipo.
 */
export function tipoMedio(contentType: string | null): string | null {
  if (contentType === null) {
    return null
  }
  const tipo = contentType.split(';')[0]?.trim().toLowerCase() ?? ''
  return tipo === '' ? null : tipo
}

/**
 * URL de datos del cuadro (`data:image/jpeg;base64,...`), o null si la
 * respuesta no es una imagen.
 *
 * Se usa una URL de datos y no una URL de objeto (`blob:`) porque la CSP que
 * fija Node-RED para `/app/` es `img-src 'self' data:`, y `'self'` no cubre el
 * esquema `blob:`: el navegador bloquearía el cuadro y el panel nunca lo
 * mostraría. Ampliar la CSP para admitir `blob:` (o `https:`) dejaría pasar
 * imágenes de terceros en el panel del profesor.
 */
export function urlDeImagen(bytes: Uint8Array, contentType: string | null): string | null {
  const tipo = tipoMedio(contentType)
  if (tipo === null || !tipo.startsWith('image/')) {
    return null
  }
  return `data:${tipo};base64,${base64DeBytes(bytes)}`
}

/**
 * Pide el último cuadro al borde. Cancela con `senal` y también si la API tarda
 * más de `tiempoMaximoMs`; la primera consulta admite más (TIEMPO_INICIAL_MS de
 * energia.ts), porque espera el diálogo de usuario y contraseña del navegador.
 *
 * La petición no lleva credenciales: el navegador es del mismo origen y reenvía
 * las que ya tiene cacheadas. `cache: 'no-store'` evita que el navegador sirva
 * un cuadro viejo de su caché en lugar del que acaba de llegar.
 */
export async function pedirPrevia(
  senal: AbortSignal,
  tiempoMaximoMs: number = TIEMPO_MAXIMO_MS,
): Promise<ConsultaPrevia> {
  const controlador = new AbortController()
  const alCancelar = () => controlador.abort()
  senal.addEventListener('abort', alCancelar)
  const temporizador = setTimeout(() => controlador.abort(), tiempoMaximoMs)
  const tiempoAgotado = () => new ErrorPrevia('La API tardó demasiado en responder. Revisa la conexión con la placa.')
  try {
    let respuesta: Response
    try {
      respuesta = await fetch(RUTA, {
        signal: controlador.signal,
        cache: 'no-store',
        headers: { Accept: 'image/jpeg' },
      })
    } catch (error) {
      if (senal.aborted) {
        throw error
      }
      throw controlador.signal.aborted
        ? tiempoAgotado()
        : new ErrorPrevia('No se pudo conectar con la API. Revisa que la placa esté encendida y conectada a la red del aula.')
    }
    const resultado = interpretarPrevia(respuesta.status, respuesta.headers.get('X-Edad-Cuadro'))
    if (resultado.tipo !== 'cuadro') {
      return { resultado, imagenUrl: null }
    }
    // El cuadro se pasa a una URL de datos (no a una URL de objeto): es lo que
    // la CSP de `/app/` admite. La validación del tipo va en urlDeImagen, que
    // devuelve null si la respuesta no es una imagen.
    const bytes = new Uint8Array(await respuesta.arrayBuffer())
    const imagenUrl = urlDeImagen(bytes, respuesta.headers.get('Content-Type'))
    if (imagenUrl === null) {
      throw new ErrorPrevia('La API respondió con algo que no es una imagen. Avisa al responsable del borde.')
    }
    return { resultado, imagenUrl }
  } finally {
    clearTimeout(temporizador)
    senal.removeEventListener('abort', alCancelar)
  }
}
