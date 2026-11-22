import { useEffect, useState } from 'react'
import { ORDENES, disponibilidadActuadores, estadoEnPalabras, etiquetaEstadoActuadores, pideConfirmacion, type GrupoOrden, type Orden } from '../api/actuadoresRespuestas'
import { filasSistema, type Sistema } from '../api/sistemaRespuestas'
import { useActuadores } from '../useActuadores'
import { DialogoPuerta } from './DialogoPuerta'

interface Props {
  /** Lo último que Node-RED sabe de la XIAO y los actuadores; null mientras no hay respuesta. */
  sistema: Sistema | null
  /** El último sondeo del estado del sistema falló: no se mandan órdenes sin saber si la XIAO responde. */
  errorEstado: boolean
  /** Vuelve a consultar el estado del sistema (para ver el efecto de una orden sin esperar el sondeo). */
  reconsultar: () => void
}

interface Grupo {
  grupo: GrupoOrden
  titulo: string
  /** Cómo se llama lo que se ve del grupo en la línea de estado ("Puerta: cerrada"). */
  campo: 'puerta' | 'led' | 'luz' | null
}

const GRUPOS: readonly Grupo[] = [
  { grupo: 'puerta', titulo: 'Puerta', campo: 'puerta' },
  { grupo: 'led', titulo: 'LED', campo: 'led' },
  { grupo: 'buzzer', titulo: 'Buzzer', campo: null },
  { grupo: 'luz', titulo: 'Luz', campo: 'luz' },
]

/**
 * Control manual del nodo del aula: puerta, LED, buzzer y luz, con el estado actual de cada uno en palabras
 * (por GET /api/sistema/estado). Con el nodo fuera de línea los botones se desactivan y un solo aviso dice por qué.
 * Los botones usan `aria-disabled` y no `disabled`: conservan el foco mientras la orden va en camino. "Abrir puerta",
 * la orden con riesgo, es el botón destacado y pide confirmación en un diálogo (DialogoPuerta); el LED, el buzzer y
 * la luz son botones pequeños y salen con un clic. Mismas piezas que la hoja de Sistema (web/DESIGN.md, Composición).
 */
export function HojaActuadores({ sistema, errorEstado, reconsultar }: Props) {
  const { ocupado, enviando, enviar } = useActuadores(reconsultar)
  // Confirmación de la puerta: preguntando (el diálogo espera la decisión) o enviando (se confirmó y la orden va en camino).
  const [confirmacion, setConfirmacion] = useState<{ orden: Orden; fase: 'preguntando' | 'enviando' } | null>(null)
  const camara = filasSistema(sistema).find((f) => f.clave === 'camara')
  const conexion = camara?.conexion ?? null
  // Con el sondeo fallando no se sabe si la XIAO responde ahora, aunque haya un estado anterior.
  const disponibilidad = disponibilidadActuadores(conexion, errorEstado)
  const estado = estadoEnPalabras(sistema?.actuadores?.datos ?? null)
  const etiquetaEstado = etiquetaEstadoActuadores(conexion, errorEstado)
  const bloqueado = ocupado || !disponibilidad.habilitado
  // Un solo aviso: el motivo del bloqueo o, sin él, la advertencia de que la orden puede no llegar.
  const aviso = disponibilidad.motivo ?? disponibilidad.aviso

  // El diálogo se cierra cuando la API responde a la orden confirmada (el resultado sale en un aviso).
  const enviandoConfirmada = confirmacion !== null && confirmacion.fase === 'enviando'
  useEffect(() => {
    if (enviandoConfirmada && enviando === null) {
      setConfirmacion(null)
    }
  }, [enviandoConfirmada, enviando])

  // Si el panel se bloquea mientras el diálogo espera la decisión (la XIAO salió de línea o el sondeo del estado falla),
  // el diálogo se cierra: ya no puede confirmar nada, y el motivo está a la vista en esta hoja (el aviso de arriba),
  // a donde vuelve el foco, el botón "Abrir puerta", que lo describe con aria-describedby. Dejarlo abierto con el
  // motivo repetido adentro mantendría tapada la hoja justo cuando hay algo que leer en ella.
  const preguntando = confirmacion !== null && confirmacion.fase === 'preguntando'
  useEffect(() => {
    if (preguntando && bloqueado) {
      setConfirmacion(null)
    }
  }, [preguntando, bloqueado])

  const pulsar = (orden: Orden) => {
    if (bloqueado) {
      return
    }
    if (pideConfirmacion(orden)) {
      setConfirmacion({ orden, fase: 'preguntando' })
    } else {
      enviar(orden)
    }
  }

  const boton = (orden: Orden) => (
    <button
      key={orden.id}
      type="button"
      className={pideConfirmacion(orden) ? 'boton boton--primario' : 'boton boton--fila'}
      aria-disabled={bloqueado}
      aria-describedby={aviso !== null ? 'motivo-actuadores' : undefined}
      onClick={() => pulsar(orden)}
    >
      {enviando === orden.id ? 'Enviando…' : orden.etiqueta}
    </button>
  )

  return (
    <>
      <section className="hoja" aria-labelledby="titulo-actuadores">
        <div className="intentos__resumen">
          <h2 id="titulo-actuadores" className="hoja__subtitulo">
            Actuadores
          </h2>
          {aviso !== null && (
            <p
              id="motivo-actuadores"
              className={`aviso ${disponibilidad.motivo !== null && (conexion === 'fuera-de-linea' || errorEstado) ? 'aviso--error' : 'aviso--advertencia'}`}
            >
              {aviso}
            </p>
          )}

          {GRUPOS.map((g) => (
            <div key={g.grupo} className="hoja__barra">
              <div>
                <h3 className="hoja__subtitulo">{g.titulo}</h3>
                {g.campo !== null && (
                  <p className="etiqueta-tecnica">
                    {etiquetaEstado}: {estado[g.campo]}
                  </p>
                )}
              </div>
              <div className="dialogo__acciones">{ORDENES.filter((o) => o.grupo === g.grupo).map(boton)}</div>
            </div>
          ))}
        </div>
      </section>
      <DialogoPuerta
        abierto={confirmacion !== null}
        enviando={enviandoConfirmada}
        bloqueado={bloqueado}
        alConfirmar={() => {
          // Solo pasa a "enviando" si la orden salió; si no, `enviar` ya avisó (demasiado rápido).
          if (confirmacion !== null && !bloqueado && enviar(confirmacion.orden)) {
            setConfirmacion({ orden: confirmacion.orden, fase: 'enviando' })
          }
        }}
        alCancelar={() => setConfirmacion(null)}
      />
    </>
  )
}
