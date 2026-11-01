#include <stdexcept>
#include <vector>

#include <opencv2/imgcodecs.hpp>

#include "minitest.hpp"
#include "vision/image_header.hpp"

namespace {

using namespace vision;

std::vector<std::uint8_t> encode(const char* ext, int width, int height) {
  cv::Mat image(height, width, CV_8UC3, cv::Scalar(10, 20, 30));
  std::vector<std::uint8_t> bytes;
  cv::imencode(ext, image, bytes);
  return bytes;
}

// Posicion del marcador SOF0 (FF C0) de un JPEG de OpenCV.
std::size_t sof0_position(const std::vector<std::uint8_t>& jpeg) {
  for (std::size_t i = 0; i + 1 < jpeg.size(); ++i) {
    if (jpeg[i] == 0xFF && jpeg[i + 1] == 0xC0) return i;
  }
  throw std::runtime_error("el JPEG de prueba no tiene SOF0");
}

void set_jpeg_size(std::vector<std::uint8_t>& jpeg, int width, int height) {
  const std::size_t sof = sof0_position(jpeg);
  jpeg[sof + 5] = static_cast<std::uint8_t>(height >> 8);
  jpeg[sof + 6] = static_cast<std::uint8_t>(height);
  jpeg[sof + 7] = static_cast<std::uint8_t>(width >> 8);
  jpeg[sof + 8] = static_cast<std::uint8_t>(width);
}

// Campos del SOS que sigue al SOF; -1 usa el valor correcto (Ns = Nf, longitud 6 + 2*Ns).
struct Scan {
  bool present = true;
  int components = -1;
  int length = -1;
  int ss = 0x00, se = 0x3F, ah_al = 0x00;
};

// JPEG minimo: SOI, `before`, un SOF con los campos dados y sus componentes, y un SOS (si
// `scan.present`) seguido de unos bytes de datos y el EOI. Si `length` es negativo se usa el
// correcto (8 + 3*Nf). Solo importa la cabecera; los datos no son decodificables.
std::vector<std::uint8_t> jpeg_with_sof(std::uint8_t marker, std::uint8_t precision, std::uint8_t components,
                                        int height, int width, int length = -1,
                                        std::vector<std::uint8_t> before = {}, Scan scan = {}) {
  if (length < 0) length = 8 + 3 * components;
  std::vector<std::uint8_t> out;
  out.reserve(32 + before.size() + 5 * static_cast<std::size_t>(components));
  out.push_back(0xFF);
  out.push_back(0xD8);
  out.insert(out.end(), before.begin(), before.end());
  for (const int byte : {0xFF, static_cast<int>(marker), length >> 8, length & 0xFF, static_cast<int>(precision),
                         height >> 8, height & 0xFF, width >> 8, width & 0xFF, static_cast<int>(components)}) {
    out.push_back(static_cast<std::uint8_t>(byte));
  }
  for (int i = 0; i < components; ++i) {
    out.push_back(static_cast<std::uint8_t>(i + 1));
    out.push_back(0x11);
    out.push_back(0x00);
  }
  if (!scan.present) return out;

  const int ns = scan.components < 0 ? components : scan.components;
  const int sos_length = scan.length < 0 ? 6 + 2 * ns : scan.length;
  for (const int byte : {0xFF, 0xDA, sos_length >> 8, sos_length & 0xFF, ns}) {
    out.push_back(static_cast<std::uint8_t>(byte));
  }
  for (int i = 0; i < ns; ++i) {
    out.push_back(static_cast<std::uint8_t>(i + 1));
    out.push_back(0x00);
  }
  for (const int byte : {scan.ss, scan.se, scan.ah_al, 0x12, 0x34, 0xFF, 0xD9}) {
    out.push_back(static_cast<std::uint8_t>(byte));
  }
  return out;
}

}  // namespace

TEST(peek_reads_png_size) {
  const auto size = peek_image_size(encode(".png", 300, 200));
  CHECK(size.has_value());
  CHECK_EQ(size->width, 300);
  CHECK_EQ(size->height, 200);
}

TEST(peek_reads_jpeg_size) {
  const auto size = peek_image_size(encode(".jpg", 64, 48));
  CHECK(size.has_value());
  CHECK_EQ(size->width, 64);
  CHECK_EQ(size->height, 48);
}

TEST(peek_reports_the_header_size_without_decoding) {
  // Cabeceras que declaran mucho mas de lo que hay: la lectura no decodifica nada.
  std::vector<std::uint8_t> jpeg = encode(".jpg", 64, 48);
  set_jpeg_size(jpeg, 8000, 6000);
  const auto jpeg_size = peek_image_size(jpeg);
  CHECK(jpeg_size.has_value());
  CHECK_EQ(jpeg_size->width, 8000);
  CHECK_EQ(jpeg_size->height, 6000);

  std::vector<std::uint8_t> png = encode(".png", 8, 8);
  png[18] = 0x01;  // ancho: 0x0108 = 264
  png[22] = 0x10;  // alto: 0x1008 = 4104
  const auto png_size = peek_image_size(png);
  CHECK(png_size.has_value());
  CHECK_EQ(png_size->width, 264);
  CHECK_EQ(png_size->height, 4104);
}

TEST(peek_rejects_unknown_truncated_and_invalid_headers) {
  CHECK(!peek_image_size({}).has_value());
  CHECK(!peek_image_size(std::vector<std::uint8_t>{1, 2, 3, 4}).has_value());

  std::vector<std::uint8_t> png = encode(".png", 8, 8);
  CHECK(!peek_image_size(std::span<const std::uint8_t>(png.data(), 20)).has_value());

  std::vector<std::uint8_t> jpeg = encode(".jpg", 64, 48);
  const std::size_t sof = sof0_position(jpeg);
  CHECK(!peek_image_size(std::span<const std::uint8_t>(jpeg.data(), sof + 6)).has_value());
  CHECK(!peek_image_size(std::span<const std::uint8_t>(jpeg.data(), 3)).has_value());

  set_jpeg_size(jpeg, 0, 48);
  CHECK(!peek_image_size(jpeg).has_value());

  std::vector<std::uint8_t> bmp = encode(".bmp", 8, 8);
  CHECK(!peek_image_size(bmp).has_value());  // solo JPEG y PNG
}

// --- JPEG: solo baseline y secuencial extendido ----------------------------------------

TEST(peek_accepts_baseline_and_extended_sequential_sof) {
  for (const std::uint8_t marker : {0xC0, 0xC1}) {
    for (const std::uint8_t components : {1, 3}) {
      const auto size = peek_image_size(jpeg_with_sof(marker, 8, components, 480, 640));
      CHECK(size.has_value());
      CHECK_EQ(size->width, 640);
      CHECK_EQ(size->height, 480);
    }
  }
}

TEST(peek_rejects_every_other_sof_marker) {
  // Progresivo (C2), sin perdida (C3), diferenciales (C5-C7) y aritmeticos (C9-CB, CD-CF).
  for (const std::uint8_t marker : {0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF}) {
    CHECK(!peek_image_size(jpeg_with_sof(marker, 8, 3, 480, 640)).has_value());
  }
}

TEST(peek_rejects_a_real_progressive_jpeg) {
  cv::Mat image(48, 64, CV_8UC3, cv::Scalar(10, 20, 30));
  std::vector<std::uint8_t> progressive;
  cv::imencode(".jpg", image, progressive, {cv::IMWRITE_JPEG_PROGRESSIVE, 1});
  CHECK(!peek_image_size(progressive).has_value());
}

TEST(peek_rejects_bad_precision_and_component_count) {
  for (const std::uint8_t precision : {0, 4, 12, 16}) {
    CHECK(!peek_image_size(jpeg_with_sof(0xC0, precision, 3, 480, 640)).has_value());
  }
  for (const std::uint8_t components : {0, 2, 4, 255}) {
    CHECK(!peek_image_size(jpeg_with_sof(0xC0, 8, components, 480, 640, 8 + 3 * components)).has_value());
  }
}

TEST(peek_checks_the_sof_segment_length) {
  CHECK(peek_image_size(jpeg_with_sof(0xC0, 8, 3, 48, 64, 17)).has_value());    // 8 + 3*3
  CHECK(!peek_image_size(jpeg_with_sof(0xC0, 8, 3, 48, 64, 16)).has_value());   // corta
  CHECK(!peek_image_size(jpeg_with_sof(0xC0, 8, 3, 48, 64, 0xFFFF)).has_value());  // pasa del buffer
  CHECK(!peek_image_size(jpeg_with_sof(0xC0, 8, 1, 48, 64, 8)).has_value());  // Nf=1 pide 11
  CHECK(!peek_image_size(jpeg_with_sof(0xC0, 8, 3, 48, 64, 20)).has_value());  // sobra: libjpeg exige exacto

  // Longitud 3 bytes mayor, con esos 3 bytes de relleno dentro del segmento y un SOS valido
  // despues: el recorrido por segmentos llegaria bien al SOS, asi que solo la longitud exacta lo frena.
  Scan no_scan;
  no_scan.present = false;
  std::vector<std::uint8_t> padded = jpeg_with_sof(0xC0, 8, 3, 48, 64, 20, {}, no_scan);
  const std::size_t header_size = padded.size();
  for (int i = 0; i < 3; ++i) padded.push_back(0x00);
  const std::vector<std::uint8_t> with_scan = jpeg_with_sof(0xC0, 8, 3, 48, 64, 20);
  padded.insert(padded.end(), with_scan.begin() + static_cast<std::ptrdiff_t>(header_size), with_scan.end());
  CHECK(!peek_image_size(padded).has_value());
}

TEST(peek_rejects_a_sof_truncated_before_its_component_count) {
  Scan no_scan;
  no_scan.present = false;
  const std::vector<std::uint8_t> full = jpeg_with_sof(0xC0, 8, 3, 48, 64, -1, {}, no_scan);
  const std::size_t sof = 2;  // justo despues del SOI
  // Cortes entre el marcador y el byte de Nf (pos+9), ambos extremos incluidos. Cada corte
  // es una copia del tamano exacto, para que un sanitizer vea cualquier lectura de mas.
  for (std::size_t keep = sof; keep < sof + 10; ++keep) {
    const std::vector<std::uint8_t> cut(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(keep));
    CHECK(!peek_image_size(cut).has_value());
  }
  // Y con los componentes incompletos tambien.
  const std::vector<std::uint8_t> short_components(full.begin(), full.end() - 1);
  CHECK(!peek_image_size(short_components).has_value());
}

TEST(peek_skips_fill_bytes_and_standalone_markers) {
  const auto fill = peek_image_size(jpeg_with_sof(0xC0, 8, 3, 48, 64, -1, {0xFF, 0xFF, 0xFF}));
  CHECK(fill.has_value());
  CHECK_EQ(fill->width, 64);

  // RST0, RST7 y TEM no llevan longitud.
  const auto standalone =
      peek_image_size(jpeg_with_sof(0xC0, 8, 3, 48, 64, -1, {0xFF, 0xD0, 0xFF, 0x01, 0xFF, 0xD7}));
  CHECK(standalone.has_value());
  CHECK_EQ(standalone->height, 48);
}

TEST(peek_handles_odd_segment_lengths) {
  // Segmento vacio (longitud 2): se salta.
  CHECK(peek_image_size(jpeg_with_sof(0xC0, 8, 3, 48, 64, -1, {0xFF, 0xE0, 0x00, 0x02})).has_value());
  // Longitud 0 o 1: invalida.
  CHECK(!peek_image_size(jpeg_with_sof(0xC0, 8, 3, 48, 64, -1, {0xFF, 0xE0, 0x00, 0x00})).has_value());
  CHECK(!peek_image_size(jpeg_with_sof(0xC0, 8, 3, 48, 64, -1, {0xFF, 0xE0, 0x00, 0x01})).has_value());
  // Longitud que pasa del final del buffer.
  CHECK(!peek_image_size(jpeg_with_sof(0xC0, 8, 3, 48, 64, -1, {0xFF, 0xE0, 0xFF, 0xFF})).has_value());
  // DHT (C4), JPG (C8) y DAC (CC) no son SOF: se saltan.
  CHECK(peek_image_size(jpeg_with_sof(0xC0, 8, 3, 48, 64, -1, {0xFF, 0xC4, 0x00, 0x02})).has_value());
}

TEST(peek_header_reports_the_format) {
  const auto png = peek_image_header(encode(".png", 8, 6));
  CHECK(png.has_value());
  CHECK(png->format == ImageFormat::Png);
  const auto jpeg = peek_image_header(encode(".jpg", 8, 6));
  CHECK(jpeg.has_value());
  CHECK(jpeg->format == ImageFormat::Jpeg);
}

TEST(peek_rejects_png_dimensions_above_int_range) {
  std::vector<std::uint8_t> png = encode(".png", 8, 8);
  png[16] = 0x80; png[17] = 0; png[18] = 0; png[19] = 0;  // ancho 0x80000000
  CHECK(!peek_image_size(png).has_value());
  png = encode(".png", 8, 8);
  png[20] = 0x80; png[21] = 0; png[22] = 0; png[23] = 0;  // alto 0x80000000
  CHECK(!peek_image_size(png).has_value());
  png = encode(".png", 8, 8);
  png[16] = 0; png[17] = 0; png[18] = 0; png[19] = 0;     // ancho 0
  CHECK(!peek_image_size(png).has_value());
}

// --- JPEG: un unico scan entrelazado ---------------------------------------------------------

TEST(peek_accepts_real_baseline_jpegs_color_gray_and_with_restart_intervals) {
  const cv::Mat color(48, 64, CV_8UC3, cv::Scalar(10, 20, 30));
  cv::Mat noise(17, 33, CV_8UC3);
  cv::randu(noise, cv::Scalar::all(0), cv::Scalar::all(255));
  const cv::Mat gray(48, 64, CV_8UC1, cv::Scalar(90));

  std::vector<std::uint8_t> bytes;
  cv::imencode(".jpg", color, bytes);
  CHECK(peek_image_size(bytes).has_value());

  cv::imencode(".jpg", noise, bytes);  // 33x17: no multiplo del MCU
  const auto odd = peek_image_size(bytes);
  CHECK(odd.has_value());
  CHECK_EQ(odd->width, 33);
  CHECK_EQ(odd->height, 17);

  cv::imencode(".jpg", noise, bytes, {cv::IMWRITE_JPEG_OPTIMIZE, 1, cv::IMWRITE_JPEG_RST_INTERVAL, 4});
  CHECK(peek_image_size(bytes).has_value());

  cv::imencode(".jpg", gray, bytes);  // Nf=1, Ns=1
  const auto mono = peek_image_size(bytes);
  CHECK(mono.has_value());
  CHECK_EQ(mono->width, 64);
}

TEST(peek_accepts_a_synthetic_grayscale_and_color_scan) {
  CHECK(peek_image_size(jpeg_with_sof(0xC0, 8, 1, 48, 64)).has_value());  // Nf=1, Ns=1
  CHECK(peek_image_size(jpeg_with_sof(0xC1, 8, 3, 48, 64)).has_value());  // Nf=3, Ns=3
}

TEST(peek_rejects_a_non_interleaved_first_scan) {
  for (const int ns : {1, 2}) {  // Nf=3 con un scan que lista menos componentes
    Scan scan;
    scan.components = ns;
    CHECK(!peek_image_size(jpeg_with_sof(0xC0, 8, 3, 48, 64, -1, {}, scan)).has_value());
  }
  Scan more;
  more.components = 3;  // Ns > Nf
  CHECK(!peek_image_size(jpeg_with_sof(0xC0, 8, 1, 48, 64, -1, {}, more)).has_value());
  Scan none;
  none.components = 0;
  CHECK(!peek_image_size(jpeg_with_sof(0xC0, 8, 3, 48, 64, -1, {}, none)).has_value());
}

TEST(peek_rejects_a_jpeg_with_no_scan_header) {
  Scan no_scan;
  no_scan.present = false;
  CHECK(!peek_image_size(jpeg_with_sof(0xC0, 8, 3, 48, 64, -1, {}, no_scan)).has_value());

  // SOF y luego EOI.
  std::vector<std::uint8_t> eoi = jpeg_with_sof(0xC0, 8, 3, 48, 64, -1, {}, no_scan);
  eoi.push_back(0xFF);
  eoi.push_back(0xD9);
  eoi.push_back(0x00);
  eoi.push_back(0x00);
  CHECK(!peek_image_size(eoi).has_value());

  // Un SOS antes de cualquier SOF.
  const std::vector<std::uint8_t> sos_first = {0xFF, 0xD8, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x01, 0x00, 0x00, 0x3F, 0x00};
  CHECK(!peek_image_size(sos_first).has_value());
}

TEST(peek_rejects_a_sos_truncated_at_every_offset) {
  Scan no_scan;
  no_scan.present = false;
  for (const std::uint8_t components : {1, 3}) {
    const std::size_t sos = jpeg_with_sof(0xC0, 8, components, 48, 64, -1, {}, no_scan).size();
    const std::vector<std::uint8_t> full = jpeg_with_sof(0xC0, 8, components, 48, 64);
    const std::size_t sos_end = sos + 2 + 6 + 2 * components;
    for (std::size_t keep = sos; keep < sos_end; ++keep) {
      const std::vector<std::uint8_t> cut(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(keep));
      CHECK(!peek_image_size(cut).has_value());
    }
    // La cabecera completa basta, aunque no haya datos despues.
    const std::vector<std::uint8_t> header_only(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(sos_end));
    CHECK(peek_image_size(header_only).has_value());
  }
}

TEST(peek_rejects_a_scan_with_wrong_spectral_selection_or_approximation) {
  struct Case {
    int ss, se, ah_al;
  };
  for (const Case c : {Case{1, 0x3F, 0}, Case{0, 0x3E, 0}, Case{0, 0x00, 0}, Case{0, 0x3F, 0x01}, Case{0, 0x3F, 0x10},
                       Case{0x20, 0x3F, 0}, Case{0, 0xFF, 0}}) {
    Scan scan;
    scan.ss = c.ss;
    scan.se = c.se;
    scan.ah_al = c.ah_al;
    CHECK(!peek_image_size(jpeg_with_sof(0xC0, 8, 3, 48, 64, -1, {}, scan)).has_value());
  }
}

TEST(peek_checks_the_sos_segment_length) {
  for (const int length : {0, 2, 6, 11, 13, 20, 0xFFFF}) {  // el correcto para Ns=3 es 12
    Scan scan;
    scan.length = length;
    CHECK(!peek_image_size(jpeg_with_sof(0xC0, 8, 3, 48, 64, -1, {}, scan)).has_value());
  }
}

TEST(peek_rejects_two_sof_segments) {
  Scan no_scan;
  no_scan.present = false;
  const std::vector<std::uint8_t> first = jpeg_with_sof(0xC0, 8, 3, 48, 64, -1, {}, no_scan);
  const std::vector<std::uint8_t> before(first.begin() + 2, first.end());  // sin el SOI
  CHECK(!peek_image_size(jpeg_with_sof(0xC0, 8, 3, 48, 64, -1, before)).has_value());
}
