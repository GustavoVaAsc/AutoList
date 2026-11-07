import { useEffect, useState } from 'react'
import { toast } from 'sonner'
import { descargarCsvSesion } from '../api/asistencia'
import {
  ETIQUETA_ESTADO_ASISTENCIA,
  ETIQUETA_METODO_ASISTENCIA,
  ETIQUETA_MOTIVO_INTENTO,
  UMBRAL_RECONOCIMIENTO,
  type DetalleSesion,
  type ResumenSesion,
} from '../api/asistenciaRespuestas'
import { filtrarAlumnos } from '../api/registroRespuestas'
import {
  formatearConfianza,
  formatearDiaYHoraCorta,
  formatearFechaYHora,
  formatearHora,
} from '../formato'
import { CampoBusqueda } from './CampoBusqueda'
import { Cifra } from './Cifra'
import { EstadoVacio } from './EstadoVacio'
import { MarcaAsistencia } from './MarcaAsistencia'
import { NumeroAnimado } from './NumeroAnimado'

// Un <option> solo admite texto, no <bdi>: aislamos materia y grupo con marcas
// Unicode FSI/PDI para que texto bidireccional no reordene la etiqueta.
const aislar = (texto: string) => `\u2068${texto}\u2069`

const entero = (n: number) => String(Math.round(n))

const ESCALON_MS = 25
const ESCALONES_MAXIMOS = 7
const ENTRADA_TOTAL_MS = ESCALON_MS * ESCALONES_MAXIMOS + 240 + 80
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
  return `#${s.id} · ${aislar(s.materia)} · Grupo ${aislar(s.grupo)} · ${fecha} (${estado})`
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
  const [entrando, setEntrando] = useState(true)
  const [resaltadas, setResaltadas] = useState<ReadonlySet<string>>(new Set())

  useEffect(() => {
    const id = window.setTimeout(() => setEntrando(false), ENTRADA_TOTAL_MS)
    return () => window.clearTimeout(id)
  }, [])

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
            {sesion.abierta ? (
              <span className="etiqueta-tecnica">
                Sesión en curso · el CSV se genera al cerrar en la pantalla del aula
              </span>
            ) : (
              <button
                type="button"
                className="boton"
                disabled={descargando}
                onClick={() => {
                  void manejarDescargaCsv(sesion.id)
                }}
              >
                {descargando ? 'Descargando CSV…' : `Descargar CSV (#${sesion.id})`}
              </button>
            )}
          </div>

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

            <dl className="campo campo--materia">
              <dt className="campo__etiqueta">Materia y grupo</dt>
              <dd className="campo__valor">
                <bdi>{sesion.materia}</bdi> · Grupo <bdi>{sesion.grupo}</bdi>
              </dd>
            </dl>

            <dl className="campo campo--horario">
              <dt className="campo__etiqueta">Horario y tolerancia</dt>
              <dd className="campo__valor">
                {formatearFechaYHora(Date.parse(sesion.inicio))}
                {sesion.fin !== null
                  ? ` a ${formatearHora(Date.parse(sesion.fin))}`
                  : ' (en curso)'}{' '}
                · {sesion.tolMin} min
              </dd>
            </dl>

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
                <NumeroAnimado valor={sesion.totalGrupo} formato={entero} />
              </dd>
            </div>
            <div className="resumen__item">
              <dt className="campo__etiqueta">Presentes</dt>
              <dd className="resumen__valor resumen__valor--presente">
                <MarcaAsistencia tipo="presente" />
                <NumeroAnimado valor={sesion.presentes} formato={entero} />
              </dd>
            </div>
            <div className="resumen__item">
              <dt className="campo__etiqueta">Retardos</dt>
              <dd className="resumen__valor resumen__valor--retardo">
                <MarcaAsistencia tipo="retardo" />
                <NumeroAnimado valor={sesion.retardos} formato={entero} />
              </dd>
            </div>
            {sesion.abierta ? (
              <div className="resumen__item">
                <dt className="campo__etiqueta">Pendientes</dt>
                <dd className="resumen__valor">
                  <MarcaAsistencia tipo="pendiente" />
                  <NumeroAnimado valor={sesion.pendientes} formato={entero} />
                </dd>
              </div>
            ) : (
              <div className="resumen__item">
                <dt className="campo__etiqueta">Faltas</dt>
                <dd className="resumen__valor resumen__valor--falta">
                  <MarcaAsistencia tipo="falta" />
                  <NumeroAnimado valor={sesion.faltas} formato={entero} />
                </dd>
              </div>
            )}
          </dl>
        </header>

        {detalle === null || detalle.sesion.id !== sesion.id ? (
          <div className="estado">
            <MarcaAsistencia tipo="pendiente" />
            <div className="estado__cuerpo">
              <h3 className="estado__titulo">Cargando la lista de la sesión #{sesion.id}</h3>
              <p className="estado__texto">Consultando asistencias e intentos registrados.</p>
            </div>
          </div>
        ) : alumnos.length === 0 ? (
          <EstadoVacio anidado titulo="Sin alumnos inscritos en este grupo">
            El grupo <bdi>{sesion.grupo}</bdi> no tiene alumnos registrados en el padrón.
          </EstadoVacio>
        ) : visibles.length === 0 ? (
          <EstadoVacio
            anidado
            titulo="Ningún alumno coincide"
            accion={
              <button
                type="button"
                className="boton estado__accion"
                onClick={() => setBusqueda('')}
              >
                Limpiar búsqueda
              </button>
            }
          >
            No hay coincidencias para <bdi>&ldquo;{busqueda.trim()}&rdquo;</bdi> en esta sesión.
          </EstadoVacio>
        ) : (
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
                <th scope="col" className="tabla__hora-col">
                  Hora
                </th>
                <th scope="col" className="tabla__derecha tabla__confianza-col">
                  Similitud
                </th>
              </tr>
            </thead>
            <tbody>
              {visibles.map((a, posicion) => {
                const entra = entrando && posicion <= ESCALONES_MAXIMOS
                const horaTexto = a.hora !== null ? formatearHora(Date.parse(a.hora)) : null
                const similitudTexto =
                  a.confianza !== null ? formatearConfianza(a.confianza) : null
                return (
                  <tr
                    key={a.cuenta}
                    className={entra ? 'tabla__fila tabla__fila--entra' : 'tabla__fila'}
                    style={entra ? { animationDelay: `${posicion * ESCALON_MS}ms` } : undefined}
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
                      <span className="tabla__sub">
                        {a.cuenta}
                        {horaTexto !== null ? ` · ${horaTexto}` : ''}
                        {similitudTexto !== null ? ` · sim. ${similitudTexto}` : ''}
                      </span>
                    </td>
                    <td>
                      <span className={`rostro rostro--${a.estado}`}>
                        <MarcaAsistencia tipo={a.estado} />
                        {ETIQUETA_ESTADO_ASISTENCIA[a.estado]}
                      </span>
                    </td>
                    <td className="tabla__hora-celda">
                      {horaTexto !== null ? (
                        horaTexto
                      ) : (
                        <span className="tabla__vacio">
                          {a.estado === 'pendiente' ? 'En espera' : 'Sin registro'}
                        </span>
                      )}
                    </td>
                    <td className="tabla__derecha tabla__confianza-celda">
                      {similitudTexto !== null ? (
                        similitudTexto
                      ) : a.metodo === 'manual' ? (
                        <span className="tabla__vacio">{ETIQUETA_METODO_ASISTENCIA.manual}</span>
                      ) : (
                        <span className="tabla__vacio">Sin dato</span>
                      )}
                    </td>
                  </tr>
                )
              })}
            </tbody>
          </table>
        )}
      </section>

      {detalle !== null && detalle.sesion.id === sesion.id && (
        <section className="hoja hoja--intentos" aria-labelledby="titulo-intentos">
          <div className="intentos__resumen">
            <h2 id="titulo-intentos" className="hoja__subtitulo">
              Confianza e intentos de reconocimiento (sesión #{sesion.id})
            </h2>
            <dl className="cifras cifras--renglones">
              <Cifra
                etiqueta="Umbral coseno configurado"
                valor={UMBRAL_RECONOCIMIENTO}
                formato={formatearConfianza}
              />
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
                  <dt className="cifra__etiqueta">Similitud en asistencias</dt>
                  <dd className="cifra__valor">
                    <span className="tabla__vacio">Sin marcas faciales</span>
                  </dd>
                </div>
              )}
              <Cifra
                etiqueta="Intentos rechazados"
                valor={sesion.intentos.total}
                formato={entero}
              />
              {sesion.intentos.total > 0 && (
                <>
                  <Cifra
                    etiqueta="Rostro no reconocido o de otro grupo"
                    valor={sesion.intentos.desconocido}
                    formato={entero}
                  />
                  <Cifra
                    etiqueta="Ya tenía asistencia en la sesión"
                    valor={sesion.intentos.duplicado}
                    formato={entero}
                  />
                  <Cifra
                    etiqueta="Imagen insuficiente"
                    valor={sesion.intentos.imagenInsuficiente}
                    formato={entero}
                  />
                </>
              )}
            </dl>
          </div>

          <div className="intentos__detalle">
            <h3 id="titulo-tabla-intentos" className="hoja__subtitulo">
              Registro de intentos rechazados
            </h3>
            {detalle.intentos.length === 0 ? (
              <p className="intentos__vacio">
                No se registraron intentos rechazados en esta sesión.
              </p>
            ) : (
              <table className="tabla tabla--intentos" aria-labelledby="titulo-tabla-intentos">
                <thead>
                  <tr>
                    <th scope="col">Hora</th>
                    <th scope="col">Motivo</th>
                    <th scope="col" className="tabla__derecha">
                      Similitud máx.
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
                        {intento.confianzaMax !== null ? (
                          formatearConfianza(intento.confianzaMax)
                        ) : (
                          <span className="tabla__vacio">Sin rostro</span>
                        )}
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            )}
          </div>
        </section>
      )}
    </>
  )
}
