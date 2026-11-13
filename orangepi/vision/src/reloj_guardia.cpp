#include "vision/reloj_guardia.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>

namespace vision {

namespace {

bool es_boot_id(const std::string& texto) {
  if (texto.empty() || texto.size() > 64) return false;
  return std::all_of(texto.begin(), texto.end(), [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || c == '-';
  });
}

std::optional<std::string> leer_archivo(const std::string& ruta, std::size_t maximo) {
  std::ifstream archivo(ruta, std::ios::binary);
  if (!archivo) return std::nullopt;
  std::string contenido(maximo + 1, '\0');
  archivo.read(contenido.data(), static_cast<std::streamsize>(contenido.size()));
  contenido.resize(static_cast<std::size_t>(archivo.gcount()));
  if (contenido.size() > maximo) return std::nullopt;  // mas grande que cualquier estado
  return contenido;
}

}  // namespace

AnclasRelojSistema::AnclasRelojSistema(std::string ruta_estado, std::string ruta_rtc,
                                       std::string ruta_boot_id)
    : ruta_estado_(std::move(ruta_estado)),
      ruta_rtc_(std::move(ruta_rtc)),
      ruta_boot_id_(std::move(ruta_boot_id)) {}

std::optional<std::int64_t> AnclasRelojSistema::rtc_epoch_seconds() {
  const std::optional<std::string> texto = leer_archivo(ruta_rtc_, 64);
  if (!texto) return std::nullopt;
  std::string limpio = *texto;
  while (!limpio.empty() && (limpio.back() == '\n' || limpio.back() == ' ')) limpio.pop_back();
  if (limpio.empty() || limpio.size() > 19 ||
      limpio.find_first_not_of("0123456789") != std::string::npos) {
    return std::nullopt;
  }
  return static_cast<std::int64_t>(std::strtoll(limpio.c_str(), nullptr, 10));
}

std::optional<std::string> AnclasRelojSistema::boot_id() {
  std::optional<std::string> texto = leer_archivo(ruta_boot_id_, 128);
  if (!texto) return std::nullopt;
  while (!texto->empty() && (texto->back() == '\n' || texto->back() == ' ')) texto->pop_back();
  if (!es_boot_id(*texto)) return std::nullopt;
  return texto;
}

std::optional<std::string> AnclasRelojSistema::leer_estado() {
  if (ruta_estado_.empty()) return std::nullopt;
  return leer_archivo(ruta_estado_, 512);
}

bool AnclasRelojSistema::guardar_estado(const std::string& contenido) {
  if (ruta_estado_.empty()) return false;
  const std::string temporal = ruta_estado_ + ".tmp";
  const int fd =
      ::open(temporal.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (fd < 0) return false;
  bool bien = ::fchmod(fd, 0600) == 0;
  std::size_t escritos = 0;
  while (bien && escritos < contenido.size()) {
    const ssize_t n = ::write(fd, contenido.data() + escritos, contenido.size() - escritos);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) bien = false;
    else escritos += static_cast<std::size_t>(n);
  }
  bien = bien && ::fsync(fd) == 0;
  ::close(fd);
  // Atomico: o queda el estado anterior entero o el nuevo entero, nunca uno a medias.
  if (!bien || ::rename(temporal.c_str(), ruta_estado_.c_str()) != 0) {
    ::unlink(temporal.c_str());
    return false;
  }
  return true;
}

std::string serializar_estado(const EstadoReloj& estado) {
  return "v1 " + std::to_string(estado.ts) + " " + estado.boot_id + " " +
         std::to_string(estado.boottime_s) + "\n";
}

std::optional<EstadoReloj> leer_estado_reloj(const std::string& texto) {
  std::istringstream entrada(texto);
  std::string version, ts_texto, boot_id, boot_texto, sobrante;
  if (!(entrada >> version >> ts_texto >> boot_id >> boot_texto)) return std::nullopt;
  if (entrada >> sobrante) return std::nullopt;
  if (version != "v1" || !es_boot_id(boot_id)) return std::nullopt;
  const auto entero = [](const std::string& t, std::int64_t& salida) {
    if (t.empty() || t.size() > 19) return false;
    if (t.find_first_not_of("0123456789") != std::string::npos) return false;
    salida = static_cast<std::int64_t>(std::strtoll(t.c_str(), nullptr, 10));
    return true;
  };
  EstadoReloj estado;
  if (!entero(ts_texto, estado.ts) || !entero(boot_texto, estado.boottime_s)) return std::nullopt;
  estado.boot_id = boot_id;
  return estado;
}

std::optional<std::int64_t> GuardiaReloj::rtc_usable() {
  if (anclas_ == nullptr) return std::nullopt;
  const std::optional<std::int64_t> rtc = anclas_->rtc_epoch_seconds();
  // Un RTC sin pila marca 1970 (o el ano de fabrica): no es una referencia.
  if (!rtc || *rtc < kMinEpochPlausible || *rtc > kMaxEpochPlausible) return std::nullopt;
  return rtc;
}

VeredictoReloj GuardiaReloj::evaluar(std::int64_t ts, std::int64_t boottime_s) {
  VeredictoReloj veredicto;
  const std::optional<std::int64_t> rtc = rtc_usable();
  const auto rtc_coincide = [&] {
    return rtc && std::llabs(ts - *rtc) <= kSaltoRelojMaxS;
  };
  const std::string revisar =
      "revisa NTP y la sincronizacion de la hora (timedatectl status); ver el README, "
      "\"El reloj del borde\"";

  if (ultima_) {
    // Hay una firma anterior en este proceso: el salto se mide contra BOOTTIME, que sigue
    // contando durante una suspension.
    const std::int64_t pared = ts - ultima_->ts;
    const std::int64_t sistema = boottime_s - ultima_->boottime_s;
    if (pared > sistema + kSaltoRelojMaxS && !rtc_coincide()) {
      veredicto.permitido = false;
      veredicto.mensaje = "el reloj del borde salto hacia adelante (el ts avanzo " +
                          std::to_string(pared) + " s y el sistema " + std::to_string(sistema) +
                          " s, suspension incluida)" +
                          (rtc ? " y el RTC de hardware no lo confirma"
                               : " y no hay un RTC de hardware que lo confirme") +
                          ": no se firma la peticion de captura; " + revisar;
      return veredicto;
    }
  } else if (anclas_ != nullptr) {
    // La primera firma tras arrancar: hace falta un ancla absoluta.
    if (rtc) {
      if (!rtc_coincide()) {
        veredicto.permitido = false;
        veredicto.mensaje = "el reloj del borde (epoch " + std::to_string(ts) +
                            ") difiere del RTC de hardware (epoch " + std::to_string(*rtc) +
                            ") en " + std::to_string(std::llabs(ts - *rtc)) +
                            " s: no se firma la peticion de captura; " + revisar;
        return veredicto;
      }
    } else {
      const std::optional<std::string> texto = anclas_->leer_estado();
      const std::optional<EstadoReloj> estado =
          texto ? leer_estado_reloj(*texto) : std::nullopt;
      if (!estado) {
        // Sin RTC y sin estado (primera instalacion), o estado corrupto: no hay con que
        // comparar. Se permite, con un aviso fuerte (ver la nota de la cabecera).
        veredicto.mensaje =
            std::string("AVISO: no hay RTC de hardware ni un archivo de estado del reloj ") +
            (texto ? "valido (esta corrupto)" : "(primera instalacion o archivo perdido)") +
            ": el ts de la primera firma no se puede comprobar contra nada. Si la hora del "
            "borde no es correcta, la placa quedara rechazando las capturas con 403 hasta que "
            "se reinicie y se borre su piso; " + revisar;
      } else {
        const std::optional<std::string> boot = anclas_->boot_id();
        piso_ = estado->ts;
        if (boot && *boot == estado->boot_id) {
          // El mismo arranque: la hora no puede haberse adelantado mas de lo que cuenta BOOTTIME.
          const std::int64_t esperado = estado->ts + (boottime_s - estado->boottime_s);
          if (ts > esperado + kSaltoRelojMaxS) {
            veredicto.permitido = false;
            veredicto.mensaje =
                "el reloj del borde salto hacia adelante respecto de la ultima firma de este "
                "arranque (se esperaba cerca de " + std::to_string(esperado) + " y es " +
                std::to_string(ts) + ") y no hay un RTC de hardware que lo confirme: no se "
                "firma la peticion de captura; " + revisar;
            return veredicto;
          }
        } else if (ts < estado->ts) {
          // Otro arranque: la hora no puede ser anterior a lo ya firmado (un reloj que arranco
          // en 1970 o con una hora vieja, sin sincronizar todavia).
          veredicto.permitido = false;
          veredicto.mensaje = "el reloj del borde (epoch " + std::to_string(ts) +
                              ") es anterior a la ultima firma guardada (epoch " +
                              std::to_string(estado->ts) +
                              "): no se firma la peticion de captura; " + revisar;
          return veredicto;
        }
      }
    }
  }

  aceptar(ts, boottime_s, veredicto);
  return veredicto;
}

void GuardiaReloj::aceptar(std::int64_t ts, std::int64_t boottime_s, VeredictoReloj& veredicto) {
  ultima_ = Par{boottime_s, ts};
  piso_ = std::max(piso_, ts);
  if (anclas_ == nullptr) return;
  if (persistido_en_ && boottime_s - *persistido_en_ < kPeriodoPersistenciaS) return;
  const std::optional<std::string> boot = anclas_->boot_id();
  const bool guardado =
      boot && anclas_->guardar_estado(serializar_estado({piso_, *boot, boottime_s}));
  persistido_en_ = boottime_s;  // tambien si falla: no se reintenta en cada foto
  if (!guardado && !aviso_persistencia_) {
    aviso_persistencia_ = true;
    if (!veredicto.mensaje.empty()) veredicto.mensaje += "\n[vision] ";
    veredicto.mensaje +=
        "AVISO: no se pudo guardar el estado del reloj (archivo de estado o boot_id): sin RTC, "
        "la proxima primera firma tras un reinicio no tendra con que compararse";
  }
}

}  // namespace vision
