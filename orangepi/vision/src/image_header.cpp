#include "vision/image_header.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace vision {

namespace {

constexpr std::array<std::uint8_t, 8> kPngSignature = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

int be16(std::span<const std::uint8_t> b, std::size_t at) {
  return (b[at] << 8) | b[at + 1];
}

std::uint32_t be32(std::span<const std::uint8_t> b, std::size_t at) {
  return (static_cast<std::uint32_t>(b[at]) << 24) | (static_cast<std::uint32_t>(b[at + 1]) << 16) |
         (static_cast<std::uint32_t>(b[at + 2]) << 8) | b[at + 3];
}

// Firma, longitud del primer chunk (13) y su tipo "IHDR", y luego ancho y alto.
std::optional<cv::Size> png_size(std::span<const std::uint8_t> b) {
  if (b.size() < 24 || !std::equal(kPngSignature.begin(), kPngSignature.end(), b.begin())) {
    return std::nullopt;
  }
  if (be32(b, 8) != 13 || b[12] != 'I' || b[13] != 'H' || b[14] != 'D' || b[15] != 'R') {
    return std::nullopt;
  }
  const std::uint32_t width = be32(b, 16);
  const std::uint32_t height = be32(b, 20);
  if (width == 0 || height == 0 || width > 0x7FFFFFFF || height > 0x7FFFFFFF) return std::nullopt;
  return cv::Size(static_cast<int>(width), static_cast<int>(height));
}

// Recorre los segmentos hasta el primer SOS, que debe ir despues de un unico SOF valido.
// El alto y el ancho son de 16 bits.
// Disposicion del SOF desde el marcador: +2 longitud, +4 precision, +5 alto, +7 ancho,
// +9 numero de componentes Nf, y 3 bytes por componente: longitud = 8 + 3*Nf.
// Disposicion del SOS: +2 longitud, +4 Ns, 2 bytes por componente, y luego Ss, Se y Ah/Al:
// longitud = 6 + 2*Ns.
//
// Se exige un unico scan entrelazado (Ns == Nf) con Ss=0, Se=63, Ah/Al=0, que es lo que
// emite una camara. Un SOF0 con Nf=3 cuyo primer SOS lista menos componentes hace que
// libjpeg acepte scans sin limite, cada uno recorriendo todos los MCU de su componente, y
// en 2 MB caben mas de cien mil scans de 12 bytes: el coste crece linealmente con ellos. Con
// Ns == Nf libjpeg espera el EOI despues del primer scan.
std::optional<cv::Size> jpeg_size(std::span<const std::uint8_t> b) {
  if (b.size() < 4 || b[0] != 0xFF || b[1] != 0xD8) return std::nullopt;
  std::optional<cv::Size> size;
  int components = 0;
  std::size_t pos = 2;
  while (pos + 4 <= b.size()) {
    if (b[pos] != 0xFF) return std::nullopt;
    const std::uint8_t marker = b[pos + 1];
    if (marker == 0xFF) {  // relleno
      ++pos;
      continue;
    }
    if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD8)) {  // sin longitud
      pos += 2;
      continue;
    }
    if (marker == 0xD9) return std::nullopt;  // fin de imagen sin llegar a un scan
    const int length = be16(b, pos + 2);
    if (length < 2) return std::nullopt;

    if (marker == 0xDA) {  // SOS
      if (!size) return std::nullopt;  // datos sin SOF previo
      if (pos + 5 > b.size()) return std::nullopt;
      const int scan_components = b[pos + 4];
      if (scan_components != components || length != 6 + 2 * scan_components ||
          pos + 2 + static_cast<std::size_t>(length) > b.size()) {
        return std::nullopt;
      }
      const std::size_t spectral = pos + 5 + 2 * static_cast<std::size_t>(scan_components);
      if (b[spectral] != 0x00 || b[spectral + 1] != 0x3F || b[spectral + 2] != 0x00) {
        return std::nullopt;
      }
      return size;
    }

    // C4 (DHT), C8 (JPG) y CC (DAC) comparten rango con los SOF pero no lo son.
    const bool is_sof = marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 &&
                        marker != 0xCC;
    if (is_sof) {
      if (size) return std::nullopt;                              // dos SOF
      if (marker != 0xC0 && marker != 0xC1) return std::nullopt;  // progresivo, aritmetico...
      if (pos + 10 > b.size()) return std::nullopt;
      components = b[pos + 9];
      if (b[pos + 4] != 8 || (components != 1 && components != 3)) return std::nullopt;
      if (length != 8 + 3 * components || pos + 2 + static_cast<std::size_t>(length) > b.size()) {
        return std::nullopt;
      }
      const int height = be16(b, pos + 5);
      const int width = be16(b, pos + 7);
      if (width == 0 || height == 0) return std::nullopt;
      size = cv::Size(width, height);
    }
    pos += 2 + static_cast<std::size_t>(length);
  }
  return std::nullopt;
}

}  // namespace

std::optional<ImageHeader> peek_image_header(std::span<const std::uint8_t> bytes) {
  if (const auto size = png_size(bytes)) return ImageHeader{ImageFormat::Png, *size};
  if (const auto size = jpeg_size(bytes)) return ImageHeader{ImageFormat::Jpeg, *size};
  return std::nullopt;
}

std::optional<cv::Size> peek_image_size(std::span<const std::uint8_t> bytes) {
  if (const auto header = peek_image_header(bytes)) return header->size;
  return std::nullopt;
}

}  // namespace vision
