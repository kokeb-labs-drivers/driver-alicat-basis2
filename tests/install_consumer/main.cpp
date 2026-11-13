/**
 * @file main.cpp
 * @brief Builds the driver from the installed package only: headers, version header and .ipp.
 */
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "alicat_basis2.hpp"

namespace {

// A port that never receives a byte: enough to instantiate the whole driver template.
class SilentPort : public alicat_basis2::UartInterface<SilentPort> {
public:
  bool EnsureInitialized() noexcept {
    return true;
  }
  bool Write(const uint8_t* /*data*/, std::size_t /*size*/) noexcept {
    return true;
  }
  std::size_t Read(uint8_t* /*data*/, std::size_t /*capacity*/, uint32_t /*timeout_ms*/) noexcept {
    return 0U;
  }
  bool FlushInput() noexcept {
    return true;
  }
  uint32_t NowMs() noexcept {
    now_ += 1U;
    return now_;
  }

private:
  uint32_t now_{0};
};

} // namespace

int main() {
  SilentPort port;
  alicat_basis2::Bus<SilentPort> bus(port);
  alicat_basis2::Basis2<SilentPort> meter(bus);
  if (!meter.EnsureInitialized().ok()) {
    return 1;
  }
  // No reply ever arrives, so the first read must time out: proof the template compiled and ran.
  const auto r = meter.ReadFlow();
  return ((r.error == alicat_basis2::Error::Timeout) &&
          !std::string_view(alicat_basis2::GetDriverVersion()).empty())
             ? 0
             : 1;
}
