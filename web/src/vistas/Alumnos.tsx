import { useCallback, useEffect, useRef, useState } from 'react'
import { gruposDe } from '../api/alumnosRespuestas'
import { filtrarAlumnos } from '../api/registroRespuestas'
import { AvisoImportacion } from '../componentes/AvisoImportacion'
import { CampoBusqueda } from '../componentes/CampoBusqueda'
import { DialogoExistentes } from '../componentes/DialogoExistentes'
import { DialogoRetiro } from '../componentes/DialogoRetiro'
import { EsqueletoCifra, EsqueletoTabla } from '../componentes/EsqueletoTabla'
import { EstadoVacio } from '../componentes/EstadoVacio'
import { MarcaAsistencia } from '../componentes/MarcaAsistencia'
import { Pagina } from '../componentes/Pagina'
import { TablaAlumnos } from '../componentes/TablaAlumnos'
import { ZonaCSV } from '../componentes/ZonaCSV'
import { formatearHora } from '../formato'
import { useAlumnos } from '../useAlumnos'
import { useImportacion } from '../useImportacion'
import { useRetiro } from '../useRetiro'

// Cada cuántos segundos consulta la lista esta vista. El rostro cambia cuando
// termina un registro, un evento raro: 10 s alcanzan y cargan poco a la placa.
const CONSULTA_CADA_S = 10

// La materia del grupo, tal como la nombra el plan (docs/HIGH-LEVEL-PLAN.md,
// línea "Materia" sin el periodo). Va de subtítulo bajo el título de la hoja.
const MATERIA = 'Fundamentos de Sistemas Embebidos'

// Un <option> solo admite texto, no <bdi>: el grupo viene del CSV y puede traer
// caracteres de derecha a izquierda, así que se aísla con las marcas de Unicode
// (FSI y PDI) para que no reordene el texto que lo rodea.
const aislar = (texto: string) => `⁨${texto}⁩`

/**
 * Alumnos: la lista misma, compuesta como una hoja de asistencia (web/DESIGN.md).
 * El encabezado lleva el filtro de grupo, la búsqueda y los conteos de lo que se
 * ve; debajo van los renglones. La búsqueda por nombre o cuenta y el filtro de
 * grupo se aplican en el cliente y los conteos siguen a lo visible. Cada renglón
 * lleva su acción (registrar o retirar el rostro). La importación del CSV es una
 * acción secundaria: con la lista vacía su zona está en el margen de la hoja y,
 * con lista, aparece al pulsar "Importar CSV".
 */
export function Alumnos() {
  const lista = useAlumnos(CONSULTA_CADA_S * 1000)
  const { alumnos, error, horaServidor, sinSesion, cambiadas, recargar } = lista
  const importacion = useImportacion(recargar, sinSesion)
  // Tras retirar un rostro, y cuando la lista nueva ya está pintada (`recargar` cumple su promesa),
  // el foco vuelve a la acción de esa fila (el botón "Retirar rostro" pasa a ser el enlace
  // "Registrar rostro", otro elemento); si la fila ya no está, al título de la hoja. Se espera a
  // que el diálogo termine de cerrarse: mientras está abierto, el resto de la página es inerte y
  // no admite foco. Si la vista se desmonta, el temporizador pendiente se cancela.
  const temporizadorFoco = useRef<number | undefined>(undefined)
  const montada = useRef(true)
  useEffect(() => {
    montada.current = true
    return () => {
      montada.current = false
      window.clearTimeout(temporizadorFoco.current)
    }
  }, [])
  const alTerminarRetiro = useCallback((cuenta: string) => {
    let intentos = 0
    const enfocar = () => {
      if (!montada.current) {
        return
      }
      if (document.querySelector('dialog[open]') !== null && intentos < 20) {
        intentos += 1
        temporizadorFoco.current = window.setTimeout(enfocar, 50)
        return
      }
      const destino = document.querySelector<HTMLElement>(`[data-accion-cuenta="${CSS.escape(cuenta)}"]`) ?? document.getElementById('titulo-lista')
      destino?.focus()
    }
    enfocar()
  }, [])
  const retiro = useRetiro(recargar, alTerminarRetiro)
  const [grupo, setGrupo] = useState('')
  const [busqueda, setBusqueda] = useState('')
  const [importarAbierto, setImportarAbierto] = useState(false)
  const botonImportar = useRef<HTMLButtonElement>(null)
  const focoEnZona = useRef(false)

  const grupos = alumnos === null ? [] : gruposDe(alumnos)
  // Si el grupo elegido desaparece de la lista (se renombró), se vuelve a "todos".
  const grupoActivo = grupos.includes(grupo) ? grupo : ''
  const delGrupo = alumnos === null ? [] : grupoActivo === '' ? alumnos : alumnos.filter((a) => a.grupo === grupoActivo)
  const visibles = filtrarAlumnos(delGrupo, busqueda)
  const buscando = busqueda.trim() !== ''

  // Los conteos son los de la hoja que se ve: con un grupo elegido, los de ese grupo.
  const conRostro = visibles.filter((a) => a.rostro === 'registrado').length
  const hayLista = alumnos !== null && alumnos.length > 0
  const listaVacia = alumnos !== null && alumnos.length === 0
  const mostrarZona = listaVacia || importarAbierto

  // Si la zona se pliega con el foco dentro (la primera importación llena la lista y la zona vacía deja de
  // mostrarse), el foco pasa al botón "Importar CSV" o, si no hay, al título.
  useEffect(() => {
    if (!mostrarZona && focoEnZona.current) {
      focoEnZona.current = false
      if (document.activeElement === null || document.activeElement === document.body) {
        ;(botonImportar.current ?? document.getElementById('titulo-lista'))?.focus()
      }
    }
  }, [mostrarZona])

  return (
    <>
      <Pagina>
        <div className={mostrarZona ? 'rejilla-alumnos' : 'rejilla-alumnos rejilla-alumnos--sola'}>
          <section className="hoja hoja--lista" aria-labelledby="titulo-lista">
            <header className="hoja__encabezado">
              <div className="hoja__barra">
                <h1 id="titulo-lista" className="hoja__titulo" tabIndex={-1}>Lista de alumnos</h1>
                {!listaVacia && (
                  <button
                    type="button"
                    className="boton"
                    ref={botonImportar}
                    aria-expanded={importarAbierto}
                    aria-controls="margen-importar"
                    onClick={() => setImportarAbierto(!importarAbierto)}
                  >
                    Importar CSV
                  </button>
                )}
              </div>
              <p className="etiqueta-tecnica hoja__subtitulo-linea">{MATERIA}</p>
              {/* La zona plegada no anuncia nada: el aviso de que se importa va aquí, siempre montado. */}
              <p role="status" className="solo-lectores">
                {importacion.enviando && !mostrarZona ? `Importando ${importacion.archivo ?? 'el archivo'}…` : ''}
              </p>
              {hayLista && (
                <div className="campos campos--dos">
                  <div className="campo campo--grupo">
                    <label htmlFor="filtro-grupo" className="campo__etiqueta">Grupo</label>
                    <select id="filtro-grupo" className="selector selector--campo" value={grupoActivo} onChange={(e) => setGrupo(e.target.value)}>
                      <option value="">Todos los grupos</option>
                      {grupos.map((g) => (
                        <option key={g} value={g}>
                          {aislar(g)}
                        </option>
                      ))}
                    </select>
                  </div>
                  <CampoBusqueda id="busqueda-alumnos" etiqueta="Buscar por nombre o cuenta" valor={busqueda} alCambiar={setBusqueda} />
                </div>
              )}
              {alumnos === null && error === null && (
                <dl className="resumen" aria-hidden="true">
                  <div className="resumen__item"><dt className="campo__etiqueta">Alumnos</dt><dd className="resumen__valor"><EsqueletoCifra /></dd></div>
                  <div className="resumen__item"><dt className="campo__etiqueta">Con rostro</dt><dd className="resumen__valor"><EsqueletoCifra /></dd></div>
                  <div className="resumen__item"><dt className="campo__etiqueta">Pendientes</dt><dd className="resumen__valor"><EsqueletoCifra /></dd></div>
                </dl>
              )}
              {hayLista && buscando && (
                <p role="status" className="solo-lectores">
                  {visibles.length === 0 ? 'Ningún alumno coincide con la búsqueda' : `${visibles.length} ${visibles.length === 1 ? 'alumno coincide' : 'alumnos coinciden'} con la búsqueda`}
                </p>
              )}
              {hayLista && (
                <dl className="resumen">
                  <div className="resumen__item">
                    <dt className="campo__etiqueta">Alumnos</dt>
                    <dd className="resumen__valor">{visibles.length}</dd>
                  </div>
                  <div className="resumen__item">
                    <dt className="campo__etiqueta">Con rostro</dt>
                    <dd className="resumen__valor resumen__valor--presente">
                      <MarcaAsistencia tipo="presente" />
                      {conRostro}
                    </dd>
                  </div>
                  <div className="resumen__item">
                    <dt className="campo__etiqueta">Pendientes</dt>
                    <dd className="resumen__valor">
                      <MarcaAsistencia tipo="pendiente" />
                      {visibles.length - conRostro}
                    </dd>
                  </div>
                </dl>
              )}
            </header>

            {error !== null && (
              <div className="hoja__cuerpo">
                <div className="aviso aviso--error">
                  <p className="aviso__titulo">{alumnos === null ? 'No se pudo cargar la lista' : 'No se pudo actualizar la lista'}</p>
                  <p>
                    {error}
                    {horaServidor !== null ? ` Mostrando la de las ${formatearHora(horaServidor)}.` : ''}
                  </p>
                  {sinSesion && (
                    <button type="button" className="boton" onClick={recargar}>
                      Reintentar
                    </button>
                  )}
                </div>
              </div>
            )}

            {importacion.aviso !== null && (
              <div className="hoja__cuerpo">
                <AvisoImportacion key={importacion.aviso.id} aviso={importacion.aviso} />
              </div>
            )}

            {alumnos === null && error === null && <EsqueletoTabla />}

            {alumnos !== null && alumnos.length === 0 && (
              <div className="hoja__cuerpo">
                <EstadoVacio anidado titulo="Todavía no hay alumnos">
                  Importa el CSV de un grupo y la lista aparecerá aquí.
                </EstadoVacio>
              </div>
            )}

            {hayLista && visibles.length === 0 && (
              <div className="hoja__cuerpo">
                <EstadoVacio
                  anidado
                  titulo="Ningún alumno coincide"
                  accion={
                    <button
                      type="button"
                      className="boton"
                      onClick={() => {
                        setBusqueda('')
                        // El botón desaparece con el estado vacío: el foco pasa al campo de búsqueda.
                        document.getElementById('busqueda-alumnos')?.focus()
                      }}
                    >
                      Borrar búsqueda
                    </button>
                  }
                >
                  Prueba con otra parte del nombre o de la cuenta.
                </EstadoVacio>
              </div>
            )}

            {hayLista && visibles.length > 0 && (
              <div className="hoja__desplazable" role="region" aria-label="Tabla de alumnos" tabIndex={0}>
                <TablaAlumnos alumnos={visibles} cambiadas={cambiadas} idTitulo="titulo-lista" alRetirar={retiro.pedir} />
              </div>
            )}
          </section>

          {/* Plegada sigue montada: así la zona sigue evitando que el navegador abra un CSV soltado fuera de ella. */}
          <section
            id="margen-importar"
            className="margen"
            aria-labelledby="titulo-importar"
            hidden={!mostrarZona}
            onFocus={() => {
              focoEnZona.current = true
            }}
            onBlur={(e) => {
              if (e.relatedTarget !== null) {
                focoEnZona.current = false
              }
            }}
          >
            <ZonaCSV enviando={importacion.enviando} archivo={importacion.archivo} alElegir={importacion.elegir} />
          </section>
        </div>
      </Pagina>
      {/* Fuera de Pagina: sus hijos directos entran escalonados y el diálogo ya tiene su propio movimiento. */}
      <DialogoExistentes
        datos={importacion.porDecidir}
        enviando={importacion.enviando}
        alDecidir={importacion.decidir}
        alCancelar={importacion.cancelar}
      />
      <DialogoRetiro alumno={retiro.porRetirar} retirando={retiro.retirando} alConfirmar={retiro.confirmar} alCancelar={retiro.cancelar} />
    </>
  )
}
