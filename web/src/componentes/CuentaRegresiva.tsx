import { useEffect, useState } from 'react'
import { anuncioContador, contadorFoto, ETIQUETA_POSE, horaDeLaPlaca, poseEnCurso, type Registro } from '../api/registroRespuestas'

interface Props {
  registro: Registro
  /** Hora de la placa en la última respuesta (`useRegistro`). */
  horaServidor: number | null
  /** performance.now() al recibir esa respuesta. */
  recibidoEn: number | null
}

// Cada cuánto se repinta la cuenta: la hora de la placa llega cada segundo, pero el número
// debe cambiar justo cuando toca, no al llegar la siguiente respuesta.
const TIC_MS = 250

/**
 * El margen de Registro con el registro abierto, arriba: la pose que se pide ahora y, en grande,
 * el tiempo que falta para la foto (5, 4, 3, 2, 1), "Tomando foto…" al llegar a 0 o mientras la
 * visión captura, y "Preparando la cámara…" si no hay datos para contar: no inventa números.
 * Solo se repinta esta caja; el temporizador se limpia al desmontarse o al cerrarse el registro.
 * Un lector de pantalla oye solo los cambios de fase (anuncioContador), nunca cada segundo.
 */
export function CuentaRegresiva({ registro, horaServidor, recibidoEn }: Props) {
  const [ahora, setAhora] = useState(() => performance.now())
  const abierto = registro.abierto
  useEffect(() => {
    if (!abierto) {
      return
    }
    const id = window.setInterval(() => setAhora(performance.now()), TIC_MS)
    return () => window.clearInterval(id)
  }, [abierto])

  const contador = contadorFoto(registro, horaDeLaPlaca(horaServidor, recibidoEn === null ? Number.NaN : ahora - recibidoEn))
  const i = poseEnCurso(registro)
  return (
    <>
      {/* Siempre montado, como en UltimaFoto: el texto cambia con la fase, no se inserta ya escrito. */}
      <p role="status" className="solo-lectores">
        {anuncioContador(contador)}
      </p>
      {contador.tipo !== 'ninguno' && (
        <section className="caja-margen contador-foto" aria-labelledby="titulo-contador">
          <h2 id="titulo-contador" className="zona__titulo">
            Foto ahora
          </h2>
          {i !== null && (
            <p className="contador-foto__pose">
              <bdi>{ETIQUETA_POSE[registro.poses[i]!.pose]}</bdi>
            </p>
          )}
          {contador.tipo === 'cuenta' ? (
            <p className="contador-foto__cuenta" aria-hidden="true">
              <span className="contador-foto__texto">Foto en</span>
              <span className="contador-foto__numero" key={contador.segundos}>
                {contador.segundos}
              </span>
            </p>
          ) : (
            <p className="contador-foto__cuenta contador-foto__cuenta--texto" aria-hidden="true">
              {contador.tipo === 'tomando' ? 'Tomando foto…' : 'Preparando la cámara…'}
            </p>
          )}
        </section>
      )}
    </>
  )
}
