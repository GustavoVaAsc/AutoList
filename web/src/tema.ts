import { useCallback, useState } from 'react'

export type PreferenciaTema = 'sistema' | 'claro' | 'oscuro'

// Misma clave y valores que public/tema.js, que aplica el tema antes de pintar.
const CLAVE = 'autolist-tema'
const VALOR_ATRIBUTO = { claro: 'light', oscuro: 'dark' } as const

function preferenciaActual(): PreferenciaTema {
  const atributo = document.documentElement.getAttribute('data-theme')
  return atributo === 'light' ? 'claro' : atributo === 'dark' ? 'oscuro' : 'sistema'
}

/** Tema elegido por la persona; "sistema" sigue prefers-color-scheme (en CSS). */
export function useTema(): [PreferenciaTema, (nueva: PreferenciaTema) => void] {
  const [preferencia, setPreferencia] = useState<PreferenciaTema>(preferenciaActual)

  const elegir = useCallback((nueva: PreferenciaTema) => {
    if (nueva === 'sistema') {
      document.documentElement.removeAttribute('data-theme')
    } else {
      document.documentElement.setAttribute('data-theme', VALOR_ATRIBUTO[nueva])
    }
    setPreferencia(nueva)
    try {
      if (nueva === 'sistema') {
        localStorage.removeItem(CLAVE)
      } else {
        localStorage.setItem(CLAVE, VALOR_ATRIBUTO[nueva])
      }
    } catch (error) {
      // Sin almacenamiento (modo privado o bloqueado): el tema vale solo en esta sesión.
      console.warn('No se pudo guardar el tema elegido:', error)
    }
  }, [])

  return [preferencia, elegir]
}
