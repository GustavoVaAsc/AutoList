import {
  TEXTO_PENDIENTE,
  TEXTO_SIN_RESPUESTA,
  faseDePrueba,
  resumirResultado,
  type Prueba,
  type ResultadoPrueba,
} from '../api/pruebaRespuestas'
import { formatearConfianza, formatearMilisegundos } from '../formato'
import { usePrueba } from '../usePrueba'
import { Cifra } from './Cifra'
import { MarcaAsistencia } from './MarcaAsistencia'

const MARCA_DE_TONO = { exito: 'presente', neutro: 'pendiente', error: 'falta' } as const

/** Las cifras del resultado, como renglones con puntos guía: a quién reconoce (si hay a quién), similitud, margen y latencia. */
function Cifras({ resultado }: { resultado: ResultadoPrueba }) {
  const total = resultado.lat_ms?.total ?? null
  return (
    <dl className="cifras cifras--renglones">
      {resultado.cuenta !== null && (
        <>
          <div className="cifra">
            <dt className="cifra__etiqueta">Alumno</dt>
            <dd className="cifra__valor">
              <span className="cifra__numero">
                <bdi>{resultado.nombre ?? 'Sin nombre en la lista'}</bdi>
              </span>
            </dd>
          </div>
          <div className="cifra">
            <dt className="cifra__etiqueta">Cuenta</dt>
            <dd className="cifra__valor">
              <span className="cifra__numero">{resultado.cuenta}</span>
            </dd>
          </div>
        </>
      )}
      {resultado.conf !== null && <Cifra etiqueta="Similitud" valor={resultado.conf} formato={formatearConfianza} />}
      {resultado.margen !== null && <Cifra etiqueta="Margen" valor={resultado.margen} formato={formatearConfianza} />}
      {total !== null && <Cifra etiqueta="Latencia total" valor={total} formato={formatearMilisegundos} unidad="ms" />}
    </dl>
  )
}

/** Lo que dice la prueba de ahora: la espera, que no hubo respuesta o el resultado. */
function EstadoPrueba({ prueba }: { prueba: Prueba }) {
  const fase = faseDePrueba(prueba)
  if (fase === 'sin-prueba') {
    return null
  }
  if (fase === 'pendiente') {
    return (
      <div className="estado-registro estado-registro--neutro" aria-busy="true">
        <MarcaAsistencia tipo="pendiente" />
        <div className="estado-registro__texto" role="status">
          <h3 className="estado-registro__titulo">{TEXTO_PENDIENTE.titulo}</h3>
          <p>{TEXTO_PENDIENTE.detalle}</p>
        </div>
      </div>
    )
  }
  if (fase === 'sin-respuesta' || prueba.resultado === null) {
    return (
      <div className="estado-registro estado-registro--error">
        <MarcaAsistencia tipo="falta" />
        <div className="estado-registro__texto" role="status">
          <h3 className="estado-registro__titulo">{TEXTO_SIN_RESPUESTA.titulo}</h3>
          <p>{TEXTO_SIN_RESPUESTA.detalle}</p>
        </div>
      </div>
    )
  }
  const resultado = prueba.resultado
  const resumen = resumirResultado(resultado)
  return (
    <>
      <div className={`estado-registro estado-registro--${resumen.tono}`}>
        <MarcaAsistencia tipo={MARCA_DE_TONO[resumen.tono]} />
        <div className="estado-registro__texto" role="status">
          <h3 className="estado-registro__titulo">{resumen.titulo}</h3>
          <p>{resumen.detalle}</p>
        </div>
      </div>
      <Cifras resultado={resultado} />
    </>
  )
}

/**
 * "Probar reconocimiento", hoja de Métricas junto a Sistema: pide al reconocimiento que decida ahora quién está
 * frente a la cámara, sin marcar asistencia y sin sesión abierta, y muestra solo el resultado en palabras y cifras
 * (la respuesta no trae imagen). Mientras espera lo dice sin simular avance; con un 401 deja de consultar y ofrece
 * "Reintentar". El botón usa `aria-disabled` y no `disabled`: conserva el foco mientras la orden va en camino. Si
 * hay un registro abierto, la API rechaza la orden y un aviso lo dice. Mismas piezas que la hoja de Sistema
 * (web/DESIGN.md): sin CSS propio.
 */
export function PruebaReconocimiento() {
  const { prueba, error, sinSesion, enviando, solicitar, reintentar } = usePrueba()
  const esperando = enviando || (prueba !== null && prueba.pendiente)
  return (
    <section className="hoja" aria-labelledby="titulo-prueba">
      <div className="intentos__resumen">
        <h2 id="titulo-prueba" className="hoja__subtitulo">
          Probar reconocimiento
        </h2>
        <p className="etiqueta-tecnica hora-lectura">Comprueba a quién reconoce la cámara. No marca asistencia.</p>
        <div>
          <button type="button" className="boton" aria-disabled={esperando} onClick={() => !esperando && solicitar()}>
            {enviando ? 'Enviando…' : esperando ? 'Probando…' : 'Probar reconocimiento'}
          </button>
        </div>
        {error !== null && (
          <div className="aviso aviso--error" role="alert">
            <p className="aviso__titulo">No se pudo consultar la prueba</p>
            <p>{error}</p>
            {sinSesion && (
              <button type="button" className="boton" onClick={reintentar}>
                Reintentar la prueba
              </button>
            )}
          </div>
        )}
        {/* Mientras la orden va en camino, lo que se ve es de la prueba anterior: se oculta hasta confirmar la nueva. */}
        {prueba !== null && !enviando && <EstadoPrueba prueba={prueba} />}
      </div>
    </section>
  )
}
