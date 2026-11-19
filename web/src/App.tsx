import { useEffect, useRef, useState, type ComponentType } from 'react'
import { Avisos } from './componentes/Avisos'
import { IndicadorEstado } from './componentes/IndicadorEstado'
import { SelectorTema } from './componentes/SelectorTema'
import { PlacaProveedor } from './estadoPlaca'
import { LecturasContexto } from './lecturasContexto'
import { SistemaContexto } from './sistemaContexto'
import { useLecturas } from './useLecturas'
import { useSistema } from './useSistema'
import { Alumnos } from './vistas/Alumnos'
import { Asistencia } from './vistas/Asistencia'
import { Metricas } from './vistas/Metricas'
import { Registro } from './vistas/Registro'

interface Vista {
  ruta: string
  nombre: string
  componente: ComponentType
}

// Las cuatro vistas del plan, sección 6.7. La ruta va en el hash (#/metricas)
// para que recargar la página conserve la vista sin que Node-RED tenga que
// resolver rutas del lado del servidor. Después de la ruta puede ir una consulta
// (#/registro?cuenta=320068234): la ruta es lo que va antes del "?", y la vista la lee.
const VISTAS: Vista[] = [
  { ruta: 'alumnos', nombre: 'Alumnos', componente: Alumnos },
  { ruta: 'registro', nombre: 'Registro', componente: Registro },
  { ruta: 'asistencia', nombre: 'Asistencia', componente: Asistencia },
  { ruta: 'metricas', nombre: 'Métricas', componente: Metricas },
]
const VISTA_INICIAL = 'asistencia'

const LECTURAS_A_PEDIR = 100
const CONSULTA_CADA_MS = 5000

function rutaActual(): string {
  const ruta = window.location.hash.replace(/^#\/?/, '').split('?')[0] ?? ''
  return VISTAS.some((v) => v.ruta === ruta) ? ruta : VISTA_INICIAL
}

export function App() {
  const lecturas = useLecturas(LECTURAS_A_PEDIR, CONSULTA_CADA_MS)
  const sistema = useSistema(CONSULTA_CADA_MS)
  const [ruta, setRuta] = useState(rutaActual)
  const principal = useRef<HTMLElement>(null)
  const primeraVez = useRef(true)

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
      <SistemaContexto.Provider value={sistema}>
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
              {/* Wordmark tipográfico, sin símbolo ni logotipo: la palabra en la serif de documento. */}
              <span className="marca">AutoList</span>
              <nav aria-label="Vistas" className="navegacion-contenedor">
                <ul className="navegacion">
                  {VISTAS.map((v) => (
                    <li key={v.ruta}>
                      <a
                        className="navegacion__enlace"
                        href={`#/${v.ruta}`}
                        aria-current={v.ruta === ruta ? 'page' : undefined}
                      >
                        {v.nombre}
                      </a>
                    </li>
                  ))}
                </ul>
              </nav>
              <IndicadorEstado />
              <SelectorTema />
            </div>
          </header>
          <main ref={principal} className="principal">
            {/* key: al cambiar de vista se remonta, y entra con un fundido (estilos.css). */}
            <div key={ruta} className="vista">
              <Contenido />
            </div>
          </main>
          <Avisos />
        </PlacaProveedor>
      </SistemaContexto.Provider>
    </LecturasContexto.Provider>
  )
}
