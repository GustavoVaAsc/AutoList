import { useMemo } from 'react'
import { useEstadoLecturas } from '../lecturasContexto'
import { AvisoObsoleta } from '../componentes/AvisoObsoleta'
import { Cifra } from '../componentes/Cifra'
import { EstadoVacio } from '../componentes/EstadoVacio'
import { GraficaPotencia } from '../componentes/GraficaPotencia'
import { Hero } from '../componentes/Hero'
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

export function Metricas() {
  const { lecturas, error, horaServidor } = useEstadoLecturas()
  const ultima = lecturas && lecturas.length > 0 ? lecturas[lecturas.length - 1]! : null
  const serie = useMemo(() => (lecturas && lecturas.length > 0 ? construirSerie(lecturas) : null), [lecturas])
  const minutosVentana = VENTANA_MS / 60_000

  // Datos de la ventana visible, calculados de la serie real (nada inventado).
  const potencias = serie !== null ? serie.visibles.map((l) => l.p) : []

  return (
    <>
      <Hero>
        <h1 className="titulo-vista" tabIndex={-1}>Métricas y asistencia</h1>
        <p className="entradilla">
          Por ahora solo hay energía. La asistencia, la latencia y la confianza llegan en un avance posterior.
        </p>

        {/* Región de energía: el h2 solo lo leen los lectores de pantalla (el h1 ya titula la vista). */}
        <section aria-labelledby="titulo-energia">
          <h2 id="titulo-energia" className="solo-lectores">Energía</h2>
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

              <dl className="cifra-principal">
                <Cifra destacada etiqueta="Potencia" valor={formatearPotencia(ultima.p)} unidad="W" />
              </dl>
              <dl className="cifras">
                <Cifra etiqueta="Voltaje" valor={formatearVoltaje(ultima.v)} unidad="V" />
                <Cifra etiqueta="Corriente" valor={formatearCorriente(ultima.i)} unidad="A" />
                <Cifra etiqueta={`Mínima en ${minutosVentana} min`} valor={formatearPotencia(Math.min(...potencias))} unidad="W" />
                <Cifra etiqueta={`Máxima en ${minutosVentana} min`} valor={formatearPotencia(Math.max(...potencias))} unidad="W" />
                <Cifra etiqueta={`Lecturas en ${minutosVentana} min`} valor={String(potencias.length)} />
              </dl>
              <p className="etiqueta-tecnica hero__hora">
                Última lectura: {formatearFechaYHora(ultima.t)} (hora de Ciudad de México)
              </p>
            </>
          )}

          {horaServidor !== null && (
            <p className="etiqueta-tecnica hero__hora">
              Actualizado a las {formatearHora(horaServidor)}, cada {CONSULTA_CADA_S} s
            </p>
          )}
        </section>
      </Hero>

      {serie !== null && lecturas !== null && (
        <>
          <section aria-labelledby="titulo-grafica" className="franja franja--oscura">
            <div className="contenedor">
              <h2 id="titulo-grafica" className="titular">Potencia en los últimos {minutosVentana} min (W)</h2>
              <GraficaPotencia serie={serie} />
            </div>
          </section>

          <section aria-labelledby="titulo-tabla" className="franja">
            <div className="contenedor">
              <h2 id="titulo-tabla" className="titular">Lecturas recientes</h2>
              <div className="tarjeta tarjeta--tabla">
                <TablaLecturas lecturas={lecturas} idTitulo="titulo-tabla" />
              </div>
            </div>
          </section>
        </>
      )}
    </>
  )
}
