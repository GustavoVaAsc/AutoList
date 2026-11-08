import { AVISO_PRIVACIDAD, formatearEdadCuadro, textoEstadoPrevia } from '../api/previa'
import type { EstadoPrevia } from '../usePrevia'

interface Props {
  estado: EstadoPrevia
}

/**
 * Panel de la vista previa de la cámara: el cuadro más reciente que envía la
 * placa, su antigüedad y el estado, con el aviso de privacidad debajo. Solo es
 * visual: el frontend no pide fotos a la cámara ni guarda nada.
 *
 * El estado va en palabras (el color no informa solo) y la antigüedad del cuadro
 * queda fuera de la región viva, porque cambia cada segundo y un lector de
 * pantalla la leería sin parar.
 */
export function VistaPrevia({ estado }: Props) {
  const { resultado, imagenUrl, reintentar } = estado
  const { titulo, detalle } = textoEstadoPrevia(resultado)
  const hayCuadro = resultado !== null && resultado.tipo === 'cuadro'
  // La imagen solo se muestra mientras el estado dice que hay cuadro: si el borde
  // falla, el marco queda vacío en vez de mostrar un cuadro viejo sin decir
  // cuántos segundos tiene.
  const imagen = hayCuadro ? imagenUrl : null
  const edad = hayCuadro ? formatearEdadCuadro(resultado.edadMs) : null
  const sinSesion = resultado !== null && resultado.tipo === 'sin-sesion'

  return (
    <div className="previa">
      <div className="previa__marco">
        {imagen !== null ? (
          <img className="previa__imagen" src={imagen} alt="Cuadro en vivo de la cámara del aula, para ver la colocación del alumno" />
        ) : (
          <p className="previa__vacia">Sin cuadro</p>
        )}
      </div>
      <div className="previa__texto">
        <h2 className="previa__titulo">Vista previa de la cámara</h2>
        <div role="status">
          <p className="previa__estado">{titulo}</p>
          <p>{detalle}</p>
        </div>
        {hayCuadro && edad !== null && <p className="etiqueta-tecnica">Cuadro de hace {edad}</p>}
        {hayCuadro && edad === null && <p className="etiqueta-tecnica">El borde no informa la antigüedad del cuadro.</p>}
        {sinSesion && (
          <button type="button" className="boton" onClick={reintentar}>
            Reintentar
          </button>
        )}
        <p className="previa__aviso etiqueta-tecnica">{AVISO_PRIVACIDAD}</p>
      </div>
    </div>
  )
}
