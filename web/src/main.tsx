import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
// Sonner inyecta su CSS en una etiqueta <style> y la CSP del frontend (style-src
// 'self', nodered/settings.js) la bloquea: la consola lo avisa y es inofensivo,
// porque la hoja se importa aquí como archivo. Va antes que estilos.css, que la
// ajusta a los tokens y gana por orden.
import 'sonner/dist/styles.css'
import './estilos.css'
import { App } from './App'

const raiz = document.getElementById('raiz')
if (!raiz) {
  throw new Error('Falta el elemento #raiz en index.html')
}

createRoot(raiz).render(
  <StrictMode>
    <App />
  </StrictMode>,
)
