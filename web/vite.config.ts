import { defineConfig, loadEnv, type ProxyOptions } from 'vite'
import react from '@vitejs/plugin-react'

// El build vive en /app/ porque Node-RED sirve el editor en / (httpStatic con
// root "/app/", nodered/settings.js). Los assets salen con ese prefijo.
//
// En desarrollo, /api se reenvía a la placa. Se define con la variable
// PLACA_URL (https://<ip-de-la-placa>:1880), en el entorno o en un
// archivo web/.env.local, que git ignora. secure: false acepta el certificado
// autofirmado de Node-RED.
//
// La vista previa de la cámara (GET /api/camara/previa) puede venir de otro
// sitio: con PREVIA_URL (por ejemplo, el servidor de desarrollo de la webcam,
// herramientas/previa_desarrollo.py) esa ruta se reenvía allí y el resto de
// /api sigue yendo a la placa. La ruta más específica va antes, porque el proxy
// de Vite usa la primera que coincide.
export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, '.', '')
  const destino = env.PLACA_URL || 'https://localhost:1880'
  const origenDestino = new URL(destino).origin
  const previa = env.PREVIA_URL

  const proxy: Record<string, string | ProxyOptions> = {}
  if (previa) {
    proxy['/api/camara/previa'] = {
      target: previa,
      changeOrigin: true,
    }
  }
  proxy['/api'] = {
    target: destino,
    changeOrigin: true,
    secure: false,
    configure: (servidor) => {
      const emisor = servidor as unknown as {
        on(
          evento: 'proxyReq',
          manejador: (
            proxyReq: { setHeader(nombre: string, valor: string): void },
            req: { headers: { origin?: string } },
          ) => void,
        ): void
      }
      emisor.on('proxyReq', (proxyReq, req) => {
        if (req.headers.origin !== undefined) {
          proxyReq.setHeader('origin', origenDestino)
        }
      })
    },
  }

  return {
    base: '/app/',
    plugins: [react()],
    server: {
      proxy,
    },
  }
})
