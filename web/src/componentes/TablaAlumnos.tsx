import { useEffect, useState } from 'react'
import type { Alumno } from '../api/alumnosRespuestas'
import { MarcaAsistencia } from './MarcaAsistencia'

interface Props {
  alumnos: Alumno[]
  /** Cuentas cuyo rostro cambió en la última consulta. */
  cambiadas: readonly string[]
  /** Id del encabezado que titula la tabla. */
  idTitulo: string
}

// Los renglones se escriben solo al montarse la tabla (la primera carga de la
// vista), de arriba abajo. Después, ni el sondeo ni el filtro repiten la
// entrada. El escalón es de 40 ms (cada renglón dura --d-fila, 360 ms, en
// estilos.css) y solo lo llevan los 12 primeros: el resto se pinta sin
// animación (una lista de 500 filas no debe crear 500 animaciones) y, al
// quedar fuera de la primera pantalla, no se nota.
const ESCALON_MS = 40
const ESCALONES_MAXIMOS = 11
const ENTRADA_TOTAL_MS = ESCALON_MS * ESCALONES_MAXIMOS + 360 + 100
// El resaltado de una fila que cambió dura 1200 ms (estilos.css, resaltar-cambio).
const RESALTE_MS = 1300

const ETIQUETA_ROSTRO = { pendiente: 'Pendiente', registrado: 'Registrado' } as const
const MARCA_ROSTRO = { pendiente: 'pendiente', registrado: 'presente' } as const

/**
 * Lista de alumnos en una tabla semántica, con la forma de una lista impresa:
 * renglones con su regla, columnas alineadas y la marca de asistencia en la
 * columna del rostro. Los nombres y los grupos vienen del CSV: son entrada
 * externa y se pintan solo como texto.
 */
export function TablaAlumnos({ alumnos, cambiadas, idTitulo }: Props) {
  const [entrando, setEntrando] = useState(true)
  const [resaltadas, setResaltadas] = useState<ReadonlySet<string>>(new Set())

  useEffect(() => {
    const id = window.setTimeout(() => setEntrando(false), ENTRADA_TOTAL_MS)
    return () => window.clearTimeout(id)
  }, [])

  // El atributo de resaltado se quita al terminar la animación: cambiar de filtro
  // dentro del plazo de una consulta no debe repetirla.
  useEffect(() => {
    if (cambiadas.length === 0) {
      setResaltadas((previas) => (previas.size === 0 ? previas : new Set()))
      return
    }
    setResaltadas(new Set(cambiadas))
    const id = window.setTimeout(() => setResaltadas(new Set()), RESALTE_MS)
    return () => window.clearTimeout(id)
  }, [cambiadas])

  return (
    <table className="tabla tabla--alumnos" aria-labelledby={idTitulo}>
      <thead>
        <tr>
          {/* Numeración de los renglones, como en la hoja impresa: es decorativa, por eso no la leen los lectores de pantalla (cabecera y celdas). */}
          <th scope="col" className="tabla__n" aria-hidden="true">N.º</th>
          <th scope="col">Cuenta</th>
          <th scope="col" className="tabla__nombre-col">Nombre</th>
          <th scope="col">Grupo</th>
          <th scope="col">Rostro</th>
        </tr>
      </thead>
      <tbody>
        {alumnos.map((a, posicion) => {
          const entra = entrando && posicion <= ESCALONES_MAXIMOS
          return (
            <tr
              key={a.cuenta}
              className={entra ? 'tabla__fila tabla__fila--entra' : 'tabla__fila'}
              style={entra ? { animationDelay: `${posicion * ESCALON_MS}ms` } : undefined}
              data-cambio={resaltadas.has(a.cuenta) ? '' : undefined}
            >
              <td className="tabla__n" aria-hidden="true">{posicion + 1}</td>
              <th scope="row" className="tabla__cuenta">{a.cuenta}</th>
              <td className="tabla__texto tabla__nombre">
                {a.nombre}
                {/* Solo en pantallas angostas (estilos.css): cuenta y grupo pasan debajo del nombre y sus columnas se ocultan. */}
                <span className="tabla__sub">
                  {a.cuenta} · <bdi>{a.grupo}</bdi>
                </span>
              </td>
              <td className="tabla__grupo">{a.grupo}</td>
              <td>
                {/* El texto lleva el estado; la marca y el color son redundantes. */}
                <span className={`rostro rostro--${a.rostro}`}>
                  <MarcaAsistencia tipo={MARCA_ROSTRO[a.rostro]} />
                  {ETIQUETA_ROSTRO[a.rostro]}
                </span>
              </td>
            </tr>
          )
        })}
      </tbody>
    </table>
  )
}
