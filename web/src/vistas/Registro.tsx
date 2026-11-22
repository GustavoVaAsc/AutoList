import { useEffect, useRef, useState, type ReactNode } from 'react'
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
import { CuentaRegresiva } from '../componentes/CuentaRegresiva'
import { EsqueletoTabla } from '../componentes/EsqueletoTabla'
import { EstadoVacio } from '../componentes/EstadoVacio'
import { MarcaAsistencia } from '../componentes/MarcaAsistencia'
import { Pagina } from '../componentes/Pagina'
import { ProgresoRegistro } from '../componentes/ProgresoRegistro'
import { SeleccionAlumno } from '../componentes/SeleccionAlumno'
import { UltimaFoto } from '../componentes/UltimaFoto'
import { formatearHora } from '../formato'
import { useAlumnos } from '../useAlumnos'
import { useRegistro } from '../useRegistro'
import { useUltimaCaptura } from '../useUltimaCaptura'

// La lista de alumnos de esta vista se consulta como en Alumnos (10 s): el rostro cambia
// cuando termina un registro, un evento raro.
const CONSULTA_ALUMNOS_S = 10

// La materia del grupo, como en Alumnos (plan, línea "Materia" sin el periodo): va de subtítulo.
const MATERIA = 'Fundamentos de Sistemas Embebidos'

const MARCA_DE_TONO = { neutro: 'pendiente', advertencia: 'retardo', error: 'falta', exito: 'presente' } as const

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
 * recargar la página), la hoja muestra las cinco poses y el margen, de arriba abajo, el contador
 * de la foto, la última foto y la acción que sigue (cancelar).
 * El progreso se consulta cada segundo (useRegistro). Mientras el reconocimiento no contesta, la
 * vista lo dice y cuenta el tiempo: no simula avance.
 */
export function Registro() {
  const alumnos = useAlumnos(CONSULTA_ALUMNOS_S * 1000)
  const registro = useRegistro()
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
  // Lo que falta para iniciar, o null si ya se puede.
  const falta: ReactNode | null = !elegidoDisponible ? (
    elegido !== null ? (
      <>
        elegir otro alumno (este ya tiene rostro; retíralo en <a href="#/alumnos">Alumnos</a>).
      </>
    ) : (
      'elegir un alumno.'
    )
  ) : !consentimiento ? (
    'marcar el consentimiento.'
  ) : null

  const registroVisible = vista.registro
  const enRegistro = (vista.fase === 'abierto' || vista.fase === 'terminado') && registroVisible !== null
  const grupoDelRegistro = registroVisible === null || lista === null ? null : (lista.find((a) => a.cuenta === registroVisible.cuenta)?.grupo ?? null)

  return (
    <Pagina>
      <div className={vista.fase === 'abierto' ? 'rejilla-alumnos rejilla-alumnos--registro' : 'rejilla-alumnos'}>
        {/* Con el registro abierto el contador va antes de la hoja en el DOM: se lee primero y, en una columna, se ve sin bajar por la tabla; con espacio, la rejilla lo pone arriba del margen. */}
        {vista.fase === 'abierto' && registroVisible !== null && (
          <CuentaRegresiva registro={registroVisible} horaServidor={registro.horaServidor} recibidoEn={registro.recibidoEn} />
        )}
        <section className="hoja hoja--lista" aria-labelledby="titulo-registro">
          <header className="hoja__encabezado">
            <h1 id="titulo-registro" ref={encabezado} className="hoja__titulo" tabIndex={-1}>
              Registro
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
                <p className="etiqueta-tecnica hoja__subtitulo-linea">{MATERIA}</p>
                {lista !== null && lista.length > 0 && (
                  <div className="campos campos--dos">
                    <CampoBusqueda id="busqueda-registro" etiqueta="Buscar por nombre o cuenta" valor={busqueda} alCambiar={setBusqueda} />
                  </div>
                )}
              </>
            )}
          </header>

          {registro.error !== null && (
            <div className="hoja__cuerpo">
              <div className="aviso aviso--error" role="alert">
                <p className="aviso__titulo">{vista.fase === 'comprobando' ? 'No se pudo comprobar el registro' : 'No se pudo actualizar el progreso'}</p>
                <p>
                  {registro.error}
                  {registro.horaServidor !== null ? ` Último dato a las ${formatearHora(registro.horaServidor)}.` : ''}
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
            <div className="hoja__cuerpo estado" aria-busy="true">
              <MarcaAsistencia tipo="pendiente" />
              <p role="status" className="estado__cuerpo">
                Comprobando…
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
          <section className={vista.fase === 'abierto' ? 'margen margen--registro' : 'margen'} aria-labelledby="titulo-accion">
            {(vista.fase === 'libre' || vista.fase === 'iniciando') && (
              <div className="caja-margen">
                <h2 id="titulo-accion" className="zona__titulo">
                  Nuevo registro
                </h2>
                {elegido !== null ? (
                  <p>
                    <strong><bdi>{elegido.nombre}</bdi></strong> · {elegido.cuenta}
                  </p>
                ) : (
                  elegida !== null && lista !== null && <p>La cuenta {elegida} del enlace no está en la lista.</p>
                )}
                <label className="casilla">
                  <input
                    type="checkbox"
                    checked={consentimiento}
                    disabled={!elegidoDisponible || registro.ocupado !== null}
                    onChange={(e) => setConsentida(e.target.checked ? elegida : null)}
                  />
                  <span>El alumno firmó el consentimiento</span>
                </label>
                <button
                  type="button"
                  className="boton boton--primario"
                  disabled={!puedeIniciar}
                  aria-describedby={falta !== null ? 'ayuda-iniciar' : undefined}
                  onClick={() => {
                    if (elegida !== null) {
                      // El consentimiento vale para un solo registro: la próxima vez se vuelve a marcar.
                      setConsentida(null)
                      registro.iniciar(elegida)
                    }
                  }}
                >
                  {registro.ocupado === 'iniciar' ? 'Iniciando…' : 'Iniciar registro'}
                </button>
                {falta !== null && (
                  <p id="ayuda-iniciar" className="etiqueta-tecnica">
                    Falta: {falta}
                  </p>
                )}
              </div>
            )}

            {vista.fase === 'abierto' && registroVisible !== null && <UltimaFotoDelRegistro registro={registroVisible} />}

            {vista.fase === 'abierto' && registroVisible !== null && (
              <div className="caja-margen">
                <h2 id="titulo-accion" className="zona__titulo">
                  Cancelar
                </h2>
                <p>Detiene la captura. No se guarda ningún rostro.</p>
                <button type="button" className="boton" aria-disabled={registro.ocupado !== null} onClick={() => registro.ocupado === null && registro.cancelar()}>
                  {registro.ocupado === 'cancelar' ? 'Cancelando…' : 'Cancelar registro'}
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
          <p className="aviso__titulo">No se pudo cargar la lista</p>
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
          Primero importa la lista del grupo en Alumnos.
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
            <p className="aviso__titulo">No se pudo actualizar la lista</p>
            <p>{error} Mostrando la última recibida.</p>
          </div>
        </div>
      )}
      <div className="hoja__cuerpo hoja__cuerpo--pie-lista">
        <p className="etiqueta-tecnica" role="status">
          {busqueda.trim() === ''
            ? `${pendientes} ${pendientes === 1 ? 'pendiente' : 'pendientes'} de ${alumnos.length}.`
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
            Prueba con otra parte del nombre o de la cuenta.
          </EstadoVacio>
        </div>
      ) : (
        <SeleccionAlumno alumnos={visibles} elegida={elegida} alElegir={alElegir} bloqueado={bloqueado} />
      )}
    </>
  )
}

/** La última foto, en el margen mientras el registro está abierto: el seguimiento vive en este componente y se pierde al cerrarse. */
function UltimaFotoDelRegistro({ registro }: { registro: DatosRegistro }) {
  return <UltimaFoto captura={useUltimaCaptura(registro)} />
}

interface PropsProgreso {
  registro: DatosRegistro
  transcurridoMs: number | null
  alumnos: Alumno[] | null
}

/** El resumen del registro, el aviso de parecido y las cinco poses. */
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
            <p className="aviso__titulo">Rostro parecido al de otro alumno</p>
            <p>
              {parecido === null ? (
                'Se parece al de otro alumno.'
              ) : parecidoA !== null ? (
                <>
                  Se parece al de <bdi>{parecidoA.nombre}</bdi> (cuenta {parecido}).
                </>
              ) : (
                <>Se parece al de la cuenta {parecido}.</>
              )}{' '}
              Se guarda igual. Si no corresponde, retíralo en Alumnos y repite el registro.
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
