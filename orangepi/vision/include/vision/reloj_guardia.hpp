#pragma once

// Guardia del reloj del borde para la firma de la captura.
//
// La placa (XIAO) guarda el ultimo ts que acepto y rechaza con 403 todo lo que sea mas de 300 s
// anterior, hasta que se reinicie y se borre su piso. Si el servicio firma UN ts que esta en el
// futuro (un NTP suplantado, o un reloj que aun no se sincronizo), la placa se queda con un
// piso imposible y rechaza todas las capturas buenas despues. Esta guardia decide si el ts
// del reloj de pared es de fiar ANTES de firmarlo:
//
//  - Dentro del proceso, el reloj de pared no puede adelantarse mas de kSaltoRelojMaxS (10
//    minutos) por encima de CLOCK_BOOTTIME (que cuenta tambien la suspension, a diferencia
//    del reloj monotonico: una suspension no es un salto) desde la ultima firma.
//  - La PRIMERA firma tras arrancar no tiene referencia dentro del proceso, asi que se ancla a
//    algo absoluto: el RTC de hardware si se puede leer y es plausible (la hora debe estar a
//    kSaltoRelojMaxS del RTC), y si no, el archivo de estado que dejo la firma anterior
//    ((ts, boot_id, CLOCK_BOOTTIME)): en el mismo arranque la hora no puede haberse adelantado
//    mas de lo que dice BOOTTIME; en otro arranque, no puede ser anterior al ts guardado.
//  - Si el RTC coincide con la hora nueva, un salto hacia adelante se acepta sin reiniciar.
//
// Sin RTC y sin archivo de estado (primera instalacion) o con el archivo corrupto no hay con que
// comparar: SE PERMITE firmar con un aviso fuerte. Rechazar ahi seria permanente: el archivo de
// estado solo se escribe al firmar, asi que nunca habria ninguno, y una placa sin RTC con el
// directorio de estado sin escritura no podria firmar jamas. El riesgo que se acepta es el de
// la primera firma de la vida de la instalacion; el orden de arranque (After=time-sync.target)
// y el aviso lo cubren en la practica.

#include <cstdint>
#include <optional>
#include <string>

#include "vision/captura_firma.hpp"

namespace vision {

// Lo absoluto contra lo que se ancla el reloj. Lo real lee /sys y /proc y un archivo; las
// pruebas ponen dobles.
class AnclasReloj {
 public:
  virtual ~AnclasReloj() = default;
  // El RTC de hardware en segundos de epoch; nullopt si no hay o no se puede leer.
  virtual std::optional<std::int64_t> rtc_epoch_seconds() = 0;
  // El id de este arranque (/proc/sys/kernel/random/boot_id); nullopt si no se puede leer.
  virtual std::optional<std::string> boot_id() = 0;
  // El contenido del archivo de estado; nullopt si no existe o no se puede leer.
  virtual std::optional<std::string> leer_estado() = 0;
  // Guarda el contenido de forma atomica (temporal y rename), modo 0600. false si falla.
  virtual bool guardar_estado(const std::string& contenido) = 0;
};

// Las anclas del sistema. Las rutas se pueden cambiar para las pruebas.
class AnclasRelojSistema final : public AnclasReloj {
 public:
  explicit AnclasRelojSistema(std::string ruta_estado,
                              std::string ruta_rtc = "/sys/class/rtc/rtc0/since_epoch",
                              std::string ruta_boot_id = "/proc/sys/kernel/random/boot_id");
  std::optional<std::int64_t> rtc_epoch_seconds() override;
  std::optional<std::string> boot_id() override;
  std::optional<std::string> leer_estado() override;
  bool guardar_estado(const std::string& contenido) override;

 private:
  std::string ruta_estado_;
  std::string ruta_rtc_;
  std::string ruta_boot_id_;
};

// El estado persistido: "v1 <ts> <boot_id> <boottime_s>\n".
struct EstadoReloj {
  std::int64_t ts = 0;          // el piso: el mayor ts firmado
  std::string boot_id;          // el arranque en que se escribio
  std::int64_t boottime_s = 0;  // CLOCK_BOOTTIME en ese momento
};
std::string serializar_estado(const EstadoReloj& estado);
// nullopt si el texto no es exactamente el formato (corrupto).
std::optional<EstadoReloj> leer_estado_reloj(const std::string& texto);

// Lo que decide la guardia.
struct VeredictoReloj {
  bool permitido = true;
  // El motivo del rechazo o un aviso (con permitido true): nunca lleva el secreto. Vacio si
  // todo esta bien.
  std::string mensaje;
};

class GuardiaReloj {
 public:
  // `anclas` puede ser nullptr (pruebas, --dry-run): entonces solo se vigila el salto dentro
  // del proceso y la primera firma no tiene ancla absoluta.
  explicit GuardiaReloj(AnclasReloj* anclas) : anclas_(anclas) {}

  // `ts`: el reloj de pared (epoch); `boottime_s`: CLOCK_BOOTTIME en segundos. Si permite,
  // recuerda el par y persiste el estado (como mucho cada kPeriodoPersistenciaS).
  VeredictoReloj evaluar(std::int64_t ts, std::int64_t boottime_s);

  static constexpr std::int64_t kPeriodoPersistenciaS = 30;

 private:
  void aceptar(std::int64_t ts, std::int64_t boottime_s, VeredictoReloj& veredicto);
  std::optional<std::int64_t> rtc_usable();

  AnclasReloj* anclas_;
  struct Par {
    std::int64_t boottime_s;
    std::int64_t ts;
  };
  std::optional<Par> ultima_;
  std::int64_t piso_ = 0;           // el mayor ts firmado (y el persistido leido)
  std::optional<std::int64_t> persistido_en_;  // BOOTTIME de la ultima escritura
  bool aviso_persistencia_ = false;  // ya se aviso de que no se puede guardar
};

}  // namespace vision
