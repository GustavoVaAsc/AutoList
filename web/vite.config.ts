import { defineConfig, loadEnv } from 'vite'
import react from '@vitejs/plugin-react'

// El build vive en /app/ porque Node-RED sirve el editor en / (httpStatic con
// root "/app/", nodered/settings.js). Los assets salen con ese prefijo.
//
// En desarrollo, /api se reenvía a la placa. Se define con la variable
// PLACA_URL (https://<ip-de-la-placa>:1880), en el entorno o en un
// archivo web/.env.local, que git ignora. secure: false acepta el certificado
// autofirmado de Node-RED.
export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, '.', '')
  const destino = env.PLACA_URL || 'https://localhost:1880'
  const origenDestino = new URL(destino).origin
  return {
    base: '/app/',
    plugins: [react()],
    server: {
      proxy: {
        '/api': {
          target: destino,
          changeOrigin: true,
          secure: false,
          configure: (proxy) => {
            const emisor = proxy as unknown as {
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
        },
      },
    },
  }
})
