import { useState } from 'react'
import { AVISO_PRIVACIDAD, formatearEdadCuadro, textoEstadoPrevia } from '../api/previa'
import type { ResultadoPrevia } from '../api/previa'
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
  // Cuadro que el navegador no pudo cargar (cuerpo vacío o JPEG corrupto). Se
  // guarda la URL y no un booleano, para que el cuadro siguiente, que llega con
  // otra URL de datos, vuelva a mostrarse solo.
  const [urlFallida, setUrlFallida] = useState<string | null>(null)
  const falloCarga = imagenUrl !== null && imagenUrl === urlFallida
  // Si el cuadro no se pudo cargar, el panel lo dice como si no hubiera cuadro:
  // no enseña la antigüedad de un cuadro que no está en la pantalla.
  const estadoVisible: ResultadoPrevia | null = falloCarga ? { tipo: 'sin-cuadro' } : resultado
  const { titulo, detalle } = textoEstadoPrevia(estadoVisible)
  const hayCuadro = estadoVisible !== null && estadoVisible.tipo === 'cuadro'
  // La imagen solo se muestra mientras el estado dice que hay cuadro: si el borde
  // falla, el marco queda vacío en vez de mostrar un cuadro viejo sin decir
  // cuántos segundos tiene.
  const imagen = hayCuadro ? imagenUrl : null
  const edad = hayCuadro ? formatearEdadCuadro(estadoVisible.edadMs) : null
  const sinSesion = estadoVisible !== null && estadoVisible.tipo === 'sin-sesion'

  return (
    <div className="previa">
      <div className="previa__marco">
        {imagen !== null ? (
          <img className="previa__imagen" src={imagen} alt="Cuadro en vivo de la cámara del aula, para ver la colocación del alumno" onError={() => setUrlFallida(imagen)} />
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
