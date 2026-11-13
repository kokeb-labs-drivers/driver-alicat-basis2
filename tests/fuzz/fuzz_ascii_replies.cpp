/**
 * @file fuzz_ascii_replies.cpp
 * @brief Fuzz target: arbitrary bytes are the reply to each of the ASCII commands, delivered in
 * chunks chosen by the input. Nothing the decoders return may be out of its documented bounds, and
 * a failed call returns zeros.
 */
#include "fuzz_common.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
  fuzz::Input in(data, size);
  const uint8_t which = in.Byte();
  alicat_basis2::Config c{};
  c.full_scale_override_milli = 500000;
  c.read_retries = 0;
  c.frame_layout = static_cast<alicat_basis2::FrameLayout>(in.Byte() % 3U);
  c.unknown_flags = static_cast<alicat_basis2::UnknownFlagPolicy>(in.Byte() & 1U);
  model::FullRig r(c);
  r.inst.faults.count = 1000;
  r.inst.faults.chunks = static_cast<uint8_t>(1U + (in.Byte() % 5U));
  std::string reply = in.Rest();
  if (reply.size() > 600U) {
    reply.resize(600U);
  }
  r.inst.faults.replace = reply.empty() ? std::string("A") : reply;
  if (!r.drv.EnsureInitialized().ok()) {
    return 0;
  }
  fuzz::WireChecker w(r.inst, 500000);
  fuzz::Input op(data, size);
  const int steps = 3;
  fuzz::SessionState st{};
  for (int i = 0; i < steps; ++i) {
    std::uint8_t pick[4] = {static_cast<std::uint8_t>(which + i), 0, 0, 0};
    fuzz::Input sel(pick, 4);
    fuzz::SessionStep(r.drv, w, sel, false, st);
  }
  FUZZ_REQUIRE(r.bus.WireViolations() == 0U);
  const auto f = r.drv.ReadFrame();
  fuzz::CheckFailedIsZero(f);
  if (f.ok()) {
    FUZZ_REQUIRE(f.value.present != 0U);
  }
  static_cast<void>(op);
  return 0;
}
