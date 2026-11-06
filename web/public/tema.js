// Aplica el tema guardado antes de pintar, para que la página no parpadee en el
// tema equivocado. Es un archivo externo y síncrono (la CSP del frontend
// prohíbe scripts en línea). Sin elección guardada, el tema es claro: es una
// lista de papel y en un proyector se lee mejor clara (web/DESIGN.md);
// "Sistema" se guarda como "system", no pone atributo y se resuelve en CSS con
// prefers-color-scheme. Debe usar la misma clave y los mismos valores que
// src/tema.ts.
(function () {
  var tema = 'light'
  try {
    tema = localStorage.getItem('autolist-tema') || 'light'
  } catch (error) {
    // Almacenamiento bloqueado: se queda en claro.
  }
  // "system" no pone atributo; un valor desconocido (guardado a mano o por una
  // versión anterior) vale como "sin elección": claro.
  if (tema !== 'system') {
    document.documentElement.setAttribute('data-theme', tema === 'dark' ? 'dark' : 'light')
  }
})()
