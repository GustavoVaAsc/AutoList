// Lógica pura de Registro (alta del rostro) y de la búsqueda de Alumnos: leer el
// estado del registro, traducir poses y motivos, interpretar las respuestas de
// iniciar, cancelar y retirar el rostro, buscar alumnos y la máquina de la vista.
// Sin DOM, sin fetch y sin importar nada: la prueba `node --test web/pruebas/*.test.js`
// la carga directo (Node 22.18 o posterior quita los tipos solo), así que aquí solo
// va sintaxis de TypeScript que se borra sin más (nada de enum).
//
// El contrato está en docs/HIGH-LEVEL-PLAN.md (sección 6.7) y en orangepi/README.md
// (POST /api/enrolamiento, POST /api/enrolamiento/cancelar, GET /api/enrolamiento/estado
// y DELETE /api/alumnos/:cuenta/rostro). La fuente de verdad son los flujos de Node-RED
// (pestaña "Registro"): el servidor manda y esta capa solo lo traduce.

/** Las cinco poses del registro, en el orden en que se toman (contrato MQTT, aula/vision/enrolamiento). */
export const POSES = ['frente', 'izquierda', 'derecha', 'arriba', 'frente_2'] as const
export type NombrePose = (typeof POSES)[number]

export type EstadoPose = 'pendiente' | 'esperando_pose' | 'capturando' | 'aceptada' | 'rechazada'
const ESTADOS_POSE: readonly EstadoPose[] = ['pendiente', 'esperando_pose', 'capturando', 'aceptada', 'rechazada']

/**
 * Cómo va el registro, según Node-RED:
 * - sin_registro: ninguno desde que arrancó Node-RED;
 * - esperando_vision: se ordenó enrolar y la visión aún no contesta;
 * - en_curso: la visión ya manda el progreso de las poses;
 * - guardando: la visión aceptó las 5 poses y falta que llegue y se guarde el embedding;
 * - completado, cancelado, error: cerrado.
 */
export type EstadoRegistro = 'sin_registro' | 'esperando_vision' | 'en_curso' | 'guardando' | 'completado' | 'cancelado' | 'error'
const ESTADOS_REGISTRO: readonly EstadoRegistro[] = ['sin_registro', 'esperando_vision', 'en_curso', 'guardando', 'completado', 'cancelado', 'error']

export interface PoseRegistro {
  pose: NombrePose
  estado: EstadoPose
  /** Código del motivo del rechazo (borrosa, rostro_chico...), si lo hay. */
  motivo: string | null
  /**
   * La miniatura del rostro (JPEG en base64) que la cámara mandó con esa pose.
   * Es efímera: solo existe mientras el registro está abierto y no se guarda en
   * ninguna parte. Si no llega o no tiene esa forma, es `null`.
   */
  miniatura: string | null
}

/** Una de las fotos no se parece a nadie más; solo la cuenta parecida, nunca embeddings. */
export interface AdvertenciaParecido {
  parecidoA: string | null
}

/** GET /api/enrolamiento/estado. Nunca lleva embeddings; las miniaturas de las poses sí, y solo mientras el registro está abierto. */
export interface Registro {
  abierto: boolean
  cuenta: string | null
  nombre: string | null
  estado: EstadoRegistro
  motivo: string | null
  /** Siempre las 5 poses, en su orden. */
  poses: PoseRegistro[]
  advertencia: AdvertenciaParecido | null
  /** Milisegundos desde 1970, según el reloj de la placa. */
  inicio: number | null
  actualizado: number | null
}

export type LecturaRegistro = { ok: true; registro: Registro } | { ok: false; motivo: string }

function objeto(valor: unknown): Record<string, unknown> | null {
  return typeof valor === 'object' && valor !== null && !Array.isArray(valor) ? (valor as Record<string, unknown>) : null
}

function textoONulo(valor: unknown): string | null | undefined {
  return valor === null ? null : typeof valor === 'string' ? valor : undefined
}

/** Tope de la miniatura: el contrato dice ~8 KB de JPEG, que en base64 son ~11 KB. */
const MINIMIURA_MAX = 12288
const BASE64 = /^[A-Za-z0-9+/]+={0,2}$/

/**
 * La miniatura de una pose, si es una miniatura (JPEG en base64). Lo que no tenga
 * esa forma se ignora (queda `null`): una miniatura rara no es motivo para rechazar
 * el estado entero del registro.
 */
function miniaturaDe(valor: unknown): string | null {
  if (typeof valor !== 'string' || valor.length === 0 || valor.length > MINIMIURA_MAX) {
    return null
  }
  return BASE64.test(valor) ? valor : null
}

function fechaONula(valor: unknown): number | null | undefined {
  if (valor === null) {
    return null
  }
  if (typeof valor !== 'string') {
    return undefined
  }
  const ms = Date.parse(valor)
  return Number.isNaN(ms) ? undefined : ms
}

const NO_RECONOCIDO = 'La API devolvió un estado del registro que la vista no reconoce. Avisa al responsable del borde.'

/**
 * Valida GET /api/enrolamiento/estado. Se copian solo los campos conocidos:
 * aunque la API algún día mandara más, la vista no los conserva. Las poses que
 * falten se completan como pendientes, así la vista siempre dibuja cinco.
 */
export function leerRegistro(cuerpo: unknown): LecturaRegistro {
  const o = objeto(cuerpo)
  if (o === null) {
    return { ok: false, motivo: NO_RECONOCIDO }
  }
  const cuenta = textoONulo(o.cuenta)
  const nombre = textoONulo(o.nombre)
  const motivo = textoONulo(o.motivo)
  const inicio = fechaONula(o.inicio)
  const actualizado = fechaONula(o.actualizado)
  const estado = o.estado
  if (
    typeof o.abierto !== 'boolean' ||
    cuenta === undefined ||
    nombre === undefined ||
    motivo === undefined ||
    inicio === undefined ||
    actualizado === undefined ||
    typeof estado !== 'string' ||
    !(ESTADOS_REGISTRO as readonly string[]).includes(estado) ||
    !Array.isArray(o.poses)
  ) {
    return { ok: false, motivo: NO_RECONOCIDO }
  }
  const porNombre = new Map<NombrePose, PoseRegistro>()
  for (const elemento of o.poses as unknown[]) {
    const p = objeto(elemento)
    const pose = p?.pose
    const estadoPose = p?.estado
    const motivoPose = textoONulo(p?.motivo)
    if (
      p === null ||
      typeof pose !== 'string' ||
      !(POSES as readonly string[]).includes(pose) ||
      typeof estadoPose !== 'string' ||
      !(ESTADOS_POSE as readonly string[]).includes(estadoPose) ||
      motivoPose === undefined
    ) {
      return { ok: false, motivo: NO_RECONOCIDO }
    }
    porNombre.set(pose as NombrePose, { pose: pose as NombrePose, estado: estadoPose as EstadoPose, motivo: motivoPose, miniatura: miniaturaDe(p?.miniatura) })
  }
  let advertencia: AdvertenciaParecido | null = null
  if (o.advertencia !== null && o.advertencia !== undefined) {
    const a = objeto(o.advertencia)
    const parecidoA = textoONulo(a?.parecido_a)
    if (a === null || parecidoA === undefined) {
      return { ok: false, motivo: NO_RECONOCIDO }
    }
    advertencia = { parecidoA }
  }
  return {
    ok: true,
    registro: {
      abierto: o.abierto,
      cuenta,
      nombre,
      estado: estado as EstadoRegistro,
      motivo,
      poses: POSES.map((pose) => porNombre.get(pose) ?? { pose, estado: 'pendiente', motivo: null, miniatura: null }),
      advertencia,
      inicio,
      actualizado,
    },
  }
}

// ---------------------------------------------------------------- textos

/** Lo que se le pide al alumno en cada pose. */
export const ETIQUETA_POSE: Record<NombrePose, string> = {
  frente: 'De frente',
  izquierda: 'Un poco a la izquierda',
  derecha: 'Un poco a la derecha',
  arriba: 'Un poco hacia arriba',
  frente_2: 'De frente otra vez',
}

const MOTIVOS: Record<string, string> = {
  sin_rostro: 'No se vio ningún rostro.',
  varios_rostros: 'Se vio más de un rostro.',
  rostro_chico: 'El rostro se ve muy pequeño: el alumno debe acercarse a la cámara.',
  borrosa: 'La foto salió borrosa.',
  giro_excesivo: 'La cabeza está demasiado girada.',
  fallo_captura: 'La cámara no pudo tomar la foto.',
  ocupado: 'El servicio de visión está ocupado con otra tarea.',
  inconsistente: 'Las fotos no se parecen entre sí.',
  npu: 'El servicio de visión no pudo procesar las fotos (falló la NPU o se rechazaron demasiadas veces las poses).',
  cuenta_invalida: 'La cuenta que recibió el servicio de visión no es válida.',
  vencido: 'Pasaron 5 minutos sin noticias del servicio de visión.',
  no_guardado: 'La base de datos no guardó el rostro.',
  embedding_invalido: 'El servicio de visión mandó un resultado que no se pudo usar.',
  desconocido: 'El servicio de visión terminó con un error sin motivo conocido.',
}

/** Traduce el código de un motivo; sin motivo, texto vacío. Un código nuevo se nombra tal cual. */
export function traducirMotivo(motivo: string | null): string {
  if (motivo === null) {
    return ''
  }
  return MOTIVOS[motivo] ?? `Motivo sin traducir (código ${motivo}).`
}

const ESTADO_POSE: Record<EstadoPose, string> = {
  pendiente: 'Pendiente',
  esperando_pose: 'Esperando la pose',
  capturando: 'Tomando la foto',
  aceptada: 'Foto aceptada',
  rechazada: 'Foto rechazada',
}

/** Estado de un renglón de pose y, si lo rechazaron, el motivo en español. */
export function textoPose(pose: PoseRegistro): { estado: string; detalle: string | null } {
  const detalle = pose.estado === 'rechazada' && pose.motivo !== null ? traducirMotivo(pose.motivo) : null
  return { estado: ESTADO_POSE[pose.estado], detalle }
}

export type TipoMarca = 'presente' | 'pendiente' | 'falta'

/** La marca de asistencia de una pose: palomita si se aceptó, tachita si se rechazó. */
export function marcaDePose(estado: EstadoPose): TipoMarca {
  return estado === 'aceptada' ? 'presente' : estado === 'rechazada' ? 'falta' : 'pendiente'
}

/** "12 s", "2 min 5 s". */
export function formatearTranscurrido(ms: number): string {
  const total = Number.isFinite(ms) && ms > 0 ? Math.floor(ms / 1000) : 0
  const minutos = Math.floor(total / 60)
  const segundos = total % 60
  return minutos === 0 ? `${segundos} s` : `${minutos} min ${segundos} s`
}

/** Índice (0 a 4) de la primera pose sin aceptar, o null si las cinco ya se aceptaron. */
export function poseEnCurso(registro: Registro): number | null {
  const i = registro.poses.findIndex((p) => p.estado !== 'aceptada')
  return i === -1 ? null : i
}

/** El tiempo transcurrido, según el reloj de la placa; null si no se conoce o el registro ya terminó. */
export function textoTiempo(registro: Registro, transcurridoMs: number | null): string | null {
  if (!registro.abierto || transcurridoMs === null) {
    return null
  }
  if (registro.estado === 'esperando_vision') {
    return `Van ${formatearTranscurrido(transcurridoMs)} sin respuesta.`
  }
  return registro.estado === 'en_curso' || registro.estado === 'guardando' ? `Tiempo transcurrido: ${formatearTranscurrido(transcurridoMs)}` : null
}

export type Tono = 'neutro' | 'advertencia' | 'error' | 'exito'

export interface ResumenRegistro {
  titulo: string
  detalle: string
  tono: Tono
}

/**
 * Lo que la vista dice del registro. Mientras la visión no contesta lo dice tal
 * cual: no simula progreso. El texto no lleva el tiempo, que cambia cada segundo y
 * va aparte (`textoTiempo`), fuera de la región viva que lo lee un lector de pantalla.
 */
export function resumirRegistro(registro: Registro): ResumenRegistro {
  switch (registro.estado) {
    case 'esperando_vision':
      return {
        titulo: 'Esperando al servicio de visión',
        detalle: 'Si el servicio de visión no está corriendo en la placa, el registro no avanza y se cancela solo a los 5 minutos.',
        tono: 'advertencia',
      }
    case 'en_curso': {
      const i = poseEnCurso(registro)
      return {
        titulo: 'Registro en curso',
        detalle: i === null ? 'Las cinco poses están aceptadas.' : `Pose ${i + 1} de 5: ${ETIQUETA_POSE[POSES[i]!].toLowerCase()}.`,
        tono: 'neutro',
      }
    }
    case 'guardando':
      return {
        titulo: 'Guardando el rostro',
        detalle: 'Las cinco poses están aceptadas. Falta recibir el resultado y guardarlo en la base de datos.',
        tono: 'neutro',
      }
    case 'completado':
      return {
        titulo: 'Registro completado',
        detalle: `${registro.nombre ?? 'El alumno'} quedó con el rostro registrado.`,
        tono: 'exito',
      }
    case 'cancelado':
      return { titulo: 'Registro cancelado', detalle: 'No se guardó ningún rostro.', tono: 'neutro' }
    case 'error':
      return {
        titulo: 'El registro terminó con error',
        detalle: `${traducirMotivo(registro.motivo ?? 'desconocido')} No se guardó ningún rostro.`,
        tono: 'error',
      }
    case 'sin_registro':
      return { titulo: 'Sin registro', detalle: 'Todavía no se inició ningún registro.', tono: 'neutro' }
  }
}

// ---------------------------------------------------------------- respuestas de la API

function fallo(status: number | null, mensaje: string): { tipo: 'fallo'; status: number | null; mensaje: string } {
  return { tipo: 'fallo', status, mensaje }
}

function motivoDe(cuerpo: unknown): string | null {
  const m = objeto(cuerpo)?.motivo
  return typeof m === 'string' ? m : null
}

/** Texto de los códigos que cualquier endpoint de cambio puede devolver; `accion` completa la frase. */
function falloGenerico(status: number, accion: string): { tipo: 'fallo'; status: number; mensaje: string } {
  switch (status) {
    case 400:
      return { tipo: 'fallo', status, mensaje: `La API rechazó los datos enviados. ${accion}` }
    case 401:
      return { tipo: 'fallo', status, mensaje: `La API pide usuario y contraseña del profesor y no se completó el inicio de sesión. ${accion}` }
    case 403:
      return {
        tipo: 'fallo',
        status,
        mensaje: `La API rechazó la petición por su origen o su cabecera de seguridad. Abre la página desde la dirección de la placa (https://<ip-de-la-placa>:1880/app/). ${accion}`,
      }
    case 415:
      return { tipo: 'fallo', status, mensaje: `La API rechazó el tipo de contenido enviado. ${accion}` }
    default:
      return { tipo: 'fallo', status, mensaje: `La API respondió con el error ${status}. ${accion} Si sigue igual, avisa al responsable del borde.` }
  }
}

export type ResultadoInicio =
  | { tipo: 'iniciado' }
  | { tipo: 'no-existe' }
  /** El alumno ya tiene rostro: hay que retirarlo antes de volver a registrar. */
  | { tipo: 'tiene-rostro' }
  /** Ya hay otro registro abierto. */
  | { tipo: 'ocupado' }
  /** El lugar se cerró mientras se escribía el consentimiento. */
  | { tipo: 'interrumpido' }
  | { tipo: 'fallo'; status: number | null; mensaje: string }

/** Traduce la respuesta de POST /api/enrolamiento. `cuerpo` es el JSON ya leído, o undefined si no era JSON. */
export function interpretarInicio(status: number, cuerpo: unknown): ResultadoInicio {
  if (status === 202) {
    return { tipo: 'iniciado' }
  }
  if (status === 404) {
    return { tipo: 'no-existe' }
  }
  if (status === 409) {
    const motivo = motivoDe(cuerpo)
    if (motivo === 'tiene_rostro') {
      return { tipo: 'tiene-rostro' }
    }
    if (motivo === 'registro_abierto') {
      return { tipo: 'ocupado' }
    }
    if (motivo === 'interrumpido') {
      return { tipo: 'interrumpido' }
    }
    return fallo(status, 'La API rechazó el registro porque hay un conflicto con el estado actual. No se inició.')
  }
  return falloGenerico(status, 'No se inició el registro.')
}

export type ResultadoCancelacion =
  /** `habia` es false si no había un registro abierto (la API es idempotente). */
  | { tipo: 'cancelado'; habia: boolean }
  | { tipo: 'fallo'; status: number | null; mensaje: string }

/** Traduce la respuesta de POST /api/enrolamiento/cancelar. */
export function interpretarCancelacion(status: number, cuerpo: unknown): ResultadoCancelacion {
  if (status === 200) {
    const o = objeto(cuerpo)
    return typeof o?.cancelado === 'boolean'
      ? { tipo: 'cancelado', habia: o.cancelado }
      : fallo(status, 'La API respondió algo que la vista no reconoce. Revisa el estado del registro.')
  }
  return falloGenerico(status, 'No se pudo confirmar la cancelación: revisa el estado del registro.')
}

export type ResultadoRetiro =
  /** `habiaRostro` es false si el alumno ya estaba pendiente. */
  | { tipo: 'retirado'; habiaRostro: boolean }
  | { tipo: 'no-existe' }
  /** Hay un registro abierto de ese alumno: se cancela antes. */
  | { tipo: 'registro-abierto' }
  | { tipo: 'fallo'; status: number | null; mensaje: string }

/** Traduce la respuesta de DELETE /api/alumnos/:cuenta/rostro. */
export function interpretarRetiro(status: number, cuerpo: unknown): ResultadoRetiro {
  if (status === 200) {
    const o = objeto(cuerpo)
    return typeof o?.retirado === 'boolean'
      ? { tipo: 'retirado', habiaRostro: o.retirado }
      : fallo(status, 'La API respondió algo que la vista no reconoce. Revisa la lista antes de reintentar.')
  }
  if (status === 404) {
    return { tipo: 'no-existe' }
  }
  if (status === 409 && motivoDe(cuerpo) === 'registro_abierto') {
    return { tipo: 'registro-abierto' }
  }
  return falloGenerico(status, 'No se retiró el rostro: revisa la lista antes de reintentar.')
}

/** Un aviso que la vista deja a la vista (role="alert") tras una acción que no terminó en éxito. */
export interface AvisoRegistro {
  tono: 'error' | 'advertencia'
  titulo: string
  texto: string
}

/** Qué dice la vista cuando iniciar no terminó en "iniciado" (null si sí). */
export function avisoDeInicio(resultado: ResultadoInicio): AvisoRegistro | null {
  switch (resultado.tipo) {
    case 'iniciado':
      return null
    case 'no-existe':
      return { tono: 'error', titulo: 'El alumno ya no está en la lista', texto: 'La cuenta no existe en la lista de alumnos. Revisa la lista en Alumnos y vuelve a elegir.' }
    case 'tiene-rostro':
      return { tono: 'error', titulo: 'El alumno ya tiene rostro registrado', texto: 'Para volver a registrarlo, retira primero su rostro en Alumnos. Después hace falta un consentimiento nuevo.' }
    case 'ocupado':
      return { tono: 'advertencia', titulo: 'Ya había un registro abierto', texto: 'Se retomó el registro que estaba abierto. Termínalo o cancélalo antes de iniciar otro.' }
    case 'interrumpido':
      return { tono: 'advertencia', titulo: 'El registro se interrumpió antes de empezar', texto: 'Se canceló mientras se guardaba el consentimiento. Vuelve a iniciarlo.' }
    case 'fallo':
      return { tono: 'error', titulo: 'No se pudo iniciar el registro', texto: resultado.mensaje }
  }
}

/** Lo que dice la vista cuando el registro abierto desaparece de Node-RED (reinicio o despliegue de los flujos). */
export const AVISO_REGISTRO_PERDIDO: AvisoRegistro = {
  tono: 'advertencia',
  titulo: 'El registro ya no está abierto',
  texto:
    'Node-RED no tiene ningún registro abierto: se reinició o se desplegaron los flujos. Revisa en Alumnos si el rostro quedó registrado y, si no, vuelve a iniciarlo.',
}

/** Qué dice la vista cuando cancelar no se pudo confirmar (null si se confirmó). */
export function avisoDeCancelacion(resultado: ResultadoCancelacion): AvisoRegistro | null {
  return resultado.tipo === 'fallo' ? { tono: 'error', titulo: 'No se pudo cancelar el registro', texto: resultado.mensaje } : null
}

// ---------------------------------------------------------------- alumnos

/** Lo mínimo de un alumno que usan la búsqueda y el orden (es un Alumno de alumnosRespuestas.ts). */
interface AlumnoBuscable {
  cuenta: string
  nombre: string
  grupo: string
  rostro: 'pendiente' | 'registrado'
}

/** Sin acentos, en minúsculas y sin espacios en los extremos: "Álvaro Núñez" y "alvaro nunez" son lo mismo. */
export function normalizarBusqueda(texto: string): string {
  return texto.normalize('NFD').replace(/\p{M}/gu, '').toLowerCase().replace(/\s+/g, ' ').trim()
}

/**
 * Alumnos cuyo nombre o cuenta contiene todas las palabras de la búsqueda, en
 * cualquier orden. El texto se toma literal (no es una expresión regular).
 * Búsqueda vacía: la misma lista.
 */
export function filtrarAlumnos<T extends { cuenta: string; nombre: string }>(alumnos: T[], consulta: string): T[] {
  const palabras = normalizarBusqueda(consulta).split(/\s+/).filter((p) => p !== '')
  if (palabras.length === 0) {
    return alumnos
  }
  return alumnos.filter((a) => {
    const texto = normalizarBusqueda(`${a.nombre} ${a.cuenta}`)
    return palabras.every((p) => texto.includes(p))
  })
}

/** Los pendientes primero (son a quienes se registra), luego grupo, nombre y cuenta. No modifica la entrada. */
export function ordenarParaRegistro<T extends AlumnoBuscable>(alumnos: T[]): T[] {
  return [...alumnos].sort(
    (a, b) =>
      Number(a.rostro === 'registrado') - Number(b.rostro === 'registrado') ||
      a.grupo.localeCompare(b.grupo, 'es') ||
      a.nombre.localeCompare(b.nombre, 'es') ||
      a.cuenta.localeCompare(b.cuenta),
  )
}

// ---------------------------------------------------------------- máquina de la vista

/**
 * - comprobando: aún no llegó la primera consulta del estado;
 * - libre: no hay registro abierto, se elige alumno;
 * - iniciando: se envió POST /api/enrolamiento y falta su respuesta y la consulta siguiente;
 * - abierto: hay un registro abierto (iniciado aquí, o retomado al cargar la página);
 * - terminado: el registro que se veía abierto se cerró; se muestra cómo terminó.
 */
export type FaseVista = 'comprobando' | 'libre' | 'iniciando' | 'abierto' | 'terminado'

export interface EstadoVista {
  fase: FaseVista
  /** El registro que se muestra (abierto o terminado); null en las demás fases. */
  registro: Registro | null
}

export type EventoVista =
  | { tipo: 'estado'; registro: Registro }
  | { tipo: 'inicio-enviado' }
  | { tipo: 'inicio-fallido' }
  | { tipo: 'descartar-resultado' }

export const VISTA_INICIAL: EstadoVista = { fase: 'comprobando', registro: null }

/**
 * Transición de la vista. Un registro cerrado solo se muestra si se vio abierto en
 * esta vista (fase abierto): al cargar la página, o mientras se inicia otro, un
 * "cerrado" es el de una sesión anterior y no se enseña. Si el registro abierto
 * desaparece (sin_registro: Node-RED perdió la inscripción), la vista vuelve a libre.
 * Función pura: no modifica el estado de entrada.
 */
export function siguienteVista(actual: EstadoVista, evento: EventoVista): EstadoVista {
  switch (evento.tipo) {
    case 'estado': {
      const { registro } = evento
      if (registro.abierto) {
        return { fase: 'abierto', registro }
      }
      if (actual.fase === 'abierto') {
        // Sin_registro con la vista en un registro abierto: Node-RED se reinició o desplegó los
        // flujos y perdió la inscripción (viven en memoria). No hay resultado que mostrar.
        return registro.estado === 'sin_registro' ? { fase: 'libre', registro: null } : { fase: 'terminado', registro }
      }
      if (actual.fase === 'comprobando') {
        return { fase: 'libre', registro: null }
      }
      return actual
    }
    case 'inicio-enviado':
      return { fase: 'iniciando', registro: null }
    case 'inicio-fallido':
      return actual.fase === 'iniciando' ? { fase: 'libre', registro: null } : actual
    case 'descartar-resultado':
      return actual.fase === 'terminado' ? { fase: 'libre', registro: null } : actual
  }
}
