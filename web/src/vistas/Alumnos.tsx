import { useState } from 'react'
import { gruposDe } from '../api/alumnosRespuestas'
import { AvisoImportacion } from '../componentes/AvisoImportacion'
import { DialogoExistentes } from '../componentes/DialogoExistentes'
import { EsqueletoCifra, EsqueletoTabla } from '../componentes/EsqueletoTabla'
import { EstadoVacio } from '../componentes/EstadoVacio'
import { MarcaAsistencia } from '../componentes/MarcaAsistencia'
import { NumeroAnimado } from '../componentes/NumeroAnimado'
import { Pagina } from '../componentes/Pagina'
import { TablaAlumnos } from '../componentes/TablaAlumnos'
import { ZonaCSV } from '../componentes/ZonaCSV'
import { formatearHora } from '../formato'
import { useAlumnos } from '../useAlumnos'
import { useImportacion } from '../useImportacion'

// Cada cuántos segundos consulta la lista esta vista. El rostro cambia cuando
// termina un registro, un evento raro: 10 s alcanzan y cargan poco a la placa.
const CONSULTA_CADA_S = 10

// La materia del grupo, tal como la nombra el plan (docs/HIGH-LEVEL-PLAN.md,
// línea "Materia" sin el periodo). Es el campo fijo del encabezado de la hoja.
const MATERIA = 'Fundamentos de Sistemas Embebidos'

// Un <option> solo admite texto, no <bdi>: el grupo viene del CSV y puede traer
// caracteres de derecha a izquierda, así que se aísla con las marcas de Unicode
// (FSI y PDI) para que no reordene el texto que lo rodea.
const aislar = (texto: string) => `⁨${texto}⁩`

const entero = (n: number) => String(Math.round(n))

/**
 * Alumnos: la lista misma, compuesta como una hoja de asistencia (web/DESIGN.md).
 * El encabezado lleva los campos del formulario (materia, grupo) y los conteos
 * de lo que se ve; debajo van los renglones. La importación del CSV es una
 * acción secundaria y vive en el margen de la hoja.
 */
export function Alumnos() {
  const lista = useAlumnos(CONSULTA_CADA_S * 1000)
  const { alumnos, error, horaServidor, sinSesion, cambiadas, recargar } = lista
  const importacion = useImportacion(recargar, sinSesion)
  const [grupo, setGrupo] = useState('')

  const grupos = alumnos === null ? [] : gruposDe(alumnos)
  // Si el grupo elegido desaparece de la lista (se renombró), se vuelve a "todos".
  const grupoActivo = grupos.includes(grupo) ? grupo : ''
  const visibles = alumnos === null ? [] : grupoActivo === '' ? alumnos : alumnos.filter((a) => a.grupo === grupoActivo)

  // Los conteos son los de la hoja que se ve: con un grupo elegido, los de ese grupo.
  const conRostro = visibles.filter((a) => a.rostro === 'registrado').length
  const hayLista = alumnos !== null && alumnos.length > 0

  return (
    <>
      <Pagina>
        <div className="rejilla-alumnos">
          <section className="hoja hoja--lista" aria-labelledby="titulo-lista">
            <header className="hoja__encabezado">
              <h1 id="titulo-lista" className="hoja__titulo" tabIndex={-1}>Lista de alumnos</h1>
              <div className="campos">
                <dl className="campo campo--materia">
                  <dt className="campo__etiqueta">Materia</dt>
                  <dd className="campo__valor">{MATERIA}</dd>
                </dl>
                {hayLista && (
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
                )}
              </div>
              {alumnos === null && error === null && (
                <dl className="resumen" aria-hidden="true">
                  <div className="resumen__item"><dt className="campo__etiqueta">Alumnos</dt><dd className="resumen__valor"><EsqueletoCifra /></dd></div>
                  <div className="resumen__item"><dt className="campo__etiqueta">Con rostro</dt><dd className="resumen__valor"><EsqueletoCifra /></dd></div>
                  <div className="resumen__item"><dt className="campo__etiqueta">Pendientes</dt><dd className="resumen__valor"><EsqueletoCifra /></dd></div>
                </dl>
              )}
              {alumnos === null && error !== null && (
                <p className="etiqueta-tecnica">Sin datos: no se pudo leer la lista de alumnos.</p>
              )}
              {hayLista && (
                <dl className="resumen">
                  <div className="resumen__item">
                    <dt className="campo__etiqueta">{visibles.length === 1 ? 'Alumno' : 'Alumnos'}{grupoActivo !== '' && <> en <bdi>{grupoActivo}</bdi></>}</dt>
                    <dd className="resumen__valor"><NumeroAnimado valor={visibles.length} formato={entero} /></dd>
                  </div>
                  <div className="resumen__item">
                    <dt className="campo__etiqueta">Con rostro</dt>
                    <dd className="resumen__valor resumen__valor--presente">
                      <MarcaAsistencia tipo="presente" />
                      <NumeroAnimado valor={conRostro} formato={entero} />
                    </dd>
                  </div>
                  <div className="resumen__item">
                    <dt className="campo__etiqueta">Pendientes</dt>
                    <dd className="resumen__valor">
                      <MarcaAsistencia tipo="pendiente" />
                      <NumeroAnimado valor={visibles.length - conRostro} formato={entero} />
                    </dd>
                  </div>
                </dl>
              )}
            </header>

            {error !== null && (
              <div className="hoja__cuerpo">
                <div className="aviso aviso--error">
                  <p className="aviso__titulo">{alumnos === null ? 'No se pudo cargar la lista de alumnos' : 'No se pudo actualizar'}</p>
                  <p>
                    {error}{' '}
                    {horaServidor !== null ? `Se muestran los datos de las ${formatearHora(horaServidor)}. ` : ''}
                    {sinSesion ? 'La vista no insistirá sola.' : `La vista lo vuelve a intentar cada ${CONSULTA_CADA_S} s.`}
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
                  Importa el CSV de un grupo con la zona para soltar el archivo y la lista aparece aquí, con el estado del rostro de cada alumno.
                </EstadoVacio>
              </div>
            )}

            {hayLista && (
              <div className="hoja__desplazable" role="region" aria-label="Tabla de alumnos, con desplazamiento horizontal" tabIndex={0}>
                <TablaAlumnos alumnos={visibles} cambiadas={cambiadas} idTitulo="titulo-lista" />
              </div>
            )}
          </section>

          <section className="margen" aria-labelledby="titulo-importar">
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
    </>
  )
}
