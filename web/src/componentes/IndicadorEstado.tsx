import { useEffect, useRef } from 'react'
import { detalleCabecera } from '../api/sistemaRespuestas'
import { useEstadoLecturas } from '../lecturasContexto'
import { useEstadoSistema } from '../sistemaContexto'
import { MarcaAsistencia, type TipoMarca } from './MarcaAsistencia'

type EstadoApi = 'conectando' | 'conectada' | 'sin-sesion' | 'sin-conexion'

const ETIQUETA: Record<EstadoApi, string> = {
  conectando: 'Conectando con la placa',
  conectada: 'Placa conectada',
  'sin-sesion': 'Falta iniciar sesión',
  'sin-conexion': 'Sin conexión con la placa',
}

// La marca dice el estado como en la hoja: palomita (respondió), casilla vacía
// (todavía no hay respuesta o falta la sesión) y tachita roja (falló).
const MARCA: Record<EstadoApi, TipoMarca> = {
  conectando: 'pendiente',
  conectada: 'presente',
  'sin-sesion': 'pendiente',
  'sin-conexion': 'falta',
}

/**
 * Estado de la comunicación con la placa, el mismo en todas las vistas. Sale de
 * la última consulta de energía (`useLecturas`, que corre en App sea cual sea la
 * vista), no de las consultas de cada vista: la lista de alumnos tiene su propio
 * sondeo y no se sube al contexto. "Placa conectada" quiere decir que esa
 * consulta respondió, o sea que la placa y la API están ahí; no dice nada de si
 * la XIAO publica (eso es la frescura de la lectura y vive en Métricas, con su
 * aviso). Antes este indicador decía "Sin lecturas recientes" estando en
 * cualquier vista, un dato de la XIAO que no tenía que ver con la pantalla.
 * El texto lleva la información y lo lee el lector de pantalla; la marca es
 * redundante y no brilla ni pulsa. La región viva, aparte, anuncia solo los
 * cambios de estado (no el inicial), para no duplicar el texto al cargar.
 * Con la placa conectada, el texto nombra lo que no está en línea (GET
 * /api/sistema/estado, `useSistema` en App): "Placa conectada · nodo del aula
 * fuera de línea", en el rojo de las faltas reales. Si todo está en línea, solo
 * dice "Placa conectada". La marca y "Placa conectada" siguen diciendo solo si la
 * API responde; sin respuesta del estado del sistema, o con su último sondeo
 * fallido (sus datos ya no son vigentes), no hay detalle.
 */
export function IndicadorEstado() {
  const { lecturas, error, sinSesion } = useEstadoLecturas()
  const sistema = useEstadoSistema()
  const tipo: EstadoApi = sinSesion ? 'sin-sesion' : error !== null ? 'sin-conexion' : lecturas === null ? 'conectando' : 'conectada'

  const detalle = tipo === 'conectada' && sistema.error === null ? detalleCabecera(sistema.sistema) : null
  const texto = detalle === null ? ETIQUETA[tipo] : `${ETIQUETA[tipo]} · ${detalle.texto}`

  // La región live se monta vacía y se llena al cambiar el estado: no anuncia
  // el "Conectando" inicial al cargar la página.
  const anuncio = useRef<HTMLSpanElement>(null)
  const textoAnterior = useRef(texto)
  useEffect(() => {
    if (texto !== textoAnterior.current) {
      textoAnterior.current = texto
      if (anuncio.current !== null) {
        anuncio.current.textContent = `Estado de la placa: ${texto}`
      }
    }
  }, [texto])

  return (
    <div className="estado-vivo">
      <MarcaAsistencia tipo={MARCA[tipo]} />
      <span className="estado-vivo__texto">
        {ETIQUETA[tipo]}
        {detalle !== null && (
          <>
            {' · '}
            <span className="rostro--falta">{detalle.texto}</span>
          </>
        )}
      </span>
      <span className="solo-lectores" role="status" ref={anuncio} />
    </div>
  )
}
