import { useEffect, useRef, useState, type DragEvent as EventoArrastre } from 'react'
import { flushSync } from 'react-dom'
import type { Entrega } from '../useImportacion'

interface Props {
  /** Hay una importación en curso: la zona no admite otro archivo. */
  enviando: boolean
  /** Nombre del archivo en curso. */
  archivo: string | null
  /** Revisa y sube los archivos. */
  alElegir: (archivos: readonly File[]) => Entrega
}

type EstadoZona = 'reposo' | 'encima' | 'soltado' | 'rechazado'

// Cuánto dura cada estado pasajero. SOLTADO_MS es la duración de `zona-soltar`
// en estilos.css (340 ms): si fuera menor, el anillo se apagaría a medio rebote.
// RECHAZADO_MS es mayor que la sacudida (400 ms) a propósito: el tinte rojo se
// queda un rato después de que la zona deja de moverse.
const SOLTADO_MS = 340
const RECHAZADO_MS = 900

function traeArchivos(e: { dataTransfer: DataTransfer | null }): boolean {
  return Array.from(e.dataTransfer?.types ?? []).includes('Files')
}

/**
 * Zona para soltar el CSV, con un botón que abre el selector de archivos: el
 * botón es la alternativa por teclado y para quien no arrastra, y es lo que
 * lee un lector de pantalla. Movimiento (estilos.css, `.zona`): al arrastrar
 * encima la zona se agranda un 1,5 % y aparece su anillo; al soltar hace un
 * rebote corto; al rechazar el archivo se sacude. El texto de la revisión
 * sale en AvisoImportacion, así que el movimiento solo refuerza.
 */
export function ZonaCSV({ enviando, archivo, alElegir }: Props) {
  const [estado, setEstado] = useState<EstadoZona>('reposo')
  const zona = useRef<HTMLDivElement>(null)
  const entrada = useRef<HTMLInputElement>(null)
  // dragenter y dragleave se disparan también al cruzar los hijos: se cuentan.
  const profundidad = useRef(0)
  const temporizador = useRef<number | undefined>(undefined)

  useEffect(() => () => window.clearTimeout(temporizador.current), [])

  // Un archivo soltado fuera de la zona (o con el diálogo abierto) lo abriría el
  // navegador y la página, con su estado, se perdería. Se cancela el arrastre
  // de archivos en toda la ventana, salvo dentro de la zona, que lo atiende.
  useEffect(() => {
    const fuera = (e: DragEvent) => {
      if (!traeArchivos(e) || (e.target instanceof Node && zona.current?.contains(e.target))) {
        return
      }
      e.preventDefault()
      if (e.dataTransfer !== null) {
        e.dataTransfer.dropEffect = 'none'
      }
    }
    window.addEventListener('dragover', fuera)
    window.addEventListener('drop', fuera)
    return () => {
      window.removeEventListener('dragover', fuera)
      window.removeEventListener('drop', fuera)
    }
  }, [])

  function pasajero(siguiente: 'soltado' | 'rechazado') {
    window.clearTimeout(temporizador.current)
    if (siguiente === estado) {
      // Una animación CSS no se reinicia sola si el estado no cambia: se pasa
      // por reposo, se confirma el cambio en el DOM y se fuerza el cálculo de estilos.
      flushSync(() => setEstado('reposo'))
      void zona.current?.offsetWidth
    }
    setEstado(siguiente)
    temporizador.current = window.setTimeout(() => setEstado('reposo'), siguiente === 'soltado' ? SOLTADO_MS : RECHAZADO_MS)
  }

  function entregar(archivos: readonly File[]) {
    const entrega = alElegir(archivos)
    if (entrega !== 'ocupado') {
      pasajero(entrega === 'enviado' ? 'soltado' : 'rechazado')
    }
  }

  function alEntrar(e: EventoArrastre) {
    if (!traeArchivos(e)) {
      return
    }
    e.preventDefault()
    profundidad.current += 1
    if (!enviando) {
      window.clearTimeout(temporizador.current)
      setEstado('encima')
    }
  }

  function alSalir(e: EventoArrastre) {
    if (!traeArchivos(e)) {
      return
    }
    profundidad.current = Math.max(0, profundidad.current - 1)
    if (profundidad.current === 0 && estado === 'encima') {
      setEstado('reposo')
    }
  }

  function alPasarEncima(e: EventoArrastre) {
    if (traeArchivos(e)) {
      // Sin preventDefault el navegador no deja soltar.
      e.preventDefault()
      e.dataTransfer.dropEffect = enviando ? 'none' : 'copy'
    }
  }

  function alSoltar(e: EventoArrastre) {
    if (!traeArchivos(e)) {
      return
    }
    e.preventDefault()
    profundidad.current = 0
    if (enviando) {
      setEstado('reposo')
      return
    }
    entregar(Array.from(e.dataTransfer.files))
  }

  return (
    <div
      ref={zona}
      className="zona"
      data-estado={estado}
      onDragEnter={alEntrar}
      onDragLeave={alSalir}
      onDragOver={alPasarEncima}
      onDrop={alSoltar}
    >
      <h2 id="titulo-importar" className="zona__titulo">Importar lista de alumnos</h2>
      <p>Suelta aquí el CSV o elígelo en tu equipo.</p>
      {/* aria-disabled y no disabled: el botón conserva el foco mientras se importa. */}
      <button
        type="button"
        className="boton boton--primario"
        aria-disabled={enviando}
        aria-describedby="ayuda-csv"
        onClick={() => {
          if (!enviando) {
            entrada.current?.click()
          }
        }}
      >
        {enviando ? 'Importando…' : 'Elegir archivo CSV'}
      </button>
      <input
        ref={entrada}
        type="file"
        accept=".csv,text/csv"
        hidden
        onChange={(e) => {
          const elegidos = Array.from(e.target.files ?? [])
          // Vacío el campo: elegir otra vez el mismo archivo también dispara onChange.
          e.target.value = ''
          if (elegidos.length > 0) {
            entregar(elegidos)
          }
        }}
      />
      <p id="ayuda-csv" className="etiqueta-tecnica zona__ayuda">
        Columnas <code>no_cuenta,nombre,grupo</code>. Hasta 500 filas y 256 KiB.
      </p>
      {enviando && archivo !== null && (
        <p role="status" className="etiqueta-tecnica zona__archivo">
          Importando <bdi>{archivo}</bdi>
        </p>
      )}
    </div>
  )
}
