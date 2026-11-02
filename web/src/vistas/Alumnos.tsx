import { EstadoVacio } from '../componentes/EstadoVacio'

export function Alumnos() {
  return (
    <>
      <h1 className="titulo-vista" tabIndex={-1}>Alumnos</h1>
      <p className="subtitulo-vista">Lista por grupo con el estado del rostro de cada alumno.</p>
      <EstadoVacio titulo="Esta vista llega en un avance posterior">
        Todavía no se puede ver la lista de alumnos ni importar la lista maestra, y no hay datos que mostrar. Por ahora, Métricas y asistencia es la única vista disponible.
      </EstadoVacio>
    </>
  )
}
