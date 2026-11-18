import { useEffect, useState } from 'react'
import {
  TEXTO_PENDIENTE,
  TEXTO_SIN_RESPUESTA,
  faseDePrueba,
  resumirResultado,
  type Prueba,
  type ResultadoPrueba,
} from '../api/pruebaRespuestas'
import { antiguedadMs } from '../api/sistemaRespuestas'
import { formatearAntiguedad, formatearConfianza, formatearMilisegundos } from '../formato'
import { usePrueba } from '../usePrueba'
import { Cifra } from './Cifra'
import { MarcaAsistencia } from './MarcaAsistencia'

interface Props {
  /** Hay otra acción de Registro esperando respuesta (iniciar): la prueba tampoco sale. */
  bloqueada: boolean
}

const MARCA_DE_TONO = { exito: 'presente', neutro: 'pendiente', error: 'falta' } as const

/** Un tick por segundo para que el "hace N s" siga contando entre consultas; solo repinta este recuadro. */
function useAhora(): number {
  const [ahora, setAhora] = useState(() => performance.now())
  useEffect(() => {
    const id = window.setInterval(() => setAhora(performance.now()), 1000)
    return () => window.clearInterval(id)
  }, [])
  return ahora
}

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

interface PropsEstado {
  prueba: Prueba
  horaServidor: number | null
  recibidoEn: number | null
}

/** Lo que dice la prueba de ahora: la espera, que no hubo respuesta o el resultado. Solo el título y el detalle son región viva: el tiempo cambia cada segundo. */
function EstadoPrueba({ prueba, horaServidor, recibidoEn }: PropsEstado) {
  const ahora = useAhora()
  const fase = faseDePrueba(prueba)
  const hace = (t: number) =>
    horaServidor === null || recibidoEn === null ? null : formatearAntiguedad(antiguedadMs(t, horaServidor, recibidoEn, ahora))
  if (fase === 'sin-prueba') {
    return null
  }
  if (fase === 'pendiente') {
    const van = prueba.solicitado === null ? null : hace(prueba.solicitado)
    return (
      <div className="estado-registro estado-registro--neutro" aria-busy="true">
        <MarcaAsistencia tipo="pendiente" />
        <div className="estado-registro__texto">
          <div role="status">
            <h3 className="estado-registro__titulo">{TEXTO_PENDIENTE.titulo}</h3>
            <p>{TEXTO_PENDIENTE.detalle}</p>
          </div>
          {van !== null && <p className="etiqueta-tecnica">Van {van} sin resultado.</p>}
        </div>
      </div>
    )
  }
  if (fase === 'sin-respuesta' || prueba.resultado === null) {
    const desde = prueba.solicitado === null ? null : hace(prueba.solicitado)
    return (
      <div className="estado-registro estado-registro--error">
        <MarcaAsistencia tipo="falta" />
        <div className="estado-registro__texto">
          <div role="status">
            <h3 className="estado-registro__titulo">{TEXTO_SIN_RESPUESTA.titulo}</h3>
            <p>{TEXTO_SIN_RESPUESTA.detalle}</p>
          </div>
          {desde !== null && <p className="etiqueta-tecnica">Se pidió hace {desde}.</p>}
        </div>
      </div>
    )
  }
  const resultado = prueba.resultado
  const resumen = resumirResultado(resultado)
  const recibido = hace(resultado.recibido)
  return (
    <>
      <div className={`estado-registro estado-registro--${resumen.tono}`}>
        <MarcaAsistencia tipo={MARCA_DE_TONO[resumen.tono]} />
        <div className="estado-registro__texto">
          <div role="status">
            <h3 className="estado-registro__titulo">{resumen.titulo}</h3>
            <p>{resumen.detalle}</p>
          </div>
          {recibido !== null && <p className="etiqueta-tecnica">Resultado de hace {recibido}.</p>}
        </div>
      </div>
      <Cifras resultado={resultado} />
    </>
  )
}

/**
 * "Probar reconocimiento", en el margen de Registro cuando no hay un registro abierto: pide a la visión que decida
 * ahora quién está frente a la cámara, sin marcar asistencia y sin sesión abierta, y muestra solo el resultado en
 * palabras y cifras (la respuesta no trae imagen). Mientras espera lo dice sin simular avance; con un 401 deja de
 * consultar y ofrece "Reintentar". El botón usa `aria-disabled` y no `disabled`: conserva el foco mientras la orden
 * va en camino. Mismas piezas que el resto de la vista (web/DESIGN.md): sin CSS propio.
 */
export function PruebaReconocimiento({ bloqueada }: Props) {
  const { prueba, error, sinSesion, horaServidor, recibidoEn, enviando, solicitar, reintentar } = usePrueba()
  const esperando = enviando || (prueba !== null && prueba.pendiente)
  const inactiva = bloqueada || esperando
  return (
    <div className="caja-margen">
      <h2 id="titulo-prueba" className="zona__titulo">
        Probar reconocimiento
      </h2>
      <p>Muestra lo que decide la visión de quien esté frente a la cámara ahora. No marca asistencia ni necesita una sesión abierta.</p>
      <button type="button" className="boton" aria-disabled={inactiva} aria-describedby="ayuda-prueba" onClick={() => !inactiva && solicitar()}>
        {enviando ? 'Enviando la orden…' : esperando ? 'Probando…' : 'Probar reconocimiento'}
      </button>
      <p id="ayuda-prueba" className="etiqueta-tecnica">
        La visión enciende la luz de la tira unos segundos para tomar la foto.
      </p>
      {error !== null && (
        <div className="aviso aviso--error" role="alert">
          <p className="aviso__titulo">No se pudo consultar la prueba</p>
          <p>
            {error}{' '}
            {sinSesion ? 'La vista no insistirá sola.' : prueba !== null && prueba.pendiente ? 'La vista lo vuelve a intentar cada segundo.' : ''}
          </p>
          {sinSesion && (
            <button type="button" className="boton" onClick={reintentar}>
              Reintentar
            </button>
          )}
        </div>
      )}
      {/* Mientras la orden va en camino, lo que se ve es de la prueba anterior: se oculta hasta confirmar la nueva. */}
      {prueba !== null && !enviando && <EstadoPrueba prueba={prueba} horaServidor={horaServidor} recibidoEn={recibidoEn} />}
    </div>
  )
}
