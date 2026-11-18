import { textoCaptura, type Captura } from '../api/registroRespuestas'
import { MarcaAsistencia } from './MarcaAsistencia'

interface Props {
  /** La última foto del registro abierto (`useUltimaCaptura`); null si aún no llega ninguna. */
  captura: Captura | null
}

/**
 * El recuadro de la última foto, debajo de las cinco poses: la miniatura que mandó la cámara
 * (JPEG de hasta 160x120) en grande, con la pose y su estado en palabras debajo, y el motivo si
 * se rechazó. Las miniaturas de cada pose siguen en su renglón. Sin ninguna foto dice que
 * todavía no llega ninguna: no pinta una imagen ni un avance de mentira. Es efímera como las
 * miniaturas: vive en la memoria de la página mientras el registro está abierto.
 */
export function UltimaFoto({ captura }: Props) {
  const texto = captura === null ? null : textoCaptura(captura)
  return (
    <section className="ultima-foto" aria-labelledby="titulo-ultima-foto">
      <h3 id="titulo-ultima-foto" className="ultima-foto__titulo etiqueta-tecnica">
        Última foto
      </h3>
      {/* Un solo aviso, siempre montado y fuera de la figura (que se vuelve a crear con cada foto): el intento hace distinto el texto de dos rechazos seguidos de la misma pose. */}
      <p role="status" className="solo-lectores">
        {captura === null || texto === null ? '' : `${texto.leyenda}${captura.intento > 1 ? `, intento ${captura.intento}` : ''}. ${texto.detalle ?? ''}`.trim()}
      </p>
      {captura === null || texto === null ? (
        <p className="ultima-foto__vacio">Todavía no llega ninguna foto. Aquí aparecerá la última que tome la cámara.</p>
      ) : (
        <figure className={`ultima-foto__figura ultima-foto__figura--${captura.estado}`} key={`${captura.pose}|${captura.estado}|${captura.motivo ?? ''}|${captura.miniatura ?? ''}`}>
          {captura.miniatura !== null ? (
            <img className="ultima-foto__imagen" src={`data:image/jpeg;base64,${captura.miniatura}`} alt={texto.alt} />
          ) : (
            <p className="ultima-foto__sin-imagen" role="img" aria-label={texto.alt}>
              La visión no mandó la imagen de esta foto.
            </p>
          )}
          <figcaption className="ultima-foto__leyenda">
            <span className={`rostro rostro--pose-${captura.estado}`}>
              <MarcaAsistencia tipo={captura.estado === 'aceptada' ? 'presente' : 'falta'} />
              {texto.leyenda}
            </span>
            {texto.detalle !== null && <span className="pose__detalle">{texto.detalle}</span>}
          </figcaption>
        </figure>
      )}
    </section>
  )
}
