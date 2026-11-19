/** Lo que no hay se dice siempre igual: un guion, y "Sin dato" para el lector de pantalla. */
export function Vacio() {
  return (
    <span className="tabla__vacio">
      <span aria-hidden="true">–</span>
      <span className="solo-lectores">Sin dato</span>
    </span>
  )
}
