import { useEffect, useRef, useState } from 'react'

// Si la transición de salida no avisa (pestaña oculta, estilos que no cargaron), se cierra igual.
const SALIDA_MAXIMA_MS = 400

/**
 * Apertura y cierre de un <dialog> nativo con showModal(), con la misma mecánica que
 * DialogoExistentes: el navegador deja inerte el resto de la página, atrapa el foco,
 * devuelve el foco al botón que lo tenía y entrega Escape como evento `cancel`. Entra
 * con `data-visible` (transición del resorte de estilos.css, `.dialogo`) y sale esperando
 * `transitionend` antes de llamar a close(), porque la salida con CSS puro depende de
 * `overlay`, que Safari y Firefox no animan. Con movimiento reducido las transiciones
 * valen 0 s y se cierra al instante.
 *
 * El foco inicial va al elemento con `data-foco-inicial` (la acción que no cambia nada).
 * Devuelve la ref del <dialog>, `visible` para el atributo `data-visible` y `alCerrar`,
 * el manejador de `onClose`: un cierre que no vino del estado (Chrome cierra a la
 * segunda Escape seguida) vuelve a abrir el diálogo si la persona aún debe decidir.
 */
export function useDialogo(abierto: boolean) {
  const dialogo = useRef<HTMLDialogElement>(null)
  const [visible, setVisible] = useState(false)

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

  const alCerrar = () => {
    if (abierto) {
      dialogo.current?.showModal()
    }
  }

  return { dialogo, visible, alCerrar }
}
