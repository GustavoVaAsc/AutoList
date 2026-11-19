const FILAS = 6

interface Props {
  /** Lo que anuncia el lector de pantalla mientras carga. */
  etiqueta?: string
  filas?: number
}

/**
 * Marcador de la tabla mientras llega la primera respuesta. Ocupa el lugar de
 * los renglones para que la página no salte al llegar los datos, y su pulso
 * (solo opacidad, en un único elemento, 4 ciclos y se detiene) dice que se
 * está esperando. Los bloques son decorativos: el estado lo anuncia el texto
 * oculto. Va dentro de la hoja de la lista, que ya tiene su título.
 */
export function EsqueletoTabla({ etiqueta = 'Cargando la lista de alumnos', filas = FILAS }: Props) {
  return (
    <div aria-busy="true">
      <p role="status" className="solo-lectores">
        {etiqueta}
      </p>
      <div className="esqueleto" aria-hidden="true">
        {Array.from({ length: filas }, (_, i) => (
          <div className="esqueleto__fila" key={i}>
            <span className="esqueleto__bloque esqueleto__bloque--n" />
            <span className="esqueleto__bloque esqueleto__bloque--cuenta" />
            <span className="esqueleto__bloque esqueleto__bloque--nombre" />
            <span className="esqueleto__bloque esqueleto__bloque--grupo" />
            <span className="esqueleto__bloque esqueleto__bloque--rostro" />
          </div>
        ))}
      </div>
    </div>
  )
}

/** Marcador de un conteo del encabezado de la hoja mientras llega la lista. */
export function EsqueletoCifra() {
  return <span className="esqueleto esqueleto__bloque esqueleto__bloque--cifra" aria-hidden="true" />
}
