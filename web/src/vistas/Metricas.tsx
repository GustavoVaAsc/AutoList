import { useMemo } from 'react'
import { elegirError } from '../api/errorVista'
import { AvisoObsoleta } from '../componentes/AvisoObsoleta'
import { Cifra } from '../componentes/Cifra'
import { EsqueletoTabla } from '../componentes/EsqueletoTabla'
import { EstadoVacio } from '../componentes/EstadoVacio'
import { GraficaPotencia } from '../componentes/GraficaPotencia'
import { HojaLatencia } from '../componentes/HojaLatencia'
import { HojaActuadores } from '../componentes/HojaActuadores'
import { HojaSistema } from '../componentes/HojaSistema'
import { Pagina } from '../componentes/Pagina'
import { PruebaReconocimiento } from '../componentes/PruebaReconocimiento'
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
import { useLatencia } from '../useLatencia'

const CONSULTA_CADA_MS = 5000
/** Mediciones de latencia que se piden: las más recientes (la API admite hasta 1000). */
const LIMITE_LATENCIAS = 100

const entero = (n: number) => String(Math.round(n))

/** Una hoja en blanco con el esqueleto de la tabla mientras llega la primera respuesta. */
function Cargando({ etiqueta }: { etiqueta: string }) {
  return (
    <div className="hoja">
      <EsqueletoTabla etiqueta={etiqueta} filas={3} />
    </div>
  )
}

export function Metricas() {
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

  const { error: errorMostrado, sinSesion } = elegirError([sistema, energia, latencia])
  const horaUltimaRespuesta =
    energia.horaServidor ?? latencia.horaServidor ?? sistema.horaServidor
  const sinDatosPrevios =
    energia.lecturas === null &&
    latencia.mediciones === null &&
    sistema.sistema === null

  function reintentarTodo() {
    sistema.reintentar()
    energia.reintentar()
    latencia.reintentar()
  }

  return (
    <Pagina titulo="Métricas">
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
            <button type="button" className="boton" onClick={reintentarTodo}>
              Reintentar
            </button>
          )}
        </div>
      )}

      {sistema.sistema === null && sistema.error === null && <Cargando etiqueta="Cargando el estado del sistema" />}

      {sistema.sistema !== null && sistema.horaServidor !== null && (
        <HojaSistema sistema={sistema.sistema} horaServidor={sistema.horaServidor} />
      )}

      <PruebaReconocimiento />

      <HojaActuadores sistema={sistema.sistema} errorEstado={sistema.error !== null} reconsultar={sistema.reintentar} />

      {energia.lecturas === null && energia.error === null && <Cargando etiqueta="Cargando las lecturas de energía" />}

      {energia.lecturas !== null && ultima === null && (
        <EstadoVacio titulo="Todavía no hay lecturas de energía">
          Revisa que el nodo del aula esté encendido. La primera lectura aparecerá aquí.
        </EstadoVacio>
      )}

      {energia.lecturas !== null && ultima !== null && serie !== null && (
        <>
          {errorMostrado === null && <AvisoObsoleta />}

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
                <p className="etiqueta-tecnica hora-lectura">Última lectura: {formatearFechaYHora(ultima.t)}</p>
                {energia.horaServidor !== null && (
                  <p className="etiqueta-tecnica">Actualizado a las {formatearHora(energia.horaServidor)}</p>
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

          <section className="hoja hoja--tabla">
            <details className="plegable plegable--hoja">
              <summary>Ver tabla de lecturas recientes</summary>
              <TablaLecturas lecturas={energia.lecturas} nombre="Lecturas de energía" />
            </details>
          </section>
        </>
      )}

      {latencia.mediciones === null && latencia.error === null && <Cargando etiqueta="Cargando las mediciones de latencia" />}

      {latencia.mediciones !== null && latencia.mediciones.length === 0 && (
        <EstadoVacio titulo="Todavía no hay mediciones de latencia">
          La primera medición aparecerá aquí cuando el reconocimiento procese su primer resultado.
        </EstadoVacio>
      )}

      {latencia.mediciones !== null && latencia.mediciones.length > 0 && (
        <HojaLatencia mediciones={latencia.mediciones} />
      )}
    </Pagina>
  )
}
