#include "vision/wall_clock.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>

namespace vision {

namespace {

bool all_digits(std::string_view text, std::size_t from, std::size_t count) {
  if (from + count > text.size()) return false;
  for (std::size_t i = from; i < from + count; ++i) {
    if (text[i] < '0' || text[i] > '9') return false;
  }
  return true;
}

}  // namespace

std::string SystemWallClock::now_iso8601() const {
  const auto now = std::chrono::system_clock::now();
  const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
  const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                          now.time_since_epoch())
                          .count() %
                      1000;

  std::tm local{};
  localtime_r(&seconds, &local);

  char date[32];
  if (std::strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", &local) == 0) {
    return {};  // no se pudo formatear: se devuelve una hora vacia
  }
  char zone[8];
  if (std::strftime(zone, sizeof(zone), "%z", &local) == 0) {
    return {};
  }

  std::string result(date);
  result += '.';
  char fraction[8];
  std::snprintf(fraction, sizeof(fraction), "%03lld", static_cast<long long>(millis));
  result += fraction;
  result += zone[0];
  result += zone[1];
  result += zone[2];
  result += ':';
  result += zone[3];
  result += zone[4];
  return result;
}

std::int64_t SystemWallClock::now_epoch_seconds() const {
  return static_cast<std::int64_t>(
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
}

bool is_iso8601_with_zone(std::string_view text) {
  // "AAAA-MM-DDTHH:MM:SS"
  if (text.size() < 25) return false;
  if (!all_digits(text, 0, 4) || text[4] != '-') return false;
  if (!all_digits(text, 5, 2) || text[7] != '-') return false;
  if (!all_digits(text, 8, 2) || text[10] != 'T') return false;
  if (!all_digits(text, 11, 2) || text[13] != ':') return false;
  if (!all_digits(text, 14, 2) || text[16] != ':') return false;
  if (!all_digits(text, 17, 2)) return false;

  std::size_t pos = 19;
  if (pos < text.size() && text[pos] == '.') {
    ++pos;
    const std::size_t from = pos;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') ++pos;
    if (pos == from) return false;  // el punto no puede ir sin cifras
  }
  // La zona: "+HH:MM" o "-HH:MM".
  if (pos + 6 != text.size()) return false;
  if (text[pos] != '+' && text[pos] != '-') return false;
  if (!all_digits(text, pos + 1, 2) || text[pos + 3] != ':') return false;
  return all_digits(text, pos + 4, 2);
}

bool is_plausible_timestamp(std::string_view text) {
  if (!is_iso8601_with_zone(text)) return false;
  // Los cuatro primeros caracteres son cifras (lo garantiza is_iso8601_with_zone).
  const int year = (text[0] - '0') * 1000 + (text[1] - '0') * 100 + (text[2] - '0') * 10 +
                   (text[3] - '0');
  return year >= kMinPlausibleYear;
}

}  // namespace vision
