/**
 * @file test_wire.cpp
 * @brief Timing and fault handling of the transaction machine against the model's wire: t3.5 gaps
 * at every speed, quarantine after a failed reply, retries of reads only, stray lines, clock wrap,
 * hook variants.
 */
#include "rig.hpp"

using alicat_basis2::Error;
using rig::E;
using rig::Start;

namespace {

alicat_basis2::Config At(uint32_t baud) {
  alicat_basis2::Config c{};
  c.baud = baud;
  return c;
}

} // namespace

TEST_CASE(
    "timing: the gap before every send is at least t3.5 from the last byte, at all six speeds") {
  const uint32_t bauds[6] = {4800, 9600, 19200, 38400, 57600, 115200};
  for (const uint32_t b : bauds) {
    model::FullRig r(At(b));
    r.inst.baud = b;
    r.inst.port_baud = b;
    REQUIRE(Start(r));
    for (int i = 0; i < 3; ++i) {
      CHECK(r.drv.ReadFlow().ok());
    }
    CHECK_EQ(r.inst.undefined_events, 0U);
    // Each write starts at least t3.5 after the previous reply ended (the first reply is the poll
    // of Begin()).
    for (std::size_t i = 1; i < r.inst.writes.size(); ++i) {
      const uint64_t gap_us =
          static_cast<uint64_t>(3500) * 10 * 1000 / (b / 1000 * 1000 / 1000) / 1000;
      static_cast<void>(gap_us);
      const uint64_t t35 = model::Instrument::T35Us(b);
      CHECK(r.inst.writes[i].at_us >= (r.inst.writes[i - 1].at_us + t35));
    }
  }
}

TEST_CASE("timing: the same driver runs on a port without DelayMs by spinning on NowMs") {
  model::BareRig r;
  REQUIRE(Start(r));
  CHECK(r.drv.ReadFlow().ok());
  CHECK(r.drv.SetSetpoint(10.0).ok());
  CHECK_EQ(r.inst.undefined_events, 0U);
}

TEST_CASE("timing: NowMs wrap-around neither sends early nor waits forever") {
  model::FullRig r;
  r.inst.wrap_ms = 0xFFFFFFF0ULL;
  REQUIRE(Start(r));
  for (int i = 0; i < 5; ++i) {
    CHECK(r.drv.ReadFlow().ok());
  }
  CHECK_EQ(r.inst.undefined_events, 0U);
}

TEST_CASE("timing: tare holds the wait for its duration, a measurement window does not block") {
  model::FullRig r;
  REQUIRE(Start(r));
  const uint64_t before = r.inst.now_us;
  CHECK(r.drv.Tare(2000).ok());
  CHECK((r.inst.now_us - before) >= (2000ULL * 1000ULL));
  const uint64_t mid = r.inst.now_us;
  CHECK(r.drv.StartMeasurement(4000).ok());
  CHECK((r.inst.now_us - mid) < (1000ULL * 1000ULL));
}

TEST_CASE("quarantine: after a timeout the next command waits for a quiet line and a late reply is "
          "never taken") {
  alicat_basis2::Config c{};
  c.read_retries = 0;
  model::FullRig r(c);
  REQUIRE(Start(r));
  r.inst.flow_milli = 100000;
  r.inst.faults.count = 1;
  r.inst.faults.delay_us = 100000; // later than the reply wait, inside the quarantine
  CHECK_EQ(E(r.drv.ReadFlow().error), E(Error::Timeout));
  r.inst.flow_milli = 200000;
  const auto f = r.drv.ReadFlow();
  CHECK_EQ(E(f.error), E(Error::Ok));
  CHECK_EQ(f.value.ToMilli(), 200000);
  CHECK_EQ(r.inst.undefined_events, 0U);
}

TEST_CASE("quarantine: every failure kind is followed by a working call") {
  struct Fault {
    const char* name;
    model::Faults f;
    Error expect;
  };
  std::vector<Fault> list;
  {
    model::Faults f;
    f.count = 1;
    f.drop = true;
    list.push_back({"drop", f, Error::Timeout});
  }
  {
    model::Faults f;
    f.count = 1;
    f.no_cr = true;
    list.push_back({"no cr", f, Error::Timeout});
  }
  {
    model::Faults f;
    f.count = 1;
    f.extend = 400;
    list.push_back({"overlong", f, Error::Overflow});
  }
  {
    model::Faults f;
    f.count = 1;
    f.wrong_id = 'B';
    list.push_back({"wrong id", f, Error::DeviceIdMismatch});
  }
  {
    model::Faults f;
    f.count = 1;
    f.garble_at = 3;
    list.push_back({"garbled", f, Error::BadReply});
  }
  {
    model::Faults f;
    f.count = 1;
    f.replace = "A +abc +1 +2 N2";
    list.push_back({"not numbers", f, Error::BadReply});
  }
  for (const Fault& entry : list) {
    alicat_basis2::Config c{};
    c.read_retries = 0;
    model::FullRig r(c);
    REQUIRE(Start(r));
    r.inst.faults = entry.f;
    const auto bad = r.drv.ReadFlow();
    CHECK_EQ(E(bad.error), E(entry.expect));
    CHECK_EQ(bad.value.ToMilli(), 0); // a failed call returns a zero-valued result
    const auto good = r.drv.ReadFlow();
    CHECK_EQ(E(good.error), E(Error::Ok));
    CHECK_EQ(r.inst.undefined_events, 0U);
  }
}

TEST_CASE("quarantine: a read is retried once, a write never") {
  model::FullRig r; // read_retries = 1
  REQUIRE(Start(r));
  std::size_t n = r.inst.writes.size();
  r.inst.faults.count = 1;
  r.inst.faults.drop = true;
  CHECK(r.drv.ReadFlow().ok());
  CHECK_EQ(r.inst.writes.size() - n, 2U);
  n = r.inst.writes.size();
  r.inst.faults.count = 1;
  r.inst.faults.drop = true;
  CHECK_EQ(E(r.drv.SetGas(alicat_basis2::GasId{3}).error), E(Error::Timeout));
  CHECK_EQ(r.inst.writes.size() - n, 1U);
  n = r.inst.writes.size();
  r.inst.faults.count = 1;
  r.inst.faults.drop = true;
  CHECK_EQ(E(r.drv.ResetTotal().error), E(Error::Timeout));
  CHECK_EQ(r.inst.writes.size() - n, 1U);
}

TEST_CASE("wire: split, prefixed, duplicated and CRLF-terminated replies") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.flow_milli = 100000;
  r.inst.faults.count = 1;
  r.inst.faults.chunks = 5;
  CHECK_EQ(r.drv.ReadFlow().value.ToMilli(), 100000);
  r.inst.faults.count = 1;
  r.inst.faults.lf_after_cr = true;
  CHECK_EQ(r.drv.ReadFlow().value.ToMilli(), 100000);
  r.inst.faults.count = 1;
  r.inst.faults.duplicate = true;
  CHECK_EQ(r.drv.ReadFlow().value.ToMilli(), 100000);
  CHECK(r.drv.ReadFlow().ok()); // the duplicate was drained as a stray line
  r.inst.faults.count = 1;
  r.inst.faults.error_reply = true;
  CHECK_EQ(E(r.drv.SetGas(alicat_basis2::GasId{1}).error), E(Error::Rejected));
  CHECK(r.drv.ReadFlow().ok());
  r.inst.knobs.error_with_id = true;
  r.inst.faults.count = 1;
  r.inst.faults.error_reply = false;
  CHECK_EQ(E(r.drv.SetGas(alicat_basis2::GasId{250}).error), E(Error::Rejected));
}

TEST_CASE("wire: a streaming peer is reported and ignored") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.InjectStray("@ +24.50 +1.0 +0.0 N2\r", 0);
  r.inst.InjectStray("@ +24.50 +1.0 +0.0 N2\r", 30000);
  r.inst.AdvanceMs(100);
  CHECK(r.drv.ReadFlow().ok());
  CHECK(r.bus.StrayLineCount() >= 1U);
  CHECK(r.bus.StreamingSuspected());
}

TEST_CASE("wire: Write and FlushInput failures are Bus errors and leave nothing pending") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.fail_write = true;
  CHECK_EQ(E(r.drv.ReadFlow().error), E(Error::Bus));
  r.inst.fail_write = false;
  CHECK(r.drv.ReadFlow().ok());
  r.inst.fail_flush = true;
  CHECK_EQ(E(r.drv.ReadFlow().error), E(Error::Bus));
  r.inst.fail_flush = false;
  CHECK(r.drv.ReadFlow().ok());
  model::FullRig q;
  q.inst.fail_init = true;
  CHECK_EQ(E(q.drv.EnsureInitialized().error), E(Error::Bus));
  CHECK_EQ(E(q.drv.Begin().error), E(Error::NotInitialized));
  CHECK_EQ(E(q.drv.ReadFlow().error), E(Error::NotInitialized));
}

TEST_CASE("wire: a half-duplex wiring that hears itself works with EchoMode::Skip only") {
  alicat_basis2::Config c{};
  c.echo = alicat_basis2::EchoMode::Skip;
  model::FullRig r(c);
  r.inst.knobs.echo_on_wire = true;
  REQUIRE(Start(r));
  CHECK(r.drv.ReadFlow().ok());
  model::FullRig q;
  q.inst.knobs.echo_on_wire = true;
  CHECK(!(q.drv.EnsureInitialized().ok() && q.drv.Begin().ok()));
}

TEST_CASE(
    "wire: a command sent while a reply is in flight is undefined; the driver never does it") {
  model::FullRig r;
  REQUIRE(Start(r));
  r.inst.faults.count = 1;
  r.inst.faults.chunks = 4;
  r.inst.faults.suffix = "A junk\r";
  for (int i = 0; i < 10; ++i) {
    static_cast<void>(r.drv.ReadFlow());
  }
  CHECK_EQ(r.inst.undefined_events, 0U);
}

TEST_CASE("wire: early partial reads and tiny read limits still complete") {
  model::FullRig r;
  r.inst.early_read_limit = 3;
  REQUIRE(Start(r));
  CHECK(r.drv.ReadFrame().ok());
}

TEST_CASE("wire: the bus refuses a second request while one is pending and Abort quarantines") {
  model::FullRig r;
  REQUIRE(Start(r));
  const auto made = alicat_basis2::ascii::Make(alicat_basis2::Command::Poll, 'A',
                                               alicat_basis2::ascii::Args{}, false);
  alicat_basis2::Timing t{};
  CHECK_EQ(E(r.bus.Submit(made.value, t)), E(Error::Ok));
  CHECK_EQ(E(r.bus.Submit(made.value, t)), E(Error::Busy));
  CHECK(r.bus.Poll() != alicat_basis2::PollState::Done);
  r.bus.Abort();
  CHECK_EQ(E(r.bus.Take().error), E(Error::Busy));
  const auto again = r.bus.Execute(made.value, t);
  CHECK_EQ(E(again.error), E(Error::Ok));
}

TEST_CASE("wire: a hold-off request closes the bus for its duration") {
  model::FullRig r;
  REQUIRE(Start(r));
  alicat_basis2::Request req = alicat_basis2::ascii::Make(alicat_basis2::Command::Poll, 'A',
                                                          alicat_basis2::ascii::Args{}, false)
                                   .value;
  req.SetTiming(0, 500);
  const uint64_t start = r.inst.now_us;
  CHECK(r.bus.Execute(req, alicat_basis2::Timing{}).ok());
  CHECK(r.drv.ReadFlow().ok());
  CHECK((r.inst.now_us - start) >= (500ULL * 1000ULL));
}
