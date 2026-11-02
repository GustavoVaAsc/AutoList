import { useEffect, useLayoutEffect, useRef, useState, type ComponentType } from 'react'
import { IndicadorEstado } from './componentes/IndicadorEstado'
import { SelectorTema } from './componentes/SelectorTema'
import { PlacaProveedor } from './estadoPlaca'
import { LecturasContexto } from './lecturasContexto'
import { useLecturas } from './useLecturas'
import { Alumnos } from './vistas/Alumnos'
import { Metricas } from './vistas/Metricas'
import { Registro } from './vistas/Registro'

interface Vista {
  ruta: string
  nombre: string
  componente: ComponentType
  /** Vista que aún no funciona; la navegación lo dice. */
  pronto?: boolean
}

// Las tres vistas del plan, sección 6.7. La ruta va en el hash (#/metricas)
// para que recargar la página conserve la vista sin que Node-RED tenga que
// resolver rutas del lado del servidor.
const VISTAS: Vista[] = [
  { ruta: 'alumnos', nombre: 'Alumnos', componente: Alumnos, pronto: true },
  { ruta: 'registro', nombre: 'Registro', componente: Registro, pronto: true },
  { ruta: 'metricas', nombre: 'Métricas y asistencia', componente: Metricas },
]
const VISTA_INICIAL = 'metricas'

const LECTURAS_A_PEDIR = 100
const CONSULTA_CADA_MS = 5000

function rutaActual(): string {
  const ruta = window.location.hash.replace(/^#\/?/, '')
  return VISTAS.some((v) => v.ruta === ruta) ? ruta : VISTA_INICIAL
}

interface CajaIndicador {
  x: number
  y: number
  ancho: number
  alto: number
}

/**
 * Posición y tamaño del segmento activo del control segmentado. Propósito: al
 * cambiar de vista, el segmento blanco se desliza de una a otra (continuidad
 * espacial). Solo se anima el cambio de ruta: medir de nuevo por un cambio de
 * tamaño o por la carga de la fuente recoloca el segmento sin transición. Se
 * mide con el DOM porque en pantallas angostas los segmentos cambian de ancho
 * y de alto (el texto se parte en dos líneas).
 */
function useIndicador(lista: React.RefObject<HTMLUListElement | null>, ruta: string) {
  const [caja, setCaja] = useState<CajaIndicador | null>(null)
  const [animado, setAnimado] = useState(false)
  const rutaPrevia = useRef(ruta)

  // Cambio de ruta: habilita la transición el tiempo que dura el deslizamiento.
  useLayoutEffect(() => {
    if (rutaPrevia.current === ruta) {
      return
    }
    rutaPrevia.current = ruta
    setAnimado(true)
    const id = window.setTimeout(() => setAnimado(false), 260)
    return () => window.clearTimeout(id)
  }, [ruta])

  useLayoutEffect(() => {
    const ul = lista.current
    if (ul === null) {
      return
    }
    let cancelado = false
    const medir = () => {
      const activo = ul.querySelector<HTMLElement>('[aria-current="page"]')
      if (activo === null) {
        return
      }
      const x = activo.offsetLeft
      const y = activo.offsetTop
      const ancho = activo.offsetWidth
      const alto = activo.offsetHeight
      setCaja((previa) =>
        previa !== null && previa.x === x && previa.y === y && previa.ancho === ancho && previa.alto === alto
          ? previa
          : { x, y, ancho, alto },
      )
    }
    medir()
    // La lista y cada enlace: el tamaño de un enlace cambia con la fuente o el texto.
    const observador = new ResizeObserver(medir)
    observador.observe(ul)
    ul.querySelectorAll('a').forEach((enlace) => observador.observe(enlace))
    // Las fuentes empaquetadas cambian el ancho de los textos al cargar.
    void document.fonts.ready.then(() => {
      if (!cancelado) {
        medir()
      }
    })
    return () => {
      cancelado = true
      observador.disconnect()
    }
  }, [lista, ruta])

  return { caja, animado }
}

export function App() {
  const lecturas = useLecturas(LECTURAS_A_PEDIR, CONSULTA_CADA_MS)
  const [ruta, setRuta] = useState(rutaActual)
  const principal = useRef<HTMLElement>(null)
  const lista = useRef<HTMLUListElement>(null)
  const primeraVez = useRef(true)
  const { caja, animado } = useIndicador(lista, ruta)

  useEffect(() => {
    const alCambiarHash = () => setRuta(rutaActual())
    window.addEventListener('hashchange', alCambiarHash)
    return () => window.removeEventListener('hashchange', alCambiarHash)
  }, [])

  const vista = VISTAS.find((v) => v.ruta === ruta) ?? VISTAS[0]!
  const Contenido = vista.componente

  const enfocarTitulo = () => principal.current?.querySelector<HTMLElement>('h1')?.focus()

  useEffect(() => {
    document.title = `${vista.nombre} · AutoList`
    // Al navegar, el foco pasa al título de la vista (no en la carga inicial),
    // para que lectores de pantalla y teclado noten el cambio.
    if (primeraVez.current) {
      primeraVez.current = false
      return
    }
    enfocarTitulo()
  }, [vista])

  return (
    <LecturasContexto.Provider value={lecturas}>
      <PlacaProveedor>
        {/* Sin ancla de destino: un hash distinto de #/ruta cambiaría la vista. */}
        <a
          className="salto"
          href="#/"
          onClick={(e) => {
            e.preventDefault()
            enfocarTitulo()
          }}
        >
          Saltar al contenido
        </a>
        <header className="encabezado">
          <div className="contenedor encabezado__interior">
            {/* Wordmark tipográfico, sin símbolo: el verde de "List" es el mismo
                verde de "en vivo" y de la línea de la gráfica (el único acento). */}
            <span className="marca">
              Auto<span className="marca__acento">List</span>
            </span>
            <nav aria-label="Vistas" className="navegacion-contenedor">
              <ul className="navegacion" ref={lista}>
                {VISTAS.map((v) => (
                  <li key={v.ruta}>
                    <a
                      className="navegacion__enlace"
                      href={`#/${v.ruta}`}
                      aria-current={v.ruta === ruta ? 'page' : undefined}
                    >
                      {v.nombre}
                      {v.pronto && <span className="navegacion__pronto"> Pronto</span>}
                    </a>
                  </li>
                ))}
              </ul>
              {caja !== null && (
                <span
                  className={`navegacion__indicador${animado ? ' navegacion__indicador--animado' : ''}`}
                  style={{ transform: `translate(${caja.x}px, ${caja.y}px)`, width: caja.ancho, height: caja.alto }}
                  aria-hidden="true"
                />
              )}
            </nav>
            <IndicadorEstado />
            <SelectorTema />
          </div>
        </header>
        <main ref={principal} className="principal">
          {/* key: al cambiar de vista se remonta y entra con un fundido corto. */}
          <div key={ruta} className="vista">
            <Contenido />
          </div>
        </main>
      </PlacaProveedor>
    </LecturasContexto.Provider>
  )
}
