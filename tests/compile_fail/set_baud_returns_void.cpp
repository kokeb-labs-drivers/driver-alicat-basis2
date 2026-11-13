/**
 * @file set_baud_returns_void.cpp
 * @brief Must NOT compile: an optional hook with the wrong signature is rejected, not ignored; a
 * `SetBaud` that cannot report failure would let a baud change leave the port at the wrong speed
 * unnoticed.
 *
 * EXPECT: SetBaud hook must be
 */
#include "mock_basis2.hpp"

class VoidBaudPort : public alicat_basis2::UartInterface<VoidBaudPort> {
public:
  bool EnsureInitialized() noexcept {
    return true;
  }
  bool Write(const uint8_t*, std::size_t) noexcept {
    return true;
  }
  std::size_t Read(uint8_t*, std::size_t, uint32_t) noexcept {
    return 0U;
  }
  bool FlushInput() noexcept {
    return true;
  }
  uint32_t NowMs() noexcept {
    return 0U;
  }
  void SetBaud(uint32_t) noexcept {}
};

int main() {
  VoidBaudPort port;
  alicat_basis2::Bus<VoidBaudPort> bus(port);
  return 0;
}
