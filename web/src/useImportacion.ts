import { useCallback, useEffect, useRef, useState } from 'react'
import { toast } from 'sonner'
import { enviarImportacion } from './api/alumnos'
import {
  resumirConteos,
  validarArchivoLocal,
  type Decision,
  type Existente,
  type ResultadoImportacion,
} from './api/alumnosRespuestas'
import { TIEMPO_INICIAL_MS } from './api/energia'

/** Lo que queda a la vista tras un intento que no terminó en éxito (el éxito va en un toast). */
export type AvisoImportacion =
  | Extract<ResultadoImportacion, { tipo: 'filas-invalidas' | 'carrera' | 'fallo' }>
  | { tipo: 'rechazo-local'; mensaje: string }

/** El aviso con un número propio: uno repetido se remonta, se vuelve a anunciar y a animar. */
export type AvisoVisible = AvisoImportacion & { id: number }

/** Archivo en espera de que la persona decida qué hacer con las cuentas que ya existen. */
export interface PorDecidir {
  archivo: File
  existentes: Existente[]
}

/** Qué pasó con los archivos que se entregaron a `elegir`. */
export type Entrega = 'enviado' | 'rechazado' | 'ocupado'

export interface EstadoImportacion {
  /** Hay una petición en curso (la zona y el diálogo no admiten otra). */
  enviando: boolean
  /** Nombre del archivo en curso, para el texto de espera. */
  archivo: string | null
  aviso: AvisoVisible | null
  porDecidir: PorDecidir | null
  /** Revisa el archivo y lo sube. 'rechazado' si lo rechazó la revisión local; 'ocupado' si ya hay un envío en curso. */
  elegir: (archivos: readonly File[]) => Entrega
  /** Reenvía el archivo en espera con la decisión de la persona. */
  decidir: (decision: Decision) => void
  /** Cierra el diálogo sin importar nada; una segunda llamada no hace nada. */
  cancelar: () => void
}

/**
 * Resultado que nadie verá en la vista porque la persona ya la dejó: Sonner vive
 * en App, así que un toast es lo único que la alcanza. Los avisos de la vista
 * (filas inválidas, carrera, fallos) y el diálogo de existentes no existen ya.
 */
function avisarSinVista(resultado: Exclude<ResultadoImportacion, { tipo: 'exito' }>, archivo: string) {
  switch (resultado.tipo) {
    case 'filas-invalidas':
      toast.error('No se importó nada', { description: resultado.error, duration: 8000 })
      break
    case 'carrera':
      toast.warning('Se importó solo una parte del archivo', {
        description: `Otra importación agregó cuentas mientras tanto. Quedó escrito esto: ${resumirConteos(resultado.conteos)}.`,
        duration: 8000,
      })
      break
    case 'existentes':
      toast.warning('No se importó nada', {
        description: `${archivo} trae cuentas que ya existen. Vuelve a Alumnos y elige el archivo otra vez para decidir qué hacer con ellas.`,
        duration: 8000,
      })
      break
    case 'fallo':
      toast.error('No se pudo importar', { description: resultado.mensaje, duration: 8000 })
      break
  }
}

/**
 * Flujo de importación del CSV. El servidor manda: la revisión local solo
 * ahorra un viaje con lo evidente (vacío, demasiado grande, no es texto). El
 * 409 con escrito:false abre el diálogo y espera: nunca se reenvía con una
 * decisión que la persona no tomó. `alEscribir` se llama cuando la API pudo
 * haber cambiado la lista (éxito o carrera), para que la vista la recargue.
 * `sinSesion` alarga el plazo del POST, porque el navegador puede volver a
 * pedir las credenciales.
 *
 * Un solo envío a la vez, decidido con refs y no con el estado ni el DOM: dos
 * clics, o un clic y una tecla, en el mismo fotograma ven el mismo estado de
 * React. Al salir de la vista el POST no se aborta (abortar no deshace lo que
 * la API ya escribió): termina, no toca el estado de un componente desmontado
 * y deja su resultado en un toast.
 */
export function useImportacion(alEscribir: () => void, sinSesion: boolean): EstadoImportacion {
  const [enviando, setEnviando] = useState(false)
  const [archivo, setArchivo] = useState<string | null>(null)
  const [aviso, setAviso] = useState<AvisoVisible | null>(null)
  const [porDecidir, setPorDecidirEstado] = useState<PorDecidir | null>(null)
  const enCurso = useRef(false)
  const montado = useRef(false)
  const numeroDeAviso = useRef(0)
  // Espejo de `porDecidir` para decidir y cancelar sin leer el estado de React.
  const pendiente = useRef<PorDecidir | null>(null)
  const actuales = useRef({ alEscribir, sinSesion })
  useEffect(() => {
    actuales.current = { alEscribir, sinSesion }
  }, [alEscribir, sinSesion])
  useEffect(() => {
    montado.current = true
    return () => {
      montado.current = false
    }
  }, [])

  const fijarPorDecidir = useCallback((valor: PorDecidir | null) => {
    pendiente.current = valor
    setPorDecidirEstado(valor)
  }, [])

  const mostrar = useCallback((valor: AvisoImportacion | null) => {
    numeroDeAviso.current += 1
    setAviso(valor === null ? null : { ...valor, id: numeroDeAviso.current })
  }, [])

  const enviar = useCallback(
    async (aImportar: File, decision?: Decision) => {
      if (enCurso.current) {
        return
      }
      enCurso.current = true
      setEnviando(true)
      setArchivo(aImportar.name)
      mostrar(null)
      try {
        const resultado = await enviarImportacion(aImportar, decision, actuales.current.sinSesion ? TIEMPO_INICIAL_MS : undefined)
        if (resultado.tipo === 'exito') {
          toast.success('Importación terminada', {
            description: `${resumirConteos(resultado.conteos)}. ${resultado.conteos.total} ${resultado.conteos.total === 1 ? 'fila' : 'filas'} en el archivo.`,
            duration: 6000,
          })
        }
        if (!montado.current) {
          if (resultado.tipo !== 'exito') {
            avisarSinVista(resultado, aImportar.name)
          }
          return
        }
        switch (resultado.tipo) {
          case 'exito':
            fijarPorDecidir(null)
            actuales.current.alEscribir()
            break
          case 'existentes':
            if (decision === undefined) {
              fijarPorDecidir({ archivo: aImportar, existentes: resultado.existentes })
            } else {
              // Con la decisión tomada la API no debería volver a preguntar.
              fijarPorDecidir(null)
              mostrar({
                tipo: 'fallo',
                status: 409,
                mensaje: 'La API volvió a pedir una decisión sobre las cuentas existentes, algo que no debería pasar. No se sabe si se importó el archivo: revisa la lista y avisa al responsable del borde.',
              })
            }
            break
          case 'carrera':
            fijarPorDecidir(null)
            actuales.current.alEscribir()
            mostrar(resultado)
            break
          default:
            fijarPorDecidir(null)
            mostrar(resultado)
        }
      } finally {
        enCurso.current = false
        if (montado.current) {
          setEnviando(false)
          setArchivo(null)
        }
      }
    },
    [fijarPorDecidir, mostrar],
  )

  const elegir = useCallback(
    (archivos: readonly File[]): Entrega => {
      if (enCurso.current || pendiente.current !== null) {
        return 'ocupado'
      }
      const [nuevo, ...otros] = archivos
      if (nuevo === undefined || otros.length > 0) {
        mostrar({ tipo: 'rechazo-local', mensaje: 'Elige un solo archivo CSV.' })
        return 'rechazado'
      }
      const motivo = validarArchivoLocal(nuevo)
      if (motivo !== null) {
        mostrar({ tipo: 'rechazo-local', mensaje: motivo })
        return 'rechazado'
      }
      void enviar(nuevo)
      return 'enviado'
    },
    [enviar, mostrar],
  )

  const decidir = useCallback(
    (decision: Decision) => {
      if (enCurso.current || pendiente.current === null) {
        return
      }
      void enviar(pendiente.current.archivo, decision)
    },
    [enviar],
  )

  const cancelar = useCallback(() => {
    if (enCurso.current || pendiente.current === null) {
      return
    }
    fijarPorDecidir(null)
    toast('Importación cancelada', { description: 'No se escribió nada en la lista de alumnos.' })
  }, [fijarPorDecidir])

  return { enviando, archivo, aviso, porDecidir, elegir, decidir, cancelar }
}
