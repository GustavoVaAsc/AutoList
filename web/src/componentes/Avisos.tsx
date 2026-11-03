import { Toaster } from 'sonner'
import { MarcaAsistencia } from './MarcaAsistencia'

/**
 * Único contenedor de toasts de la aplicación (Sonner), montado una vez en
 * App. Los colores, el radio, la tipografía y las duraciones no se pasan por
 * props: salen de los tokens de estilos.css (sección "Avisos de Sonner"), así
 * siguen el tema Claro u Oscuro y el movimiento reducido. El CSS de Sonner se
 * importa desde main.tsx, no lo inyecta Sonner (ver ahí). Los iconos de
 * éxito, advertencia y error son la marca de asistencia (palomita, casilla
 * vacía y tachita): el mismo gesto que el resto de la interfaz.
 */
export function Avisos() {
  return (
    <Toaster
      position="bottom-right"
      closeButton
      visibleToasts={3}
      containerAriaLabel="Avisos"
      toastOptions={{ closeButtonAriaLabel: 'Cerrar aviso' }}
      icons={{
        success: <MarcaAsistencia tipo="presente" />,
        warning: <MarcaAsistencia tipo="pendiente" />,
        error: <MarcaAsistencia tipo="falta" />,
      }}
    />
  )
}
