/**
 * @file fuzz_modbus_replies.cpp
 * @brief Fuzz target: arbitrary bytes are the reply frame to each Modbus request, delivered in
 * chunks chosen by the input. A failed call returns zeros; a successful one decodes in bounds.
 */
#include "fuzz_common.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
  fuzz::Input in(data, size);
  const uint8_t which = in.Byte();
  alicat_basis2::Config c = model::ModbusConfig();
  c.full_scale_override_milli = 500000;
  c.read_retries = 0;
  c.modbus_flow_decimals = static_cast<uint8_t>(in.Byte() % 4U);
  model::FullRig r(c);
  r.inst.faults.count = 1000;
  r.inst.faults.chunks = static_cast<uint8_t>(1U + (in.Byte() % 5U));
  std::string reply = in.Rest();
  if (reply.size() > 300U) {
    reply.resize(300U);
  }
  r.inst.faults.replace = reply.empty() ? std::string("\x01") : reply;
  if (!r.drv.EnsureInitialized().ok()) {
    return 0;
  }
  fuzz::WireChecker w(r.inst, 500000);
  fuzz::SessionState st{};
  for (int i = 0; i < 3; ++i) {
    std::uint8_t pick[4] = {static_cast<std::uint8_t>(which + i), 0, 0, 0};
    fuzz::Input sel(pick, 4);
    fuzz::SessionStep(r.drv, w, sel, true, st);
  }
  FUZZ_REQUIRE(r.bus.WireViolations() == 0U);
  const auto f = r.drv.ReadFrame();
  fuzz::CheckFailedIsZero(f);
  return 0;
}
