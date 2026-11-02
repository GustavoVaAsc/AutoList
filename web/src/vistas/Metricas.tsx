import { useMemo, type ReactNode } from 'react'
import { useEstadoLecturas } from '../lecturasContexto'
import { AvisoObsoleta } from '../componentes/AvisoObsoleta'
import { EstadoVacio } from '../componentes/EstadoVacio'
import { GraficaPotencia } from '../componentes/GraficaPotencia'
import { TablaLecturas } from '../componentes/TablaLecturas'
import { TarjetaCifra } from '../componentes/TarjetaCifra'
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

export function Metricas() {
  const { lecturas, error, horaServidor } = useEstadoLecturas()
  const ultima = lecturas && lecturas.length > 0 ? lecturas[lecturas.length - 1]! : null
  const serie = useMemo(() => (lecturas && lecturas.length > 0 ? construirSerie(lecturas) : null), [lecturas])
  const minutosVentana = VENTANA_MS / 60_000

  // Dato de la ventana visible, calculado de la serie real (nada inventado).
  let ventana: ReactNode = null
  if (serie !== null) {
    const potencias = serie.visibles.map((l) => l.p)
    ventana = (
      <div className="ventana">
        <p className="etiqueta-tecnica">Últimos {minutosVentana} min</p>
        <dl className="ventana__lista">
          <div>
            <dt>Mínima</dt>
            <dd>
              {formatearPotencia(Math.min(...potencias))} <span className="etiqueta-tecnica">W</span>
            </dd>
          </div>
          <div>
            <dt>Máxima</dt>
            <dd>
              {formatearPotencia(Math.max(...potencias))} <span className="etiqueta-tecnica">W</span>
            </dd>
          </div>
          <div>
            <dt>Lecturas</dt>
            <dd>{serie.visibles.length}</dd>
          </div>
        </dl>
      </div>
    )
  }

  return (
    <>
      <h1 className="titulo-vista" tabIndex={-1}>Métricas y asistencia</h1>
      <p className="subtitulo-vista">
        Por ahora solo hay energía. La asistencia por sesión, la latencia de inferencia y la confianza llegan en un
        avance posterior.
      </p>

      <section aria-labelledby="titulo-energia" className="seccion">
        <div className="seccion__encabezado">
          <h2 id="titulo-energia" className="subtitulo">Energía</h2>
          {horaServidor !== null && (
            <p className="etiqueta-tecnica">
              Actualizado a las {formatearHora(horaServidor)}, cada {CONSULTA_CADA_S} s
            </p>
          )}
        </div>

        {error !== null && (
          <div className="aviso aviso--error">
            <p className="aviso__titulo">
              {lecturas === null ? 'No se pudieron cargar las lecturas' : 'No se pudo actualizar'}
            </p>
            <p>
              {error}{' '}
              {horaServidor !== null ? `Se muestran los datos de las ${formatearHora(horaServidor)}. ` : ''}
              La vista lo vuelve a intentar cada {CONSULTA_CADA_S} s.
            </p>
          </div>
        )}

        {lecturas === null && error === null && (
          <div className="tarjeta estado">
            <p className="estado__texto">Cargando las lecturas de energía</p>
          </div>
        )}

        {lecturas !== null && ultima === null && (
          <EstadoVacio titulo="Todavía no hay lecturas de energía">
            Revisa que la XIAO esté encendida y publicando en{' '}
            <span className="etiqueta-tecnica">aula/camara/energia</span>. La primera lectura aparecerá aquí sin
            recargar la página.
          </EstadoVacio>
        )}

        {lecturas !== null && ultima !== null && (
          <>
            <AvisoObsoleta />

            <dl className="cifras">
              <TarjetaCifra
                destacada
                etiqueta="Potencia"
                valor={formatearPotencia(ultima.p)}
                unidad="W"
                nota={ventana}
              />
              <TarjetaCifra etiqueta="Voltaje" valor={formatearVoltaje(ultima.v)} unidad="V" />
              <TarjetaCifra etiqueta="Corriente" valor={formatearCorriente(ultima.i)} unidad="A" />
            </dl>
            <p className="etiqueta-tecnica cifras__hora">
              Última lectura: {formatearFechaYHora(ultima.t)} (hora de Ciudad de México)
            </p>

            <div className="tarjeta bloque">
              <h3 className="bloque__titulo">Potencia en los últimos {minutosVentana} min (W)</h3>
              {serie !== null && <GraficaPotencia serie={serie} />}
            </div>

            <div className="tarjeta bloque">
              <h3 id="titulo-tabla" className="bloque__titulo">Lecturas recientes</h3>
              <TablaLecturas lecturas={lecturas} idTitulo="titulo-tabla" />
            </div>
          </>
        )}
      </section>
    </>
  )
}
