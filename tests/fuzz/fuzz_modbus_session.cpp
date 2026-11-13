/**
 * @file fuzz_modbus_session.cpp
 * @brief Fuzz target: a random sequence of API calls over Modbus RTU against the register model,
 * with faults chosen by the input; every frame written is checked against the invariants of
 * `fuzz_common.hpp`.
 */
#include "fuzz_common.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
  fuzz::Input in(data, size);
  alicat_basis2::Config c = model::ModbusConfig();
  c.modbus_flow_decimals = static_cast<uint8_t>(in.Byte() % 4U);
  const uint8_t options = in.Byte();
  c.read_retries = static_cast<uint8_t>(options & 1U);
  const bool shared = (options & 2U) != 0U; // I15: a line declared to hold two instruments
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
  r.inst.mb_decimals = c.modbus_flow_decimals;
  r.inst.firmware = (in.Byte() & 1U) != 0U ? 0x305 : 0x240;
  r.inst.flow_milli = static_cast<int64_t>(in.Word() % 3000U) * 10;
  if (!r.drv.EnsureInitialized().ok()) {
    return 0;
  }
  fuzz::WireChecker w(r.inst, 500000);
  static_cast<void>(r.drv.Begin());
  w.Check(true);
  fuzz::SessionState st{};
  st.shared = shared;
  for (int step = 0; (step < 24) && !in.Empty(); ++step) {
    const uint8_t choose = in.Byte() & 3U;
    if (choose == 0U) {
      r.inst.faults = fuzz::ChooseFaults(in);
    } else if (choose == 1U) {
      // A line that may not go quiet (I9, I16): a peer talking for up to 5.1 s, from shorter than
      // a reply wait to longer than a measurement window the session asks for.
      r.inst.Babble(static_cast<uint64_t>(in.Byte()) * 20'000U);
    } else if ((choose == 2U) && ((in.Byte() & 7U) == 0U)) {
      r.inst.AdvanceMs(0x8000'0000U + (static_cast<uint32_t>(in.Byte()) * 1000U)); // ~25 days quiet
    } else {
      // No fault this step.
    }
    fuzz::SessionStep(r.drv, w, in, true, st);
    FUZZ_REQUIRE(r.bus.WireViolations() == 0U);
  }
  return 0;
}
