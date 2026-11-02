// Aplica el tema guardado antes de pintar, para que la página no parpadee en el
// tema equivocado. Es un archivo externo y síncrono (la CSP del frontend
// prohíbe scripts en línea). La preferencia "Sistema" no guarda nada y se
// resuelve en CSS con prefers-color-scheme. Debe usar la misma clave y los
// mismos valores que src/tema.ts.
(function () {
  try {
    var tema = localStorage.getItem('autolist-tema')
    if (tema === 'light' || tema === 'dark') {
      document.documentElement.setAttribute('data-theme', tema)
    }
  } catch (error) {
    // Almacenamiento bloqueado: se queda en "Sistema".
  }
})()
