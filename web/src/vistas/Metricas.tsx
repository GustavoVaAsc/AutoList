import { useMemo } from 'react'
import { elegirError } from '../api/errorVista'
import { AvisoObsoleta } from '../componentes/AvisoObsoleta'
import { Cifra } from '../componentes/Cifra'
import { EstadoVacio } from '../componentes/EstadoVacio'
import { GraficaPotencia } from '../componentes/GraficaPotencia'
import { HojaAsistencia } from '../componentes/HojaAsistencia'
import { HojaLatencia } from '../componentes/HojaLatencia'
import { HojaActuadores } from '../componentes/HojaActuadores'
import { HojaSistema } from '../componentes/HojaSistema'
import { MarcaAsistencia } from '../componentes/MarcaAsistencia'
import { Pagina } from '../componentes/Pagina'
import { TablaLecturas } from '../componentes/TablaLecturas'
import {
  formatearCorriente,
  formatearFechaYHora,
  formatearHora,
  formatearPotencia,
  formatearVoltaje,
} from '../formato'
import { useEstadoLecturas } from '../lecturasContexto'
import { construirSerie, VENTANA_MS } from '../serie'
import { useEstadoSistema } from '../sistemaContexto'
import { useAsistencia } from '../useAsistencia'
import { useLatencia } from '../useLatencia'

const CONSULTA_CADA_S = 5
const CONSULTA_CADA_MS = CONSULTA_CADA_S * 1000
/** Mediciones de latencia que se piden: las más recientes (la API admite hasta 1000). */
const LIMITE_LATENCIAS = 100

const entero = (n: number) => String(Math.round(n))

export function Metricas() {
  const asistencia = useAsistencia(CONSULTA_CADA_MS)
  const energia = useEstadoLecturas()
  const latencia = useLatencia(LIMITE_LATENCIAS, CONSULTA_CADA_MS)
  // El sondeo del sistema vive en App (la cabecera también lo usa); aquí solo se lee.
  const sistema = useEstadoSistema()

  const ultima =
    energia.lecturas && energia.lecturas.length > 0
      ? energia.lecturas[energia.lecturas.length - 1]!
      : null
  const serie = useMemo(
    () => (energia.lecturas && energia.lecturas.length > 0 ? construirSerie(energia.lecturas) : null),
    [energia.lecturas],
  )
  const minutosVentana = VENTANA_MS / 60_000
  const potencias = serie !== null ? serie.visibles.map((l) => l.p) : []

  const { error: errorMostrado, sinSesion } = elegirError([sistema, asistencia, energia, latencia])
  const horaUltimaRespuesta =
    asistencia.horaServidor ?? energia.horaServidor ?? latencia.horaServidor ?? sistema.horaServidor
  const sinDatosPrevios =
    asistencia.sesiones === null &&
    energia.lecturas === null &&
    latencia.mediciones === null &&
    sistema.sistema === null

  function reintentarTodo() {
    sistema.reintentar()
    asistencia.reintentar()
    energia.reintentar()
    latencia.reintentar()
  }

  return (
    <Pagina
      titulo="Métricas y asistencia"
      descripcion={`Estado del sistema, control de los actuadores, asistencia por sesión, intentos de reconocimiento, energía de la placa y latencia de inferencia, actualizados cada ${CONSULTA_CADA_S} s.`}
    >
      {errorMostrado !== null && (
        <div className="aviso aviso--error">
          <p className="aviso__titulo">
            {sinDatosPrevios ? 'No se pudieron cargar los datos' : 'No se pudo actualizar'}
          </p>
          <p>
            {errorMostrado}{' '}
            {horaUltimaRespuesta !== null
              ? `Se muestran los datos de las ${formatearHora(horaUltimaRespuesta)}. `
              : ''}
            {sinSesion
              ? 'La vista no insistirá sola.'
              : `La vista lo vuelve a intentar cada ${CONSULTA_CADA_S} s.`}
          </p>
          {sinSesion && (
            <button type="button" className="boton" onClick={reintentarTodo}>
              Reintentar
            </button>
          )}
        </div>
      )}

      {sistema.sistema === null && sistema.error === null && (
        <div className="hoja estado" aria-busy="true">
          <MarcaAsistencia tipo="pendiente" />
          <div className="estado__cuerpo">
            <h2 className="estado__titulo">Cargando el estado del sistema</h2>
            <p className="estado__texto">Preguntando a Node-RED por el servicio de visión, la XIAO y la placa.</p>
          </div>
        </div>
      )}

      {sistema.sistema !== null && sistema.horaServidor !== null && sistema.recibidoEn !== null && (
        <HojaSistema sistema={sistema.sistema} horaServidor={sistema.horaServidor} recibidoEn={sistema.recibidoEn} />
      )}

      <HojaActuadores sistema={sistema.sistema} errorEstado={sistema.error !== null} reconsultar={sistema.reintentar} />

      {asistencia.sesiones === null && asistencia.error === null && (
        <div className="hoja estado" aria-busy="true">
          <MarcaAsistencia tipo="pendiente" />
          <div className="estado__cuerpo">
            <h2 className="estado__titulo">Cargando sesiones de asistencia</h2>
            <p className="estado__texto">Consultando el historial de sesiones en la placa.</p>
          </div>
        </div>
      )}

      {asistencia.sesiones !== null && asistencia.sesiones.length === 0 && (
        <EstadoVacio titulo="Todavía no hay sesiones de asistencia">
          Inicia una sesión desde la pantalla táctil del aula para comenzar el pase de lista. Al
          abrirse o cerrarse, aparecerá aquí automáticamente.
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

      {energia.lecturas === null && energia.error === null && (
        <div className="hoja estado" aria-busy="true">
          <MarcaAsistencia tipo="pendiente" />
          <div className="estado__cuerpo">
            <h2 className="estado__titulo">Cargando las lecturas de energía</h2>
            <p className="estado__texto">Esperando la respuesta del monitor de energía.</p>
          </div>
        </div>
      )}

      {energia.lecturas !== null && ultima === null && (
        <EstadoVacio titulo="Todavía no hay lecturas de energía">
          Revisa que la XIAO esté encendida y publicando en <code>aula/camara/energia</code>. La
          primera lectura aparece aquí sin recargar la página.
        </EstadoVacio>
      )}

      {energia.lecturas !== null && ultima !== null && serie !== null && (
        <>
          <AvisoObsoleta />

          <section className="hoja hoja--energia" aria-label="Energía">
            <div className="energia__cifras">
              <dl className="cifras">
                <Cifra
                  destacada
                  etiqueta="Potencia"
                  valor={ultima.p}
                  formato={formatearPotencia}
                  unidad="W"
                />
              </dl>
              <div className="energia__hora">
                <p className="etiqueta-tecnica hora-lectura">
                  Última lectura: {formatearFechaYHora(ultima.t)} (hora de Ciudad de México)
                </p>
                {energia.horaServidor !== null && (
                  <p className="etiqueta-tecnica">
                    Actualizado a las {formatearHora(energia.horaServidor)}, cada {CONSULTA_CADA_S} s
                  </p>
                )}
              </div>
              <dl className="cifras cifras--renglones">
                <Cifra
                  etiqueta="Voltaje"
                  valor={ultima.v}
                  formato={formatearVoltaje}
                  unidad="V"
                />
                <Cifra
                  etiqueta="Corriente"
                  valor={ultima.i}
                  formato={formatearCorriente}
                  unidad="A"
                />
                <Cifra
                  etiqueta={`Mínima en ${minutosVentana} min`}
                  valor={Math.min(...potencias)}
                  formato={formatearPotencia}
                  unidad="W"
                />
                <Cifra
                  etiqueta={`Máxima en ${minutosVentana} min`}
                  valor={Math.max(...potencias)}
                  formato={formatearPotencia}
                  unidad="W"
                />
                <Cifra
                  etiqueta={`Lecturas en ${minutosVentana} min`}
                  valor={potencias.length}
                  formato={entero}
                />
              </dl>
            </div>
            <div className="energia__grafica">
              <h2 id="titulo-grafica" className="hoja__subtitulo">
                Potencia en los últimos {minutosVentana} min (W)
              </h2>
              <GraficaPotencia serie={serie} />
            </div>
          </section>

          <section className="hoja hoja--tabla" aria-labelledby="titulo-tabla">
            <header className="hoja__encabezado hoja__encabezado--tabla">
              <h2 id="titulo-tabla" className="hoja__subtitulo">
                Lecturas recientes
              </h2>
            </header>
            <TablaLecturas lecturas={energia.lecturas} idTitulo="titulo-tabla" />
          </section>
        </>
      )}

      {latencia.mediciones === null && latencia.error === null && (
        <div className="hoja estado" aria-busy="true">
          <MarcaAsistencia tipo="pendiente" />
          <div className="estado__cuerpo">
            <h2 className="estado__titulo">Cargando las mediciones de latencia</h2>
            <p className="estado__texto">Consultando la latencia de inferencia guardada en la placa.</p>
          </div>
        </div>
      )}

      {latencia.mediciones !== null && latencia.mediciones.length === 0 && (
        <EstadoVacio titulo="Todavía no hay mediciones de latencia">
          Cada resultado que el servicio de visión publica en <code>aula/vision/resultado</code> deja
          aquí su latencia. Revisa que el servicio esté en marcha y que la hora de la placa sea la
          correcta; la primera medición aparece sin recargar la página.
        </EstadoVacio>
      )}

      {latencia.mediciones !== null && latencia.mediciones.length > 0 && (
        <HojaLatencia mediciones={latencia.mediciones} />
      )}
    </Pagina>
  )
}
