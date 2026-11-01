#pragma once

// Mini-runner de pruebas sin dependencias externas (no hay gtest ni catch2 en la placa).
// Un CHECK fallido aborta solo esa prueba. El main esta en test_main.cpp.

#include <cmath>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace minitest {

struct TestCase {
  std::string name;
  std::function<void()> body;
};

inline std::vector<TestCase>& registry() {
  static std::vector<TestCase> tests;
  return tests;
}

struct Registrar {
  Registrar(std::string name, std::function<void()> body) {
    registry().push_back({std::move(name), std::move(body)});
  }
};

struct Failure {
  std::string message;
};

template <typename A, typename B>
void check_eq(const A& a, const B& b, const char* expr_a, const char* expr_b, const char* file,
              int line) {
  if (!(a == b)) {
    std::ostringstream out;
    out << file << ":" << line << ": CHECK_EQ(" << expr_a << ", " << expr_b << ") -> " << a
        << " != " << b;
    throw Failure{out.str()};
  }
}

inline void check_near(double a, double b, double eps, const char* expr, const char* file,
                       int line) {
  if (!(std::fabs(a - b) <= eps)) {
    std::ostringstream out;
    out << file << ":" << line << ": CHECK_NEAR(" << expr << ") -> " << a << " vs " << b
        << " (eps " << eps << ")";
    throw Failure{out.str()};
  }
}

}  // namespace minitest

#define TEST(name)                                                     \
  static void name();                                                  \
  static ::minitest::Registrar minitest_registrar_##name(#name, name); \
  static void name()

#define CHECK(cond)                                                                       \
  do {                                                                                    \
    if (!(cond)) {                                                                        \
      throw ::minitest::Failure{std::string(__FILE__) + ":" + std::to_string(__LINE__) + \
                                ": CHECK(" #cond ")"};                                    \
    }                                                                                     \
  } while (0)

#define CHECK_EQ(a, b) ::minitest::check_eq((a), (b), #a, #b, __FILE__, __LINE__)
#define CHECK_NEAR(a, b, eps) ::minitest::check_near((a), (b), (eps), #a " ~ " #b, __FILE__, __LINE__)

// La expresion debe lanzar una excepcion del tipo indicado.
#define CHECK_THROWS(expr, ExceptionType)                                                 \
  do {                                                                                    \
    bool minitest_thrown = false;                                                         \
    try {                                                                                 \
      (void)(expr);                                                                       \
    } catch (const ExceptionType&) {                                                      \
      minitest_thrown = true;                                                             \
    }                                                                                     \
    if (!minitest_thrown) {                                                               \
      throw ::minitest::Failure{std::string(__FILE__) + ":" + std::to_string(__LINE__) + \
                                ": CHECK_THROWS(" #expr ", " #ExceptionType ")"};         \
    }                                                                                     \
  } while (0)
