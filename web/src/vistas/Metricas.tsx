import { useMemo } from 'react'
import { useEstadoLecturas } from '../lecturasContexto'
import { AvisoObsoleta } from '../componentes/AvisoObsoleta'
import { Cifra } from '../componentes/Cifra'
import { EstadoVacio } from '../componentes/EstadoVacio'
import { GraficaPotencia } from '../componentes/GraficaPotencia'
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
import { construirSerie, VENTANA_MS } from '../serie'

// Cada cuántos segundos consulta la API App.tsx (useLecturas); solo se usa en los textos.
const CONSULTA_CADA_S = 5

const entero = (n: number) => String(Math.round(n))

export function Metricas() {
  const { lecturas, error, horaServidor, sinSesion, reintentar } = useEstadoLecturas()
  const ultima = lecturas && lecturas.length > 0 ? lecturas[lecturas.length - 1]! : null
  const serie = useMemo(() => (lecturas && lecturas.length > 0 ? construirSerie(lecturas) : null), [lecturas])
  const minutosVentana = VENTANA_MS / 60_000

  // Datos de la ventana visible, calculados de la serie real (nada inventado).
  const potencias = serie !== null ? serie.visibles.map((l) => l.p) : []

  return (
    <Pagina
      titulo="Métricas y asistencia"
      descripcion={`Energía de la placa, actualizada cada ${CONSULTA_CADA_S} s. La asistencia, la latencia y la confianza llegan en un avance posterior.`}
    >
      {error !== null && (
        <div className="aviso aviso--error">
          <p className="aviso__titulo">
            {lecturas === null ? 'No se pudieron cargar las lecturas' : 'No se pudo actualizar'}
          </p>
          <p>
            {error}{' '}
            {horaServidor !== null ? `Se muestran los datos de las ${formatearHora(horaServidor)}. ` : ''}
            {sinSesion ? 'La vista no insistirá sola.' : `La vista lo vuelve a intentar cada ${CONSULTA_CADA_S} s.`}
          </p>
          {sinSesion && (
            <button type="button" className="boton" onClick={reintentar}>
              Reintentar
            </button>
          )}
        </div>
      )}

      {lecturas === null && error === null && (
        <div className="hoja estado" aria-busy="true">
          <MarcaAsistencia tipo="pendiente" />
          <div className="estado__cuerpo">
            <h2 className="estado__titulo">Cargando las lecturas de energía</h2>
            <p className="estado__texto">Esperando la primera respuesta de la placa.</p>
          </div>
        </div>
      )}

      {lecturas !== null && ultima === null && (
        <EstadoVacio titulo="Todavía no hay lecturas de energía">
          Revisa que la XIAO esté encendida y publicando en <code>aula/camara/energia</code>. La primera lectura aparece
          aquí sin recargar la página.
        </EstadoVacio>
      )}

      {lecturas !== null && ultima !== null && serie !== null && (
        <>
          <AvisoObsoleta />

          {/* Una hoja de dos columnas: la potencia y las demás cifras como renglones con puntos guía, y la gráfica. */}
          <section className="hoja hoja--energia" aria-label="Energía">
            <div className="energia__cifras">
              <dl className="cifras">
                <Cifra destacada etiqueta="Potencia" valor={ultima.p} formato={formatearPotencia} unidad="W" />
              </dl>
              <div className="energia__hora">
                <p className="etiqueta-tecnica hora-lectura">
                  Última lectura: {formatearFechaYHora(ultima.t)} (hora de Ciudad de México)
                </p>
                {horaServidor !== null && (
                  <p className="etiqueta-tecnica">
                    Actualizado a las {formatearHora(horaServidor)}, cada {CONSULTA_CADA_S} s
                  </p>
                )}
              </div>
              <dl className="cifras cifras--renglones">
                <Cifra etiqueta="Voltaje" valor={ultima.v} formato={formatearVoltaje} unidad="V" />
                <Cifra etiqueta="Corriente" valor={ultima.i} formato={formatearCorriente} unidad="A" />
                <Cifra etiqueta={`Mínima en ${minutosVentana} min`} valor={Math.min(...potencias)} formato={formatearPotencia} unidad="W" />
                <Cifra etiqueta={`Máxima en ${minutosVentana} min`} valor={Math.max(...potencias)} formato={formatearPotencia} unidad="W" />
                <Cifra etiqueta={`Lecturas en ${minutosVentana} min`} valor={potencias.length} formato={entero} />
              </dl>
            </div>
            <div className="energia__grafica">
              <h2 id="titulo-grafica" className="hoja__subtitulo">Potencia en los últimos {minutosVentana} min (W)</h2>
              <GraficaPotencia serie={serie} />
            </div>
          </section>

          <section className="hoja hoja--tabla" aria-labelledby="titulo-tabla">
            <header className="hoja__encabezado hoja__encabezado--tabla">
              <h2 id="titulo-tabla" className="hoja__subtitulo">Lecturas recientes</h2>
            </header>
            <TablaLecturas lecturas={lecturas} idTitulo="titulo-tabla" />
          </section>
        </>
      )}

      <EstadoVacio titulo="Todavía no hay asistencia">
        La asistencia por sesión llega en un avance posterior. Hoy no hay sesiones que mostrar, y por eso no hay cifras.
      </EstadoVacio>
    </Pagina>
  )
}
