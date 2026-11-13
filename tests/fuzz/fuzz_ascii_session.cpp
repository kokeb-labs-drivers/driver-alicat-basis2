/**
 * @file fuzz_ascii_session.cpp
 * @brief Fuzz target: a random sequence of API calls over the ASCII protocol against the model,
 * with faults chosen by the input; every write is checked against the invariants of
 * `fuzz_common.hpp`.
 */
#include "fuzz_common.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
  fuzz::Input in(data, size);
  alicat_basis2::Config c{};
  c.baud = (in.Byte() & 1U) != 0U ? 38400U : 9600U;
  const uint8_t options = in.Byte();
  c.read_retries = static_cast<uint8_t>(options & 1U);
  const bool shared = (options & 2U) != 0U; // I15: a line declared to hold two instruments
  c.frame_layout = static_cast<alicat_basis2::FrameLayout>(in.Byte() % 3U);
  c.unknown_flags = static_cast<alicat_basis2::UnknownFlagPolicy>(in.Byte() & 1U);
  model::FullRig r(c);
  r.bus.SetDeviceCount(shared ? 2U : 1U);
  // I11: a clock just below its half range or its wrap (the 32-bit NowMs of a long-running device).
  if ((options & 4U) != 0U) {
    r.inst.wrap_ms = 0x7FFF'F000ULL;
  } else if ((options & 8U) != 0U) {
    r.inst.wrap_ms = 0xFFFF'F000ULL;
  } else {
    r.inst.wrap_ms = 0ULL;
  }
  r.inst.baud = c.baud;
  r.inst.port_baud = c.baud;
  r.inst.controller = (in.Byte() & 1U) != 0U;
  r.inst.knobs.aligned_columns = (in.Byte() & 1U) != 0U;
  r.inst.knobs.bracket_flags = (in.Byte() & 1U) != 0U;
  r.inst.flow_milli = static_cast<int64_t>(in.Word()) * 10;
  if (!r.drv.EnsureInitialized().ok()) {
    return 0;
  }
  fuzz::WireChecker w(r.inst, 500000);
  static_cast<void>(r.drv.Begin());
  w.Check(false);
  bool faulted = false;
  fuzz::SessionState st{};
  st.shared = shared;
  for (int step = 0; (step < 24) && !in.Empty(); ++step) {
    const uint8_t choose = in.Byte() & 3U;
    if (choose == 0U) {
      r.inst.faults = fuzz::ChooseFaults(in);
      faulted = true;
    } else if (choose == 1U) {
      // A line that may not go quiet (I9, I16): a peer talking for up to 5.1 s, from shorter than
      // a reply wait to longer than a measurement window the session asks for.
      r.inst.Babble(static_cast<uint64_t>(in.Byte()) * 20'000U);
      faulted = true;
    } else if ((choose == 2U) && ((in.Byte() & 7U) == 0U)) {
      r.inst.AdvanceMs(0x8000'0000U + (static_cast<uint32_t>(in.Byte()) * 1000U)); // ~25 days quiet
    } else {
      // No fault this step.
    }
    fuzz::SessionStep(r.drv, w, in, false, st);
    FUZZ_REQUIRE(r.bus.WireViolations() == 0U);
    // I13: a command sent into a reply still on the wire needs a fault chosen by the session.
    FUZZ_REQUIRE((r.inst.undefined_events == 0U) || faulted);
  }
  return 0;
}
