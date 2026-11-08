import { useState } from 'react'
import { AVISO_PRIVACIDAD, estadoDelPanel, formatearEdadCuadro, textoEstadoPrevia } from '../api/previa'
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
  const { resultado, imagenUrl, numeroCuadro, reintentar } = estado
  // Cuadro que el navegador no pudo pintar (cuerpo vacío o JPEG corrupto). Se
  // guarda el número del cuadro y no su URL: la URL de datos es el contenido del
  // cuadro en base64, así que dos cuadros con los mismos bytes (una escena
  // quieta) tienen la misma URL, y un fallo guardado por URL alcanzaría al
  // cuadro siguiente y dejaría el panel en «Sin cuadro» para siempre.
  const [numeroFallido, setNumeroFallido] = useState<number | null>(null)
  // El fallo vale para el cuadro que falló. Al llegar un cuadro nuevo, el
  // anterior ya no dice nada: el marcador se limpia en el render (no en un
  // efecto) para que el fallo de un cuadro no se lleve al siguiente.
  const [cuadroDelMarcador, setCuadroDelMarcador] = useState<number | null>(null)
  if (cuadroDelMarcador !== numeroCuadro) {
    setCuadroDelMarcador(numeroCuadro)
    setNumeroFallido(null)
  }
  // El panel dice la verdad: si el cuadro llegó pero el navegador no pudo
  // mostrarlo, es un estado propio, no «no hay cuadro».
  const estadoVisible = estadoDelPanel(resultado, numeroCuadro, numeroFallido)
  const { titulo, detalle } = textoEstadoPrevia(estadoVisible)
  const hayCuadro = estadoVisible !== null && estadoVisible.tipo === 'cuadro'
  const noPintado = estadoVisible !== null && estadoVisible.tipo === 'cuadro-no-pintado'
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
          // La clave es el número del cuadro: así el navegador monta la imagen
          // de nuevo en cada cuadro y reintenta la carga aunque los bytes sean
          // los mismos que los del cuadro anterior.
          <img
            key={numeroCuadro}
            className="previa__imagen"
            src={imagen}
            alt="Cuadro en vivo de la cámara del aula, para ver la colocación del alumno"
            onError={() => setNumeroFallido(numeroCuadro)}
          />
        ) : (
          <p className="previa__vacia">{noPintado ? 'No se pudo mostrar el cuadro' : 'Sin cuadro'}</p>
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
