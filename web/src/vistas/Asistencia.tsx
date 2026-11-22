import { elegirError } from '../api/errorVista'
import { EsqueletoTabla } from '../componentes/EsqueletoTabla'
import { EstadoVacio } from '../componentes/EstadoVacio'
import { HojaAsistencia } from '../componentes/HojaAsistencia'
import { Pagina } from '../componentes/Pagina'
import { formatearHora } from '../formato'
import { useAsistencia } from '../useAsistencia'

const CONSULTA_CADA_MS = 5000

export function Asistencia() {
  const asistencia = useAsistencia(CONSULTA_CADA_MS)

  const { error: errorMostrado, sinSesion } = elegirError([asistencia])
  const horaUltimaRespuesta = asistencia.horaServidor
  const sinDatosPrevios = asistencia.sesiones === null

  return (
    <Pagina titulo="Asistencia">
      {errorMostrado !== null && (
        <div className="aviso aviso--error">
          <p className="aviso__titulo">
            {sinDatosPrevios ? 'No se pudieron cargar los datos' : 'No se pudo actualizar'}
          </p>
          <p>
            {errorMostrado}
            {horaUltimaRespuesta !== null
              ? ` Mostrando los datos de las ${formatearHora(horaUltimaRespuesta)}.`
              : ''}
          </p>
          {sinSesion && (
            <button type="button" className="boton" onClick={asistencia.reintentar}>
              Reintentar
            </button>
          )}
        </div>
      )}

      {asistencia.sesiones === null && asistencia.error === null && (
        <div className="hoja">
          <EsqueletoTabla etiqueta="Cargando las sesiones" filas={4} />
        </div>
      )}

      {asistencia.sesiones !== null && asistencia.sesiones.length === 0 && (
        <EstadoVacio titulo="Todavía no hay sesiones de asistencia">
          Inicia una sesión en la pantalla del aula: aparecerá aquí.
        </EstadoVacio>
      )}

      {asistencia.sesiones !== null && asistencia.sesiones.length > 0 && (
        <HojaAsistencia
          sesiones={asistencia.sesiones}
          detalle={asistencia.detalle}
          idSeleccionado={asistencia.idSeleccionado}
          cargandoDetalle={asistencia.cargandoDetalle}
          cambiadas={asistencia.cambiadas}
          alSeleccionarSesion={asistencia.seleccionarSesion}
        />
      )}
    </Pagina>
  )
}
