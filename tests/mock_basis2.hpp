/**
 * @file mock_basis2.hpp
 * @brief An independent model of an Alicat BASIS 2 instrument and of the serial wire it sits on.
 *
 * The model shares no encoder, tokenizer or table with the driver: the ASCII grammar and the Modbus
 * register file are written a second time here, from the manual, with plain strings and integers.
 * The wire has a virtual clock in microseconds, ten bits per character at the instrument's baud
 * rate, the instrument's wait of 3.5 character times of silence before it acts, and fault injection
 * (drops, delays, splits, garbage, stray lines, bad CRC).
 *
 * Assumptions of the manual are knobs (`Knobs`); each is named K-... in the design and in
 * docs/datasheets.yml.
 */
#ifndef ALICAT_BASIS2_TEST_MOCK_BASIS2_HPP
#define ALICAT_BASIS2_TEST_MOCK_BASIS2_HPP

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "alicat_basis2.hpp"

namespace model {

// Characters are compared as bytes and letters looked up in tables; no character is used as a
// number (MISRA C++:2023 Rule 7.0.3). The model has its own helpers, not the driver's.
inline constexpr std::string_view kUpper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
inline constexpr std::string_view kLower = "abcdefghijklmnopqrstuvwxyz";
inline constexpr std::string_view kDigits = "0123456789";
/// The byte of a character.
[[nodiscard]] constexpr uint8_t ByteOf(char c) noexcept {
  return std::bit_cast<uint8_t>(c);
}
/// The character of a byte.
[[nodiscard]] constexpr char CharOf(uint8_t b) noexcept {
  return std::bit_cast<char>(b);
}
/// Whether two characters are the same.
[[nodiscard]] constexpr bool SameChar(char a, char b) noexcept {
  return ByteOf(a) == ByteOf(b);
}
/// Whether `c` is one of the characters of `set`.
[[nodiscard]] constexpr bool In(std::string_view set, char c) noexcept {
  return set.find(c) != std::string_view::npos;
}

/// How a setter answers (K-SETREPLY).
enum class SetReply : uint8_t { Frame, ReadForm, Silent, Text };
/// How `VE` prints the firmware (K-VE).
enum class FwFormat : uint8_t { Dotted, V3v0v5, VDotted, Garbage };

/// Assumptions of the manual, each a knob (design section 6).
struct Knobs {
  bool aligned_columns{
      true}; ///< K-COLS: the frame is column aligned (the manual's example) or single spaced.
  bool bracket_flags{true};             ///< K-BRACKET: `[HLD]` or `HLD`.
  bool error_with_id{false};            ///< K-ERR: the error reply is `A ?` instead of `?`.
  SetReply set_reply{SetReply::Frame};  ///< K-SETREPLY.
  FwFormat fw_format{FwFormat::Dotted}; ///< K-VE.
  bool status_as_integer{
      false};                 ///< K-DVSTATUS: `DV` prints the status as an integer, not as flags.
  bool list_format_ok{true};  ///< K-GSLIST: `GS *` replies `A n name n name`; false: free text.
  bool undefined_drops{true}; ///< K-UNDEF: a command sent while the reply is in flight is dropped.
  uint32_t latency_us{4000};  ///< Time from the end of the quiet gap to the first reply character.
  uint32_t restore_us{2500000}; ///< K-RESTORE: how long FACTORY RESTORE keeps the instrument deaf.
  bool echo_on_wire{false};     ///< K-ECHO: the wiring hears its own transmission (half duplex).
  uint8_t factory_modbus_address{
      1};                  ///< K-FACTORYADDR: the address after FACTORY RESTORE (inferred, A24).
  bool reject_baud{false}; ///< K-BAUDREJECT: `NCB` and register 21 are refused, the speed stays.
  bool ignore_unit_change{false}; ///< K-IDIGNORE: `@=` and register 46 answer but change nothing.
  bool prev_avg_low_first{false}; ///< K-PREVAVG: 4214 holds the low word of the average (A25).
  int32_t baud_report{-1};        ///< Test knob: when not negative, register 21 reads this code.
  int32_t address_report{-1};     ///< Test knob: when not negative, `MA` and register 45 read this.
  int32_t refuse_register{-1};    ///< Test knob: a read covering this register is an exception.
  int32_t silent_register{-1};    ///< Test knob: a read covering this register is not answered.
};

/// Faults applied to the next `count` replies.
struct Faults {
  uint32_t count{0};         ///< Replies still to corrupt.
  bool drop{false};          ///< No reply at all.
  uint32_t delay_us{0};      ///< Extra time before the reply.
  uint8_t chunks{1};         ///< Deliver in this many pieces, one character time apart.
  bool no_cr{false};         ///< Leave out the terminating CR.
  uint32_t extend{0};        ///< Append this many printable bytes before the CR.
  std::string prefix;        ///< Bytes before the reply.
  std::string suffix;        ///< Bytes after the reply.
  bool duplicate{false};     ///< The reply twice.
  char wrong_id{'\0'};       ///< Replace the first character (the unit ID) by this.
  std::string replace;       ///< Replace the whole reply text (ASCII) or frame bytes (Modbus).
  bool lf_after_cr{false};   ///< CR LF terminator.
  bool bad_crc{false};       ///< Modbus: flip a bit of the CRC.
  bool error_reply{false};   ///< Answer `?`.
  int garble_at{-1};         ///< Replace the byte at this index by 0x80.
  bool wrong_address{false}; ///< Modbus: another slave address in the reply.
};

/// One recorded write of the driver.
struct WriteRecord {
  uint64_t at_us{0};
  std::string text;
};

/// The instrument and the wire.
class Instrument {
public:
  // --- identity and physical state (set by tests) ---
  char unit_id{'A'};
  bool controller{true};
  std::string serial{"SN123456"};
  uint16_t firmware{0x305}; ///< 256a + 16b + c, V3.0.5.
  std::string unit_label{"SCCM"};
  uint8_t unit_code{0};
  uint32_t full_scale_sccm{500};
  int64_t full_scale_milli{500000};
  int gas{8}; ///< 0 Air, 1 Ar, 2 CO2, 3 N2, 4 O2, 5 N2O, 6 H2, 7 He, 8 CH4 (M p.14 order, assumed).
  int64_t flow_milli{0};
  int64_t setpoint_milli{0};
  int64_t clamp_setpoint_milli{
      -1}; ///< When not negative, a setpoint above this is stored as this (read-back differs).
  int64_t temperature_c100{2457};
  int64_t total_milli{0};
  int64_t valve_milli{0}; ///< percent x 1000
  int64_t batch_milli{0};
  int64_t batch_remaining_milli{0};
  bool hld{false};
  bool vtm{false};
  bool mov{false};
  bool tov{false};
  bool ovr{false};
  char source{'U'}; ///< A, S or U.
  uint32_t ramp_rate_milli{0};
  int ramp_unit{4};
  uint32_t watchdog_ms{0};
  uint32_t gain_p{100};
  uint32_t gain_i{200};
  uint32_t ref_temp_c100{2100};
  uint32_t averaging_ms{250};
  uint32_t trigger{0};
  bool autotare{false};
  int totalizer_limit{0};
  uint32_t modbus_address{1};
  uint32_t baud{38400};
  int protocol{2};
  uint32_t mb_decimals{1};
  uint32_t nvm_writes{0};
  uint32_t measure_ms{0};
  int64_t avg_flow_milli{0};
  int64_t min_flow_milli{0};
  int64_t max_flow_milli{0};
  uint32_t samples_cur{0};
  uint32_t samples_prev{0};
  int64_t prev_min_milli{0};
  int64_t prev_max_milli{0};
  int64_t prev_avg_milli{0};
  uint32_t tare_samples{400};
  uint32_t flow_offset{8192};
  uint32_t tares{0};
  uint32_t total_resets{0};
  uint32_t restores{0};
  bool modbus_enabled{true};
  bool ascii_enabled{true};

  Knobs knobs{};
  Faults faults{};

  // --- wire state ---
  std::vector<WriteRecord> writes; ///< Every Write() of the driver, in order.
  uint32_t undefined_events{0};    ///< Commands sent while a reply was still on the wire.
  uint32_t commands_handled{0};
  uint32_t port_baud{38400};   ///< What the port runs at (SetBaud of the driver).
  uint32_t baud_sets{0};       ///< How often the driver switched the port's speed.
  std::size_t rx_buffer{1024}; ///< The port's receive buffer (unread bytes beyond it are lost).
  uint64_t now_us{0};
  uint64_t wrap_ms{0};   ///< Added to NowMs() to test wrap-around.
  uint32_t spin_us{200}; ///< Time one NowMs() call costs when the port has no DelayMs.
  bool auto_spin{true};
  bool fail_write{false};
  bool fail_flush{false};
  bool fail_init{false};
  uint32_t early_read_limit{0}; ///< When non-zero, Read() returns at most this many bytes.
  std::vector<std::string> log_lines;
  std::string last_command;

  // ------------------------------------------------------------------------------------------------------------
  // Time and wire
  // ------------------------------------------------------------------------------------------------------------

  [[nodiscard]] static uint64_t CharUs(uint32_t b) {
    return 10'000'000ULL / b;
  }
  [[nodiscard]] static uint64_t T35Us(uint32_t b) {
    return (CharUs(b) * 35ULL) / 10ULL;
  }

  [[nodiscard]] uint32_t NowMsValue() {
    pump();
    return static_cast<uint32_t>((now_us / 1000ULL) + wrap_ms);
  }
  void Advance(uint64_t us) {
    now_us += us;
  }
  void AdvanceMs(uint32_t ms) {
    now_us += static_cast<uint64_t>(ms) * 1000ULL;
  }

  /// The driver writes bytes: they reach the instrument one character time apart.
  bool WriteBytes(const uint8_t* data, std::size_t n) {
    if (fail_write) {
      return false;
    }
    WriteRecord rec;
    rec.at_us = now_us;
    rec.text.assign(reinterpret_cast<const char*>(data), n);
    writes.push_back(rec);
    last_command = rec.text;
    const uint64_t cu = CharUs(port_baud);
    if (knobs.echo_on_wire) {
      for (std::size_t i = 0; i < n; ++i) {
        queue(now_us + (cu * (i + 1U)), static_cast<char>(data[i]));
      }
    }
    if ((port_baud != baud) || !portAlive()) {
      return true; // garbled by a speed mismatch: the instrument sees nothing it understands
    }
    if (now_us < reply_end_us_) {
      ++undefined_events;
      if (knobs.undefined_drops) {
        return true;
      }
    }
    if (now_us < deaf_until_us_) {
      return true;
    }
    for (std::size_t i = 0; i < n; ++i) {
      rx_.push_back(static_cast<char>(data[i]));
    }
    last_rx_us_ = now_us + (cu * n);
    return true;
  }

  /// A peer that talks back to back for `duration_us` from now (a babbling or streaming device).
  /// The bytes are made as the clock reaches them, so seconds of it cost no memory.
  void Babble(uint64_t duration_us, uint64_t after_us = 0U) {
    babble_next_us_ = std::max(babble_next_us_, now_us + after_us);
    babble_until_us_ = std::max(babble_until_us_, now_us + after_us + duration_us);
    last_byte_us_ = std::max(last_byte_us_, babble_until_us_);
  }

  /// The driver reads: whatever arrived by now (never more than `cap`).
  std::size_t ReadBytes(uint8_t* out, std::size_t cap) {
    feedBabble();
    pump();
    overflow();
    std::size_t n = 0;
    const std::size_t limit =
        ((early_read_limit != 0U) && (early_read_limit < cap)) ? early_read_limit : cap;
    while ((n < limit) && !out_.empty() && (out_.front().at_us <= now_us)) {
      out[n++] = static_cast<uint8_t>(out_.front().byte);
      out_.pop_front();
    }
    return n;
  }

  void Flush() {
    feedBabble();
    pump();
    while (!out_.empty() && (out_.front().at_us <= now_us)) {
      out_.pop_front();
    }
  }

  /// Put a stray line on the wire at `at_us` after now (another instrument, a streaming peer).
  void InjectStray(const std::string& text, uint32_t after_us = 0) {
    const uint64_t cu = CharUs(baud);
    for (std::size_t i = 0; i < text.size(); ++i) {
      queue(now_us + after_us + (cu * i), text[i]);
    }
  }

  /// When the last byte the instrument or a peer put on the line arrives, in microseconds.
  [[nodiscard]] uint64_t LastByteUs() const {
    return last_byte_us_;
  }

  /// When the last reply byte leaves the wire, in microseconds.
  [[nodiscard]] uint64_t ReplyEndUs() const {
    return reply_end_us_;
  }

  /// Everything the instrument has sent that the driver has not read yet.
  [[nodiscard]] std::size_t Pending() const {
    return out_.size();
  }

  /// Power-cycle the instrument: unsaved settings are lost, saved ones kept.
  void PowerCycle() {
    setpoint_milli = (SameChar(source, 'S')) ? setpoint_milli : 0;
    if (!SameChar(source, 'S')) {
      source = 'A';
    }
    rx_.clear();
    out_.clear();
    reply_end_us_ = 0;
  }

private:
  struct Out {
    uint64_t at_us;
    char byte;
  };
  std::string rx_;
  uint64_t last_rx_us_{0};
  std::deque<Out> out_;
  uint64_t reply_end_us_{0};
  uint64_t last_byte_us_{0};    ///< When the last byte put on the line (reply or stray) arrives.
  uint64_t babble_next_us_{0};  ///< When the next babbled byte arrives.
  uint64_t babble_until_us_{0}; ///< When the babbling peer stops.
  uint32_t babble_count_{0};    ///< Babbled bytes so far.
  uint64_t deaf_until_us_{0};

  [[nodiscard]] bool portAlive() const {
    return true;
  }

  void queue(uint64_t at, char b) {
    last_byte_us_ = std::max(last_byte_us_, at);
    // Keep the queue ordered by arrival time.
    std::size_t pos = out_.size();
    while ((pos > 0) && (out_[pos - 1].at_us > at)) {
      --pos;
    }
    out_.insert(out_.begin() + static_cast<std::ptrdiff_t>(pos), Out{at, b});
  }

  /// The port's receive buffer holds `rx_buffer` bytes: older unread bytes are lost on overflow,
  /// as on a UART whose ring buffer filled while nobody read (after days without a call).
  void overflow() {
    std::size_t arrived = 0U;
    while ((arrived < out_.size()) && (out_[arrived].at_us <= now_us)) {
      ++arrived;
    }
    while (arrived > rx_buffer) {
      out_.pop_front();
      --arrived;
    }
  }

  void feedBabble() {
    const uint64_t cu = CharUs(baud);
    while ((babble_next_us_ <= now_us) && (babble_next_us_ < babble_until_us_)) {
      queue(babble_next_us_, ((babble_count_ % 20U) == 19U) ? '\r' : 'x');
      ++babble_count_;
      babble_next_us_ += cu;
    }
  }

  void pump() {
    if (rx_.empty()) {
      return;
    }
    const uint64_t quiet_at = last_rx_us_ + T35Us(baud);
    if (now_us < quiet_at) {
      return;
    }
    if (isModbusFrame()) {
      if (modbus_enabled) {
        handleModbus(quiet_at);
      }
      rx_.clear();
      return;
    }
    const std::size_t cr = rx_.find('\r');
    if (cr == std::string::npos) {
      return; // an unfinished line waits for its CR
    }
    const std::string line = rx_.substr(0, cr);
    rx_.erase(0, cr + 1U);
    if (ascii_enabled && ((protocol == 2) || (upper(line).substr(1) == "P2"))) {
      handleAscii(line, quiet_at);
    }
  }

  /// A frame that starts with the Modbus address byte and contains a non-printable byte is binary.
  [[nodiscard]] bool isModbusFrame() const {
    return std::ranges::any_of(rx_, [](char c) {
      const auto u = static_cast<uint8_t>(c);
      return (u < 0x20U) && (u != 0x0DU);
    });
  }

  // ------------------------------------------------------------------------------------------------------------
  // Reply output with faults
  // ------------------------------------------------------------------------------------------------------------

  void emit(std::string reply, uint64_t start_us, bool modbus) {
    ++commands_handled;
    if (port_baud != baud) {
      return; // the reply is at one speed and the port listens at another: nothing intelligible
              // arrives
    }
    if (faults.count > 0U) {
      --faults.count;
      if (faults.drop) {
        return;
      }
      if (faults.error_reply && !modbus) {
        reply = "?";
      }
      if ((!SameChar(faults.wrong_id, '\0')) && !reply.empty() && !modbus) {
        reply[0] = faults.wrong_id;
      }
      if (faults.wrong_address && modbus && (reply.size() > 2U)) {
        reply[0] = static_cast<char>(reply[0] + 1);
        const uint16_t c = Crc(reinterpret_cast<const uint8_t*>(reply.data()), reply.size() - 2U);
        reply[reply.size() - 2U] = static_cast<char>(c & 0xFFU);
        reply[reply.size() - 1U] = static_cast<char>(c >> 8U);
      }
      if (!faults.replace.empty()) {
        reply = faults.replace;
      }
      if ((faults.garble_at >= 0) && (static_cast<std::size_t>(faults.garble_at) < reply.size())) {
        reply[static_cast<std::size_t>(faults.garble_at)] = CharOf(0x80U);
      }
      if (faults.extend > 0U) {
        reply.append(faults.extend, 'X');
      }
      if (faults.bad_crc && modbus && !reply.empty()) {
        reply.back() =
            static_cast<char>(static_cast<uint32_t>(static_cast<uint8_t>(reply.back())) ^ 0x01U);
      }
      if (!modbus && !faults.no_cr) {
        reply += faults.lf_after_cr ? "\r\n" : "\r";
      }
      if (faults.duplicate) {
        reply += reply;
      }
      reply = faults.prefix + reply + faults.suffix;
      start_us += faults.delay_us;
    } else if (!modbus) {
      reply += "\r";
    } else {
      // A Modbus frame carries no terminator.
    }
    const uint64_t cu = CharUs(baud);
    const std::size_t pieces = (faults.chunks > 1U) ? faults.chunks : 1U;
    const std::size_t per = (reply.size() + pieces - 1U) / pieces;
    uint64_t t = start_us;
    for (std::size_t i = 0; i < reply.size(); ++i) {
      if ((per > 0U) && (i > 0U) && ((i % per) == 0U)) {
        t += cu * 2U; // a visible pause between the pieces
      }
      queue(t, reply[i]);
      t += cu;
    }
    reply_end_us_ = t;
  }

  // ------------------------------------------------------------------------------------------------------------
  // ASCII: a second, hand-written grammar
  // ------------------------------------------------------------------------------------------------------------

  static std::string upper(std::string s) {
    for (char& c : s) {
      if (In(kLower, c)) {
        c = kUpper[kLower.find(c)];
      }
    }
    return s;
  }
  static std::string fixed(int64_t milli, int decimals) {
    char buf[40];
    static_cast<void>(
        std::snprintf(buf, sizeof buf, "%+.*f", decimals, static_cast<double>(milli) / 1000.0));
    return buf;
  }
  static std::string num(int64_t v) {
    return std::to_string(v);
  }
  static bool parseInt(const std::string& s, int64_t& v) {
    if (s.empty() || (s.size() > 12U)) {
      return false;
    }
    std::size_t i = ((SameChar(s[0], '-')) || (SameChar(s[0], '+'))) ? 1U : 0U;
    if (i == s.size()) {
      return false;
    }
    for (std::size_t k = i; k < s.size(); ++k) {
      if (!In(kDigits, s[k])) {
        return false;
      }
    }
    v = std::strtoll(s.c_str(), nullptr, 10);
    return true;
  }
  static bool parseMilli(const std::string& s, int64_t& v) {
    if (s.empty() || (s.size() > 20U)) {
      return false;
    }
    std::size_t i = ((SameChar(s[0], '-')) || (SameChar(s[0], '+'))) ? 1U : 0U;
    bool dot = false;
    bool digit = false;
    for (std::size_t k = i; k < s.size(); ++k) {
      if ((SameChar(s[k], '.')) && !dot) {
        dot = true;
      } else if (In(kDigits, s[k])) {
        digit = true;
      } else {
        return false;
      }
    }
    if (!digit) {
      return false;
    }
    v = static_cast<int64_t>(std::llround(std::strtod(s.c_str(), nullptr) * 1000.0));
    return true;
  }
  static std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> t;
    std::string cur;
    for (const char c : s) {
      if (SameChar(c, ' ')) {
        if (!cur.empty()) {
          t.push_back(cur);
          cur.clear();
        }
      } else {
        cur.push_back(c);
      }
    }
    if (!cur.empty()) {
      t.push_back(cur);
    }
    return t;
  }

  [[nodiscard]] std::string gasText() const {
    static const char* const kNames[9] = {"Air", "Ar", "CO2", "N2", "O2", "N2O", "H2", "He", "CH4"};
    return ((gas >= 0) && (gas <= 8)) ? kNames[gas] : "GAS" + std::to_string(gas);
  }
  /// The setpoint source register value of a source letter (M p.21): A 0, S 1, U 2.
  [[nodiscard]] static int sourceCode(char letter) {
    if (SameChar(letter, 'A')) {
      return 0;
    }
    return (SameChar(letter, 'S')) ? 1 : 2;
  }
  /// The source letter of a source register value.
  [[nodiscard]] static char sourceLetter(int code) {
    if (code == 0) {
      return 'A';
    }
    return (code == 1) ? 'S' : 'U';
  }
  /// Milliseconds of one ramp time unit (3 ms, 4 s, otherwise minutes).
  [[nodiscard]] static double rampUnitMs(int unit) {
    if (unit == 3) {
      return 1.0;
    }
    return (unit == 4) ? 1000.0 : 60000.0;
  }
  /// The flag text of a data frame: empty, bracketed or bare, with its leading space if bracketed.
  [[nodiscard]] std::string flagsText() const {
    const std::string f = flags();
    if (f.empty()) {
      return {};
    }
    return knobs.bracket_flags ? (" " + f) : f;
  }
  [[nodiscard]] std::string flags() const {
    std::string f;
    const std::pair<bool, const char*> list[5] = {
        {tov, "TOV"}, {mov, "MOV"}, {ovr, "OVR"}, {hld, "HLD"}, {vtm, "VTM"}};
    for (const auto& e : list) {
      if (e.first) {
        f += knobs.bracket_flags ? (std::string("[") + e.second + "]")
                                 : (std::string(" ") + e.second);
      }
    }
    return f;
  }
  [[nodiscard]] unsigned statusBits() const {
    return (mov ? 1U : 0U) + (tov ? 2U : 0U) + (ovr ? 4U : 0U) + (hld ? 8U : 0U) + (vtm ? 16U : 0U);
  }
  [[nodiscard]] std::string frameText() const {
    std::string sep = knobs.aligned_columns ? "  " : " ";
    std::string s(1, unit_id);
    s += sep + fixed(temperature_c100 * 10, 2) + " " + fixed(flow_milli, 1) + " " +
         fixed(total_milli, 1);
    if (controller) {
      s += " " + fixed(setpoint_milli, 1) + sep + fixed(valve_milli, 2);
    }
    s += sep + gasText();
    std::string f = flags();
    if (!f.empty()) {
      s += (knobs.bracket_flags ? " " : "") + f;
    }
    return s;
  }
  [[nodiscard]] std::string id() const {
    std::string id(1, unit_id);
    return id;
  }
  [[nodiscard]] std::string err() const {
    return knobs.error_with_id ? id() + " ?" : "?";
  }

  [[nodiscard]] std::string setterReply(const std::string& read_form,
                                        const std::string& text) const {
    switch (knobs.set_reply) {
    case SetReply::Frame:
      return frameText();
    case SetReply::ReadForm:
      return read_form;
    case SetReply::Text:
      return id() + " " + text;
    case SetReply::Silent:
      break;
    }
    return {};
  }

  void sendReply(const std::string& text, uint64_t start_us, uint64_t extra_us = 0) {
    if (text.empty()) {
      return;
    }
    emit(text, start_us + extra_us, false);
  }

  void handleAscii(const std::string& raw, uint64_t quiet_at) {
    std::string line = upper(raw);
    if (line.empty()) {
      return;
    }
    const bool broadcast = SameChar(line[0], '*');
    if (!SameChar(line[0], unit_id) && !broadcast) {
      return; // not for this instrument
    }
    const std::string body = line.substr(1);
    const uint64_t start = quiet_at + knobs.latency_us;
    std::string reply;
    uint64_t extra = 0;
    const bool handled = dispatch(body, reply, extra);
    if (!handled) {
      sendReply(err(), start);
      return;
    }
    sendReply(reply, start, extra);
  }

  [[nodiscard]] bool needsController() const {
    return controller;
  }

  bool dispatch(const std::string& body, std::string& reply, uint64_t& extra) {
    const std::vector<std::string> tok = split(body);
    const std::string cmd = tok.empty() ? "" : tok[0];
    const std::size_t n = tok.size();
    int64_t v = 0;
    if (body.empty()) {
      reply = frameText();
      return true;
    }
    if (body == "FACTORY RESTORE") {
      restore();
      reply = id() + " ok";
      deaf_until_us_ = now_us + knobs.restore_us;
      return true;
    }
    if (body.starts_with("@=") && (body.size() == 3U) && In(kUpper, body[2])) {
      unit_id = knobs.ignore_unit_change ? unit_id : body[2];
      reply = frameText();
      return true;
    }
    if (body == "P2") {
      protocol = 2;
      reply = id() + " 2";
      return true;
    }
    if ((cmd == "P") && (n == 1)) {
      reply = id() + " " + num(protocol);
      return true;
    }
    if ((cmd == "P") && (n == 2) && (tok[1] == "1")) {
      const bool ok = (unit_code == 0) || (unit_code == 2) || (unit_code == 1) || (unit_code == 3);
      if (!ok) {
        return false;
      }
      reply = id() + " 1";
      protocol = 1;
      return true;
    }
    if ((cmd == "DV") && (n == 2) && parseInt(tok[1], v) && (v >= 1) && (v <= 255)) {
      return dataValues(static_cast<unsigned>(v), reply);
    }
    if ((cmd == "DVAS") && (n == 2) && parseInt(tok[1], v) && (v >= 1)) {
      measure_ms = static_cast<uint32_t>(v);
      samples_prev = samples_cur;
      prev_min_milli = min_flow_milli;
      prev_max_milli = max_flow_milli;
      prev_avg_milli = avg_flow_milli;
      min_flow_milli = max_flow_milli = avg_flow_milli = flow_milli;
      samples_cur = static_cast<uint32_t>(v * 10 / 25);
      reply = frameText();
      extra = static_cast<uint64_t>(v) * 1000U;
      extra = 0;
      return true;
    }
    if ((cmd == "DVAA") && (n == 1)) {
      reply = id() + " " + num(measure_ms) + " " + fixed(temperature_c100 * 10, 2) + " " +
              fixed(avg_flow_milli, 1);
      return true;
    }
    if ((cmd == "DVAR") && (n == 1)) {
      reply = id() + " " + num(measure_ms) + " " + fixed(temperature_c100 * 10, 2) + " " +
              fixed(temperature_c100 * 10, 2) + " " + fixed(min_flow_milli, 1) + " " +
              fixed(max_flow_milli, 1);
      return true;
    }
    return dispatchMore(body, tok, reply, extra);
  }

  bool dataValues(unsigned mask, std::string& reply) {
    if (!controller && ((mask & (2U | 8U | 64U)) != 0U)) {
      return false;
    }
    std::string s = id();
    if ((mask & 1U) != 0U) {
      s += " " + fixed(flow_milli, 1);
    }
    if ((mask & 2U) != 0U) {
      s += " " + fixed(setpoint_milli, 1);
    }
    if ((mask & 4U) != 0U) {
      s += " " + fixed(temperature_c100 * 10, 2);
    }
    if ((mask & 8U) != 0U) {
      s += " " + fixed(valve_milli, 2);
    }
    if ((mask & 16U) != 0U) {
      s += " " + gasText();
    }
    if ((mask & 32U) != 0U) {
      s += " " + fixed(total_milli, 1);
    }
    if ((mask & 64U) != 0U) {
      s += " " + fixed(batch_remaining_milli, 1);
    }
    if ((mask & 128U) != 0U) {
      if (knobs.status_as_integer) {
        s += " " + num(statusBits());
      } else {
        s += flagsText();
      }
    }
    reply = s;
    return true;
  }

  bool dispatchMore(const std::string& body, const std::vector<std::string>& tok,
                    std::string& reply, uint64_t& extra) {
    const std::string& cmd = tok[0];
    const std::size_t n = tok.size();
    int64_t v = 0;
    int64_t w = 0;
    const std::string rd = id() + " ";
    auto ctrl = [this]() { return controller; };
    if ((cmd == "S") && (n == 2) && ctrl() && parseMilli(tok[1], v)) {
      if ((v < 0) || (v > ((full_scale_milli * 1025) / 1000))) {
        return false;
      }
      if (SameChar(source, 'A')) {
        return false;
      }
      if (SameChar(source, 'S')) {
        ++nvm_writes;
      }
      setpoint_milli =
          ((clamp_setpoint_milli >= 0) && (v > clamp_setpoint_milli)) ? clamp_setpoint_milli : v;
      reply = setterReply(frameText(), fixed(v, 1));
      return true;
    }
    if ((cmd == "LSS") && ctrl()) {
      if (n == 1) {
        reply = rd + std::string(1, sourceLetter(sourceCode(source)));
        return true;
      }
      if ((n == 2) && ((tok[1] == "A") || (tok[1] == "S") || (tok[1] == "U"))) {
        source = tok[1][0];
        ++nvm_writes;
        reply = setterReply(rd + tok[1], tok[1]);
        return true;
      }
      return false;
    }
    if ((cmd == "SR") && ctrl()) {
      if (n == 1) {
        reply = rd + fixed(ramp_rate_milli, 3) + " " + num(ramp_unit);
        return true;
      }
      if ((n == 2) && (tok[1] == "0")) {
        ramp_rate_milli = 0;
        reply = setterReply(rd + "0", "0");
        return true;
      }
      if ((n == 3) && parseMilli(tok[1], v) && (v >= 1) && parseInt(tok[2], w) && (w >= 3) &&
          (w <= 5)) {
        ramp_rate_milli = static_cast<uint32_t>(v);
        ramp_unit = static_cast<int>(w);
        ++nvm_writes;
        reply = setterReply(rd + tok[1] + " " + tok[2], tok[1]);
        return true;
      }
      return false;
    }
    if ((cmd == "WD") && ctrl()) {
      if (n == 1) {
        reply = rd + num(watchdog_ms);
        return true;
      }
      if ((n == 2) && parseInt(tok[1], v) && (v >= 0) && (v <= 5000)) {
        watchdog_ms = static_cast<uint32_t>(v);
        reply = setterReply(rd + tok[1], tok[1]);
        return true;
      }
      return false;
    }
    if ((cmd == "HPUR") && (n == 2) && ctrl() && parseMilli(tok[1], v) && (v >= 0) &&
        (v <= 100000)) {
      hld = true;
      valve_milli = v;
      reply = frameText();
      return true;
    }
    if ((cmd == "C") && (n == 1) && ctrl()) {
      hld = false;
      reply = frameText();
      return true;
    }
    if ((cmd == "LCG") && ctrl()) {
      if (n == 1) {
        reply = rd + num(gain_p) + " " + num(gain_i);
        return true;
      }
      if ((n == 3) && parseInt(tok[1], v) && parseInt(tok[2], w) && (v >= 0) && (v <= 65535) &&
          (w >= 0) && (w <= 65535)) {
        gain_p = static_cast<uint32_t>(v);
        gain_i = static_cast<uint32_t>(w);
        reply = setterReply(rd + tok[1] + " " + tok[2], tok[1]);
        return true;
      }
      return false;
    }
    if (cmd == "RT") {
      if (n == 1) {
        reply = rd + fixed(static_cast<int64_t>(ref_temp_c100) * 10, 2);
        return true;
      }
      if ((n == 2) && parseMilli(tok[1], v) && (v >= 0) && (v <= 30000)) {
        ref_temp_c100 = static_cast<uint32_t>((v + 5) / 10);
        reply = setterReply(rd + tok[1], tok[1]);
        return true;
      }
      return false;
    }
    if (cmd == "DCA") {
      if (n == 1) {
        reply = rd + num(averaging_ms);
        return true;
      }
      if ((n == 2) && parseInt(tok[1], v) && (v >= 0) && (v <= 2500)) {
        averaging_ms = static_cast<uint32_t>(v);
        reply = setterReply(rd + tok[1], tok[1]);
        return true;
      }
      return false;
    }
    if ((cmd == "MT") && ctrl()) {
      if (n == 1) {
        reply = rd + num(trigger);
        return true;
      }
      if ((n == 2) && parseInt(tok[1], v) && (v >= 0) && (v <= 7)) {
        trigger = static_cast<uint32_t>(v);
        reply = setterReply(rd + tok[1], tok[1]);
        return true;
      }
      return false;
    }
    if ((cmd == "V") && (n == 2) && parseInt(tok[1], v) && (v >= 1) && (v <= 32767)) {
      ++tares;
      flow_milli = 0;
      reply = frameText();
      extra = static_cast<uint64_t>(v) * 1000U;
      return true;
    }
    return dispatchRest(body, tok, reply, extra);
  }

  bool dispatchRest(const std::string& /*cmd*/, const std::vector<std::string>& tok,
                    std::string& reply, uint64_t& /*extra_us*/) {
    const std::string& cmd = tok[0];
    const std::size_t n = tok.size();
    int64_t v = 0;
    const std::string rd = id() + " ";
    if ((cmd == "ZCA") && controller) {
      if (n == 1) {
        reply = rd + (autotare ? "1" : "0");
        return true;
      }
      if ((n == 2) && ((tok[1] == "0") || (tok[1] == "1"))) {
        autotare = tok[1] == "1";
        reply = setterReply(rd + tok[1], tok[1]);
        return true;
      }
      return false;
    }
    if (cmd == "GS") {
      if (n == 1) {
        reply = rd + num(gas) + " " + gasText();
        return true;
      }
      if ((n == 2) && (tok[1] == "*")) {
        if (firmware < 0x305) {
          return false;
        }
        reply = knobs.list_format_ok ? rd + "0 Air 1 Ar 2 CO2 3 N2 4 O2 5 N2O 6 H2 7 He 8 CH4"
                                     : rd + "gas list: see manual";
        return true;
      }
      if ((n == 2) && parseInt(tok[1], v) && (v >= 0) && (v <= 255)) {
        if (v > 8) {
          return false;
        }
        gas = static_cast<int>(v);
        reply = rd + num(gas) + " " + gasText();
        return true;
      }
      return false;
    }
    if ((cmd == "T") && (n == 1)) {
      total_milli = 0;
      ovr = false;
      ++total_resets;
      reply = frameText();
      return true;
    }
    if ((cmd == "TB") && controller && (n == 2)) {
      int64_t m = 0;
      if (tok[1] == "0") {
        batch_milli = 0;
        batch_remaining_milli = 0;
      } else if (parseMilli(tok[1], m) && (m >= 1)) {
        batch_milli = m;
        batch_remaining_milli = m;
      } else {
        return false;
      }
      reply = setterReply(rd + tok[1], tok[1]);
      return true;
    }
    if (cmd == "TC") {
      if (n == 1) {
        reply = rd + num(totalizer_limit);
        return true;
      }
      if ((n == 2) && parseInt(tok[1], v) && (v >= 0) && (v <= 3)) {
        totalizer_limit = static_cast<int>(v);
        reply = setterReply(rd + tok[1], tok[1]);
        return true;
      }
      return false;
    }
    if ((cmd == "FPF") && (n == 2) && parseInt(tok[1], v) && (v >= 0) && (v <= 2)) {
      if (v == 0) {
        reply = rd + fixed(full_scale_milli, 3) + " " + unit_label;
      } else if (v == 1) {
        reply = rd + fixed(9999000, 3) + " SL";
      } else {
        reply = rd + fixed(60000, 3) + " C";
      }
      return true;
    }
    if ((cmd == "SN") && (n == 1)) {
      reply = rd + serial;
      return true;
    }
    if ((cmd == "VE") && (n == 1)) {
      reply = rd + firmwareText();
      return true;
    }
    if (cmd == "MA") {
      if (n == 1) {
        reply = rd + num((knobs.address_report >= 0) ? static_cast<int64_t>(knobs.address_report)
                                                     : static_cast<int64_t>(modbus_address));
        return true;
      }
      if ((n == 2) && parseInt(tok[1], v) && (v >= 1) && (v <= 247)) {
        modbus_address = static_cast<uint32_t>(v);
        reply = rd + tok[1];
        ++nvm_writes;
        return true;
      }
      return false;
    }
    if (cmd == "NCB") {
      if (n == 1) {
        reply = rd + num(baud);
        return true;
      }
      if ((n == 2) && parseInt(tok[1], v) && isBaud(v)) {
        if (((v == 57600) || (v == 115200)) && (firmware < 0x222)) {
          return false;
        }
        if (knobs.reject_baud) {
          return false;
        }
        reply = rd + tok[1];
        baud = static_cast<uint32_t>(v); // the reply goes out at the new speed
        ++nvm_writes;
        return true;
      }
      return false;
    }
    return false;
  }

  static bool isBaud(int64_t v) {
    return (v == 4800) || (v == 9600) || (v == 19200) || (v == 38400) || (v == 57600) ||
           (v == 115200);
  }

  [[nodiscard]] std::string firmwareText() const {
    const unsigned a = (static_cast<uint32_t>(firmware) >> 8U) & 0xFU;
    const unsigned b = (static_cast<uint32_t>(firmware) >> 4U) & 0xFU;
    const unsigned c = firmware & 0xFU;
    char buf[40];
    switch (knobs.fw_format) {
    case FwFormat::Dotted:
      static_cast<void>(std::snprintf(buf, sizeof buf, "%u.%u.%u", a, b, c));
      break;
    case FwFormat::V3v0v5:
      static_cast<void>(std::snprintf(buf, sizeof buf, "%uv%uv%u", a, b, c));
      break;
    case FwFormat::VDotted:
      static_cast<void>(std::snprintf(buf, sizeof buf, "V%u.%u.%u", a, b, c));
      break;
    case FwFormat::Garbage:
      static_cast<void>(std::snprintf(buf, sizeof buf, "GS-%u-x", a));
      break;
    }
    return buf;
  }

  void restore() {
    unit_id = 'A';
    baud = 38400;
    modbus_address = knobs.factory_modbus_address;
    protocol = 2;
    source = 'A';
    setpoint_milli = 0;
    ramp_rate_milli = 0;
    averaging_ms = 250;
    totalizer_limit = 0;
    ++restores;
  }

  // ------------------------------------------------------------------------------------------------------------
  // Modbus RTU: a second, separate register file
  // ------------------------------------------------------------------------------------------------------------

public:
  [[nodiscard]] static uint16_t Crc(const uint8_t* p, std::size_t n) {
    uint16_t crc = 0xFFFF;
    for (std::size_t i = 0; i < n; ++i) {
      crc = static_cast<uint16_t>(crc ^ p[i]);
      for (int k = 0; k < 8; ++k) {
        crc = ((static_cast<uint32_t>(crc) & 1U) != 0U)
                  ? static_cast<uint16_t>((static_cast<uint32_t>(crc) >> 1U) ^ 0xA001U)
                  : static_cast<uint16_t>(static_cast<uint32_t>(crc) >> 1U);
      }
    }
    return crc;
  }

private:
  static int64_t toScaled(int64_t milli, uint32_t d) {
    int64_t f = 1;
    for (uint32_t i = 0; i < d; ++i) {
      f *= 10;
    }
    return (milli * f + (milli >= 0 ? 500 : -500)) / 1000;
  }

  /// The firmware that added register `a`, from the manual's "Added in V" notes (independent of the
  /// driver's register table). An older instrument has no such register: a read is Bad Address.
  [[nodiscard]] static uint32_t addedIn(uint32_t a) {
    if (a == 2053U) {
      return 0x210U;
    }
    if ((a == 45U) || (a == 46U) || (a == 47U) || (a == 48U) || (a == 51U) || (a == 52U) ||
        (a == 519U) || (a == 520U)) {
      return 0x240U;
    }
    if (a == 53U) {
      return 0x255U;
    }
    if ((a == 49U) || (a == 54U) || (a == 55U) || (a == 56U) || (a == 521U) || (a == 522U) ||
        (a == 524U) || (a == 525U) || ((a >= 2100U) && (a <= 2109U)) ||
        ((a >= 4200U) && (a <= 4215U))) {
      return 0x300U; // register 49 is taken as V3.0.0 too: the manual's text is ambiguous
    }
    if ((a >= 81U) && (a <= 140U)) {
      return 0x305U;
    }
    return 0U;
  }

  /// Register value, or -1 for an undefined register (or one the firmware does not have yet).
  [[nodiscard]] int32_t readReg(uint32_t a) const {
    if (firmware < addedIn(a)) {
      return -1;
    }
    const auto u16 = [](int64_t v) {
      return static_cast<int32_t>(static_cast<uint16_t>(static_cast<uint64_t>(v) & 0xFFFFU));
    };
    if ((a >= 26) && (a <= 31)) {
      const std::size_t i = static_cast<std::size_t>(a - 26U) * 2U;
      const char c0 = i < serial.size() ? serial[i] : '\0';
      const char c1 = (i + 1U) < serial.size() ? serial[i + 1U] : '\0';
      return static_cast<int32_t>((static_cast<uint32_t>(ByteOf(c0)) << 8U) |
                                  static_cast<uint32_t>(ByteOf(c1)));
    }
    if ((a >= 81) && (a <= 140)) {
      return gasTableReg(a - 81U);
    }
    switch (a) {
    case 21:
      return (knobs.baud_report >= 0) ? knobs.baud_report : baudCode();
    case 25:
      return firmware;
    case 32:
      return static_cast<int32_t>(flow_offset);
    case 35:
      return static_cast<int32_t>(full_scale_sccm >> 16U);
    case 36:
      return static_cast<int32_t>(full_scale_sccm & 0xFFFFU);
    case 45:
      return (knobs.address_report >= 0) ? knobs.address_report
                                         : static_cast<int32_t>(modbus_address);
    case 46:
      return unit_id;
    case 47:
      return static_cast<int32_t>(static_cast<uint64_t>(full_scale_milli) >> 16U);
    case 48:
      return static_cast<int32_t>(static_cast<uint64_t>(full_scale_milli) & 0xFFFFU);
    case 49:
      return unit_code;
    case 51:
      return static_cast<int32_t>(tare_samples);
    case 52:
      return static_cast<int32_t>(ref_temp_c100);
    case 54:
      return totalizer_limit;
    case 55:
      return static_cast<int32_t>(averaging_ms);
    case 56:
      return protocol == 2 ? 0 : 1;
    case 514:
      return static_cast<int32_t>(watchdog_ms);
    case 515:
      return autotare ? 1 : 0;
    case 516:
      return sourceCode(source);
    case 519:
      return static_cast<int32_t>(gain_p);
    case 520:
      return static_cast<int32_t>(gain_i);
    case 521:
      return static_cast<int32_t>(static_cast<uint64_t>(toScaled(batch_milli, mb_decimals)) >> 16U);
    case 522:
      return static_cast<int32_t>(static_cast<uint64_t>(toScaled(batch_milli, mb_decimals)) &
                                  0xFFFFU);
    case 524:
      return static_cast<int32_t>(rampRaw() >> 16U);
    case 525:
      return static_cast<int32_t>(rampRaw() & 0xFFFFU);
    case 2053:
      return static_cast<int32_t>((static_cast<uint64_t>(setpoint_milli) >> 16U) & 0xFFFFU);
    case 2054:
      return static_cast<int32_t>(static_cast<uint64_t>(setpoint_milli) & 0xFFFFU);
    case 2100:
      return gas;
    case 2101:
      return static_cast<int32_t>(statusBits());
    case 2102:
      return u16(temperature_c100);
    case 2103:
      return u16(toScaled(flow_milli, mb_decimals));
    case 2104:
      return static_cast<int32_t>(
          (static_cast<uint64_t>(toScaled(total_milli, mb_decimals)) >> 16U) & 0xFFFFU);
    case 2105:
      return static_cast<int32_t>(static_cast<uint64_t>(toScaled(total_milli, mb_decimals)) &
                                  0xFFFFU);
    case 2106:
      return u16(toScaled(setpoint_milli, mb_decimals));
    case 2107:
      return u16(valve_milli / 10);
    case 2108:
      return static_cast<int32_t>(
          (static_cast<uint64_t>(toScaled(batch_remaining_milli, mb_decimals)) >> 16U) & 0xFFFFU);
    case 2109:
      return static_cast<int32_t>(
          static_cast<uint64_t>(toScaled(batch_remaining_milli, mb_decimals)) & 0xFFFFU);
    case 4200:
      return static_cast<int32_t>(trigger);
    case 4201:
      return static_cast<int32_t>(measure_samples_);
    case 4202:
    case 4203:
      return u16(temperature_c100);
    case 4204:
      return u16(toScaled(min_flow_milli, mb_decimals));
    case 4205:
      return u16(toScaled(max_flow_milli, mb_decimals));
    case 4206:
      return static_cast<int32_t>(samples_cur);
    case 4207:
      return u16(temperature_c100);
    case 4208:
      return u16(toScaled(avg_flow_milli, mb_decimals));
    case 4209:
      return u16(valve_milli / 10);
    case 4210:
      return u16(toScaled(prev_min_milli, mb_decimals));
    case 4211:
      return u16(toScaled(prev_max_milli, mb_decimals));
    case 4212:
      return static_cast<int32_t>(samples_prev);
    case 4213:
      return u16(temperature_c100);
    case 4214:
    case 4215: {
      const auto avg = static_cast<uint64_t>(toScaled(prev_avg_milli, mb_decimals));
      const bool high = (a == 4214U) != knobs.prev_avg_low_first;
      return static_cast<int32_t>((high ? (avg >> 16U) : avg) & 0xFFFFU);
    }
    default:
      break;
    }
    return -1;
  }

  [[nodiscard]] int32_t gasTableReg(uint32_t off) const {
    static const char* const kLabels[12] = {"Air", "Ar", "CO2", "N2", "O2", "N2O",
                                            "H2",  "He", "CH4", "",   "",   ""};
    const uint32_t entry = off / 5U;
    const uint32_t part = off % 5U;
    if (part == 0U) {
      return static_cast<int32_t>(entry);
    }
    if (part == 1U) {
      return entry < 9U ? 4096 : 0;
    }
    const std::string label = kLabels[entry];
    const std::size_t i = static_cast<std::size_t>(part - 2U) * 2U;
    const char c0 = i < label.size() ? label[i] : '\0';
    const char c1 = (i + 1U) < label.size() ? label[i + 1U] : '\0';
    return static_cast<int32_t>((static_cast<uint32_t>(ByteOf(c0)) << 8U) |
                                static_cast<uint32_t>(ByteOf(c1)));
  }

  [[nodiscard]] int32_t baudCode() const {
    switch (baud) {
    case 4800:
      return 0;
    case 9600:
      return 1;
    case 19200:
      return 2;
    case 38400:
      return 3;
    case 57600:
      return 4;
    default:
      break;
    }
    return 5;
  }
  [[nodiscard]] uint32_t rampRaw() const {
    if (ramp_rate_milli == 0U) {
      return 0U;
    }
    const double unit_ms = rampUnitMs(ramp_unit);
    const double pct_per_ms = (static_cast<double>(ramp_rate_milli) / 1000.0) /
                              (static_cast<double>(full_scale_milli) / 1000.0) * 100.0 / unit_ms;
    return static_cast<uint32_t>(std::llround(pct_per_ms * 1.0e7));
  }

  /// Accepts a write; false for a read-only or undefined register, or a value out of range (an
  /// exception reply).
  bool writeReg(uint32_t a, uint16_t v, uint64_t& extra_us) {
    switch (a) {
    case 21:
      if ((v > 5) || ((v >= 4) && (firmware < 0x222))) {
        return false;
      }
      {
        static constexpr std::array<uint32_t, 6> kBauds{4800, 9600, 19200, 38400, 57600, 115200};
        pending_baud_ = kBauds[v];
      }
      return true;
    case 39:
      if (v != 0xAA55) {
        return false;
      }
      ++tares;
      flow_milli = 0;
      extra_us = static_cast<uint64_t>(tare_samples) * 2500U;
      return true;
    case 45:
      modbus_address = ((v >= 1) && (v <= 247)) ? v : 1U;
      return true;
    case 46:
      if (!knobs.ignore_unit_change) {
        unit_id = ((v >= 65) && (v <= 90)) ? static_cast<char>(v) : 'A';
      }
      return true;
    case 51:
      if (v < 1) {
        return false;
      }
      tare_samples = v;
      return true;
    case 52:
      if (v > 3000) {
        return false;
      }
      ref_temp_c100 = v;
      return true;
    case 53:
      if (v != 0xAA55) {
        return false;
      }
      total_milli = 0;
      ovr = false;
      ++total_resets;
      return true;
    case 54:
      if (v > 3) {
        return false;
      }
      totalizer_limit = v;
      return true;
    case 55:
      if (v > 2500) {
        return false;
      }
      averaging_ms = v;
      return true;
    case 56:
      if (v > 1) {
        return false;
      }
      if (v == 1) {
        protocol = 1;
      }
      return true;
    case 80:
      if (v != 0x5214) {
        return false;
      }
      restore();
      deaf_until_us_ = now_us + knobs.restore_us;
      return true;
    case 514:
      if (v > 5000) {
        return false;
      }
      watchdog_ms = v;
      return true;
    case 515:
      if (v > 1) {
        return false;
      }
      autotare = v == 1;
      return true;
    case 516:
      if (v > 2) {
        return false;
      }
      source = sourceLetter(v);
      return true;
    case 519:
      gain_p = v;
      return true;
    case 520:
      gain_i = v;
      return true;
    case 2100:
      if (v <= 8) {
        gas = v;
      }
      return true;
    case 4200:
      if (v > 7) {
        return false;
      }
      trigger = v;
      return true;
    case 4201:
      measure_samples_ = v;
      samples_cur = v;
      samples_prev = samples_cur;
      min_flow_milli = max_flow_milli = avg_flow_milli = flow_milli;
      return true;
    default:
      break;
    }
    return false;
  }

  bool writeReg32(uint32_t a, uint32_t hi, uint32_t lo) {
    const uint32_t value = (hi << 16U) | lo;
    switch (a) {
    case 521: {
      int64_t f = 1;
      for (uint32_t i = 0; i < mb_decimals; ++i) {
        f *= 10;
      }
      batch_milli = static_cast<int64_t>(value) * 1000 / f;
      batch_remaining_milli = batch_milli;
      return true;
    }
    case 524: {
      ramp_rate_milli = 0;
      if (value != 0U) {
        const double pct = static_cast<double>(value) / 1.0e7; // percent of full scale per ms
        ramp_rate_milli = static_cast<uint32_t>(
            std::llround(pct * (static_cast<double>(full_scale_milli) / 100.0)));
        ramp_unit = 3;
      }
      return true;
    }
    case 2053: {
      const int64_t sp = static_cast<int32_t>(value);
      if ((sp < 0) || (sp > ((full_scale_milli * 1025) / 1000))) {
        return false;
      }
      setpoint_milli = sp;
      return true;
    }
    default:
      break;
    }
    return false;
  }

  static void putU16(std::string& s, uint32_t v) {
    s.push_back(static_cast<char>((v >> 8U) & 0xFFU));
    s.push_back(static_cast<char>(v & 0xFFU));
  }
  static std::string seal(std::string f) {
    const uint16_t c = Crc(reinterpret_cast<const uint8_t*>(f.data()), f.size());
    f.push_back(static_cast<char>(c & 0xFFU));
    f.push_back(static_cast<char>(c >> 8U));
    return f;
  }

  void handleModbus(uint64_t quiet_at) {
    const std::size_t n = rx_.size();
    if (n < 8U) {
      return;
    }
    const auto* p = reinterpret_cast<const uint8_t*>(rx_.data());
    if (Crc(p, n - 2U) != static_cast<uint16_t>(static_cast<uint32_t>(p[n - 2U]) |
                                                (static_cast<uint32_t>(p[n - 1U]) << 8U))) {
      return; // a bad CRC is not answered
    }
    if (p[0] != modbus_address) {
      return;
    }
    const uint32_t fn = p[1];
    const uint64_t start = quiet_at + knobs.latency_us;
    std::string reply;
    uint64_t extra = 0;
    auto exception = [p, fn](uint32_t code) {
      std::string e;
      e.push_back(static_cast<char>(p[0]));
      e.push_back(static_cast<char>(fn | 0x80U));
      e.push_back(static_cast<char>(code));
      return seal(e);
    };
    if (fn == 3U) {
      const uint32_t first = static_cast<uint32_t>(p[2] << 8U) | p[3];
      const uint32_t count = static_cast<uint32_t>(p[4] << 8U) | p[5];
      const auto covers = [first, count](int32_t r) {
        return (r >= 0) && std::cmp_greater_equal(r, first) && std::cmp_less(r, first + count);
      };
      if (covers(knobs.silent_register)) {
        return;
      }
      if ((count < 1U) || (count > 125U) || covers(knobs.refuse_register)) {
        reply = exception(((count < 1U) || (count > 125U)) ? 3U : 2U);
      } else {
        std::string r;
        r.push_back(static_cast<char>(p[0]));
        r.push_back(CharOf(3U));
        r.push_back(static_cast<char>(count * 2U));
        bool bad = false;
        for (uint32_t i = 0; (i < count) && !bad; ++i) {
          const int32_t v = readReg(first + i);
          if (v < 0) {
            bad = true;
          } else {
            putU16(r, static_cast<uint32_t>(v));
          }
        }
        reply = bad ? exception(2) : seal(r);
      }
    } else if (fn == 6U) {
      const uint32_t a = static_cast<uint32_t>(p[2] << 8U) | p[3];
      const uint32_t v = static_cast<uint32_t>(p[4] << 8U) | p[5];
      pending_baud_ = 0;
      if (a == 2054U) {
        reply = exception(2);
      } else if (writeReg(a, static_cast<uint16_t>(v), extra)) {
        reply = seal(std::string(rx_.substr(0, 6)));
      } else {
        reply = exception(readReg(a) < 0 ? 2U : 3U);
      }
    } else if ((fn == 16U) && (n >= 11U)) {
      const uint32_t a = static_cast<uint32_t>(p[2] << 8U) | p[3];
      const uint32_t cnt = static_cast<uint32_t>(p[4] << 8U) | p[5];
      const uint32_t bytes = p[6];
      if ((cnt != 2U) || (bytes != 4U) || (n != 13U)) {
        reply = exception(3);
      } else {
        const uint32_t hi = static_cast<uint32_t>(p[7] << 8U) | p[8];
        const uint32_t lo = static_cast<uint32_t>(p[9] << 8U) | p[10];
        const bool ok = ((a == 2053U) || (a == 521U) || (a == 524U)) && writeReg32(a, hi, lo);
        reply = ok ? seal(std::string(rx_.substr(0, 6)))
                   : exception((a == 2053U) || (a == 521U) || (a == 524U) ? 3U : 2U);
      }
    } else {
      reply = exception(1);
    }
    const uint32_t switch_to = pending_baud_;
    pending_baud_ = 0;
    if ((switch_to != 0U) && (reply.size() > 2U) && (static_cast<uint8_t>(reply[1]) < 0x80U)) {
      baud = switch_to; // the reply goes out at the new speed
    }
    emit(reply, start + extra, true);
  }

  uint32_t pending_baud_{0};
  uint32_t measure_samples_{0};
};

/// Port with every optional hook: sleeps by advancing the virtual clock, switches baud, logs.
class FullPort : public alicat_basis2::UartInterface<FullPort> {
public:
  explicit FullPort(Instrument& m) : m_(m) {}
  bool EnsureInitialized() noexcept {
    return !m_.fail_init;
  }
  bool Write(const uint8_t* data, std::size_t n) noexcept {
    return m_.WriteBytes(data, n);
  }
  std::size_t Read(uint8_t* out, std::size_t cap, uint32_t /*timeout_ms*/) noexcept {
    return m_.ReadBytes(out, cap);
  }
  bool FlushInput() noexcept {
    if (m_.fail_flush) {
      return false;
    }
    m_.Flush();
    return true;
  }
  uint32_t NowMs() noexcept {
    return m_.NowMsValue();
  }
  void DelayMs(uint32_t ms) noexcept {
    m_.AdvanceMs(ms);
  }
  bool SetBaud(uint32_t baud) noexcept {
    m_.port_baud = baud;
    ++m_.baud_sets;
    return baud != 0U;
  }
  void Log(alicat_basis2::LogLevel /*level*/, const char* message) noexcept {
    m_.log_lines.emplace_back(message);
  }

private:
  Instrument& m_;
};

/// Port with no optional hook: the driver spins on `NowMs()`, which costs `spin_us` of virtual time
/// per call.
class BarePort : public alicat_basis2::UartInterface<BarePort> {
public:
  explicit BarePort(Instrument& m) : m_(m) {}
  bool EnsureInitialized() noexcept {
    return !m_.fail_init;
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
    m_.Advance(m_.spin_us);
    return m_.NowMsValue();
  }

private:
  Instrument& m_;
};

/// A ready-made rig: instrument, port, bus, driver.
template <typename PortT>
struct Rig {
  Instrument inst;
  PortT port;
  alicat_basis2::Bus<PortT> bus;
  alicat_basis2::Basis2<PortT> drv;

  explicit Rig(const alicat_basis2::Config& config = alicat_basis2::Config{})
      : port(inst), bus(port), drv(bus, config) {}
};

using FullRig = Rig<FullPort>;
using BareRig = Rig<BarePort>;

/// A driver configuration for Modbus with the decimals the model uses.
inline alicat_basis2::Config ModbusConfig() {
  alicat_basis2::Config c{};
  c.protocol = alicat_basis2::Protocol::ModbusRtu;
  c.modbus_flow_decimals = 1;
  return c;
}

} // namespace model

#endif // ALICAT_BASIS2_TEST_MOCK_BASIS2_HPP
