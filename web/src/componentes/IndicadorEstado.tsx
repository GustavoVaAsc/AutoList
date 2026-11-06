import { useEffect, useRef } from 'react'
import { useEstadoLecturas } from '../lecturasContexto'
import { MarcaAsistencia, type TipoMarca } from './MarcaAsistencia'

type EstadoApi = 'conectando' | 'conectada' | 'sin-sesion' | 'sin-conexion'

const ETIQUETA: Record<EstadoApi, string> = {
  conectando: 'Conectando con la placa',
  conectada: 'Placa conectada',
  'sin-sesion': 'Sin sesión en la API',
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
 */
export function IndicadorEstado() {
  const { lecturas, error, sinSesion } = useEstadoLecturas()
  const tipo: EstadoApi = sinSesion ? 'sin-sesion' : error !== null ? 'sin-conexion' : lecturas === null ? 'conectando' : 'conectada'

  // La región live se monta vacía y se llena al cambiar el estado: no anuncia
  // el "Conectando" inicial al cargar la página.
  const anuncio = useRef<HTMLSpanElement>(null)
  const tipoAnterior = useRef(tipo)
  useEffect(() => {
    if (tipo !== tipoAnterior.current) {
      tipoAnterior.current = tipo
      if (anuncio.current !== null) {
        anuncio.current.textContent = `Estado de la placa: ${ETIQUETA[tipo]}`
      }
    }
  }, [tipo])

  return (
    <div className="estado-vivo">
      <MarcaAsistencia tipo={MARCA[tipo]} />
      <span className="estado-vivo__texto">{ETIQUETA[tipo]}</span>
      <span className="solo-lectores" role="status" ref={anuncio} />
    </div>
  )
}
