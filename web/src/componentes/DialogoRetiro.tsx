import { useState } from 'react'
import type { Alumno } from '../api/alumnosRespuestas'
import { useDialogo } from '../useDialogo'

interface Props {
  /** El alumno cuyo rostro se va a retirar; null cierra el diálogo. */
  alumno: Alumno | null
  /** Hay un retiro esperando respuesta: no se puede confirmar otra vez ni cancelar. */
  retirando: boolean
  alConfirmar: () => void
  alCancelar: () => void
}

/**
 * Confirmación de "Retirar rostro" (DELETE /api/alumnos/:cuenta/rostro). Es una acción
 * destructiva: borra datos biométricos y no se puede deshacer. Usa <dialog> nativo con
 * showModal(), con el mismo movimiento y la misma mecánica que DialogoExistentes (ver
 * useDialogo): el navegador deja inerte el resto de la página, devuelve el foco al botón
 * que lo abrió y entrega Escape como `cancel`. Foco inicial en "Cancelar", la única
 * acción que no cambia nada. Sin cierre por clic en el fondo, para que un clic suelto no
 * decida por la persona. Mientras se retira, los botones y Escape no hacen nada
 * (`aria-disabled`, no `disabled`, para no perder el foco).
 */
export function DialogoRetiro({ alumno, retirando, alConfirmar, alCancelar }: Props) {
  const abierto = alumno !== null
  const { dialogo, visible, alCerrar } = useDialogo(abierto)
  // El contenido sobrevive a la salida: `alumno` ya es null mientras el diálogo se desvanece.
  const [mostrado, setMostrado] = useState(alumno)
  if (alumno !== null && alumno !== mostrado) {
    setMostrado(alumno)
  }

  return (
    <dialog
      ref={dialogo}
      className="dialogo"
      data-visible={visible ? '' : undefined}
      aria-labelledby="titulo-retiro"
      aria-describedby="descripcion-retiro"
      onCancel={(e) => {
        // Escape: no lo cierra el navegador; lo cierra el estado, con su animación.
        e.preventDefault()
        if (!retirando) {
          alCancelar()
        }
      }}
      onClose={alCerrar}
    >
      {mostrado !== null && (
        <div className="dialogo__interior">
          <h2 id="titulo-retiro" className="dialogo__titulo">
            Retirar el rostro de <bdi>{mostrado.nombre}</bdi>
          </h2>
          <p id="descripcion-retiro">
            Se borran el rostro guardado (el vector con que se reconoce al alumno) y su consentimiento. El alumno sigue en la lista. Para volver a
            registrarlo hará falta un consentimiento nuevo y repetir las cinco fotos. Esta acción no se puede deshacer.
          </p>
          <p className="etiqueta-tecnica">
            Cuenta {mostrado.cuenta} · Grupo <bdi>{mostrado.grupo}</bdi>
          </p>
          <div className="dialogo__acciones">
            <button type="button" className="boton" data-foco-inicial aria-disabled={retirando} onClick={() => !retirando && alCancelar()}>
              Cancelar
            </button>
            <button type="button" className="boton" aria-disabled={retirando} onClick={() => !retirando && alConfirmar()}>
              Retirar rostro
            </button>
          </div>
          {retirando && (
            <p role="status" className="etiqueta-tecnica">
              Retirando el rostro
            </p>
          )}
        </div>
      )}
    </dialog>
  )
}
