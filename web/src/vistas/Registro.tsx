import { EstadoVacio } from '../componentes/EstadoVacio'
import { Hero } from '../componentes/Hero'

export function Registro() {
  return (
    <Hero>
      <h1 className="titulo-vista" tabIndex={-1}>Registro</h1>
      <p className="entradilla">Registro del rostro de un alumno, con su consentimiento.</p>
      <section aria-labelledby="titulo-registro">
        <h2 id="titulo-registro" className="solo-lectores">Registro de un alumno</h2>
        <EstadoVacio titulo="Esta vista llega en un avance posterior">
          Todavía no se puede registrar a ningún alumno desde aquí, y no hay datos que mostrar. Por ahora, Métricas y asistencia es la única vista disponible.
        </EstadoVacio>
      </section>
    </Hero>
  )
}
