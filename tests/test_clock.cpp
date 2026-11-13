/**
 * @file test_clock.cpp
 * @brief The 32-bit millisecond clock wraps every 49.7 days and every wait is an elapsed time:
 * a driver started in the clock's upper half, a quiet line of more than 2^31 ms, a quarantine or a
 * hold-off followed by such a quiet, and a clock that crosses its wrap mid-transaction. Also the
 * bus's checks of a `Timing`, an abort before the send, and `Recover()` on a busy line from a state
 * that is not ready.
 */
#include <cstdint>
#include <string>

#include "rig.hpp"

using alicat_basis2::Error;
using rig::E;
using rig::Start;

namespace {
/// Half the clock's range and a little more: a quiet line of about 24.9 days.
constexpr uint32_t kHalfRangeMs = 0x8000'0000U + 5000U;
} // namespace

TEST_CASE("clock: a driver started when NowMs is in its upper half sends the switch-off") {
  model::FullRig r;
  r.inst.wrap_ms = 0x8000'1000ULL;
  REQUIRE(Start(r));
  const std::size_t n = r.inst.writes.size();
  CHECK(r.drv.SetSetpoint(0.0).ok());
  CHECK(r.inst.writes.size() > n);
}

TEST_CASE("clock: after more than 2^31 ms of quiet the switch-off is sent at once, twice") {
  model::FullRig r;
  REQUIRE(Start(r));
  REQUIRE(r.drv.ReadFlow().ok());
  r.inst.AdvanceMs(kHalfRangeMs);
  const std::size_t n = r.inst.writes.size();
  const uint64_t t0 = r.inst.now_us;
  CHECK(r.drv.SetSetpoint(0.0).ok());
  CHECK(r.inst.writes.size() > n);
  CHECK((r.inst.now_us - t0) < 500'000U);
  CHECK(r.drv.SetSetpoint(0.0).ok());
}

TEST_CASE("clock: a quarantine followed by more than 2^31 ms of quiet does not hold the bus") {
  alicat_basis2::Config c{};
  c.read_retries = 0;
  model::FullRig r(c);
  REQUIRE(Start(r));
  r.inst.faults.count = 1;
  r.inst.faults.drop = true;
  CHECK_EQ(E(r.drv.ReadFlow().error), E(Error::Timeout)); // the bus quarantines
  r.inst.AdvanceMs(kHalfRangeMs);
  const std::size_t n = r.inst.writes.size();
  const uint64_t t0 = r.inst.now_us;
  CHECK(r.drv.SetSetpoint(0.0).ok());
  CHECK(r.inst.writes.size() > n);
  CHECK((r.inst.now_us - t0) < 500'000U);
}

TEST_CASE("clock: a tare hold-off followed by more than 2^31 ms of quiet does not hold the bus") {
  model::FullRig r;
  REQUIRE(Start(r));
  REQUIRE(r.drv.Tare(2000U).ok());
  r.inst.AdvanceMs(kHalfRangeMs);
  const std::size_t n = r.inst.writes.size();
  CHECK(r.drv.SetSetpoint(0.0).ok());
  CHECK(r.inst.writes.size() > n);
  CHECK_STR(rig::Wire(r.inst, n).substr(0, 9), "AS 0.000$");
}

TEST_CASE("clock: a reply wait, a quarantine and a hold-off across the clock's wrap") {
  alicat_basis2::Config c{};
  c.read_retries = 0;
  model::FullRig r(c);
  REQUIRE(Start(r));
  // Put the wrap one second ahead: it comes during the tare's hold-off.
  r.inst.wrap_ms = (0x1'0000'0000ULL - 1000ULL) - (r.inst.now_us / 1000U);
  REQUIRE(r.inst.NowMsValue() > 0xFFFF'0000U);
  REQUIRE(r.drv.Tare(2000U).ok());    // its hold-off ends after the wrap
  CHECK(r.drv.SetSetpoint(0.0).ok()); // waits the hold-off out, then goes
  r.inst.faults.count = 1;
  r.inst.faults.drop = true;
  CHECK(!r.drv.ReadFlow().ok());
  CHECK(r.drv.ReadFlow().ok());         // the quarantine across the wrap ends
  CHECK(r.inst.NowMsValue() < 10'000U); // the clock did wrap
}

// --- the bus refuses a Timing it cannot compute waits from (review 9, m2) ---------------------

TEST_CASE("bus: a Timing with no line speed, or a request with an overlong duration, is refused") {
  model::FullRig r;
  REQUIRE(Start(r));
  auto poll = alicat_basis2::ascii::Make(alicat_basis2::Command::Poll, 'A',
                                         alicat_basis2::ascii::Args{}, false);
  REQUIRE(poll.ok());
  alicat_basis2::Timing t{};
  t.baud = 0U;
  CHECK_EQ(E(r.bus.Submit(poll.value, t)), E(Error::InvalidArgument));
  t.baud = 38400U;
  t.switch_to_baud = 1U;
  CHECK_EQ(E(r.bus.Submit(poll.value, t)), E(Error::InvalidArgument));
  t.switch_to_baud = 0U;
  poll.value.SetTiming(600'001U, 0U);
  CHECK_EQ(E(r.bus.Submit(poll.value, t)), E(Error::InvalidArgument));
  poll.value.SetTiming(0U, 600'001U);
  CHECK_EQ(E(r.bus.Submit(poll.value, t)), E(Error::InvalidArgument));
  poll.value.SetTiming(600'000U, 600'000U); // the longest allowed
  CHECK_EQ(E(r.bus.Submit(poll.value, t)), E(Error::Ok));
  r.bus.Abort();
}

// --- Abort before the send (review 9, m3) -------------------------------------------------------

TEST_CASE("bus: a request aborted in its gap is never sent, also once the line is quiet") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.Babble(50'000U); // the line is busy, the request waits in its gap
  const auto poll = alicat_basis2::ascii::Make(alicat_basis2::Command::Poll, 'A',
                                               alicat_basis2::ascii::Args{}, false);
  REQUIRE(poll.ok());
  alicat_basis2::Timing t{};
  t.baud = 38400U;
  REQUIRE(r.bus.Submit(poll.value, t) == Error::Ok);
  static_cast<void>(r.bus.Poll());
  const std::size_t n = r.inst.writes.size();
  r.bus.Abort();
  for (int i = 0; i < 500; ++i) {
    r.inst.AdvanceMs(1);
    static_cast<void>(r.bus.Poll());
  }
  CHECK_EQ(r.inst.writes.size(), n);
  CHECK(r.drv.ReadFlow().ok()); // and the bus is free again
}

// --- Recover on a busy line from a state that is not ready (review 9, m4) ---------------------

TEST_CASE("Recover under endless chatter from a driver that is not ready leaves it not ready") {
  model::FullRig r;
  REQUIRE(r.drv.EnsureInitialized().ok()); // Begin() never ran
  r.inst.Babble(600'000'000U);
  CHECK_EQ(E(r.drv.Recover().error), E(Error::Busy));
  CHECK(!r.drv.IsReady());
  CHECK(!r.drv.IsStateUnknown());
}

// --- the waits themselves, to the millisecond -------------------------------------------------

TEST_CASE("timing: after a reply the next request waits t3.5 and not more") {
  model::FullRig r;
  REQUIRE(Start(r));
  REQUIRE(r.drv.ReadFlow().ok());
  const uint64_t end = r.inst.ReplyEndUs();
  const std::size_t n = r.inst.writes.size();
  REQUIRE(r.drv.ReadFlow().ok());
  const uint64_t t35 = model::Instrument::T35Us(38400U);
  CHECK(r.inst.writes[n].at_us >= (end + t35));
  CHECK(r.inst.writes[n].at_us <= (end + t35 + 2000U));
}

TEST_CASE("timing: a tare holds the bus for its whole hold-off from the send") {
  model::FullRig r;
  REQUIRE(Start(r));
  const std::size_t n = r.inst.writes.size();
  REQUIRE(r.drv.Tare(2000U).ok());
  const uint64_t sent = r.inst.writes[n].at_us;
  const std::size_t m = r.inst.writes.size();
  REQUIRE(r.drv.ReadFlow().ok());
  const uint64_t holdoff = (2000U + r.drv.GetConfig().processing_ms) * 1000ULL;
  CHECK((r.inst.writes[m].at_us - sent) >= holdoff);
  CHECK((r.inst.writes[m].at_us - sent) <= (holdoff + 10'000U));
}

TEST_CASE("timing: after a lost reply a quiet line frees the bus after one reply wait, not two") {
  alicat_basis2::Config c{};
  c.read_retries = 0;
  model::FullRig r(c);
  REQUIRE(Start(r));
  r.inst.faults.count = 1;
  r.inst.faults.drop = true;
  const std::size_t n = r.inst.writes.size();
  CHECK_EQ(E(r.drv.ReadFlow().error), E(Error::Timeout));
  const uint64_t timed_out = r.inst.now_us;
  const uint64_t wait = timed_out - r.inst.writes[n].at_us;
  REQUIRE(r.drv.ReadFlow().ok());
  CHECK((r.inst.writes[n + 1U].at_us - timed_out) < ((wait * 3U) / 2U));
  CHECK((r.inst.writes[n + 1U].at_us - timed_out) >= wait);
}

TEST_CASE("scan: an answer that is no frame is drained before the next unit ID is polled") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.faults.count = 1;
  r.inst.faults.replace = "A garbled";
  const std::size_t n = r.inst.writes.size();
  const auto s = r.drv.ScanUnitIds();
  REQUIRE(s.ok());
  REQUIRE(r.inst.writes.size() > (n + 1U));
  // The poll of B waits out a reply wait after the bad answer (at least the 50 ms turnaround).
  CHECK((r.inst.writes[n + 1U].at_us - r.inst.writes[n].at_us) >= 50'000U);
}

TEST_CASE("Recover: a line that starts talking during its Begin() leaves the state as it was") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.Babble(600'000'000U, 40'000U); // after the scan's first poll has its answer
  const std::size_t n = r.inst.writes.size();
  CHECK_EQ(E(r.drv.Recover().error), E(Error::Busy));
  REQUIRE(r.inst.writes.size() > n); // the scan's poll went out and was answered
  CHECK(r.drv.IsReady());
  CHECK(!r.drv.IsStateUnknown());
}

TEST_CASE(
    "timing: a request right after a byte waits the rest of the gap on top of its line wait") {
  alicat_basis2::Config c{};
  c.baud = 4800U;
  model::FullRig r(c);
  r.inst.baud = 4800U;
  r.inst.port_baud = 4800U;
  REQUIRE(Start(r));
  r.inst.AdvanceMs(1000);
  r.inst.InjectStray("x", 0);
  static_cast<void>(r.bus.Poll()); // the byte is drained now: the gap starts again
  const auto poll = alicat_basis2::ascii::Make(alicat_basis2::Command::Poll, 'A',
                                               alicat_basis2::ascii::Args{}, false);
  REQUIRE(poll.ok());
  alicat_basis2::Timing t{};
  t.baud = 4800U;
  const auto line = [](uint32_t chars) { return ((chars * 10U * 1000U) + 4799U) / 4800U; };
  const uint32_t gap = 8U;
  const uint32_t line_wait = gap + line(poll.value.ReplyChars()) + t.processing_ms;
  // The peer talks until 3 ms before the line wait ends: quiet t3.5 later is still in time
  // only because the rest of the first gap counts on top of the line wait.
  const uint64_t talk_us = (static_cast<uint64_t>(line_wait) - 3U) * 1000U;
  std::string chatter(static_cast<std::size_t>(talk_us / model::Instrument::CharUs(4800U)), 'x');
  r.inst.InjectStray(chatter, 0);
  const std::size_t n = r.inst.writes.size();
  CHECK(r.bus.Execute(poll.value, t).ok());
  CHECK(r.inst.writes.size() > n);
}

// --- review 10 ---------------------------------------------------------------------------------

TEST_CASE("restore: a lost reply keeps the restore hold-off, the switch-off waits it out") {
  for (const bool modbus : {false, true}) {
    model::FullRig r(modbus ? model::ModbusConfig() : alicat_basis2::Config{});
    REQUIRE(Start(r));
    r.inst.faults.count = 1;
    r.inst.faults.drop = true; // the instrument restores, its reply is lost
    const std::size_t n = r.inst.writes.size();
    CHECK(!r.drv.FactoryRestore(alicat_basis2::ConfirmFactoryRestore{}).ok());
    REQUIRE(r.inst.writes.size() > n);
    const uint64_t sent = r.inst.writes[n].at_us;
    const std::size_t m = r.inst.writes.size();
    static_cast<void>(r.drv.SetSetpoint(0.0));
    REQUIRE(r.inst.writes.size() > m);
    const uint64_t holdoff = static_cast<uint64_t>(r.drv.GetConfig().restore_holdoff_ms) * 1000U;
    CHECK((r.inst.writes[m].at_us - sent) >= holdoff);
  }
}

TEST_CASE("scan: chatter during one middle poll makes the whole scan Busy") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.Babble(500'000U, 300'000U); // a peer talks for 0.5 s while a middle ID is polled
  const std::size_t n = r.inst.writes.size();
  CHECK_EQ(E(r.drv.ScanUnitIds().error), E(Error::Busy));
  CHECK(r.inst.writes.size() > (n + 1U));  // some polls went out before
  CHECK(r.inst.writes.size() < (n + 26U)); // and the scan stopped
}

TEST_CASE("timing: a quarantine ends t3.5 + 2 ms after the last stray byte") {
  alicat_basis2::Config c{};
  c.read_retries = 0;
  model::FullRig r(c);
  REQUIRE(Start(r));
  r.inst.faults.count = 1;
  r.inst.faults.drop = true;
  const std::size_t n = r.inst.writes.size();
  CHECK_EQ(E(r.drv.ReadFlow().error), E(Error::Timeout));
  const uint64_t wait = r.inst.now_us - r.inst.writes[n].at_us;
  // A peer talks past the quarantine's minimum, so the quiet line ends it.
  r.inst.InjectStray(
      std::string(static_cast<std::size_t>((wait + 20'000U) / model::Instrument::CharUs(38400U)),
                  'x'),
      0);
  const uint64_t last = r.inst.LastByteUs();
  const std::size_t m = r.inst.writes.size();
  REQUIRE(r.drv.ReadFlow().ok());
  const uint64_t t35 = model::Instrument::T35Us(38400U);
  CHECK(r.inst.writes[m].at_us >= (last + t35 + 2000U));
}

TEST_CASE("baud change: the reply wait is computed at the new speed") {
  alicat_basis2::Config c{};
  c.processing_ms = 5; // a short turnaround: the reply's line time decides
  model::FullRig r(c);
  r.inst.knobs.latency_us = 1000U;
  REQUIRE(Start(r));
  CHECK(r.drv.ChangeBaud(alicat_basis2::Baud::B4800, alicat_basis2::ConfirmBaudChange{}).ok());
  CHECK_EQ(r.drv.GetConfig().baud, 4800U);
}

TEST_CASE("bus: the highest line speed a Timing may name is 4000000") {
  model::FullRig r;
  REQUIRE(Start(r));
  const auto poll = alicat_basis2::ascii::Make(alicat_basis2::Command::Poll, 'A',
                                               alicat_basis2::ascii::Args{}, false);
  REQUIRE(poll.ok());
  alicat_basis2::Timing t{};
  t.baud = 4'000'001U;
  CHECK_EQ(E(r.bus.Submit(poll.value, t)), E(Error::InvalidArgument));
  t.baud = 38400U;
  t.switch_to_baud = 4'000'001U;
  CHECK_EQ(E(r.bus.Submit(poll.value, t)), E(Error::InvalidArgument));
  t.switch_to_baud = 0U;
  t.baud = 4'000'000U;
  CHECK_EQ(E(r.bus.Submit(poll.value, t)), E(Error::Ok));
  r.bus.Abort();
}

TEST_CASE("Recover: an instrument found at the other speed before a busy line keeps that speed") {
  // Twin rigs: the first shows when Begin() sends its first request after the scan found the
  // instrument at 19200; in the second a peer starts talking just before that.
  const auto doubt = [](model::FullRig& r) {
    r.inst.faults.count = 1;
    r.inst.faults.drop = true;
    return !r.drv.ChangeBaud(alicat_basis2::Baud::B19200, alicat_basis2::ConfirmBaudChange{}).ok();
  };
  model::FullRig a;
  REQUIRE(Start(a));
  REQUIRE(doubt(a));
  const uint64_t a0 = a.inst.now_us;
  const std::size_t an = a.inst.writes.size();
  REQUIRE(a.drv.Recover().ok());
  std::size_t found = an;
  while ((found < a.inst.writes.size()) && !a.inst.writes[found].text.starts_with("AVE")) {
    ++found; // the scan polls with `A`; Begin() starts by reading the firmware
  }
  REQUIRE(found < a.inst.writes.size());
  const uint64_t begin_at = a.inst.writes[found].at_us - a0;
  model::FullRig r;
  REQUIRE(Start(r));
  REQUIRE(doubt(r));
  r.inst.Babble(600'000'000U, begin_at - 500U);
  CHECK_EQ(E(r.drv.Recover().error), E(Error::Busy));
  CHECK(r.drv.IsStateUnknown());
  CHECK_EQ(r.drv.GetConfig().baud, 19200U);
  CHECK_EQ(r.inst.port_baud, 19200U);
}

TEST_CASE("Recover: a busy line leaves the learnt frame layout locked") {
  model::FullRig r;
  REQUIRE(Start(r));
  REQUIRE(r.drv.ReadFlow().ok()); // the controller's five-number frame is learnt
  r.inst.Babble(300'000U);
  CHECK_EQ(E(r.drv.Recover().error), E(Error::Busy));
  r.inst.AdvanceMs(1000);
  r.inst.controller = false; // a three-number frame now: the locked layout refuses it
  CHECK(!r.drv.ReadFrame().ok());
}

TEST_CASE("Recover: an instrument found under another ID before a busy line keeps that ID") {
  const auto moved = [](model::FullRig& r) { r.inst.unit_id = 'C'; }; // changed behind our back
  model::FullRig a;
  REQUIRE(Start(a));
  moved(a);
  const uint64_t a0 = a.inst.now_us;
  const std::size_t an = a.inst.writes.size();
  REQUIRE(a.drv.Recover().ok());
  std::size_t found = an;
  while ((found < a.inst.writes.size()) && !a.inst.writes[found].text.starts_with("CVE")) {
    ++found;
  }
  REQUIRE(found < a.inst.writes.size());
  const uint64_t begin_at = a.inst.writes[found].at_us - a0;
  model::FullRig r;
  REQUIRE(Start(r));
  moved(r);
  r.inst.Babble(600'000'000U, begin_at - 500U);
  CHECK_EQ(E(r.drv.Recover().error), E(Error::Busy));
  CHECK_CHAR(r.drv.GetConfig().unit_id, 'C');
  CHECK(r.drv.IsStateUnknown()); // found but not confirmed
  CHECK(!r.drv.IsReady());
}

TEST_CASE("baud change: after the send the bus waits for the reply at the new speed") {
  model::FullRig r;
  REQUIRE(Start(r));
  alicat_basis2::ascii::Args a{};
  a.a = 4800;
  const auto req = alicat_basis2::ascii::Make(alicat_basis2::Command::ChangeBaud, 'A', a, true);
  REQUIRE(req.ok());
  alicat_basis2::Timing t{};
  t.baud = 38400U;
  t.switch_to_baud = 4800U;
  REQUIRE(r.bus.Submit(req.value, t) == Error::Ok);
  const std::size_t n = r.inst.writes.size();
  for (int i = 0; (i < 50) && (r.inst.writes.size() == n); ++i) {
    r.inst.AdvanceMs(1);
    static_cast<void>(r.bus.Poll());
  }
  REQUIRE(r.inst.writes.size() > n); // sent: the port runs at 4800 now
  const auto line = [](uint32_t chars, uint32_t baud) {
    return ((chars * 10U * 1000U) + baud - 1U) / baud;
  };
  const auto bytes = static_cast<uint32_t>(req.value.Bytes().size());
  const uint32_t wait = 8U + line(req.value.ReplyChars(), 4800U) + t.slow_ms;
  const uint32_t line_wait = wait;
  CHECK_EQ(r.bus.WaitBoundMs(), 8U + line_wait + line(bytes, 4800U) + (3U * wait) + 2U + 8U);
  r.bus.Abort();
}
