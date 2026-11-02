import { useTema, type PreferenciaTema } from '../tema'

const OPCIONES: { valor: PreferenciaTema; nombre: string }[] = [
  { valor: 'sistema', nombre: 'Sistema' },
  { valor: 'claro', nombre: 'Claro' },
  { valor: 'oscuro', nombre: 'Oscuro' },
]

/** Selector nativo de tema con etiqueta visible: Sistema, Claro u Oscuro. Se guarda en este navegador. */
export function SelectorTema() {
  const [preferencia, elegir] = useTema()
  return (
    <div className="selector-tema">
      <label htmlFor="selector-tema" className="selector-tema__etiqueta">Tema</label>
      <select
        id="selector-tema"
        className="selector"
        value={preferencia}
        onChange={(e) => elegir(e.target.value as PreferenciaTema)}
      >
        {OPCIONES.map((o) => (
          <option key={o.valor} value={o.valor}>
            {o.nombre}
          </option>
        ))}
      </select>
    </div>
  )
}
