import { useEffect, useRef } from 'react'
import type { Alumno } from '../api/alumnosRespuestas'
import { MarcaAsistencia } from './MarcaAsistencia'

interface Props {
  /** Alumnos ya filtrados por la búsqueda y ordenados (pendientes primero). */
  alumnos: Alumno[]
  /** Cuenta elegida, o null. */
  elegida: string | null
  alElegir: (cuenta: string) => void
  /** Hay un envío en curso: no se cambia de alumno. */
  bloqueado: boolean
}

/**
 * Los alumnos como renglones de la hoja, con una casilla de opción cada uno (radio
 * nativo: flechas, espacio y lector de pantalla gratis). Los que ya tienen rostro
 * se ven pero no se pueden elegir: volver a registrarlos exige retirar antes su rostro
 * y dar un consentimiento nuevo (plan, sección 5.4). Los nombres y los grupos vienen
 * del CSV: son entrada externa y se pintan solo como texto, aislados para texto
 * bidireccional.
 */
export function SeleccionAlumno({ alumnos, elegida, alElegir, bloqueado }: Props) {
  const lista = useRef<HTMLDivElement>(null)

  // Al llegar con una cuenta elegida (el enlace de Alumnos), su renglón queda a la vista.
  useEffect(() => {
    if (elegida !== null) {
      lista.current?.querySelector<HTMLElement>('input:checked')?.scrollIntoView({ block: 'nearest' })
    }
  }, [elegida])

  return (
    <fieldset className="eleccion">
      <legend className="solo-lectores">Alumno por registrar</legend>
      <div ref={lista} className="eleccion__lista" role="region" aria-label="Alumnos" tabIndex={0}>
        {alumnos.map((a) => {
          const registrado = a.rostro === 'registrado'
          return (
            <label key={a.cuenta} className={registrado ? 'eleccion__fila eleccion__fila--registrado' : 'eleccion__fila'}>
              <input
                type="radio"
                name="alumno-por-registrar"
                value={a.cuenta}
                checked={elegida === a.cuenta}
                disabled={registrado || bloqueado}
                onChange={() => alElegir(a.cuenta)}
              />
              <span className="eleccion__texto">
                <span className="eleccion__nombre">
                  <bdi>{a.nombre}</bdi>
                </span>
                <span className="etiqueta-tecnica">
                  {a.cuenta} · <bdi>{a.grupo}</bdi>
                </span>
              </span>
              <span className={`rostro rostro--${a.rostro}`}>
                <MarcaAsistencia tipo={registrado ? 'presente' : 'pendiente'} />
                {registrado ? 'Registrado' : 'Pendiente'}
              </span>
            </label>
          )
        })}
      </div>
    </fieldset>
  )
}
