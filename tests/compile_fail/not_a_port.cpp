/**
 * @file not_a_port.cpp
 * @brief Must NOT compile: the bus is constrained by the port concept.
 *
 * EXPECT: UartPort
 */
#include "alicat_basis2.hpp"

struct NotAPort {
  bool EnsureInitialized() noexcept {
    return true;
  }
};

int main() {
  NotAPort port;
  alicat_basis2::Bus<NotAPort> bus(port);
  return 0;
}
