#include <exception>
#include <iostream>

#include "minitest.hpp"

int main() {
  int failed = 0;
  for (const minitest::TestCase& test : minitest::registry()) {
    try {
      test.body();
      std::cout << "[ OK ] " << test.name << '\n';
    } catch (const minitest::Failure& failure) {
      ++failed;
      std::cout << "[FAIL] " << test.name << "\n       " << failure.message << '\n';
    } catch (const std::exception& error) {
      ++failed;
      std::cout << "[FAIL] " << test.name << "\n       excepcion inesperada: " << error.what() << '\n';
    }
  }
  std::cout << (minitest::registry().size() - failed) << "/" << minitest::registry().size()
            << " pruebas pasaron\n";
  return failed == 0 ? 0 : 1;
}
