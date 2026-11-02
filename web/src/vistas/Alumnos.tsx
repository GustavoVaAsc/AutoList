import { EstadoVacio } from '../componentes/EstadoVacio'
import { Hero } from '../componentes/Hero'

export function Alumnos() {
  return (
    <Hero>
      <h1 className="titulo-vista" tabIndex={-1}>Alumnos</h1>
      <p className="entradilla">Lista por grupo con el estado del rostro de cada alumno.</p>
      <section aria-labelledby="titulo-alumnos">
        <h2 id="titulo-alumnos" className="solo-lectores">Lista de alumnos</h2>
        <EstadoVacio titulo="Esta vista llega en un avance posterior">
          Todavía no se puede ver la lista de alumnos ni importar la lista maestra, y no hay datos que mostrar. Por ahora, Métricas y asistencia es la única vista disponible.
        </EstadoVacio>
      </section>
    </Hero>
  )
}
