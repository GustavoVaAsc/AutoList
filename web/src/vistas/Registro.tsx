import { EstadoVacio } from '../componentes/EstadoVacio'

export function Registro() {
  return (
    <>
      <h1 className="titulo-vista" tabIndex={-1}>Registro</h1>
      <p className="subtitulo-vista">Registro del rostro de un alumno, con su consentimiento.</p>
      <EstadoVacio titulo="Esta vista llega en un avance posterior">
        Todavía no se puede registrar a ningún alumno desde aquí, y no hay datos que mostrar. Por ahora, Métricas y asistencia es la única vista disponible.
      </EstadoVacio>
    </>
  )
}
