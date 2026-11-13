/**
 * @file test_busy.cpp
 * @brief What `Busy` means and how long it lasts: the quarantine after an unsent request, a busy
 * line in a later step of a writing call, the bound of the wait for a busy bus with a port that
 * follows the interface contract (and one that does not), and the reads `Begin()` adds over
 * Modbus. Each case fails on a one-edit change of the line it names.
 */
#include <cstdint>
#include <string>

#include "rig.hpp"

using alicat_basis2::ConfirmBaudChange;
using alicat_basis2::Error;
using rig::E;
using rig::Start;

namespace {

/// Back-to-back stray lines from a streaming peer, about `lines` x 5.7 ms at 38400 baud.
std::string Chatter(int lines) {
  std::string s;
  for (int i = 0; i < lines; ++i) {
    s += "@ +24.50 +1.0 +0.0 N2\r";
  }
  return s;
}

/// `ms` of back-to-back stray characters at `baud`.
std::string ChatterMs(uint32_t ms, uint32_t baud) {
  const uint64_t n = (static_cast<uint64_t>(ms) * 1000ULL) / model::Instrument::CharUs(baud);
  std::string s;
  for (uint64_t i = 0U; i < n; ++i) {
    s.push_back(((i % 20U) == 19U) ? '\r' : 'x');
  }
  return s;
}

/// A port as the interface contract describes it: `Write()` returns after the last stop bit (the
/// clock moves by the transmission time), and `DelayMs()` sleeps `OvershootMs` longer than asked
/// (one tick, as an RTOS rounds up; more than one is outside the contract).
template <uint32_t OvershootMs>
class ContractPort : public alicat_basis2::UartInterface<ContractPort<OvershootMs>> {
public:
  explicit ContractPort(model::Instrument& m) : m_(m) {}
  bool EnsureInitialized() noexcept {
    return true;
  }
  bool Write(const uint8_t* data, std::size_t n) noexcept {
    const bool ok = m_.WriteBytes(data, n);
    m_.Advance(model::Instrument::CharUs(m_.port_baud) * n);
    return ok;
  }
  std::size_t Read(uint8_t* out, std::size_t cap, uint32_t /*timeout_ms*/) noexcept {
    return m_.ReadBytes(out, cap);
  }
  bool FlushInput() noexcept {
    m_.Flush();
    return true;
  }
  uint32_t NowMs() noexcept {
    return m_.NowMsValue();
  }
  void DelayMs(uint32_t ms) noexcept {
    m_.AdvanceMs(ms + OvershootMs);
  }
  bool SetBaud(uint32_t baud) noexcept {
    m_.port_baud = baud;
    return baud != 0U;
  }

private:
  model::Instrument& m_;
};

/// A port whose task is preempted once: the first `DelayMs()` after `Arm()` returns `StallMs`
/// late (outside the contract; the wait for a busy bus must still end at its bound).
template <uint32_t StallMs>
class StallPort : public alicat_basis2::UartInterface<StallPort<StallMs>> {
public:
  explicit StallPort(model::Instrument& m) : m_(m) {}
  void Arm() {
    armed_ = true;
  }
  bool EnsureInitialized() noexcept {
    return true;
  }
  bool Write(const uint8_t* data, std::size_t n) noexcept {
    return m_.WriteBytes(data, n);
  }
  std::size_t Read(uint8_t* out, std::size_t cap, uint32_t /*timeout_ms*/) noexcept {
    return m_.ReadBytes(out, cap);
  }
  bool FlushInput() noexcept {
    m_.Flush();
    return true;
  }
  uint32_t NowMs() noexcept {
    return m_.NowMsValue();
  }
  void DelayMs(uint32_t ms) noexcept {
    m_.AdvanceMs(ms + (armed_ ? StallMs : 0U));
    armed_ = false;
  }

private:
  model::Instrument& m_;
  bool armed_{false};
};

/// For every start of a long chatter from 0 to `until_us`: when the call put anything on the wire,
/// it must not return `Busy` (a later step that meets the busy line reports `Timeout`).
template <typename Setup, typename Call>
int BusyAfterSend(Setup setup, Call call, uint32_t until_us) {
  int bad = 0;
  for (uint32_t delay = 0U; delay < until_us; delay += 1000U) {
    model::FullRig r(setup());
    if (!Start(r)) {
      return -1;
    }
    r.inst.AdvanceMs(4000);
    r.inst.InjectStray(Chatter(4000), delay);
    const std::size_t n = r.inst.writes.size();
    const Error e = call(r);
    if ((e == Error::Busy) && (r.inst.writes.size() > n)) {
      ++bad;
    }
  }
  return bad;
}

alicat_basis2::Config Ascii() {
  return alicat_basis2::Config{};
}
alicat_basis2::Config Modbus() {
  return model::ModbusConfig();
}

} // namespace

// --- the quarantine after an unsent request (review 7, M1) --------------------------------------

TEST_CASE("busy: after a finished measurement window an unsent read's quarantine is its own") {
  model::FullRig r;
  REQUIRE(Start(r));
  REQUIRE(r.drv.StartMeasurement(60000U).ok()); // the last request sent waits a minute
  r.inst.AdvanceMs(100);
  r.inst.InjectStray(Chatter(50), 0); // about 0.29 s
  CHECK_EQ(E(r.drv.ReadFlow().error), E(Error::Busy));
  const uint64_t t1 = r.inst.now_us;
  const std::size_t n = r.inst.writes.size();
  CHECK(r.drv.SetSetpoint(0.0).ok());
  CHECK(r.inst.writes.size() > n);
  CHECK((r.inst.now_us - t1) < 1'000'000U);
}

TEST_CASE("busy: after a slow setpoint an unsent read does not inherit its wait") {
  alicat_basis2::Config c{};
  c.verify_setpoint = alicat_basis2::VerifyPolicy::Off;
  model::FullRig r(c);
  REQUIRE(Start(r));
  REQUIRE(r.drv.SetSetpointSource(alicat_basis2::SetpointSource::SavedDigital).ok());
  r.inst.AdvanceMs(3000);
  REQUIRE(r.drv.SetSetpoint(10.0).ok()); // the saved source: a one-second reply wait
  r.inst.AdvanceMs(5000);
  r.inst.InjectStray(Chatter(50), 0); // about 0.29 s
  CHECK_EQ(E(r.drv.ReadFlow().error), E(Error::Busy));
  const uint64_t t1 = r.inst.now_us;
  CHECK(r.drv.ReadFlow().ok());
  CHECK((r.inst.now_us - t1) < 500'000U);
}

// --- Busy in a later step of a writing call (review 7, M2) --------------------------------------

TEST_CASE("busy: a setpoint whose verify meets a busy line reports Timeout, not Busy") {
  const int bad = BusyAfterSend(
      Ascii, [](model::FullRig& r) { return r.drv.SetSetpoint(10.0).error; }, 120'000U);
  CHECK_EQ(bad, 0);
}

TEST_CASE("busy: a Modbus gas whose read-back meets a busy line reports Timeout") {
  const int bad = BusyAfterSend(
      Modbus, [](model::FullRig& r) { return r.drv.SetGas(alicat_basis2::GasId{3}).error; },
      60'000U);
  CHECK_EQ(bad, 0);
}

TEST_CASE("busy: the second write of a Modbus tare or gains meets a busy line: Timeout") {
  CHECK_EQ(
      BusyAfterSend(Modbus, [](model::FullRig& r) { return r.drv.Tare(100U).error; }, 60'000U), 0);
  CHECK_EQ(BusyAfterSend(
               Modbus, [](model::FullRig& r) { return r.drv.SetGains(10U, 20U).error; }, 60'000U),
           0);
}

TEST_CASE("busy: a read retried after a lost reply reports Timeout when the retry meets chatter") {
  const auto setup = [] {
    alicat_basis2::Config c{};
    c.read_retries = 1;
    return c;
  };
  int bad = 0;
  for (uint32_t delay = 0U; delay < 400'000U; delay += 2000U) {
    model::FullRig r(setup());
    REQUIRE(Start(r));
    r.inst.faults.count = 1;
    r.inst.faults.drop = true; // the first try is sent and its reply lost
    r.inst.InjectStray(Chatter(4000), delay);
    const std::size_t n = r.inst.writes.size();
    const Error e = r.drv.ReadFlow().error;
    bad += ((e == Error::Busy) && (r.inst.writes.size() > n)) ? 1 : 0;
  }
  CHECK_EQ(bad, 0);
}

TEST_CASE("busy: a Modbus read retried after a lost reply reports Timeout as well") {
  int bad = 0;
  for (uint32_t delay = 0U; delay < 400'000U; delay += 2000U) {
    alicat_basis2::Config c = model::ModbusConfig();
    c.read_retries = 1;
    model::FullRig r(c);
    REQUIRE(Start(r));
    r.inst.faults.count = 1;
    r.inst.faults.drop = true;
    r.inst.InjectStray(Chatter(4000), delay);
    const std::size_t n = r.inst.writes.size();
    const Error e = r.drv.ReadTotal().error;
    bad += ((e == Error::Busy) && (r.inst.writes.size() > n)) ? 1 : 0;
  }
  CHECK_EQ(bad, 0);
}

TEST_CASE("busy: a SetRamp that was never sent leaves the setpoint verify on") {
  model::FullRig r;
  REQUIRE(Start(r));
  REQUIRE(r.drv.SetSetpointSource(alicat_basis2::SetpointSource::UnsavedDigital).ok());
  r.inst.InjectStray(Chatter(200), 0);
  CHECK_EQ(E(r.drv.SetRamp(1.0, alicat_basis2::TimeUnit::Second).error), E(Error::Busy));
  r.inst.AdvanceMs(2000);
  r.inst.clamp_setpoint_milli = 50'000;
  CHECK_EQ(E(r.drv.SetSetpoint(100.0).error), E(Error::VerifyMismatch));
}

// --- the wait for a busy bus (review 7, m1) -----------------------------------------------------

TEST_CASE("busy: with a contract port the wait for a transaction in flight never gives up early") {
  int busy = 0;
  for (uint32_t b1 = 496U; b1 <= 508U; b1 += 2U) {
    for (uint32_t o2 = 1040U; o2 <= 1080U; o2 += 8U) {
      for (uint32_t e2 = 2040U; e2 <= 2090U; e2 += 10U) {
        alicat_basis2::Config c{};
        c.baud = 4800U;
        model::Rig<ContractPort<1U>> r(c);
        r.inst.baud = 4800U;
        r.inst.port_baud = 4800U;
        REQUIRE(Start(r));
        r.inst.AdvanceMs(1000);
        alicat_basis2::ascii::Args sa{};
        sa.a = 10000;
        sa.dec = 3;
        const auto req =
            alicat_basis2::ascii::Make(alicat_basis2::Command::SetSetpoint, 'A', sa, false);
        REQUIRE(req.ok());
        alicat_basis2::Timing t{};
        t.baud = 4800U;
        t.processing_ms = 300U;
        r.inst.faults.count = 1;
        r.inst.faults.drop = true; // the request in flight is sent and its reply lost
        r.inst.InjectStray(ChatterMs(b1, 4800U), 0);
        r.inst.InjectStray(ChatterMs(e2 - o2, 4800U), o2 * 1000U);
        REQUIRE(r.bus.Submit(req.value, t) == Error::Ok);
        busy += (r.drv.ReadFlow().error == Error::Busy) ? 1 : 0;
      }
    }
  }
  CHECK_EQ(busy, 0);
}

TEST_CASE("busy: when the port stalls past the bound the wait gives up, Busy and unsent") {
  model::Rig<StallPort<3000U>> r;
  REQUIRE(Start(r));
  const auto poll = alicat_basis2::ascii::Make(alicat_basis2::Command::Poll, 'A',
                                               alicat_basis2::ascii::Args{}, false);
  REQUIRE(poll.ok());
  alicat_basis2::Timing t{};
  t.baud = 38400U;
  r.inst.InjectStray(Chatter(5600), 0); // about 3.2 s: the request in flight cannot be sent
  REQUIRE(r.bus.Submit(poll.value, t) == Error::Ok);
  r.port.Arm(); // the first sleep of the wait returns three seconds late
  const uint64_t t0 = r.inst.now_us;
  const std::size_t n = r.inst.writes.size();
  CHECK_EQ(E(r.drv.ReadFlow().error), E(Error::Busy));
  CHECK_EQ(r.inst.writes.size(), n);
  CHECK((r.inst.now_us - t0) < 3'100'000U); // given up at the first check past the bound
}

TEST_CASE("busy: the wait bound is gap, a line wait, the request's line time, three reply waits, "
          "hold-off and slack") {
  model::Rig<ContractPort<1U>> r;
  REQUIRE(Start(r));
  alicat_basis2::ascii::Args sa{};
  sa.a = 10000;
  sa.dec = 3;
  auto req = alicat_basis2::ascii::Make(alicat_basis2::Command::SetSetpoint, 'A', sa, false);
  REQUIRE(req.ok());
  req.value.SetTiming(5000U, 700U); // a request with its own duration and a hold-off
  alicat_basis2::Timing t{};
  t.baud = 4800U;
  t.processing_ms = 300U;
  REQUIRE(r.bus.Submit(req.value, t) == Error::Ok);
  // At 4800 baud a character takes 10/4800 s: t3.5 is 8 ms (rounded up), the request's line time
  // is its characters at that rate, a line wait is t3.5, the reply's line time and 300 ms, and a
  // reply wait adds the request's own 5000 ms.
  const auto line = [](uint32_t chars) { return ((chars * 10U * 1000U) + 4799U) / 4800U; };
  const uint32_t gap = 8U;
  const uint32_t line_wait = gap + line(req.value.ReplyChars()) + 300U;
  const uint32_t wait = line_wait + 5000U;
  const auto sent = static_cast<uint32_t>(req.value.Bytes().size());
  CHECK_EQ(r.bus.WaitBoundMs(), gap + line_wait + line(sent) + (3U * wait) + 700U + 2U + (4U * 2U));
}

// --- after an unsent request with a long duration (review 8, M1) -------------------------------

TEST_CASE(
    "busy: after an unsent measurement window a switch-off on a quiet line goes out at once") {
  for (const uint32_t window : {60000U, 10000U}) {
    model::FullRig r;
    REQUIRE(Start(r));
    r.inst.AdvanceMs(100);
    r.inst.Babble((static_cast<uint64_t>(window) + 3000U) * 1000U); // longer than the window
    CHECK_EQ(E(r.drv.StartMeasurement(window).error), E(Error::Busy));
    CHECK((r.inst.now_us) < (r.inst.LastByteUs())); // it gave up while the peer still talked
    r.inst.now_us = r.inst.LastByteUs() + 50'000U;  // the peer stopped; a quiet line
    const std::size_t n = r.inst.writes.size();
    const uint64_t t1 = r.inst.now_us;
    CHECK(r.drv.SetSetpoint(0.0).ok());
    CHECK(r.inst.writes.size() > n);
    CHECK((r.inst.now_us - t1) < 1'000'000U);
  }
}

TEST_CASE("busy: after an unsent request the quarantine ends as soon as the line is quiet") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.AdvanceMs(100);
  const uint64_t t0 = r.inst.now_us;
  r.inst.Babble(150'000U); // longer than the line wait of the request, much shorter than its window
  CHECK_EQ(E(r.drv.StartMeasurement(60000U).error), E(Error::Busy));
  const uint64_t busy_at = r.inst.now_us - t0;
  REQUIRE(busy_at < 150'000U); // it gave up while the peer still talked
  const std::size_t n = r.inst.writes.size();
  CHECK(r.drv.SetSetpoint(0.0).ok());
  REQUIRE(r.inst.writes.size() > n);
  // Sent a t3.5 after the peer stopped, not after two line waits from the Busy.
  CHECK((r.inst.writes[n].at_us - t0) < (150'000U + 10'000U));
  CHECK((r.inst.writes[n].at_us - t0) < (busy_at + 2U * busy_at));
}

TEST_CASE("busy: on a line that keeps talking a switch-off after an unsent window gives up fast") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.AdvanceMs(100);
  r.inst.Babble(400'000'000U); // 400 s
  const uint64_t t0 = r.inst.now_us;
  CHECK_EQ(E(r.drv.StartMeasurement(60000U).error), E(Error::Busy));
  CHECK((r.inst.now_us - t0) < 1'000'000U); // the gap wait leaves the window out
  const uint64_t t1 = r.inst.now_us;
  CHECK_EQ(E(r.drv.SetSetpoint(0.0).error), E(Error::Busy));
  CHECK((r.inst.now_us - t1) < 2'000'000U); // its own line waits, not two windows
}

TEST_CASE("busy: a recent byte puts the gap before the quiet wait; the bound covers it "
          "(review 8, P1)") {
  int early = 0;
  for (int64_t b1 = -2000; b1 <= 1000; b1 += 500) {
    for (uint64_t ph = 0U; ph < 2000U; ph += 250U) {
      for (int64_t o2 = -1000; o2 <= 3000; o2 += 1000) {
        alicat_basis2::Config c{};
        c.baud = 4800U;
        model::Rig<ContractPort<1U>> r(c);
        r.inst.baud = 4800U;
        r.inst.port_baud = 4800U;
        REQUIRE(Start(r));
        r.inst.AdvanceMs(1000);
        r.inst.Advance(ph);
        alicat_basis2::ascii::Args sa{};
        sa.a = 10000;
        sa.dec = 3;
        const auto req =
            alicat_basis2::ascii::Make(alicat_basis2::Command::SetSetpoint, 'A', sa, false);
        REQUIRE(req.ok());
        alicat_basis2::Timing t{};
        t.baud = 4800U;
        t.processing_ms = 300U;
        const auto line = [](uint64_t chars) { return ((chars * 10U * 1000U) + 4799U) / 4800U; };
        const uint64_t w = 8U + line(req.value.ReplyChars()) + 300U;
        const uint64_t l = line(req.value.Bytes().size());
        r.inst.faults.count = 1;
        r.inst.faults.drop = true;
        const auto b1us = static_cast<uint64_t>(static_cast<int64_t>(w * 1000U) + b1);
        r.inst.InjectStray(ChatterMs(static_cast<uint32_t>(b1us / 1000U), 4800U), 0);
        r.inst.InjectStray("x", static_cast<uint32_t>(b1us));
        const auto s2 =
            static_cast<uint64_t>(static_cast<int64_t>(b1us + 8000U + ((l + w) * 1000U)) + o2);
        r.inst.InjectStray(ChatterMs(static_cast<uint32_t>((2U * w) + 30U), 4800U),
                           static_cast<uint32_t>(s2));
        static_cast<void>(r.bus.Poll()); // a stray byte drained: send_at is the gap after now
        REQUIRE(r.bus.Submit(req.value, t) == Error::Ok);
        const auto poll = alicat_basis2::ascii::Make(alicat_basis2::Command::Poll, 'A',
                                                     alicat_basis2::ascii::Args{}, false);
        early += (r.bus.Execute(poll.value, t).error == Error::Busy) ? 1 : 0;
      }
    }
  }
  CHECK_EQ(early, 0);
}

// --- scans and Recover() on a line that never goes quiet (review 8, m2) -------------------------

TEST_CASE("busy: a unit ID or address scan under endless chatter reports Busy, not nobody") {
  model::FullRig a;
  REQUIRE(Start(a));
  a.inst.Babble(600'000'000U);
  const std::size_t na = a.inst.writes.size();
  CHECK_EQ(E(a.drv.ScanUnitIds().error), E(Error::Busy));
  CHECK_EQ(a.inst.writes.size(), na);
  CHECK_CHAR(a.drv.GetConfig().unit_id, 'A');
  model::FullRig m(model::ModbusConfig());
  REQUIRE(Start(m));
  m.inst.Babble(600'000'000U);
  CHECK_EQ(E(m.drv.ScanModbusAddresses(1, 247).error), E(Error::Busy));
}

TEST_CASE("busy: Recover under endless chatter reports Busy and leaves the state as it was") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.Babble(600'000'000U);
  const std::size_t n = r.inst.writes.size();
  CHECK_EQ(E(r.drv.Recover().error), E(Error::Busy));
  CHECK_EQ(r.inst.writes.size(), n);
  CHECK(!r.drv.IsStateUnknown());
  CHECK(r.drv.IsReady());
  model::FullRig m(model::ModbusConfig());
  REQUIRE(Start(m));
  m.inst.faults.count = 1;
  m.inst.faults.drop = true;
  CHECK(!m.drv.ChangeBaud(alicat_basis2::Baud::B19200, ConfirmBaudChange{}).ok());
  REQUIRE(m.drv.IsStateUnknown());
  m.inst.Babble(600'000'000U);
  const uint32_t sets = m.inst.baud_sets;
  CHECK_EQ(E(m.drv.Recover().error), E(Error::Busy));
  CHECK(m.drv.IsStateUnknown());    // the doubt stays
  CHECK_EQ(m.inst.baud_sets, sets); // no other speed is tried on a line that never goes quiet
  CHECK_EQ(m.inst.port_baud, 38400U);
}

// --- Recover on a shared line (review 7, m5) ----------------------------------------------------

TEST_CASE("shared line: Recover polls the held ID at the held speed only") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.faults.count = 1;
  r.inst.faults.drop = true;
  CHECK(!r.drv.ChangeBaud(alicat_basis2::Baud::B19200, ConfirmBaudChange{}).ok());
  REQUIRE(r.drv.IsStateUnknown());
  r.bus.SetDeviceCount(2);
  CHECK(!r.drv.Recover().ok());
  CHECK_EQ(r.inst.port_baud, 38400U);
  CHECK_EQ(r.drv.GetConfig().baud, 38400U);
  CHECK(r.drv.IsStateUnknown());
}

// --- the reads Begin() adds over Modbus (review 7, m3) ------------------------------------------

TEST_CASE("Modbus Begin: an exception on 516 or on 524 leaves that item unknown") {
  model::FullRig s(model::ModbusConfig());
  s.inst.source = 'U';
  s.inst.knobs.refuse_register = 516;
  REQUIRE(Start(s));
  CHECK(!s.drv.GetIdentity().source_known);
  model::FullRig p(model::ModbusConfig());
  p.inst.ramp_rate_milli = 5000;
  p.inst.knobs.refuse_register = 524;
  REQUIRE(Start(p));
  CHECK(p.drv.GetIdentity().source_known);
  CHECK(!p.drv.GetIdentity().ramp_active);
}

TEST_CASE("Modbus Begin: no reply to 516 or to 524 is a Timeout") {
  model::FullRig s(model::ModbusConfig());
  REQUIRE(s.drv.EnsureInitialized().ok());
  s.inst.knobs.silent_register = 516;
  CHECK_EQ(E(s.drv.Begin().error), E(Error::Timeout));
  CHECK(!s.drv.IsReady());
  model::FullRig p(model::ModbusConfig());
  REQUIRE(p.drv.EnsureInitialized().ok());
  p.inst.knobs.silent_register = 524;
  CHECK_EQ(E(p.drv.Begin().error), E(Error::Timeout));
}
