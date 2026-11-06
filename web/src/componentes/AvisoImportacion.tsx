import { resumirConteos } from '../api/alumnosRespuestas'
import type { AvisoImportacion as Aviso } from '../useImportacion'

/**
 * Lo que queda a la vista cuando una importación no terminó en éxito. Es una
 * alerta (`role="alert"`): la persona la provocó y necesita enterarse. Los
 * textos de la API que hablan en términos de `?existentes=` no se muestran:
 * son para quien usa curl, no para el profesor. Los nombres de archivo y los
 * motivos se pintan como texto.
 */
export function AvisoImportacion({ aviso }: { aviso: Aviso }) {
  if (aviso.tipo === 'carrera') {
    const { conteos } = aviso
    return (
      <div className="aviso aviso--advertencia aviso--entra" role="alert">
        <p className="aviso__titulo">Se importó solo una parte del archivo</p>
        <p>
          Otra importación agregó cuentas del archivo mientras esta se escribía. Quedó escrito esto: {resumirConteos(conteos)}, de{' '}
          {conteos.total} {conteos.total === 1 ? 'fila' : 'filas'}. Las omitidas son cuentas que la otra importación ya había agregado y no se
          tocaron. Para actualizar sus nombres o grupos, vuelve a elegir el archivo y decide qué hacer con las cuentas existentes.
        </p>
      </div>
    )
  }
  if (aviso.tipo === 'filas-invalidas') {
    const { filas, totalErrores } = aviso
    return (
      <div className="aviso aviso--error aviso--entra" role="alert">
        <p className="aviso__titulo">No se importó nada</p>
        <p>{aviso.error}</p>
        {filas.length > 0 && (
          <>
            {totalErrores > filas.length && (
              <p>
                Se muestran las primeras {filas.length} de {totalErrores} filas con error.
              </p>
            )}
            <ul className="aviso__filas" tabIndex={0} aria-label="Filas con error">
              {filas.map((f, posicion) => (
                <li key={`${f.fila}-${posicion}`}>
                  <span className="aviso__fila">Fila {f.fila}</span> {f.error}
                </li>
              ))}
            </ul>
          </>
        )}
      </div>
    )
  }
  return (
    <div className="aviso aviso--error aviso--entra" role="alert">
      <p className="aviso__titulo">{aviso.tipo === 'rechazo-local' ? 'No se subió el archivo' : 'No se pudo importar'}</p>
      <p>{aviso.mensaje}</p>
    </div>
  )
}
