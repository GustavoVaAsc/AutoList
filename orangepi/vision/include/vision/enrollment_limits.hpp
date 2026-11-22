#pragma once

// Valores por omision y topes de la verificacion del registro (pose, identidad, rostros de
// fondo y duplicados), en UN solo lugar: los leen PoseLimits (head_pose.hpp), ServiceConfig
// (vision_service.hpp), ThresholdSettings y el analisis de vision.conf (process_config.*) y la
// validacion del constructor del servicio. Si el valor cambia aqui, cambia en todos.
//
// Todos los valores por omision son PROVISIONALES: no se han calibrado con la ESP32 (ver el
// comentario de cada grupo en vision_service.hpp y en el README).

namespace vision::enrollment_limits {

// --- Pose (grados aproximados; ver PoseLimits) ---
inline constexpr float kMaxFrontalYawDeg = 15.0f;
inline constexpr float kMinLateralYawDeg = 20.0f;
inline constexpr float kMaxYawDeg = 55.0f;
inline constexpr float kMaxFirstPitchDeg = 30.0f;
inline constexpr float kMaxFrontalPitchDeg = 20.0f;
inline constexpr float kMinUpPitchDeg = 8.0f;
inline constexpr float kMaxPitchDeg = 45.0f;
// El pitch de "frente" solo sirve de referencia para "arriba" hasta este valor (ver PoseLimits).
inline constexpr float kMaxReferencePitchDeg = 15.0f;
// Topes: atan nunca llega a 90 grados, y "arriba" no puede confundirse con el ruido (~7 grados).
inline constexpr float kAngleCeilingDeg = 90.0f;
inline constexpr float kMinUpPitchFloorDeg = 5.0f;

// --- Identidad contra "frente" ---
inline constexpr float kMinFrontSimilarity = 0.20f;
inline constexpr float kMinFront2Similarity = 0.45f;
inline constexpr int kMaxIdentityRejections = 3;
// Avisa al arrancar por encima de este numero de rechazos de identidad.
inline constexpr int kHighIdentityRejections = 10;

// --- Rostros de fondo ---
inline constexpr float kOtherFaceMinAreaRatio = 0.35f;
inline constexpr float kOtherFaceMinScore = 0.50f;
// Por encima de estos topes el control casi no detecta nada: se rechaza la configuracion.
inline constexpr float kOtherFaceMaxAreaRatio = 0.8f;
inline constexpr float kOtherFaceMaxScore = 0.9f;
// Por encima de estos valores se avisa al arrancar (el control esta muy debilitado).
inline constexpr float kOtherFaceWeakAreaRatio = 0.6f;
inline constexpr float kOtherFaceWeakScore = 0.8f;

// --- Duplicados en la galeria ---
// Coseno (media final contra otra cuenta) desde el que el registro cierra como duplicado. No
// puede pasar de min_dudoso_score: si no, la misma persona podria tener dos cuentas con
// parecidos que el pase de lista ya trata como "dudoso" o "reconocido".
inline constexpr float kMinDuplicateSimilarity = 0.45f;

// --- Galeria ---
// Intervalo base del reintento de la recarga fallida (ms); se duplica en cada fallo hasta
// kGalleryRetryMaxFactor veces el base.
inline constexpr int kGalleryRetryMs = 30000;
inline constexpr int kGalleryRetryMinMs = 1000;
// Tope del intervalo base: 5 minutos. Con el backoff de 10x encima, el peor caso es de 50
// minutos entre intentos, que ya es demasiado para dejar el registro cerrado; un valor mayor es
// casi seguro un error de unidades (segundos escritos como ms, o al reves).
inline constexpr int kGalleryRetryMaxMs = 300000;
inline constexpr int kGalleryRetryMaxFactor = 10;

}  // namespace vision::enrollment_limits
