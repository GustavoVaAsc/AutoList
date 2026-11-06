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
  return {
    base: '/app/',
    plugins: [react()],
    server: {
      proxy: {
        '/api': {
          target: env.PLACA_URL || 'https://localhost:1880',
          changeOrigin: true,
          secure: false,
        },
      },
    },
  }
})
