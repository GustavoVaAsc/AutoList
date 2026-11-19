import { useEffect, useState } from 'react'
import { toast } from 'sonner'
import { descargarCsvSesion } from '../api/asistencia'
import {
  ETIQUETA_ESTADO_ASISTENCIA,
  ETIQUETA_METODO_ASISTENCIA,
  ETIQUETA_MOTIVO_INTENTO,
  type DetalleSesion,
  type ResumenSesion,
} from '../api/asistenciaRespuestas'
import { filtrarAlumnos } from '../api/registroRespuestas'
import { formatearConfianza, formatearDiaYHoraCorta, formatearHora } from '../formato'
import { CampoBusqueda } from './CampoBusqueda'
import { Cifra } from './Cifra'
import { EsqueletoTabla } from './EsqueletoTabla'
import { EstadoVacio } from './EstadoVacio'
import { MarcaAsistencia } from './MarcaAsistencia'
import { Vacio } from './Vacio'

// Un <option> solo admite texto, no <bdi>: aislamos el grupo con marcas
// Unicode FSI/PDI para que texto bidireccional no reordene la etiqueta.
const aislar = (texto: string) => `\u2068${texto}\u2069`

const entero = (n: number) => String(Math.round(n))

const RESALTE_MS = 1300

interface PropsHoja {
  sesiones: ResumenSesion[]
  detalle: DetalleSesion | null
  idSeleccionado: number | null
  cargandoDetalle: boolean
  cambiadas: readonly string[]
  alSeleccionarSesion: (id: number) => void
}

function etiquetaOpcionSesion(s: ResumenSesion): string {
  const fecha = formatearDiaYHoraCorta(Date.parse(s.inicio))
  const estado = s.abierta ? 'En curso' : 'Cerrada'
  return `${fecha} · ${aislar(s.grupo)} · ${estado}`
}

export function HojaAsistencia({
  sesiones,
  detalle,
  idSeleccionado,
  cargandoDetalle,
  cambiadas,
  alSeleccionarSesion,
}: PropsHoja) {
  const [busqueda, setBusqueda] = useState('')
  const [descargandoId, setDescargandoId] = useState<number | null>(null)
  const [resaltadas, setResaltadas] = useState<ReadonlySet<string>>(new Set())

  useEffect(() => {
    if (cambiadas.length === 0) {
      setResaltadas((previas) => (previas.size === 0 ? previas : new Set()))
      return
    }
    setResaltadas(new Set(cambiadas))
    const id = window.setTimeout(() => setResaltadas(new Set()), RESALTE_MS)
    return () => window.clearTimeout(id)
  }, [cambiadas])

  const sesion =
    detalle?.sesion ?? sesiones.find((s) => s.id === idSeleccionado) ?? sesiones[0] ?? null

  if (sesion === null) {
    return null
  }

  const alumnos = detalle?.sesion.id === sesion.id ? detalle.alumnos : []
  const visibles = filtrarAlumnos(alumnos, busqueda)
  const buscando = busqueda.trim() !== ''
  const descargando = descargandoId === sesion.id

  async function manejarDescargaCsv(idSesion: number) {
    if (descargandoId !== null) return
    setDescargandoId(idSesion)
    const resultado = await descargarCsvSesion(idSesion)
    setDescargandoId(null)
    if (!resultado.ok) {
      toast.error('No se pudo descargar el CSV', { description: resultado.mensaje })
      return
    }
    const url = URL.createObjectURL(resultado.blob)
    const enlace = document.createElement('a')
    enlace.href = url
    enlace.download = resultado.nombreArchivo
    document.body.appendChild(enlace)
    enlace.click()
    enlace.remove()
    window.setTimeout(() => URL.revokeObjectURL(url), 1000)
  }

  return (
    <>
      <section
        className="hoja hoja--asistencia"
        aria-labelledby="titulo-asistencia"
        aria-busy={cargandoDetalle ? 'true' : undefined}
      >
        <header className="hoja__encabezado">
          <div className="hoja__barra">
            <h2 id="titulo-asistencia" className="hoja__titulo">
              Lista de asistencia
            </h2>
            <div className="hoja__descarga">
              {sesion.abierta && (
                <span id="ayuda-descarga" className="etiqueta-tecnica">
                  El CSV estará al cerrar la sesión.
                </span>
              )}
              <button
                type="button"
                className="boton"
                aria-disabled={sesion.abierta || descargando}
                aria-describedby={sesion.abierta ? 'ayuda-descarga' : undefined}
                onClick={() => {
                  if (!sesion.abierta && !descargando) {
                    void manejarDescargaCsv(sesion.id)
                  }
                }}
              >
                {descargando ? 'Descargando…' : 'Descargar CSV'}
              </button>
            </div>
          </div>

          <p className="etiqueta-tecnica hoja__subtitulo-linea">
            <bdi>{sesion.materia}</bdi> · Grupo <bdi>{sesion.grupo}</bdi> ·{' '}
            {formatearHora(Date.parse(sesion.inicio))}
            {sesion.fin !== null ? ` a ${formatearHora(Date.parse(sesion.fin))}` : ' (en curso)'} · Tolerancia{' '}
            {sesion.tolMin} min
          </p>

          <div className="campos">
            <div className="campo campo--sesion">
              <label htmlFor="selector-sesion" className="campo__etiqueta">
                Sesión
              </label>
              <select
                id="selector-sesion"
                className="selector selector--campo"
                value={idSeleccionado ?? sesion.id}
                onChange={(e) => {
                  const siguiente = Number.parseInt(e.target.value, 10)
                  if (Number.isFinite(siguiente)) {
                    alSeleccionarSesion(siguiente)
                  }
                }}
              >
                {sesiones.map((s) => (
                  <option key={s.id} value={s.id}>
                    {etiquetaOpcionSesion(s)}
                  </option>
                ))}
              </select>
            </div>

            {alumnos.length > 0 && (
              <CampoBusqueda
                id="busqueda-asistencia"
                etiqueta="Buscar por nombre o cuenta"
                valor={busqueda}
                alCambiar={setBusqueda}
              />
            )}
          </div>

          {alumnos.length > 0 && buscando && (
            <p role="status" className="solo-lectores">
              {visibles.length === 0
                ? 'Ningún alumno coincide con la búsqueda'
                : `${visibles.length} ${visibles.length === 1 ? 'alumno coincide' : 'alumnos coinciden'} con la búsqueda`}
            </p>
          )}

          <dl className="resumen">
            <div className="resumen__item">
              <dt className="campo__etiqueta">Inscritos</dt>
              <dd className="resumen__valor">
                {sesion.totalGrupo}
              </dd>
            </div>
            <div className="resumen__item">
              <dt className="campo__etiqueta">Presentes</dt>
              <dd className="resumen__valor resumen__valor--presente">
                <MarcaAsistencia tipo="presente" />
                {sesion.presentes}
              </dd>
            </div>
            <div className="resumen__item">
              <dt className="campo__etiqueta">Retardos</dt>
              <dd className="resumen__valor resumen__valor--retardo">
                <MarcaAsistencia tipo="retardo" />
                {sesion.retardos}
              </dd>
            </div>
            {sesion.abierta ? (
              <div className="resumen__item">
                <dt className="campo__etiqueta">Pendientes</dt>
                <dd className="resumen__valor">
                  <MarcaAsistencia tipo="pendiente" />
                  {sesion.pendientes}
                </dd>
              </div>
            ) : (
              <div className="resumen__item">
                <dt className="campo__etiqueta">Faltas</dt>
                <dd className="resumen__valor resumen__valor--falta">
                  <MarcaAsistencia tipo="falta" />
                  {sesion.faltas}
                </dd>
              </div>
            )}
          </dl>
        </header>

        {detalle === null || detalle.sesion.id !== sesion.id ? (
          <EsqueletoTabla etiqueta="Cargando la lista de la sesión" />
        ) : alumnos.length === 0 ? (
          <EstadoVacio anidado titulo="Sin alumnos inscritos en este grupo">
            Importa la lista del grupo en Alumnos.
          </EstadoVacio>
        ) : visibles.length === 0 ? (
          <EstadoVacio
            anidado
            titulo="Ningún alumno coincide"
            accion={
              <button
                type="button"
                className="boton"
                onClick={() => setBusqueda('')}
              >
                Borrar búsqueda
              </button>
            }
          >
            Prueba con otra parte del nombre o de la cuenta.
          </EstadoVacio>
        ) : (
          <div className="hoja__desplazable" role="region" aria-label="Tabla de asistencia" tabIndex={0}>
            <table className="tabla tabla--asistencia" aria-labelledby="titulo-asistencia">
              <thead>
                <tr>
                  <th scope="col" className="tabla__n" aria-hidden="true">
                    N.º
                  </th>
                  <th scope="col">Cuenta</th>
                  <th scope="col" className="tabla__nombre-col">
                    Nombre
                  </th>
                  <th scope="col">Asistencia</th>
                  <th scope="col">Hora</th>
                  <th scope="col" className="tabla__derecha">
                    Similitud
                  </th>
                </tr>
              </thead>
              <tbody>
                {visibles.map((a, posicion) => {
                  const horaTexto = a.hora !== null ? formatearHora(Date.parse(a.hora)) : null
                  const similitudTexto =
                    a.confianza !== null ? formatearConfianza(a.confianza) : null
                  return (
                    <tr
                      key={a.cuenta}
                      className="tabla__fila"
                      data-cambio={resaltadas.has(a.cuenta) ? '' : undefined}
                    >
                      <td className="tabla__n" aria-hidden="true">
                        {posicion + 1}
                      </td>
                      <th scope="row" className="tabla__cuenta">
                        {a.cuenta}
                      </th>
                      <td className="tabla__texto tabla__nombre">
                        <bdi>{a.nombre}</bdi>
                      </td>
                      <td>
                        <span className={`rostro rostro--${a.estado}`}>
                          <MarcaAsistencia tipo={a.estado} />
                          {ETIQUETA_ESTADO_ASISTENCIA[a.estado]}
                        </span>
                      </td>
                      <td className="tabla__hora-celda">
                        {horaTexto !== null ? horaTexto : <Vacio />}
                      </td>
                      <td className="tabla__derecha">
                        {similitudTexto !== null ? (
                          similitudTexto
                        ) : a.metodo === 'manual' ? (
                          <span className="tabla__vacio">{ETIQUETA_METODO_ASISTENCIA.manual}</span>
                        ) : (
                          <Vacio />
                        )}
                      </td>
                    </tr>
                  )
                })}
              </tbody>
            </table>
          </div>
        )}
      </section>

      {detalle !== null && detalle.sesion.id === sesion.id && (
        <section className="hoja hoja--intentos" aria-labelledby="titulo-intentos">
          <div className="intentos__resumen">
            <h2 id="titulo-intentos" className="hoja__subtitulo">
              Reconocimiento
            </h2>
            <dl className="cifras cifras--renglones">
              {sesion.confianza.promedio !== null ? (
                <>
                  <Cifra
                    etiqueta="Similitud promedio"
                    valor={sesion.confianza.promedio}
                    formato={formatearConfianza}
                  />
                  {sesion.confianza.min !== null && (
                    <Cifra
                      etiqueta="Similitud mínima"
                      valor={sesion.confianza.min}
                      formato={formatearConfianza}
                    />
                  )}
                  {sesion.confianza.max !== null && (
                    <Cifra
                      etiqueta="Similitud máxima"
                      valor={sesion.confianza.max}
                      formato={formatearConfianza}
                    />
                  )}
                </>
              ) : (
                <div className="cifra">
                  <dt className="cifra__etiqueta">Similitud promedio</dt>
                  <dd className="cifra__valor">
                    <span className="cifra__numero">
                      <Vacio />
                    </span>
                  </dd>
                </div>
              )}
              {sesion.intentos.total > 0 && (
                <>
                  <Cifra
                    etiqueta={ETIQUETA_MOTIVO_INTENTO.desconocido}
                    valor={sesion.intentos.desconocido}
                    formato={entero}
                  />
                  <Cifra
                    etiqueta={ETIQUETA_MOTIVO_INTENTO.duplicado}
                    valor={sesion.intentos.duplicado}
                    formato={entero}
                  />
                  <Cifra
                    etiqueta={ETIQUETA_MOTIVO_INTENTO.imagen_insuficiente}
                    valor={sesion.intentos.imagenInsuficiente}
                    formato={entero}
                  />
                </>
              )}
            </dl>
          </div>

          <div className="intentos__detalle">
            {detalle.intentos.length === 0 ? (
              <p className="intentos__vacio">Sin intentos rechazados.</p>
            ) : (
              <details className="plegable">
                <summary>Intentos rechazados ({detalle.intentos.length})</summary>
                <div className="hoja__desplazable">
                  <table className="tabla tabla--intentos" aria-label="Intentos rechazados">
                    <thead>
                      <tr>
                        <th scope="col">Hora</th>
                        <th scope="col">Motivo</th>
                        <th scope="col" className="tabla__derecha">
                          Similitud
                        </th>
                      </tr>
                    </thead>
                    <tbody>
                      {detalle.intentos.map((intento) => (
                        <tr key={intento.id} className="tabla__fila">
                          <td className="tabla__hora-celda">
                            {formatearHora(Date.parse(intento.hora))}
                          </td>
                          <td className="tabla__texto">{ETIQUETA_MOTIVO_INTENTO[intento.motivo]}</td>
                          <td className="tabla__derecha">
                            {intento.confianzaMax !== null ? formatearConfianza(intento.confianzaMax) : <Vacio />}
                          </td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              </details>
            )}
          </div>
        </section>
      )}
    </>
  )
}
