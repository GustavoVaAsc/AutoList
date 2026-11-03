import { EstadoVacio } from '../componentes/EstadoVacio'
import { Pagina } from '../componentes/Pagina'

export function Registro() {
  return (
    <Pagina titulo="Registro" descripcion="Alta del rostro de un alumno, con su consentimiento.">
      <EstadoVacio
        grande
        titulo="El registro llega en un avance posterior"
        accion={
          <a className="boton" href="#/alumnos">
            Ir a Alumnos
          </a>
        }
      >
        Todavía no se puede registrar a nadie desde aquí. Mientras tanto, importa la lista maestra y revisa en Alumnos quién tiene el rostro pendiente.
      </EstadoVacio>
    </Pagina>
  )
}
