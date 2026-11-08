import { useEffect, useRef, useState } from 'react'
import type { Alumno } from '../api/alumnosRespuestas'
import {
  filtrarAlumnos,
  ordenarParaRegistro,
  resumirRegistro,
  textoTiempo,
  type AvisoRegistro,
  type Registro as DatosRegistro,
} from '../api/registroRespuestas'
import { CampoBusqueda } from '../componentes/CampoBusqueda'
import { EsqueletoTabla } from '../componentes/EsqueletoTabla'
import { EstadoVacio } from '../componentes/EstadoVacio'
import { MarcaAsistencia } from '../componentes/MarcaAsistencia'
import { Pagina } from '../componentes/Pagina'
import { ProgresoRegistro } from '../componentes/ProgresoRegistro'
import { SeleccionAlumno } from '../componentes/SeleccionAlumno'
import { VistaPrevia } from '../componentes/VistaPrevia'
import { formatearHora } from '../formato'
import { useAlumnos } from '../useAlumnos'
import { usePrevia } from '../usePrevia'
import { useRegistro } from '../useRegistro'

// La lista de alumnos de esta vista se consulta como en Alumnos (10 s): el rostro cambia
// cuando termina un registro, un evento raro.
const CONSULTA_ALUMNOS_S = 10

// La materia del grupo, como en Alumnos (plan, línea "Materia" sin el periodo).
const MATERIA = 'Fundamentos de Sistemas Embebidos'

const MARCA_DE_TONO = { neutro: 'pendiente', advertencia: 'pendiente', error: 'falta', exito: 'presente' } as const

/** `#/registro?cuenta=320068234`: la cuenta con que Alumnos manda a esta vista, o null. */
function cuentaDelEnlace(): string | null {
  const consulta = window.location.hash.split('?')[1]
  const cuenta = new URLSearchParams(consulta ?? '').get('cuenta')
  return cuenta !== null && /^[0-9]{9}$/.test(cuenta) ? cuenta : null
}

function AvisoDeAccion({ aviso, alCerrar }: { aviso: AvisoRegistro; alCerrar: () => void }) {
  return (
    <div className={`aviso aviso--${aviso.tono === 'error' ? 'error' : 'advertencia'} aviso--entra`} role="alert">
      <p className="aviso__titulo">{aviso.titulo}</p>
      <p>{aviso.texto}</p>
      <button type="button" className="boton" onClick={alCerrar}>
        Cerrar aviso
      </button>
    </div>
  )
}

/**
 * Registro: alta del rostro de un alumno, compuesta como la hoja de Alumnos (web/DESIGN.md).
 * Con la vista libre, la hoja es la lista para elegir al alumno y el margen lleva el
 * consentimiento y "Iniciar registro". Con un registro abierto (iniciado aquí o retomado al
 * recargar la página), la hoja muestra las cinco poses y el margen, la acción que sigue.
 * El progreso se consulta cada segundo (useRegistro). Mientras la visión no contesta, la
 * vista lo dice y cuenta el tiempo: no simula avance.
 */
export function Registro() {
  const alumnos = useAlumnos(CONSULTA_ALUMNOS_S * 1000)
  const registro = useRegistro()
  // La vista previa de la cámara se consulta cada segundo, como el progreso del
  // registro: es la guía de colocación que el profesor mira mientras el alumno
  // se acomoda.
  const previa = usePrevia()
  const { vista } = registro
  const [busqueda, setBusqueda] = useState('')
  // La cuenta elegida y la cuenta para la que se marcó el consentimiento.
  const [elegida, setElegida] = useState<string | null>(cuentaDelEnlace)
  const [consentida, setConsentida] = useState<string | null>(null)
  const encabezado = useRef<HTMLHeadingElement>(null)
  const faseAnterior = useRef(vista.fase)

  // Elegir a otro alumno borra el consentimiento marcado: es de cada alumno, y volver a uno
  // anterior no lo recupera.
  const elegir = (cuenta: string) => {
    if (cuenta !== elegida) {
      setElegida(cuenta)
      setConsentida(null)
    }
  }

  // Al cambiar de fase (se abre el registro, termina) el botón que tenía el foco desaparece:
  // el foco pasa al título de la hoja, para que teclado y lector de pantalla noten el cambio.
  useEffect(() => {
    if (faseAnterior.current !== vista.fase) {
      if (vista.fase === 'abierto' || vista.fase === 'terminado') {
        encabezado.current?.focus()
      }
      faseAnterior.current = vista.fase
    }
  }, [vista.fase])

  const lista = alumnos.alumnos
  const elegido = lista === null || elegida === null ? null : (lista.find((a) => a.cuenta === elegida) ?? null)
  const elegidoDisponible = elegido !== null && elegido.rostro === 'pendiente'
  const consentimiento = elegida !== null && consentida === elegida
  const puedeIniciar = elegidoDisponible && consentimiento && registro.ocupado === null

  const registroVisible = vista.registro
  const enRegistro = (vista.fase === 'abierto' || vista.fase === 'terminado') && registroVisible !== null
  const grupoDelRegistro = registroVisible === null || lista === null ? null : (lista.find((a) => a.cuenta === registroVisible.cuenta)?.grupo ?? null)

  return (
    <Pagina>
      <div className="rejilla-alumnos">
        <section className="hoja hoja--lista" aria-labelledby="titulo-registro">
          <header className="hoja__encabezado">
            <h1 id="titulo-registro" ref={encabezado} className="hoja__titulo" tabIndex={-1}>
              Registro de rostro
            </h1>
            {enRegistro ? (
              <div className="campos">
                <dl className="campo campo--materia">
                  <dt className="campo__etiqueta">Alumno</dt>
                  <dd className="campo__valor">
                    <bdi>{registroVisible.nombre ?? 'Sin nombre'}</bdi>
                  </dd>
                </dl>
                <dl className="campo">
                  <dt className="campo__etiqueta">Cuenta</dt>
                  <dd className="campo__valor">{registroVisible.cuenta}</dd>
                </dl>
                {grupoDelRegistro !== null && (
                  <dl className="campo">
                    <dt className="campo__etiqueta">Grupo</dt>
                    <dd className="campo__valor">
                      <bdi>{grupoDelRegistro}</bdi>
                    </dd>
                  </dl>
                )}
              </div>
            ) : (
              <>
                <p className="entradilla hoja__entradilla">
                  Elige al alumno y confirma su consentimiento. El servicio de visión toma cinco fotos, una por pose, y guarda el rostro.
                </p>
                <div className="campos campos--dos">
                  <dl className="campo campo--materia">
                    <dt className="campo__etiqueta">Materia</dt>
                    <dd className="campo__valor">{MATERIA}</dd>
                  </dl>
                  {lista !== null && lista.length > 0 && (
                    <CampoBusqueda id="busqueda-registro" etiqueta="Buscar por nombre o cuenta" valor={busqueda} alCambiar={setBusqueda} />
                  )}
                </div>
              </>
            )}
          </header>

          {/* La vista previa va arriba, antes de la lista y de las poses: es la guía de colocación que se mira mientras el alumno se acomoda. */}
          <div className="hoja__cuerpo">
            <VistaPrevia estado={previa} />
          </div>

          {registro.error !== null && (
            <div className="hoja__cuerpo">
              <div className="aviso aviso--error" role="alert">
                <p className="aviso__titulo">{vista.fase === 'comprobando' ? 'No se pudo comprobar el registro' : 'No se pudo actualizar el progreso'}</p>
                <p>
                  {registro.error}{' '}
                  {registro.horaServidor !== null ? `Último dato recibido a las ${formatearHora(registro.horaServidor)}. ` : ''}
                  {registro.sinSesion ? 'La vista no insistirá sola.' : vista.fase === 'abierto' ? 'La vista lo vuelve a intentar cada segundo.' : 'La vista lo vuelve a intentar cada 3 s.'}
                </p>
                {registro.sinSesion && (
                  <button type="button" className="boton" onClick={registro.reintentar}>
                    Reintentar
                  </button>
                )}
              </div>
            </div>
          )}

          {registro.aviso !== null && (
            <div className="hoja__cuerpo">
              <AvisoDeAccion aviso={registro.aviso} alCerrar={registro.descartarAviso} />
            </div>
          )}

          {vista.fase === 'comprobando' && registro.error === null && (
            <div className="hoja__cuerpo" aria-busy="true">
              <p role="status" className="etiqueta-tecnica">
                Comprobando si hay un registro abierto
              </p>
            </div>
          )}

          {(vista.fase === 'libre' || vista.fase === 'iniciando') && (
            <SeccionLista
              alumnos={lista}
              error={alumnos.error}
              busqueda={busqueda}
              alBorrarBusqueda={() => {
                setBusqueda('')
                // El botón desaparece con el estado vacío: el foco pasa al campo de búsqueda.
                document.getElementById('busqueda-registro')?.focus()
              }}
              elegida={elegida}
              alElegir={elegir}
              bloqueado={registro.ocupado !== null}
            />
          )}

          {enRegistro && <SeccionProgreso registro={registroVisible} transcurridoMs={registro.transcurridoMs} alumnos={lista} />}
        </section>

        {vista.fase !== 'comprobando' && (
          <section className="margen" aria-labelledby="titulo-accion">
            {(vista.fase === 'libre' || vista.fase === 'iniciando') && (
              <div className="caja-margen">
                <h2 id="titulo-accion" className="zona__titulo">
                  Consentimiento
                </h2>
                {elegido !== null ? (
                  <p>
                    Alumno elegido: <strong><bdi>{elegido.nombre}</bdi></strong>, cuenta {elegido.cuenta}.
                  </p>
                ) : (
                  <p>{elegida !== null && lista !== null ? `La cuenta ${elegida} del enlace no está en la lista de alumnos.` : 'Elige un alumno de la lista.'}</p>
                )}
                {elegido !== null && !elegidoDisponible && (
                  <p>
                    Este alumno ya tiene rostro registrado. Para volver a registrarlo, retira antes su rostro en <a href="#/alumnos">Alumnos</a>; hará falta un consentimiento nuevo.
                  </p>
                )}
                <label className="casilla">
                  <input
                    type="checkbox"
                    checked={consentimiento}
                    disabled={!elegidoDisponible || registro.ocupado !== null}
                    onChange={(e) => setConsentida(e.target.checked ? elegida : null)}
                  />
                  <span>El alumno firmó el formato de consentimiento</span>
                </label>
                <p className="etiqueta-tecnica">
                  El formato de consentimiento está pendiente de redactar (<code>docs/consentimiento.md</code>). Sin la casilla marcada no se puede iniciar el registro.
                </p>
                <button
                  type="button"
                  className="boton boton--primario"
                  disabled={!puedeIniciar}
                  aria-describedby="ayuda-iniciar"
                  onClick={() => {
                    if (elegida !== null) {
                      // El consentimiento vale para un solo registro: la próxima vez se vuelve a marcar.
                      setConsentida(null)
                      registro.iniciar(elegida)
                    }
                  }}
                >
                  {registro.ocupado === 'iniciar' ? 'Iniciando el registro' : 'Iniciar registro'}
                </button>
                <p id="ayuda-iniciar" className="etiqueta-tecnica">
                  {!elegidoDisponible ? 'Falta elegir un alumno sin rostro registrado.' : !consentimiento ? 'Falta marcar el consentimiento.' : 'Listo para iniciar.'}
                </p>
              </div>
            )}

            {vista.fase === 'abierto' && registroVisible !== null && (
              <div className="caja-margen">
                <h2 id="titulo-accion" className="zona__titulo">
                  Cancelar
                </h2>
                <p>Detiene la captura. No se guarda ningún rostro.</p>
                <button type="button" className="boton" aria-disabled={registro.ocupado !== null} onClick={() => registro.ocupado === null && registro.cancelar()}>
                  {registro.ocupado === 'cancelar' ? 'Cancelando' : 'Cancelar registro'}
                </button>
              </div>
            )}

            {vista.fase === 'terminado' && (
              <div className="caja-margen">
                <h2 id="titulo-accion" className="zona__titulo">
                  Siguiente
                </h2>
                <button type="button" className="boton boton--primario" onClick={registro.descartarResultado}>
                  Registrar a otro alumno
                </button>
                <a className="boton" href="#/alumnos">
                  Ver la lista de alumnos
                </a>
              </div>
            )}
          </section>
        )}
      </div>
    </Pagina>
  )
}

interface PropsLista {
  alumnos: Alumno[] | null
  error: string | null
  busqueda: string
  alBorrarBusqueda: () => void
  elegida: string | null
  alElegir: (cuenta: string) => void
  bloqueado: boolean
}

/** La lista para elegir al alumno: carga, error, vacía, sin coincidencias o los renglones. */
function SeccionLista({ alumnos, error, busqueda, alBorrarBusqueda, elegida, alElegir, bloqueado }: PropsLista) {
  if (alumnos === null) {
    return error === null ? (
      <EsqueletoTabla />
    ) : (
      <div className="hoja__cuerpo">
        <div className="aviso aviso--error" role="alert">
          <p className="aviso__titulo">No se pudo cargar la lista de alumnos</p>
          <p>{error}</p>
        </div>
      </div>
    )
  }
  if (alumnos.length === 0) {
    return (
      <div className="hoja__cuerpo">
        <EstadoVacio
          anidado
          titulo="Todavía no hay alumnos"
          accion={
            <a className="boton" href="#/alumnos">
              Ir a Alumnos
            </a>
          }
        >
          Para registrar un rostro primero hace falta la lista del grupo. Impórtala en Alumnos con el archivo CSV.
        </EstadoVacio>
      </div>
    )
  }
  const visibles = ordenarParaRegistro(filtrarAlumnos(alumnos, busqueda))
  const pendientes = alumnos.filter((a) => a.rostro === 'pendiente').length
  return (
    <>
      {error !== null && (
        <div className="hoja__cuerpo">
          <div className="aviso aviso--error" role="alert">
            <p className="aviso__titulo">No se pudo actualizar la lista de alumnos</p>
            <p>{error} Se muestran los últimos datos recibidos.</p>
          </div>
        </div>
      )}
      <div className="hoja__cuerpo hoja__cuerpo--pie-lista">
        <p className="etiqueta-tecnica" role="status">
          {busqueda.trim() === ''
            ? `${pendientes} ${pendientes === 1 ? 'alumno pendiente' : 'alumnos pendientes'} de ${alumnos.length}. Los pendientes van primero.`
            : visibles.length === 0
              ? 'Ningún alumno coincide con la búsqueda.'
              : `${visibles.length} ${visibles.length === 1 ? 'alumno coincide' : 'alumnos coinciden'} con la búsqueda.`}
        </p>
      </div>
      {visibles.length === 0 ? (
        <div className="hoja__cuerpo">
          <EstadoVacio
            anidado
            titulo="Ningún alumno coincide"
            accion={
              <button type="button" className="boton" onClick={alBorrarBusqueda}>
                Borrar búsqueda
              </button>
            }
          >
            Prueba con otra parte del nombre o de la cuenta. La búsqueda ignora acentos y mayúsculas.
          </EstadoVacio>
        </div>
      ) : (
        <SeleccionAlumno alumnos={visibles} elegida={elegida} alElegir={alElegir} bloqueado={bloqueado} />
      )}
    </>
  )
}

interface PropsProgreso {
  registro: DatosRegistro
  transcurridoMs: number | null
  alumnos: Alumno[] | null
}

/** El resumen del registro, la advertencia de parecido y las cinco poses. */
function SeccionProgreso({ registro, transcurridoMs, alumnos }: PropsProgreso) {
  const resumen = resumirRegistro(registro)
  const tiempo = textoTiempo(registro, transcurridoMs)
  const parecido = registro.advertencia === null ? null : registro.advertencia.parecidoA
  const parecidoA = parecido === null || alumnos === null ? null : (alumnos.find((a) => a.cuenta === parecido) ?? null)
  return (
    <>
      <div className="hoja__cuerpo">
        <div className={`estado-registro estado-registro--${resumen.tono}`}>
          <MarcaAsistencia tipo={MARCA_DE_TONO[resumen.tono]} />
          <div className="estado-registro__texto">
            {/* Solo el título y el detalle son región viva: el tiempo cambia cada segundo y un lector de pantalla lo leería sin parar. */}
            <div role="status">
              <h2 className="estado-registro__titulo">{resumen.titulo}</h2>
              <p>{resumen.detalle}</p>
            </div>
            {tiempo !== null && <p className="etiqueta-tecnica">{tiempo}</p>}
          </div>
        </div>
      </div>

      {registro.advertencia !== null && (
        <div className="hoja__cuerpo">
          <div className="aviso aviso--advertencia aviso--entra" role="status">
            <p className="aviso__titulo">El rostro se parece al de otro alumno</p>
            <p>
              {parecido === null ? (
                'La visión avisó de un parecido con otro alumno, sin decir con cuál.'
              ) : parecidoA !== null ? (
                <>
                  Se parece al de <bdi>{parecidoA.nombre}</bdi> (cuenta {parecido}).
                </>
              ) : (
                <>Se parece al de la cuenta {parecido}.</>
              )}{' '}
              El registro se guarda igual. Si no corresponde, retira el rostro en Alumnos y repite el registro.
            </p>
          </div>
        </div>
      )}

      <h2 id="titulo-poses" className="solo-lectores">
        Poses del registro
      </h2>
      <div className="hoja__desplazable">
        <ProgresoRegistro poses={registro.poses} idTitulo="titulo-poses" />
      </div>
    </>
  )
}
