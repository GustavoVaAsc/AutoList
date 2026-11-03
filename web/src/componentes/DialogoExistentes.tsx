import { useEffect, useRef, useState } from 'react'
import type { Decision } from '../api/alumnosRespuestas'
import type { PorDecidir } from '../useImportacion'

interface Props {
  /** Las cuentas por decidir; null cierra el diálogo. */
  datos: PorDecidir | null
  /** Hay un reenvío en curso: no se puede decidir otra vez ni cancelar. */
  enviando: boolean
  alDecidir: (decision: Decision) => void
  alCancelar: () => void
}

// Si la transición de salida no avisa (pestaña oculta, estilos que no cargaron), se cierra igual.
const SALIDA_MAXIMA_MS = 400

/**
 * Diálogo del 409 con escrito:false: ya existen cuentas del archivo y la
 * decisión es del profesor. Usa <dialog> nativo con showModal(): el navegador
 * deja inerte el resto de la página, atrapa el foco, devuelve el foco al botón
 * que lo tenía y entrega Escape como evento `cancel`; nada de eso se reescribe.
 *
 * Movimiento (estilos.css, `.dialogo`): entra con un resorte corto
 * (transición con curva linear(), 300 ms, un 4.6 % de rebote) y sale en 150 ms
 * con ease-out, más rápido porque la persona ya decidió. Son transiciones y no
 * keyframes, así que se pueden interrumpir: si `datos` vuelve a llegar durante
 * la salida, la transición se revierte desde donde va. La salida la espera el
 * código (transitionend) antes de llamar a close(), porque la salida de
 * <dialog> con CSS puro depende de `overlay`, que Safari y Firefox no animan.
 * Con movimiento reducido las transiciones valen 0 s y se cierra al instante.
 *
 * Foco inicial: "Cancelar", la única acción que no cambia nada; ninguna de las
 * dos decisiones queda como predeterminada.
 */
export function DialogoExistentes({ datos, enviando, alDecidir, alCancelar }: Props) {
  const dialogo = useRef<HTMLDialogElement>(null)
  const [visible, setVisible] = useState(false)
  // El contenido sobrevive a la salida: `datos` ya es null mientras el diálogo se desvanece.
  const [mostrado, setMostrado] = useState(datos)
  if (datos !== null && datos !== mostrado) {
    setMostrado(datos)
  }
  const abierto = datos !== null

  useEffect(() => {
    const el = dialogo.current
    if (el === null) {
      return
    }
    if (abierto) {
      if (!el.open) {
        el.showModal()
      }
      el.querySelector<HTMLElement>('[data-foco-inicial]')?.focus()
      // Fija el estado oculto antes de activar `data-visible`: sin esta lectura el
      // navegador no ve el cambio y la transición no corre.
      void el.offsetWidth
      setVisible(true)
      return
    }
    setVisible(false)
    if (!el.open) {
      return
    }
    let terminado = false
    const cerrar = () => {
      if (!terminado) {
        terminado = true
        el.close()
      }
    }
    if (!(Number.parseFloat(getComputedStyle(el).transitionDuration) > 0)) {
      cerrar()
      return
    }
    const alTerminar = (e: TransitionEvent) => {
      if (e.target === el && e.propertyName === 'opacity') {
        cerrar()
      }
    }
    el.addEventListener('transitionend', alTerminar)
    const respaldo = window.setTimeout(cerrar, SALIDA_MAXIMA_MS)
    return () => {
      terminado = true
      el.removeEventListener('transitionend', alTerminar)
      window.clearTimeout(respaldo)
    }
  }, [abierto])

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
      onClose={() => {
        // Un cierre que no vino del estado (Chrome cierra a la segunda Escape seguida):
        // si la persona aún debe decidir, se vuelve a abrir.
        if (abierto) {
          dialogo.current?.showModal()
        }
      }}
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
