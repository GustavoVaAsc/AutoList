import { useLayoutEffect, useRef } from 'react'
import { CUENTA_CAMBIO_MS, CUENTA_INICIAL_MS, suavizar, valorIntermedio } from '../animacion'

interface Props {
  valor: number
  /** Debe ser estable entre renders (una función de módulo): si cambia, la cuenta se reinicia. */
  formato: (n: number) => string
}

/**
 * Una cifra que cuenta hasta su valor. Propósito: mostrar que el dato es nuevo
 * y de dónde viene (de cero la primera vez, del valor anterior después), y
 * llevar el ojo a la cifra. La primera vez dura 800 ms; cuando el valor cambia
 * en una consulta, 300 ms desde el valor que ya estaba. Con
 * `prefers-reduced-motion` aparece el valor final, sin cuenta.
 *
 * La cuenta escribe el texto con requestAnimationFrame y sin estado de React:
 * un `setState` por fotograma volvería a pintar el componente 50 veces. El
 * texto que cambia va oculto para lectores de pantalla (aria-hidden) y una
 * copia con el valor final es la que se lee.
 */
export function NumeroAnimado({ valor, formato }: Props) {
  const nodo = useRef<HTMLSpanElement>(null)
  // Lo que hay escrito ahora (a medias durante una cuenta); null antes de la primera.
  const escrito = useRef<number | null>(null)

  useLayoutEffect(() => {
    const el = nodo.current
    if (el === null) {
      return
    }
    const primera = escrito.current === null
    const desde = escrito.current ?? 0
    if (desde === valor || window.matchMedia('(prefers-reduced-motion: reduce)').matches) {
      el.textContent = formato(valor)
      escrito.current = valor
      return
    }
    el.textContent = formato(desde)
    const duracion = primera ? CUENTA_INICIAL_MS : CUENTA_CAMBIO_MS
    const inicio = performance.now()
    let id = 0
    const paso = (ahora: number) => {
      const avance = Math.min(1, (ahora - inicio) / duracion)
      const actual = avance === 1 ? valor : valorIntermedio(desde, valor, suavizar(avance))
      escrito.current = actual
      el.textContent = formato(actual)
      if (avance < 1) {
        id = requestAnimationFrame(paso)
      }
    }
    id = requestAnimationFrame(paso)
    return () => cancelAnimationFrame(id)
  }, [valor, formato])

  return (
    <>
      <span ref={nodo} aria-hidden="true" />
      <span className="solo-lectores">{formato(valor)}</span>
    </>
  )
}
