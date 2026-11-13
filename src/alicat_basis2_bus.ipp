/**
 * @file alicat_basis2_bus.ipp
 * @brief The transaction machine of `Bus<Port>`, included by `alicat_basis2_uart_interface.hpp`.
 * @copyright Copyright (c) 2026 The driver-alicat-basis2 authors. SPDX-License-Identifier: MIT
 *
 * Section numbers refer to the operating manual DOC-MANUAL-BASIS2 Rev 4 (printed pages "M p.N").
 */
#ifndef ALICAT_BASIS2_BUS_IPP
#define ALICAT_BASIS2_BUS_IPP

namespace alicat_basis2 {

namespace detail {
/// Extra quiet time on top of t3.5 before a drained line counts as quiet, in milliseconds.
inline constexpr uint32_t kQuietExtraMs = 2;
/// How late one phase of a transaction may be seen to end: a poll sleeps 1 ms, which a port's
/// `DelayMs()` may round up by one tick.
inline constexpr uint32_t kPollLagMs = 2;
/// Line speeds a `Timing` may name: every wait is computed from it (0 would divide by zero).
inline constexpr uint32_t kMinBaud = 1200;
inline constexpr uint32_t kMaxBaud = 4'000'000;
/// The longest duration (reply wait extra, hold-off) a request may carry, in milliseconds: ten
/// minutes, above the longest measurement window, so no computed wait can overflow.
inline constexpr uint32_t kMaxDurationMs = 600'000;
/// Upper bound of port reads in one `Poll()`.
inline constexpr uint32_t kMaxReadsPerPoll = 8;
/// Upper bound of `Poll()` iterations in one blocking call (a broken clock cannot hang the caller).
inline constexpr uint32_t kMaxPollLoops = 4'000'000;
/// Whether every wait of `request` at `timing` can be computed: a line speed in range (0 would
/// divide by zero), and durations short enough that no sum of them overflows.
[[nodiscard]] constexpr bool SpeedUsable(uint32_t baud) noexcept {
  return (baud >= kMinBaud) && (baud <= kMaxBaud);
}
[[nodiscard]] constexpr bool TimingUsable(const Request& request, const Timing& timing) noexcept {
  const bool switch_ok = (timing.switch_to_baud == 0U) || SpeedUsable(timing.switch_to_baud);
  return SpeedUsable(timing.baud) && switch_ok && (request.ExtraMs() <= kMaxDurationMs) &&
         (request.HoldoffMs() <= kMaxDurationMs);
}
} // namespace detail

template <UartPort Port>
void Bus<Port>::log(LogLevel level, const char* message) noexcept {
  if constexpr (detail::HasLog<Port>) {
    port_.Log(level, message);
  } else {
    static_cast<void>(level);
    static_cast<void>(message);
  }
}

template <UartPort Port>
void Bus<Port>::sleep(uint32_t ms) noexcept {
  if constexpr (detail::HasDelayMs<Port>) {
    port_.DelayMs(ms);
  } else {
    static_cast<void>(ms); // the caller spins on NowMs()
  }
}

template <UartPort Port>
Result<void> Bus<Port>::ApplyBaud(uint32_t baud) noexcept {
  if constexpr (detail::HasSetBaud<Port>) {
    return port_.SetBaud(baud) ? Result<void>::Success() : Result<void>::Failure(Error::Bus);
  } else {
    static_cast<void>(baud);
    return Result<void>::Failure(Error::Unsupported);
  }
}

template <UartPort Port>
uint32_t Bus<Port>::gapMs() const noexcept {
  return request_.IsModbus() ? detail::ModbusGapMs(timing_.baud) : detail::T35Ms(timing_.baud);
}

template <UartPort Port>
uint32_t Bus<Port>::lineWaitMs() const noexcept {
  const uint32_t chars = request_.ReplyChars();
  const uint32_t line = request_.IsModbus() ? detail::ModbusFrameMs(chars, timing_.baud)
                                            : detail::LineMs(chars, timing_.baud);
  const uint32_t turnaround = request_.IsSlow() ? timing_.slow_ms : timing_.processing_ms;
  return gapMs() + line + turnaround;
}

template <UartPort Port>
uint32_t Bus<Port>::replyMs() const noexcept {
  return lineWaitMs() + request_.ExtraMs();
}

template <UartPort Port>
PollState Bus<Port>::pollState() const noexcept {
  if (result_ready_) {
    return (result_ == Error::Ok) ? PollState::Done : PollState::Failed;
  }
  return (state_ == BusState::Idle) ? PollState::Idle : PollState::Pending;
}

template <UartPort Port>
Error Bus<Port>::Submit(const Request& request, const Timing& timing) noexcept {
  const uint32_t now = port_.NowMs();
  if (state_ == BusState::Settling) {
    settle(now);
  }
  if ((state_ != BusState::Idle) || result_ready_) {
    return Error::Busy;
  }
  const std::span<const uint8_t> bytes = request.Bytes();
  const bool legal =
      request.IsModbus() ? modbus::IsLegalModbusRequest(bytes) : ascii::IsLegalRequest(bytes);
  if (bytes.empty() || !legal) {
    if (!bytes.empty()) {
      ++wire_violations_;
    }
    return Error::InvalidArgument;
  }
  if (!detail::TimingUsable(request, timing)) {
    return Error::InvalidArgument; // no wait could be computed (or it would overflow)
  }
  request_ = request;
  timing_ = timing;
  line_len_ = 0U;
  echo_pending_ = timing.echo == EchoMode::Skip;
  // Every wait is an elapsed time since its start, an unsigned difference, so the 32-bit clock
  // may wrap (I11). A line that never goes quiet (a streaming or babbling peer) must not hold the
  // request forever: after the rest of the gap and one reply wait without the request's own
  // duration (a window, a tare) it ends unsent.
  const uint32_t gap = gapMs();
  const uint32_t since = have_activity_ ? (now - last_activity_ms_) : gap;
  const uint32_t to_quiet = (since >= gap) ? 0U : (gap - since);
  gap_start_ms_ = now;
  gap_limit_ms_ = to_quiet + lineWaitMs();
  on_wire_ = false;
  has_holdoff_ = false;
  state_ = BusState::Gap;
  return Error::Ok;
}

template <UartPort Port>
std::size_t Bus<Port>::readChunk(uint32_t now) noexcept {
  const std::size_t n = port_.Read(chunk_.data(), chunk_.size(), uint32_t{0U});
  const std::size_t got = (n < chunk_.size()) ? n : chunk_.size();
  if (got > 0U) {
    last_activity_ms_ = now;
    have_activity_ = true;
  }
  return got;
}

template <UartPort Port>
void Bus<Port>::send(uint32_t now) noexcept {
  static_cast<void>(now);
  if (!port_.FlushInput()) {
    complete(Error::Bus, false, now);
    return;
  }
  const std::span<const uint8_t> bytes = request_.Bytes();
  on_wire_ = true; // from here bytes of the request may be on the line, and a reply on its way
  holdoff_ms_ = request_.HoldoffMs();
  has_holdoff_ = holdoff_ms_ > 0U;
  const bool written = port_.Write(bytes.data(), bytes.size());
  sent_ms_ = port_.NowMs(); // the reply wait and a hold-off run from here, written or not
  if (!written) {
    // Part of the request may be on the wire: quarantine so the next one is not glued to it.
    complete(Error::Bus, true, port_.NowMs());
    log(LogLevel::Error, "alicat_basis2: Write failed");
    return;
  }
  if (timing_.switch_to_baud != 0U) {
    // The reply to a baud change arrives at the new speed (M p.17): switch before reading.
    if (!ApplyBaud(timing_.switch_to_baud).ok()) {
      complete(Error::Bus, true, port_.NowMs());
      return;
    }
    timing_.baud = timing_.switch_to_baud;
  }
  sent_ms_ = port_.NowMs();
  last_activity_ms_ = sent_ms_;
  have_activity_ = true;
  reply_wait_ms_ = replyMs();
  line_len_ = 0U;
  state_ = BusState::AwaitReply;
}

template <UartPort Port>
void Bus<Port>::startQuarantine(uint32_t now) noexcept {
  quarantine_ = true;
  state_ = BusState::Settling;
  settle_start_ms_ = now;
  if (on_wire_) {
    // A late reply of the failed command is on its way until its full reply wait has passed. A
    // hold-off of the request (a factory restore leaves the instrument deaf) is kept as well: the
    // quarantine lasts at least until the hold-off from the send is over.
    const uint32_t wait = replyMs();
    const uint32_t since_send = now - sent_ms_;
    const uint32_t hold_left =
        (has_holdoff_ && (since_send < holdoff_ms_)) ? (holdoff_ms_ - since_send) : 0U;
    quiet_min_ms_ = (hold_left > wait) ? hold_left : wait;
    settle_ms_ = (hold_left > (2U * wait)) ? hold_left : (2U * wait);
  } else {
    // Nothing of the request reached the wire: no reply can come, the line only has to go quiet.
    // The deadline is that of the request's line, without its own duration (a window, a tare).
    quiet_min_ms_ = 0U;
    settle_ms_ = 2U * lineWaitMs();
  }
}

template <UartPort Port>
void Bus<Port>::complete(Error error, bool quarantine, uint32_t now) noexcept {
  result_ = error;
  result_ready_ = true;
  if ((error == Error::Ok) || (error == Error::Rejected)) {
    reply_ = Reply{};
    for (std::size_t i = 0U; i < line_len_; ++i) {
      reply_.data[i] = line_[i];
    }
    reply_.length = static_cast<uint16_t>(line_len_);
  } else {
    reply_ = Reply{};
  }
  quarantine_ = false;
  if (quarantine) {
    startQuarantine(now);
  } else if ((error == Error::Ok) && has_holdoff_ && ((now - sent_ms_) < holdoff_ms_)) {
    state_ = BusState::Settling; // the hold-off runs from the send
    settle_start_ms_ = sent_ms_;
    settle_ms_ = holdoff_ms_;
  } else {
    state_ = BusState::Idle;
  }
}

template <UartPort Port>
void Bus<Port>::finishLine() noexcept {
  const std::span<const char> line{line_.data(), line_len_};
  const uint32_t now = port_.NowMs();
  if (echo_pending_) {
    const std::span<const uint8_t> sent = request_.Bytes();
    bool same = (sent.size() == (line_len_ + 1U));
    for (std::size_t i = 0U; same && (i < line_len_); ++i) {
      same = detail::ByteOf(line_[i]) == sent[i];
    }
    echo_pending_ = false;
    if (same) {
      line_len_ = 0U;
      return;
    }
  }
  ascii::Tokens t{};
  if (ascii::Tokenize(line, t) != Error::Ok) {
    complete(Error::BadReply, true, now);
  } else if (ascii::IsErrorToken(line, request_.Id())) {
    complete(Error::Rejected, false, now);
  } else if (ascii::StartsWithId(line, t, request_.Id()) ||
             ((!detail::SameChar(request_.AltId(), '\0')) &&
              ascii::StartsWithId(line, t, request_.AltId()))) {
    complete(Error::Ok, false, now);
  } else {
    complete(Error::DeviceIdMismatch, true, now);
  }
}

template <UartPort Port>
void Bus<Port>::feedAscii(uint8_t byte, uint32_t now) noexcept {
  const uint32_t b = byte;
  if (b == 0x0DU) {
    if (line_len_ > 0U) {
      finishLine();
    }
  } else if ((b == 0x0AU) && (line_len_ == 0U)) {
    // A terminal-style peer sends LF after CR: ignored when it arrives between lines.
  } else if ((b < 0x20U) || (b > 0x7EU)) {
    complete(Error::BadReply, true, now);
  } else if (line_len_ >= kMaxLine) {
    complete(Error::Overflow, true, now);
  } else {
    line_[line_len_] = detail::CharOf(byte);
    ++line_len_;
  }
}

template <UartPort Port>
void Bus<Port>::feedModbus(uint8_t byte) noexcept {
  if (line_len_ >= kMaxLine) {
    complete(Error::Overflow, true, port_.NowMs());
    return;
  }
  line_[line_len_] = detail::CharOf(byte);
  ++line_len_;
  const std::size_t want = modbus::ReplyLength(std::span<const char>{line_.data(), line_len_});
  if (want > kMaxLine) {
    complete(Error::BadReply, true, port_.NowMs());
  } else if ((want != 0U) && (line_len_ >= want)) {
    complete(Error::Ok, false, port_.NowMs());
  } else {
    // Frame not complete yet.
  }
}

template <UartPort Port>
void Bus<Port>::pollReply(uint32_t now) noexcept {
  for (uint32_t reads = 0U; (reads < detail::kMaxReadsPerPoll) && (state_ == BusState::AwaitReply);
       ++reads) {
    const std::size_t got = readChunk(now);
    if (got == 0U) {
      break;
    }
    for (std::size_t i = 0U; (i < got) && (state_ == BusState::AwaitReply) && !result_ready_; ++i) {
      if (request_.IsModbus()) {
        feedModbus(chunk_[i]);
      } else {
        feedAscii(chunk_[i], now);
      }
      if (result_ready_) {
        for (std::size_t j = i + 1U; j < got; ++j) {
          strayByte(chunk_[j]);
        }
      }
    }
  }
  if ((state_ == BusState::AwaitReply) && ((now - sent_ms_) >= reply_wait_ms_)) {
    complete(Error::Timeout, true, now);
    log(LogLevel::Warning, "alicat_basis2: reply timeout");
  }
}

template <UartPort Port>
void Bus<Port>::strayByte(uint8_t byte) noexcept {
  if (byte == 0x0DU) {
    if (stray_len_ > 0U) {
      ++stray_lines_;
      streaming_ = streaming_ || (detail::SameChar(stray_first_, '@'));
    }
    stray_len_ = 0U;
  } else if ((byte >= 0x20U) && (byte <= 0x7EU)) {
    if (stray_len_ == 0U) {
      stray_first_ = detail::CharOf(byte);
    }
    ++stray_len_;
  } else {
    // Control bytes and bytes above 0x7E inside a stray line carry no meaning.
  }
}

template <UartPort Port>
void Bus<Port>::drainStray(uint32_t now) noexcept {
  for (uint32_t reads = 0U; reads < detail::kMaxReadsPerPoll; ++reads) {
    const std::size_t got = readChunk(now);
    if (got == 0U) {
      break;
    }
    for (std::size_t i = 0U; i < got; ++i) {
      strayByte(chunk_[i]);
    }
  }
}

template <UartPort Port>
void Bus<Port>::settle(uint32_t now) noexcept {
  drainStray(now);
  if (quarantine_) {
    const uint32_t quiet = gapMs() + detail::kQuietExtraMs;
    const bool is_quiet = (now - last_activity_ms_) >= quiet;
    const uint32_t elapsed = now - settle_start_ms_;
    if ((is_quiet && (elapsed >= quiet_min_ms_)) || (elapsed >= settle_ms_)) {
      static_cast<void>(port_.FlushInput());
      quarantine_ = false;
      state_ = BusState::Idle;
    }
  } else if ((now - settle_start_ms_) >= settle_ms_) {
    state_ = BusState::Idle;
  } else {
    // Hold-off still running.
  }
}

template <UartPort Port>
PollState Bus<Port>::Poll() noexcept {
  const uint32_t now = port_.NowMs();
  switch (state_) {
  case BusState::Idle:
    drainStray(now);
    break;
  case BusState::Gap:
    drainStray(now); // anything still arriving moves the quiet time forward
    if (!have_activity_ || ((now - last_activity_ms_) >= gapMs())) {
      send(now);
    } else if ((now - gap_start_ms_) >= gap_limit_ms_) {
      // Nothing was written: `Busy` says so, and the quarantine drains the line before the next.
      complete(Error::Busy, true, now);
      log(LogLevel::Warning, "alicat_basis2: the line never went quiet, request not sent");
    } else {
      // Still waiting for the quiet gap.
    }
    break;
  case BusState::AwaitReply:
    pollReply(now);
    break;
  case BusState::Settling:
    settle(now);
    break;
  default:
    break;
  }
  return pollState();
}

template <UartPort Port>
Result<Reply> Bus<Port>::Take() noexcept {
  if (!result_ready_) {
    return Result<Reply>::Failure(Error::Busy);
  }
  result_ready_ = false;
  if ((result_ == Error::Ok) || (result_ == Error::Rejected)) {
    return Result<Reply>{.value = reply_, .error = result_};
  }
  return Result<Reply>::Failure(result_);
}

template <UartPort Port>
void Bus<Port>::Abort() noexcept {
  result_ready_ = false;
  if ((state_ == BusState::Gap) || (state_ == BusState::AwaitReply)) {
    startQuarantine(port_.NowMs());
  }
}

template <UartPort Port>
void Bus<Port>::EnterQuarantine() noexcept {
  startQuarantine(port_.NowMs());
}

template <UartPort Port>
uint32_t Bus<Port>::busyBoundMs() const noexcept {
  // The transaction on the bus (the last one submitted) is over by then: the gap and one line wait
  // for a quiet line (gap_limit_ms_), the time `Write()` takes to put the request on the line,
  // one reply wait, a quarantine of two reply waits (startQuarantine; an unsent request's is
  // shorter) and its hold-off. Each phase starts at the poll that sees the previous one end, so
  // each may start one poll late (a sleep that overshoots by a tick): 2 ms of slack per phase.
  const uint32_t wait = replyMs();
  const auto sent = static_cast<uint32_t>(request_.Bytes().size());
  const uint32_t write = request_.IsModbus() ? detail::ModbusFrameMs(sent, timing_.baud)
                                             : detail::LineMs(sent, timing_.baud);
  return gapMs() + lineWaitMs() + write + (3U * wait) + request_.HoldoffMs() +
         detail::kQuietExtraMs + (4U * detail::kPollLagMs);
}

template <UartPort Port>
void Bus<Port>::dropUntaken() noexcept {
  if (result_ready_) {
    result_ready_ = false;
    log(LogLevel::Warning, "alicat_basis2: a result nobody took was dropped");
  }
}

template <UartPort Port>
Result<Reply> Bus<Port>::Execute(const Request& request, const Timing& timing) noexcept {
  // A result nobody took must not hold a blocking call (a switch-off among them) back: its
  // transaction is over, so it is dropped after each poll; the wait for a busy bus is bounded in
  // time.
  const uint32_t start = port_.NowMs();
  const uint32_t bound = busyBoundMs();
  Error e = Submit(request, timing);
  for (uint32_t i = 0U;
       (e == Error::Busy) && (i < detail::kMaxPollLoops) && ((port_.NowMs() - start) <= bound);
       ++i) {
    static_cast<void>(Poll());
    dropUntaken();
    sleep(uint32_t{1U});
    e = Submit(request, timing);
  }
  if (e != Error::Ok) {
    return Result<Reply>::Failure(e); // `Busy`: never submitted, nothing sent
  }
  // The transaction itself ends by its own deadlines (gap, reply); the time bound backs them up.
  const uint32_t submitted = port_.NowMs();
  const uint32_t own = busyBoundMs();
  PollState s = PollState::Pending;
  for (uint32_t i = 0U; (s == PollState::Pending) && (i < detail::kMaxPollLoops) &&
                        ((port_.NowMs() - submitted) <= own);
       ++i) {
    s = Poll();
    if (s == PollState::Pending) {
      sleep(uint32_t{1U});
    }
  }
  if (s == PollState::Pending) {
    Abort();
    return Result<Reply>::Failure(Error::Timeout);
  }
  return Take();
}

} // namespace alicat_basis2

#endif // ALICAT_BASIS2_BUS_IPP
