import { useState } from 'react'
import type { Decision } from '../api/alumnosRespuestas'
import { useDialogo } from '../useDialogo'
import type { PorDecidir } from '../useImportacion'

interface Props {
  /** Las cuentas por decidir; null cierra el diálogo. */
  datos: PorDecidir | null
  /** Hay un reenvío en curso: no se puede decidir otra vez ni cancelar. */
  enviando: boolean
  alDecidir: (decision: Decision) => void
  alCancelar: () => void
}

/**
 * Diálogo del 409 con escrito:false: ya existen cuentas del archivo y la
 * decisión es del profesor. Usa <dialog> nativo con showModal() mediante
 * `useDialogo`: el navegador deja inerte el resto de la página, atrapa el foco,
 * devuelve el foco al botón que lo tenía y entrega Escape como evento `cancel`;
 * nada de eso se reescribe.
 *
 * Foco inicial: "Cancelar", la única acción que no cambia nada; ninguna de las
 * dos decisiones queda como predeterminada.
 */
export function DialogoExistentes({ datos, enviando, alDecidir, alCancelar }: Props) {
  const abierto = datos !== null
  const { dialogo, visible, alCerrar } = useDialogo(abierto)
  // El contenido sobrevive a la salida: `datos` ya es null mientras el diálogo se desvanece.
  const [mostrado, setMostrado] = useState(datos)
  if (datos !== null && datos !== mostrado) {
    setMostrado(datos)
  }

  const cambian = mostrado?.existentes.filter((e) => e.cambia).length ?? 0
  const total = mostrado?.existentes.length ?? 0

  return (
    <dialog
      ref={dialogo}
      className="dialogo"
      data-visible={visible ? '' : undefined}
      aria-labelledby="titulo-dialogo"
      aria-describedby="descripcion-dialogo"
      onCancel={(e) => {
        // Escape: no lo cierra el navegador; lo cierra el estado, con su animación.
        e.preventDefault()
        if (!enviando) {
          alCancelar()
        }
      }}
      onClose={alCerrar}
    >
      {mostrado !== null && (
        <div className="dialogo__interior">
          <h2 id="titulo-dialogo" className="dialogo__titulo">
            {total === 1 ? 'Una cuenta del archivo ya existe' : `${total} cuentas del archivo ya existen`}
          </h2>
          <p id="descripcion-dialogo">
            Todavía no se importó nada. Elige qué hacer con las cuentas que ya existen; en los dos casos se importan las nuevas.
            Nunca se borra a nadie ni se toca el rostro ni el consentimiento de nadie.
          </p>
          <ul className="existentes" tabIndex={0} aria-label="Cuentas que ya existen">
            {mostrado.existentes.map((e) => (
              <li key={e.cuenta} className="existentes__item">
                <span className="etiqueta-tecnica">{e.cuenta}</span>
                <span className="existentes__texto">
                  <span className="existentes__actual">
                    <bdi>{e.actual.nombre}</bdi> · <bdi>{e.actual.grupo}</bdi>
                  </span>
                  <span className="etiqueta-tecnica">
                    {e.cambia ? (
                      <>
                        El archivo trae: <bdi>{e.csv.nombre}</bdi> · <bdi>{e.csv.grupo}</bdi>
                      </>
                    ) : (
                      'El archivo trae el mismo nombre y grupo'
                    )}
                    {e.tieneRostro ? '. Tiene rostro registrado' : ''}
                  </span>
                </span>
              </li>
            ))}
          </ul>
          <p className="etiqueta-tecnica">
            <strong>Actualizar nombre y grupo</strong> pone los del archivo en las cuentas existentes (cambian {cambian} de {total}).{' '}
            <strong>Omitir existentes</strong> las deja como están.
          </p>
          <div className="dialogo__acciones">
            <button type="button" className="boton" data-foco-inicial aria-disabled={enviando} onClick={() => !enviando && alCancelar()}>
              Cancelar
            </button>
            <button type="button" className="boton" aria-disabled={enviando} onClick={() => !enviando && alDecidir('omitir')}>
              Omitir existentes
            </button>
            <button type="button" className="boton" aria-disabled={enviando} onClick={() => !enviando && alDecidir('actualizar')}>
              Actualizar nombre y grupo
            </button>
          </div>
          {enviando && (
            <p role="status" className="etiqueta-tecnica">
              Importando el archivo
            </p>
          )}
        </div>
      )}
    </dialog>
  )
}
