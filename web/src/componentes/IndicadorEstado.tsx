import { useEffect, useRef, useState } from 'react'
import { useEstadoPlaca, type EstadoPlaca } from '../estadoPlaca'
import { useEstadoLecturas } from '../lecturasContexto'

const ETIQUETA: Record<EstadoPlaca, string> = {
  conectando: 'Conectando con la placa',
  vivo: 'En vivo',
  obsoleto: 'Sin lecturas recientes',
  'sin-conexion': 'Sin conexión con la placa',
}

/**
 * Estado real de la placa, a partir del sondeo: en vivo (última lectura con
 * menos de 60 s según la hora de la placa), sin lecturas recientes o sin
 * conexión. El texto lleva la información; el punto es redundante.
 * Movimiento: un pulso, una sola vez, cuando llega una lectura nueva.
 * Anuncio para lectores de pantalla: solo aquí (región oculta), y solo cuando
 * el estado cambia; el resto de la interfaz no anuncia estados.
 */
export function IndicadorEstado() {
  const { tipo, edadMs } = useEstadoPlaca()
  const { lecturas } = useEstadoLecturas()
  const [pulsos, setPulsos] = useState(0)
  const [pulsando, setPulsando] = useState(false)
  const [anuncio, setAnuncio] = useState('')

  // Pulso: depende solo de que llegue una lectura nueva, no del estado.
  const ultimaHora = lecturas?.[lecturas.length - 1]?.t ?? null
  const anterior = useRef<number | null>(null)
  useEffect(() => {
    if (ultimaHora !== null && anterior.current !== null && ultimaHora !== anterior.current) {
      setPulsos((n) => n + 1)
      // Con movimiento reducido no hay animación y `onAnimationEnd` nunca llegaría a limpiar el estado.
      if (!window.matchMedia('(prefers-reduced-motion: reduce)').matches) setPulsando(true)
    }
    anterior.current = ultimaHora
  }, [ultimaHora])

  // La región live se monta vacía y se llena al cambiar el estado: no anuncia
  // el "Conectando" inicial al cargar la página.
  const tipoAnterior = useRef(tipo)
  useEffect(() => {
    if (tipo !== tipoAnterior.current) {
      tipoAnterior.current = tipo
      setAnuncio(`Estado de la placa: ${ETIQUETA[tipo]}`)
    }
  }, [tipo])

  const texto = tipo === 'vivo' && edadMs !== null ? `En vivo · hace ${Math.floor(edadMs / 1000)} s` : ETIQUETA[tipo]

  return (
    <div className="estado-vivo">
      {/* key: cada lectura nueva remonta el punto y la animación corre una vez;
          al terminar se limpia `pulsando`, así un cambio de estado no la repite. */}
      <span
        key={pulsos}
        className={`punto punto--${tipo}${pulsando ? ' punto--pulso' : ''}`}
        aria-hidden="true"
        onAnimationEnd={() => setPulsando(false)}
      />
      <span className="estado-vivo__texto" aria-hidden="true">{texto}</span>
      <span className="solo-lectores" role="status">{anuncio}</span>
    </div>
  )
}
