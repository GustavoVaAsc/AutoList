import { CONFIRMACION_PUERTA } from '../api/actuadoresRespuestas'
import { useDialogo } from '../useDialogo'

interface Props {
  abierto: boolean
  /** La orden va en camino: no se puede confirmar otra vez ni cancelar. */
  enviando: boolean
  /** Los botones de la hoja están bloqueados (la XIAO quedó fuera de línea, o el panel se enfría): confirmar no hace nada. */
  bloqueado: boolean
  alConfirmar: () => void
  alCancelar: () => void
}

/**
 * Confirmación de "Abrir puerta" (POST /api/actuadores {"servo":"abrir"}): abrirla da paso al aula, así que una
 * pulsación suelta no la abre. Usa <dialog> nativo con showModal(), con la misma mecánica que DialogoRetiro (ver
 * useDialogo): el navegador deja inerte el resto de la página, devuelve el foco al botón que lo abrió y entrega
 * Escape como `cancel`. Foco inicial en "Cancelar", la única acción que no hace nada. Sin cierre por clic en el
 * fondo. Mientras la orden va en camino, los botones y Escape no hacen nada (`aria-disabled`, no `disabled`, para no
 * perder el foco); la hoja cierra el diálogo cuando la API responde y el resultado sale en un aviso.
 */
export function DialogoPuerta({ abierto, enviando, bloqueado, alConfirmar, alCancelar }: Props) {
  const { dialogo, visible, alCerrar } = useDialogo(abierto)
  const inactivo = enviando || bloqueado

  return (
    <dialog
      ref={dialogo}
      className="dialogo"
      data-visible={visible ? '' : undefined}
      aria-labelledby="titulo-puerta"
      aria-describedby="descripcion-puerta"
      onCancel={(e) => {
        // Escape: no lo cierra el navegador; lo cierra el estado, con su animación.
        e.preventDefault()
        if (!enviando) {
          alCancelar()
        }
      }}
      onClose={alCerrar}
    >
      <div className="dialogo__interior">
        <h2 id="titulo-puerta" className="dialogo__titulo">
          {CONFIRMACION_PUERTA.titulo}
        </h2>
        <p id="descripcion-puerta">{CONFIRMACION_PUERTA.descripcion}</p>
        <div className="dialogo__acciones">
          <button type="button" className="boton" data-foco-inicial aria-disabled={enviando} onClick={() => !enviando && alCancelar()}>
            {CONFIRMACION_PUERTA.cancelar}
          </button>
          <button type="button" className="boton boton--primario" aria-disabled={inactivo} onClick={() => !inactivo && alConfirmar()}>
            {CONFIRMACION_PUERTA.confirmar}
          </button>
        </div>
        {enviando && (
          <p role="status" className="etiqueta-tecnica">
            {CONFIRMACION_PUERTA.enviando}
          </p>
        )}
      </div>
    </dialog>
  )
}
