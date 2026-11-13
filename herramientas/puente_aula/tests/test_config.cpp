#include <string>

#include "minitest.hpp"
#include "puente_aula/config.hpp"

using namespace puente_aula;

namespace {

// Direcciones de documentacion (RFC 5737): validas pero nunca reales.
const char* kHttp = R"("http": {
  "escucha": {"ip": "192.0.2.10", "puerto": 8080},
  "permitidos": ["198.51.100.7"],
  "upstream": {"ip": "192.0.2.50"}
})";
const char* kMqtt = R"("mqtt": {
  "escucha": {"ip": "192.0.2.11"},
  "permitidos": ["192.0.2.50"],
  "upstream": {"ip": "198.51.100.7"}
})";

std::string con(const std::string& http, const std::string& mqtt, const std::string& extra = "") {
  std::string texto = "{";
  std::string coma;
  for (const std::string& pieza : {extra, http, mqtt}) {
    if (pieza.empty()) continue;
    texto += coma + pieza;
    coma = ",";
  }
  return texto + "}";
}

// El mensaje de error, o "" si la configuracion vale.
std::string error_de(const std::string& texto) {
  try {
    analizar_config(texto);
  } catch (const ErrorConfig& error) {
    return error.what();
  }
  return "";
}

bool contiene(const std::string& texto, const std::string& trozo) {
  return texto.find(trozo) != std::string::npos;
}

}  // namespace

TEST(config_buena_y_valores_por_omision) {
  const Config config = analizar_config(con(kHttp, kMqtt));
  CHECK(!config.permitir_bind_comodin);
  CHECK(config.http.has_value());
  CHECK(config.mqtt.has_value());
  CHECK_EQ(config.http->escucha.puerto, 8080);
  CHECK_EQ(config.http->upstream.puerto, 80);
  CHECK_EQ(config.http->max_body_bytes, static_cast<std::size_t>(2 * 1024 * 1024));
  CHECK_EQ(config.http->connect_timeout_ms, 2000);
  CHECK_EQ(config.http->read_timeout_ms, 3000);
  CHECK_EQ(config.http->total_timeout_ms, 4000);  // por debajo de los 5000 del cliente de vision
  CHECK(config.http->escucha.interfaz.empty());
  CHECK(config.mqtt->escucha.interfaz.empty());
  CHECK_EQ(config.http->permitidos.size(), static_cast<std::size_t>(1));
  CHECK_EQ(config.mqtt->escucha.puerto, 1883);
  CHECK_EQ(config.mqtt->upstream.puerto, 1883);
  CHECK_EQ(config.mqtt->max_conexiones, static_cast<std::size_t>(2));
  CHECK_EQ(config.mqtt->keepalive.inactividad_s, 10);
  CHECK_EQ(config.mqtt->keepalive.intervalo_s, 5);
  CHECK_EQ(config.mqtt->keepalive.sondeos, 3);
}

TEST(config_acepta_cada_seccion_por_separado_y_comentarios) {
  CHECK(error_de(con(kHttp, "")).empty());
  CHECK(error_de(con("", kMqtt)).empty());
  CHECK(error_de("// comentario\n{ /* otro */ " + std::string(kHttp) + " }").empty());
}

TEST(config_valores_explicitos) {
  const Config config = analizar_config(R"({
    "mqtt": {
      "escucha": {"ip": "192.0.2.11", "puerto": 18830},
      "permitidos": ["192.0.2.50", "fd00::5"],
      "upstream": {"ip": "198.51.100.7", "puerto": 1884},
      "max_conexiones": 4, "connect_timeout_ms": 1500,
      "keepalive": {"inactividad_s": 7, "intervalo_s": 3, "sondeos": 2}
    }})");
  CHECK_EQ(config.mqtt->escucha.puerto, 18830);
  CHECK_EQ(config.mqtt->upstream.puerto, 1884);
  CHECK_EQ(config.mqtt->permitidos.size(), static_cast<std::size_t>(2));
  CHECK_EQ(config.mqtt->max_conexiones, static_cast<std::size_t>(4));
  CHECK_EQ(config.mqtt->connect_timeout_ms, 1500);
  CHECK_EQ(config.mqtt->keepalive.inactividad_s, 7);
  CHECK_EQ(config.mqtt->keepalive.sondeos, 2);
}

TEST(config_el_ejemplo_versionado_es_valido) {
  const Config config = cargar_config(std::string(PUENTE_SOURCE_DIR) + "/puente_aula.conf.example");
  CHECK(config.http.has_value());
  CHECK(config.mqtt.has_value());
  CHECK(!config.permitir_bind_comodin);
}

TEST(config_rechaza_json_roto_y_raiz_no_objeto) {
  CHECK(contiene(error_de("{ esto no es json"), "JSON invalido"));
  CHECK(contiene(error_de(""), "JSON invalido"));
  CHECK(!error_de("[]").empty());
  CHECK(contiene(error_de("{}"), "al menos una seccion"));
}

TEST(config_rechaza_claves_desconocidas_y_tipos_erroneos) {
  CHECK(contiene(error_de(con(kHttp, kMqtt, R"("extra": 1)")), "extra: clave desconocida"));
  CHECK(contiene(error_de(R"({"http": {"escucha": {"ip":"192.0.2.10","puerto":80,"x":1},
      "permitidos":["198.51.100.7"],"upstream":{"ip":"192.0.2.50"}}})"),
                 "http.escucha.x: clave desconocida"));
  CHECK(contiene(error_de(R"({"http": {"escucha": {"ip":"192.0.2.10","puerto":"80"},
      "permitidos":["198.51.100.7"],"upstream":{"ip":"192.0.2.50"}}})"),
                 "http.escucha.puerto: se esperaba un entero"));
  CHECK(contiene(error_de(R"({"http": "no"})"), "http: se esperaba un objeto"));
  CHECK(contiene(error_de(R"({"mqtt": {"escucha":{"ip":"192.0.2.11"},"permitidos":["192.0.2.50"],
      "upstream":{"ip":"198.51.100.7"},"max_conexiones":"dos"}})"),
                 "mqtt.max_conexiones: se esperaba un entero"));
}

TEST(config_rechaza_puertos_y_limites_fuera_de_rango) {
  const auto con_puerto = [](const std::string& puerto) {
    return R"({"http": {"escucha": {"ip":"192.0.2.10","puerto":)" + puerto +
           R"(},"permitidos":["198.51.100.7"],"upstream":{"ip":"192.0.2.50"}}})";
  };
  CHECK(contiene(error_de(con_puerto("0")), "http.escucha.puerto"));
  CHECK(contiene(error_de(con_puerto("65536")), "http.escucha.puerto"));
  CHECK(contiene(error_de(con_puerto("-1")), "http.escucha.puerto"));
  CHECK(contiene(error_de(con_puerto("80.5")), "http.escucha.puerto"));
  CHECK(error_de(con_puerto("65535")).empty());

  const auto con_mqtt = [](const std::string& extra) {
    return R"({"mqtt": {"escucha":{"ip":"192.0.2.11"},"permitidos":["192.0.2.50"],
      "upstream":{"ip":"198.51.100.7"},)" + extra + "}}";
  };
  CHECK(contiene(error_de(con_mqtt(R"("max_conexiones": 0)")), "mqtt.max_conexiones"));
  CHECK(contiene(error_de(con_mqtt(R"("max_conexiones": 9)")), "mqtt.max_conexiones"));
  CHECK(error_de(con_mqtt(R"("max_conexiones": 8)")).empty());
  CHECK(contiene(error_de(con_mqtt(R"("connect_timeout_ms": 0)")), "mqtt.connect_timeout_ms"));
  CHECK(contiene(error_de(con_mqtt(R"("keepalive": {"sondeos": 0})")), "mqtt.keepalive.sondeos"));
  CHECK(contiene(error_de(con_mqtt(R"("keepalive": {"intervalo_s": 601})")),
                 "mqtt.keepalive.intervalo_s"));
  CHECK(contiene(error_de(con_mqtt(R"("keepalive": 5)")), "mqtt.keepalive: se esperaba un objeto"));

  const std::string http_base = R"("escucha":{"ip":"192.0.2.10","puerto":80},
      "permitidos":["198.51.100.7"],"upstream":{"ip":"192.0.2.50"})";
  CHECK(contiene(error_de("{\"http\":{" + http_base + R"(,"max_body_bytes":0}})"),
                 "http.max_body_bytes"));
  CHECK(contiene(error_de("{\"http\":{" + http_base + R"(,"max_body_bytes":99999999999}})"),
                 "http.max_body_bytes"));
  CHECK(contiene(error_de("{\"http\":{" + http_base + R"(,"read_timeout_ms":-5}})"),
                 "http.read_timeout_ms"));
}

TEST(config_rechaza_nombres_en_vez_de_ips) {
  CHECK(contiene(error_de(R"({"http": {"escucha": {"ip":"localhost","puerto":80},
      "permitidos":["198.51.100.7"],"upstream":{"ip":"192.0.2.50"}}})"),
                 "http.escucha.ip"));
  CHECK(contiene(error_de(R"({"http": {"escucha": {"ip":"192.0.2.10","puerto":80},
      "permitidos":["orangepi.local"],"upstream":{"ip":"192.0.2.50"}}})"),
                 "http.permitidos[0]"));
  CHECK(contiene(error_de(R"({"http": {"escucha": {"ip":"192.0.2.10","puerto":80},
      "permitidos":["198.51.100.7"],"upstream":{"ip":"placa.local"}}})"),
                 "http.upstream.ip"));
  CHECK(contiene(error_de(R"({"http": {"escucha": {"ip":"192.0.2.10","puerto":80},
      "permitidos":["198.51.100.7", 5],"upstream":{"ip":"192.0.2.50"}}})"),
                 "http.permitidos[1]"));
}

TEST(config_permitidos_vacios_o_ausentes_son_un_error) {
  CHECK(contiene(error_de(R"({"http": {"escucha": {"ip":"192.0.2.10","puerto":80},
      "permitidos":[],"upstream":{"ip":"192.0.2.50"}}})"),
                 "http.permitidos: no puede estar vacia"));
  CHECK(contiene(error_de(R"({"http": {"escucha": {"ip":"192.0.2.10","puerto":80},
      "upstream":{"ip":"192.0.2.50"}}})"),
                 "http.permitidos: falta"));
  CHECK(contiene(error_de(R"({"mqtt": {"escucha": {"ip":"192.0.2.11"},
      "permitidos":"192.0.2.50","upstream":{"ip":"198.51.100.7"}}})"),
                 "mqtt.permitidos: se esperaba una lista"));
}

TEST(config_un_comodin_nunca_vale_de_permitido_ni_de_destino) {
  CHECK(contiene(error_de(R"({"http": {"escucha": {"ip":"192.0.2.10","puerto":80},
      "permitidos":["0.0.0.0"],"upstream":{"ip":"192.0.2.50"}}})"),
                 "http.permitidos[0]"));
  // Conectar a 0.0.0.0 es conectar a esta misma maquina.
  CHECK(contiene(error_de(R"({"mqtt": {"escucha": {"ip":"192.0.2.11"},
      "permitidos":["192.0.2.50"],"upstream":{"ip":"0.0.0.0"}}})"),
                 "mqtt.upstream.ip"));
  // Ni con la opcion peligrosa.
  CHECK(!error_de(R"({"inseguro_permitir_escuchar_en_todas_las_interfaces": true,
      "mqtt": {"escucha": {"ip":"192.0.2.11"},
      "permitidos":["192.0.2.50"],"upstream":{"ip":"::"}}})").empty());
}

TEST(config_se_niega_a_escuchar_en_todas_las_interfaces_salvo_opcion_explicita) {
  const auto con_escucha = [](const std::string& ip, const std::string& extra) {
    return "{" + extra + R"("http": {"escucha": {"ip":")" + ip +
           R"(","puerto":8080},"permitidos":["198.51.100.7"],"upstream":{"ip":"192.0.2.50"}}})";
  };
  for (const std::string ip : {"0.0.0.0", "::", "0:0:0:0:0:0:0:0", "::ffff:0.0.0.0"}) {
    const std::string mensaje = error_de(con_escucha(ip, ""));
    CHECK(contiene(mensaje, "http.escucha.ip"));
    CHECK(contiene(mensaje, "no puede ser 0.0.0.0 ni ::"));
  }
  const std::string abierta = R"("inseguro_permitir_escuchar_en_todas_las_interfaces": true,)";
  CHECK(error_de(con_escucha("0.0.0.0", abierta)).empty());
  CHECK(error_de(con_escucha("::", abierta)).empty());
  CHECK(analizar_config(con_escucha("0.0.0.0", abierta)).permitir_bind_comodin);
  // false equivale a no ponerla.
  CHECK(!error_de(con_escucha("0.0.0.0",
                              R"("inseguro_permitir_escuchar_en_todas_las_interfaces": false,)"))
             .empty());
  CHECK(!error_de(con_escucha("0.0.0.0",
                              R"("inseguro_permitir_escuchar_en_todas_las_interfaces": "si",)"))
             .empty());
  // Tambien vale para el reenviador MQTT.
  CHECK(!error_de(R"({"mqtt": {"escucha": {"ip":"0.0.0.0"},
      "permitidos":["192.0.2.50"],"upstream":{"ip":"198.51.100.7"}}})").empty());
}

TEST(config_rechaza_bucles_y_escuchas_repetidas) {
  CHECK(contiene(error_de(R"({"mqtt": {"escucha": {"ip":"192.0.2.11"},
      "permitidos":["192.0.2.50"],"upstream":{"ip":"192.0.2.11"}}})"),
                 "bucle"));
  CHECK(contiene(error_de(R"({
      "http": {"escucha": {"ip":"192.0.2.11","puerto":1883},"permitidos":["198.51.100.7"],
               "upstream":{"ip":"192.0.2.50"}},
      "mqtt": {"escucha": {"ip":"192.0.2.11"},"permitidos":["192.0.2.50"],
               "upstream":{"ip":"198.51.100.7"}}})"),
                 "mqtt.escucha"));
}

TEST(config_cargar_archivo_inexistente_dice_cual) {
  try {
    cargar_config("/no/existe/puente_aula.conf");
    CHECK(false);
  } catch (const ErrorConfig& error) {
    CHECK(contiene(error.what(), "/no/existe/puente_aula.conf"));
  }
}

TEST(config_interfaz_de_escucha_valida_y_rechaza) {
  const auto con_interfaz = [](const std::string& valor) {
    return R"({"http": {"escucha": {"ip":"192.0.2.10","puerto":80,"interfaz":)" + valor +
           R"(},"permitidos":["198.51.100.7"],"upstream":{"ip":"192.0.2.50"}}})";
  };
  Config config = analizar_config(con_interfaz(R"("zt7nnjxxxx")"));
  CHECK_EQ(config.http->escucha.interfaz, std::string("zt7nnjxxxx"));
  CHECK_EQ(analizar_config(con_interfaz(R"("wlan0")")).http->escucha.interfaz, std::string("wlan0"));
  CHECK(error_de(con_interfaz(R"("br-lan.10")")).empty());
  CHECK(contiene(error_de(con_interfaz(R"("")")), "http.escucha.interfaz"));
  CHECK(contiene(error_de(con_interfaz(R"("una_interfaz_larga")")), "1 a 15"));  // 18 caracteres
  CHECK(contiene(error_de(con_interfaz(R"("wlan 0")")), "solo admite"));
  CHECK(contiene(error_de(con_interfaz(R"("w/lan")")), "solo admite"));
  CHECK(contiene(error_de(con_interfaz("5")), "se esperaba un texto"));
  // La interfaz es del que escucha: en un destino no tiene sentido.
  CHECK(contiene(error_de(R"({"http": {"escucha": {"ip":"192.0.2.10","puerto":80},
      "permitidos":["198.51.100.7"],"upstream":{"ip":"192.0.2.50","interfaz":"eth0"}}})"),
                 "http.upstream.interfaz: clave desconocida"));
  // Tambien en mqtt.
  CHECK(analizar_config(R"({"mqtt": {"escucha": {"ip":"192.0.2.11","interfaz":"wlan0"},
      "permitidos":["192.0.2.50"],"upstream":{"ip":"198.51.100.7"}}})").mqtt->escucha.interfaz ==
        "wlan0");
}

TEST(config_total_timeout_http) {
  const std::string base = R"({"http": {"escucha": {"ip":"192.0.2.10","puerto":80},
      "permitidos":["198.51.100.7"],"upstream":{"ip":"192.0.2.50"},)";
  CHECK_EQ(analizar_config(base + R"("total_timeout_ms": 2500}})").http->total_timeout_ms, 2500);
  CHECK(contiene(error_de(base + R"("total_timeout_ms": 0}})"), "http.total_timeout_ms"));
  CHECK(contiene(error_de(base + R"("total_timeout_ms": 120001}})"), "http.total_timeout_ms"));
}

TEST(config_el_destino_de_un_componente_no_puede_ser_la_escucha_del_otro) {
  const auto http_con = [](const std::string& upstream_ip, int upstream_puerto) {
    return R"("http": {"escucha": {"ip":"192.0.2.10","puerto":8080},"permitidos":["198.51.100.7"],
      "upstream":{"ip":")" + upstream_ip + R"(","puerto":)" + std::to_string(upstream_puerto) + "}}";
  };
  const auto mqtt_con = [](const std::string& upstream_ip, int upstream_puerto) {
    return R"("mqtt": {"escucha": {"ip":"192.0.2.11","puerto":1883},"permitidos":["192.0.2.50"],
      "upstream":{"ip":")" + upstream_ip + R"(","puerto":)" + std::to_string(upstream_puerto) + "}}";
  };
  // http.upstream == mqtt.escucha
  CHECK(contiene(error_de("{" + http_con("192.0.2.11", 1883) + "," + mqtt_con("198.51.100.7", 1883) + "}"),
                 "http.upstream"));
  // mqtt.upstream == http.escucha
  CHECK(contiene(error_de("{" + http_con("192.0.2.50", 80) + "," + mqtt_con("192.0.2.10", 8080) + "}"),
                 "mqtt.upstream"));
  // Misma IP con otro puerto es otra direccion: vale.
  CHECK(error_de("{" + http_con("192.0.2.11", 80) + "," + mqtt_con("198.51.100.7", 1883) + "}").empty());
}

TEST(config_avisa_de_cada_escucha_sin_interfaz) {
  Config config = analizar_config(con(kHttp, kMqtt));
  auto avisos = avisos_de_escucha(config);
  CHECK_EQ(avisos.size(), static_cast<std::size_t>(2));
  CHECK(contiene(avisos[0], "AVISO"));
  CHECK(contiene(avisos[0], "http.escucha.interfaz"));
  CHECK(contiene(avisos[1], "mqtt.escucha.interfaz"));
  CHECK(contiene(avisos[0], "puente_aula.nft"));

  config.http->escucha.interfaz = "ztXXXXXXXX";
  avisos = avisos_de_escucha(config);
  CHECK_EQ(avisos.size(), static_cast<std::size_t>(1));
  CHECK(contiene(avisos[0], "mqtt.escucha.interfaz"));
  config.mqtt->escucha.interfaz = "wlan0";
  CHECK(avisos_de_escucha(config).empty());
  // Una seccion ausente no avisa de nada.
  CHECK(avisos_de_escucha(analizar_config(con(kHttp, ""))).size() == 1);
}
