#pragma once

#include <array>

#include <opencv2/core.hpp>

namespace vision {

inline constexpr int kAlignedFaceSize = 112;

// Puntos de referencia estandar de ArcFace para un recorte de 112x112, en el mismo
// orden que FaceDetection::landmarks.
inline const std::array<cv::Point2f, 5> kArcFaceReference = {{
    {30.2946f, 51.6963f},
    {65.5318f, 51.5014f},
    {48.0252f, 71.7366f},
    {33.5493f, 92.3655f},
    {62.7299f, 92.2041f},
}};

// Transformacion de similitud (rotacion, escala uniforme y traslacion, sin reflexion)
// que minimiza el error cuadratico entre src y dst: la solucion cerrada de Umeyama, la
// misma que SimilarityTransform de insightface. Es determinista y usa los 5 puntos.
// Devuelve la matriz 2x3 CV_64F que lleva src a dst. Lanza std::invalid_argument si algun
// punto no es finito y std::runtime_error si los puntos de src coinciden (no hay escala).
cv::Mat similarity_transform(const std::array<cv::Point2f, 5>& src,
                             const std::array<cv::Point2f, 5>& dst);

// Alinea la cara a 112x112 con similarity_transform(landmarks, kArcFaceReference).
// Lanza std::invalid_argument si la imagen esta vacia o los landmarks no son finitos, y
// std::runtime_error si no se puede estimar la transformacion.
cv::Mat align_face(const cv::Mat& image, const std::array<cv::Point2f, 5>& landmarks);

}  // namespace vision
