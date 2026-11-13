/**
 * @file alicat_basis2.hpp
 * @brief ALICAT_BASIS2 driver: the one header to include.
 * @copyright Copyright (c) 2026 The driver-alicat-basis2 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * `alicat_basis2::Basis2<Port>` drives one Alicat BASIS 2 mass flow meter (B series) or controller
 * (BC series) through a `Bus<Port>` that owns the serial port. The driver speaks the instrument's
 * ASCII protocol ("BASIS Protocol 2", the default) or Modbus RTU behind the same typed API. It has
 * no platform code, allocates nothing, throws nothing, takes no lock and is not thread-safe: give
 * each bus one owner, or guard it with one mutex.
 *
 * Streaming mode is never started; broadcast (`*`) is never sent; there is no raw-text call. Six
 * commands can strand the instrument (unit ID, Modbus address, baud rate, protocol switch, factory
 * restore) and need a guard token naming the consequence.
 *
 * Not verified on hardware: every reply format the manual does not show is an assumption with a
 * knob (see `docs/configuration.md`).
 */
#ifndef ALICAT_BASIS2_HPP
#define ALICAT_BASIS2_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "alicat_basis2_ascii.hpp"
#include "alicat_basis2_detail.hpp"
#include "alicat_basis2_modbus.hpp"
#include "alicat_basis2_registers.hpp"
#include "alicat_basis2_request.hpp"
#include "alicat_basis2_types.hpp"
#include "alicat_basis2_uart_interface.hpp"
#include "alicat_basis2_version.h"

namespace alicat_basis2 {

/**
 * @defgroup alicat_basis2_guards Guard tokens
 * @brief Empty structs a caller must name to reach a command that can strand the instrument.
 * @{
 */
/** @brief Confirms `ChangeUnitId()`: the instrument answers to the new ID afterwards; connect one
 * instrument only. */
struct ConfirmUnitIdChange {};
/** @brief Confirms `SetModbusAddress()`: the Modbus address changes at once. */
struct ConfirmAddressChange {};
/** @brief Confirms `ChangeBaud()`: the reply arrives at the new speed and the port must follow. */
struct ConfirmBaudChange {};
/** @brief Confirms `SwitchToProtocol1()`: the instrument leaves the protocol this driver speaks. */
struct ConfirmAbandonProtocol2 {};
/// Guard token of `SwitchToProtocol2`.
struct ConfirmReturnToProtocol2 {};
/** @brief Confirms `FactoryRestore()`: unit ID, baud rate, Modbus address and protocol revert;
 * power cycle. */
struct ConfirmFactoryRestore {};
/** @} */

/** @brief The unit IDs that answered a scan: bit 0 is `A`. */
struct UnitIdScan {
  uint32_t found{0}; ///< One bit per unit ID `A` to `Z`.
};

/** @brief The Modbus addresses that answered a scan: bit n of byte n/8 is address n. */
struct AddressScan {
  std::array<uint8_t, 32>
      found{}; ///< One bit per address 0 to 255 (only 1 to 247 are ever probed).
};

/**
 * @brief Driver for one instrument on a `Bus`.
 *
 * Lifecycle: construct with the configuration (no I/O), then `EnsureInitialized()` (port up,
 * configuration checked), then `Begin()` (a read-only sequence that learns the identity). Every
 * call that touches the instrument returns `NotInitialized` before `EnsureInitialized()` has
 * succeeded.
 *
 * @tparam Port Your class derived from `UartInterface<Port>`.
 */
template <UartPort Port>
class Basis2 {
public:
  /**
   * @brief Bind to a bus and store the configuration. No I/O, cannot fail.
   * @param bus The serial line; it must outlive the driver.
   * @param config Checked by `EnsureInitialized()` before any I/O.
   */
  explicit Basis2(Bus<Port>& bus, const Config& config = {}) noexcept
      : bus_(bus), config_(config) {}
  /** @brief One driver object per instrument. */
  Basis2(const Basis2&) = delete;
  /** @brief One driver object per instrument. */
  Basis2& operator=(const Basis2&) = delete;

  /// @name Lifecycle
  /// @{

  /**
   * @brief Bring the port up and check the stored configuration; no instrument traffic.
   * @return `Ok`, `InvalidArgument` (bad `Config`), `Bus` (the port could not start).
   */
  Result<void> EnsureInitialized() noexcept;

  /**
   * @brief Learn the instrument: a poll, the firmware, serial number, full scale, gas, totalizer
   * limit, averaging, reference temperature and, for a controller, setpoint source, ramp and
   * autotare. Sends no write.
   *
   * The instrument needs about one minute after power-up to settle (S19 p.1). In Modbus mode
   * `Config::modbus_flow_decimals` must be set (A11).
   *
   * @return `Ok`, `NotInitialized`, `InvalidArgument` (Modbus decimals unset), or the error of the
   * first failing transaction.
   */
  Result<void> Begin() noexcept;

  /**
   * @brief Find the instrument again after a guarded command left `StateUnknown` or after a lost
   * line: poll the configured ID, else (only when `Bus::DeviceCount()` is one) scan `A` to `Z` or
   * the Modbus addresses, adopt the ID that answers and run `Begin()`. `StateUnknown` ends only
   * when that `Begin()` succeeds and the serial number, firmware and kind equal those learned
   * before; on any failure the driver stays in `StateUnknown` and keeps its old ID.
   * @return `Ok`, `NotInitialized`, `Timeout` (nothing answered), `VerifyMismatch` (another
   * instrument answered) or an error of `Begin()`.
   */
  Result<void> Recover() noexcept;

  /** @return true after `EnsureInitialized()` succeeded. No I/O. */
  [[nodiscard]] bool IsInitialized() const noexcept {
    return initialized_;
  }
  /** @return true after `Begin()` succeeded and until a guarded command leaves the state unknown.
   */
  [[nodiscard]] bool IsReady() const noexcept {
    return ready_ && !state_unknown_;
  }
  /** @return true while the driver doubts the instrument's state (a guarded command's outcome was
   * not seen); only a successful `Recover()` ends it. No I/O. */
  [[nodiscard]] bool IsStateUnknown() const noexcept {
    return state_unknown_;
  }
  /** @return The configuration in force. */
  [[nodiscard]] const Config& GetConfig() const& noexcept {
    return config_;
  }
  /** @brief Not on a temporary. */
  void GetConfig() const&& = delete;
  /** @return What `Begin()` learned. */
  [[nodiscard]] const Identity& GetIdentity() const& noexcept {
    return identity_;
  }
  /** @brief Not on a temporary. */
  void GetIdentity() const&& = delete;
  /** @return The Modbus exception code of the last `Rejected` Modbus reply, else 0. */
  [[nodiscard]] uint8_t LastException() const noexcept {
    return last_exception_;
  }

  /**
   * @brief Single-shot raw read of `count` registers from `first` (Modbus function 3): one frame,
   * no retry, no scaling. The reply is still accepted only after its CRC, address and length check.
   * @param first First register.
   * @param count Number of registers, 1 to `reg::kMaxRead`.
   * @return The register words, or `Unsupported` over ASCII, `InvalidArgument` or a bus error.
   */
  Result<modbus::Response> ReadRegisterRaw(uint16_t first, uint16_t count) noexcept;
  /**
   * @brief Single-shot raw read of one ASCII command that only reads (never one that writes): one
   * request, no retry, no decoding; the reply line (without its CR) is returned as received.
   * @param command A read command of the grammar table, for example `Command::ReadGas`.
   * @return The reply line, or `Unsupported` over Modbus, `InvalidArgument` or a bus error.
   */
  Result<Reply> ReadRawLine(Command command) noexcept;
  /** @return The last reply any call accepted from the bus (no I/O); empty before the first. */
  [[nodiscard]] const Reply& GetLastReply() const& noexcept {
    return last_reply_;
  }
  /** @brief Not on a temporary. */
  void GetLastReply() const&& = delete;

  /// @}
  /// @name Data
  /// @{

  /** @brief Poll: the full data frame (C01), or the Modbus block 2100 to 2109. @return The frame.
   */
  Result<Frame> ReadFrame() noexcept;
  /**
   * @brief Read selected fields (`DV n`, C02). In Modbus mode the block is read and the fields
   * asked are filled.
   * @param items `DataItem` bits, 1 to 255; controller-only items on a meter are `Unsupported`.
   * @return The frame with `present == items`.
   */
  Result<Frame> ReadData(uint8_t items) noexcept;
  /** @brief Flow in the configured unit. @return The number. */
  Result<Decimal> ReadFlow() noexcept;
  /** @brief Temperature in C. @return The number. */
  Result<Decimal> ReadTemperature() noexcept;
  /** @brief Totalizer. @return The number. */
  Result<Decimal> ReadTotal() noexcept;
  /** @brief Valve drive in percent (controllers). @return The number. */
  Result<Decimal> ReadValveDrive() noexcept;
  /** @brief The current setpoint (controllers). @return The number. */
  Result<Decimal> ReadSetpointValue() noexcept;
  /** @brief Batch volume remaining (controllers). @return The number. */
  Result<Decimal> ReadBatchRemaining() noexcept;
  /** @brief Status flags. @return The flags. */
  Result<StatusFlags> ReadStatus() noexcept;
  /** @brief The selected gas (C27). @return The gas and its name. */
  Result<Frame> ReadGas() noexcept;

  /// @}
  /// @name Setpoint and valve (controllers)
  /// @{

  /**
   * @brief Set the setpoint (C03; Modbus registers 2053 to 2054).
   *
   * Checked against 0 to full scale x 1.025 (M p.11). While the source is `SavedDigital` (or
   * unknown) a change sooner than `Config::saved_setpoint_min_interval_ms` after the last one is
   * `RateLimited`: the saved source wears non-volatile memory (M p.11). Read back per
   * `Config::verify_setpoint`: one extra transaction.
   *
   * @param flow The setpoint in the instrument's flow unit.
   * @note A setpoint of 0 is a switch-off: it is never refused for `StateUnknown` or `RateLimited`.
   * @return `Ok`, `InvalidArgument`, `Unsupported` (meter), `RateLimited`, `VerifyMismatch` or a
   * bus error.
   */
  Result<void> SetSetpoint(double flow) noexcept;
  /** @brief Read the setpoint source (C04, register 516). @return The source. */
  Result<SetpointSource> ReadSetpointSource() noexcept;
  /**
   * @brief Set the setpoint source (C05, register 516). `SavedDigital` wears non-volatile memory:
   * use `UnsavedDigital` for frequent changes.
   * @param source The source.
   * @return `Ok`, `InvalidArgument`, `Unsupported` or a bus error.
   */
  Result<void> SetSetpointSource(SetpointSource source) noexcept;
  /** @brief Read the ramp (C06, registers 524 to 525; Modbus: the raw register). @return The ramp.
   */
  Result<Ramp> ReadRamp() noexcept;
  /**
   * @brief Set the maximum setpoint ramp rate (C07; Modbus 524 to 525 after conversion with the
   * full scale, A9).
   * @param rate Flow units per time unit, above 0.
   * @param unit The time unit.
   * @return `Ok`, `InvalidArgument`, `Unsupported` or a bus error.
   */
  Result<void> SetRamp(double rate, TimeUnit unit) noexcept;
  /** @brief Disable ramp limiting (C08). @return `Ok` or an error. */
  Result<void> DisableRamp() noexcept;
  /** @brief Read the watchdog in ms (C09, register 514). @return The time, 0 when disabled. */
  Result<uint32_t> ReadWatchdog() noexcept;
  /**
   * @brief Set the communication watchdog (C10, register 514). It acts only for Modbus-set
   * setpoints with the unsaved digital source (M p.12 to 13, A8): never rely on it with ASCII; see
   * `WatchdogProtects()`.
   * @param ms 0 to 5000; 0 disables.
   * @return `Ok`, `InvalidArgument`, `Unsupported` or a bus error.
   */
  Result<void> SetWatchdog(uint32_t ms) noexcept;
  /**
   * @brief Hold the valve at a percentage of full drive (C11). ASCII only; the Modbus map has no
   * such register.
   * @param percent 0 to 100.
   * @return `Ok`, `InvalidArgument`, `Unsupported` (meter or Modbus) or a bus error.
   */
  Result<void> HoldValve(double percent) noexcept;
  /** @brief Resume closed-loop control after a hold (C12). @return `Ok` or an error. */
  Result<void> ResumeControl() noexcept;
  /**
   * @brief Hold the valve fully open: `HoldValve(100)` (the FlowVision "open" button is not a
   * command, A20).
   * @return `Ok` or an error.
   */
  Result<void> OpenValve() noexcept;
  /**
   * @brief Hold the valve closed: `HoldValve(0)`. Never refused for `StateUnknown`.
   * @return `Ok` or an error.
   */
  Result<void> CloseValve() noexcept;

  /// @}
  /// @name Control tuning (controllers)
  /// @{

  /** @brief Read the loop gains (C13, registers 519 and 520). @return P and I. */
  Result<Gains> ReadGains() noexcept;
  /** @brief Set the loop gains (C14). @param p P gain 0 to 65535. @param i I gain 0 to 65535.
   * @return `Ok`/error. */
  Result<void> SetGains(uint32_t p, uint32_t i) noexcept;
  /** @brief Read autotare (C25, register 515). @return true when enabled. */
  Result<bool> ReadAutotare() noexcept;
  /** @brief Enable or disable autotare (C26): the zero is taken after 2 s at a zero setpoint.
   * @param on Enabled. @return `Ok` or an error. */
  Result<void> SetAutotare(bool on) noexcept;
  /** @brief Read the measurement trigger bits (C19, register 4200). @return A mask of
   * `TriggerMode`. */
  Result<uint8_t> ReadMeasurementTrigger() noexcept;
  /** @brief Set the trigger bits (C20). @param mask 0 to 7. @return `Ok`/error. */
  Result<void> SetMeasurementTrigger(uint8_t mask) noexcept;

  /// @}
  /// @name Measurement window and tare
  /// @{

  /**
   * @brief Start a measurement window (C21; Modbus register 4201 in samples of about 2.5 ms).
   * @param duration_ms Duration in ms.
   * @return `Ok`, `InvalidArgument` or an error.
   */
  Result<void> StartMeasurement(uint32_t duration_ms) noexcept;
  /** @brief Averages of the current or most recent window (C22). @return Time, temperature, flow.
   */
  Result<Averages> ReadAverages() noexcept;
  /** @brief Minimum and maximum of the current or most recent window (C23). @return The ranges. */
  Result<Ranges> ReadRanges() noexcept;
  /** @brief The previous completed window (Modbus only; reading it may start a window, A17).
   * @return The values. */
  Result<PreviousMeasurement> ReadPreviousMeasurement() noexcept;
  /**
   * @brief Tare the flow sensor (C24; Modbus 51 and 39). Only on the pure process gas at true zero
   * flow (M p.14). The bus stays closed for the duration.
   * @param duration_ms 1 to 32767.
   * @return `Ok`, `InvalidArgument` or an error.
   */
  Result<void> Tare(uint32_t duration_ms) noexcept;
  /** @brief `Tare(1000)`, the manual's example duration. @return `Ok`/error. */
  Result<void> TareDefault() noexcept;

  /// @}
  /// @name Gas, totalizer, processing
  /// @{

  /** @brief Select the gas (C29, register 2100). @param gas The gas. @return `Ok`/error. */
  Result<void> SetGas(GasId gas) noexcept;
  /** @brief The gases the instrument offers (C28, firmware 3.0.5). @return The list or its raw
   * text. */
  Result<GasList> ReadGasList() noexcept;
  /** @brief The twelve-entry gas table (Modbus only, firmware 3.0.5). @return The table. */
  Result<GasTable> ReadGasTable() noexcept;
  /** @brief Reset the totalizer (C30, register 53). @return `Ok`/error. */
  Result<void> ResetTotal() noexcept;
  /** @brief Read the totalizer limit mode (C33, register 54). @return The mode. */
  Result<TotalizerLimit> ReadTotalizerLimit() noexcept;
  /** @brief Set the totalizer limit mode (C34). @param mode The mode. @return `Ok`/error. */
  Result<void> SetTotalizerLimit(TotalizerLimit mode) noexcept;
  /** @brief Set a batch volume (C31; the valve closes after it has flowed). @param volume Above 0.
   * @return Result. */
  Result<void> SetBatch(double volume) noexcept;
  /** @brief Disable the batch (C32). @return `Ok`/error. */
  Result<void> DisableBatch() noexcept;
  /** @brief Flow averaging time constant in ms (C17, register 55). @return The time. */
  Result<uint32_t> ReadAveraging() noexcept;
  /** @brief Set the averaging time constant (C18). @param ms 0 to 2500. @return `Ok`/error. */
  Result<void> SetAveraging(uint32_t ms) noexcept;
  /** @brief Reference temperature in C (C15, register 52). @return The number. */
  Result<Decimal> ReadReferenceTemperature() noexcept;
  /** @brief Set the reference temperature (C16): 0 to 30.00 C (A10). @param celsius The value.
   * @return Result. */
  Result<void> SetReferenceTemperature(double celsius) noexcept;

  /// @}
  /// @name Identity
  /// @{

  /** @brief Read a full-scale value with its unit text (C35). @param item Which. @return The value.
   */
  Result<FullScale> ReadFullScale(FullScaleItem item) noexcept;
  /** @brief Serial number (C37, registers 26 to 31). @return The text. */
  Result<Text> ReadSerialNumber() noexcept;
  /** @brief Firmware version (C38, register 25). @return The text as printed (Modbus: `V a.b.c`).
   */
  Result<Text> ReadFirmware() noexcept;
  /** @brief Modbus address (C39, register 45). @return The address. */
  Result<uint32_t> ReadModbusAddress() noexcept;
  /** @brief Baud rate (C41, register 21). @return The speed. */
  Result<Baud> ReadBaud() noexcept;
  /** @brief Command protocol (C43, register 56). @return The protocol. */
  Result<CommandProtocol> ReadProtocol() noexcept;
  /** @brief The flow unit code of register 49 (Modbus only). @return The unit. */
  Result<FlowUnit> ReadFlowUnit() noexcept;
  /** @brief The full scale in SCCM (registers 35 to 36, Modbus only). @return The value. */
  Result<uint32_t> ReadFullScaleSccm() noexcept;
  /** @brief The flow sensor offset (register 32, Modbus only). @return The ADC integer defined as
   * zero. */
  Result<uint16_t> ReadSensorOffset() noexcept;

  /// @}
  /// @name Guarded commands
  /// @{

  /**
   * @brief Change the unit ID (C36, register 46). Connect one instrument at a time (M p.16):
   * refused when `Bus::DeviceCount()` is above one. The reply is accepted under either ID, then
   * verified with a poll under the new ID; if that fails the state is `StateUnknown` until
   * `Recover()`.
   * @param new_id `A` to `Z`.
   * @return `Ok`, `GuardNotConfirmed`, `InvalidArgument`, `Unsupported` or `StateUnknown`.
   */
  Result<void> ChangeUnitId(char new_id, ConfirmUnitIdChange /*confirm*/) noexcept;
  /**
   * @brief Change the Modbus address (C40, register 45).
   * @param address 1 to 247.
   * @return `Ok`, `InvalidArgument`, `StateUnknown` or an error.
   */
  Result<void> SetModbusAddress(uint8_t address, ConfirmAddressChange /*confirm*/) noexcept;
  /**
   * @brief Change the baud rate (C42, register 21): the reply arrives at the new speed. Needs the
   * port's `SetBaud()` hook. On failure the port is set back once and the state is `StateUnknown`.
   * @param baud The new speed.
   * @return `Ok`, `Unsupported` (no hook), `FirmwareTooOld`, `StateUnknown` or an error.
   */
  Result<void> ChangeBaud(Baud baud, ConfirmBaudChange /*confirm*/) noexcept;
  /**
   * @brief Switch the instrument to BASIS Protocol 1 (C44, register 56). Only with the flow units
   * SCCM, SLPM, NCCM or NLPM (M p.17). Afterwards the driver is abandoned: every call is
   * `Unsupported` until `SwitchToProtocol2()` or `Begin()`.
   * @return `Ok`, `Unsupported` (unit not allowed) or an error.
   */
  Result<void> SwitchToProtocol1(ConfirmAbandonProtocol2 /*confirm*/) noexcept;
  /** @brief Switch back to BASIS Protocol 2 (C45). @return `Ok`/error. */
  Result<void> SwitchToProtocol2(ConfirmReturnToProtocol2 /*confirm*/) noexcept;
  /**
   * @brief Restore the factory configuration (C46, register 80). Unit ID, baud rate, Modbus address
   * and protocol revert: the state is `StateUnknown`; power-cycle the instrument and call
   * `Recover()`.
   * @return `Ok` or an error.
   */
  Result<void> FactoryRestore(ConfirmFactoryRestore /*confirm*/) noexcept;

  /// @}
  /// @name Scans
  /// @{

  /** @brief Poll `A` to `Z` (26 reads at most) and report which answered. @return The IDs. */
  Result<UnitIdScan> ScanUnitIds() noexcept;
  /**
   * @brief Read register 25 at each address of a range (function 3 only).
   * @param first First address, 1 to 247.
   * @param last Last address, `first` to 247.
   * @return The addresses that answered.
   */
  Result<AddressScan> ScanModbusAddresses(uint8_t first, uint8_t last) noexcept;

  /// @}

  /**
   * @brief Whether the instrument's watchdog protects a setpoint (A8): only for Modbus-written
   * setpoints with the unsaved digital source.
   * @param protocol The protocol the setpoint is written with.
   * @param source The setpoint source.
   * @return true only for `ModbusRtu` with `UnsavedDigital`.
   */
  [[nodiscard]] static constexpr bool WatchdogProtects(Protocol protocol,
                                                       SetpointSource source) noexcept {
    return (protocol == Protocol::ModbusRtu) && (source == SetpointSource::UnsavedDigital);
  }

private:
  [[nodiscard]] Timing timing() const noexcept;
  [[nodiscard]] ascii::FrameRules rules() const noexcept;
  Error ready(bool needs_identity, bool switch_off = false) const noexcept;
  bool scanAddresses(bool alone) noexcept;
  bool scanAtSpeed(uint32_t speed, bool alone) noexcept;
  bool scanForUnit() noexcept;
  void afterFailedRecover(const detail::RecoverHeld& held, bool found, Error error) noexcept;
  Error setpointAllowed(int64_t milli) const noexcept;
  Result<SetpointSource> learnSource(SetpointSource source) noexcept;
  Error needController() const noexcept;
  Error needAscii() const noexcept;
  Error needModbus() const noexcept;
  Error gate(uint16_t min_fw) const noexcept;
  [[nodiscard]] int64_t fullScaleMilli() const noexcept;
  Result<Reply> run(const Request& request) noexcept;
  Result<Reply> runOnce(const Request& request) noexcept;
  Result<Reply> asciiRun(Command command, const ascii::Args& args) noexcept;
  Result<void> asciiSet(Command command, const ascii::Args& args) noexcept;
  Result<ascii::Values> asciiValues(Command command, uint8_t count) noexcept;
  Result<modbus::Response> mbReadOnce(const Request& request, uint16_t count) noexcept;
  Result<modbus::Response> mbRead(uint16_t first, uint16_t count) noexcept;
  Result<void> mbWrite1(uint16_t target, uint16_t value, uint32_t extra_ms = 0U,
                        uint32_t hold_ms = 0U) noexcept;
  Result<void> mbWrite2(uint16_t target, uint32_t value, uint32_t extra_ms = 0U) noexcept;
  Result<void> mbWriteBuilt(const Result<Request>& built, uint32_t extra_ms,
                            uint32_t hold_ms) noexcept;
  Result<uint32_t> readInt(Command command, uint16_t target, uint32_t hi) noexcept;
  Result<void> setInt(Command command, uint16_t target, int64_t value) noexcept;
  Result<Frame> asciiFrame(Command command, uint8_t items) noexcept;
  Result<Frame> modbusFrame(uint8_t items) noexcept;
  Result<Text> modbusText(uint16_t first, uint16_t count) noexcept;
  Result<void> verifySetpoint(int64_t milli) noexcept;
  Result<void> learnAscii() noexcept;
  Result<void> learnMore() noexcept;
  Result<void> learnFullScale() noexcept;
  Result<void> learnControlModbus() noexcept;
  [[nodiscard]] bool baudVerified(bool sent, uint8_t code) noexcept;
  Result<void> learnModbus() noexcept;
  Result<void> guardedFinish(char new_id) noexcept;
  Result<void> guardedAscii(Command command, const ascii::Args& args) noexcept;
  Result<void> setAddressOnBus(uint8_t address, uint16_t target) noexcept;
  Result<void> failState(Error error) noexcept;
  Result<void> holdValve(int64_t milli) noexcept;
  Result<void> writeSetpoint(int64_t milli) noexcept;
  Result<void> verifiedAfterWrite(int64_t milli) noexcept;
  [[nodiscard]] uint32_t setpointExtraMs() const noexcept;
  Result<void> offOnce(bool setpoint) noexcept;
  Result<void> offInDoubt(bool setpoint) noexcept;
  Result<void> offAt(bool setpoint, const detail::Alternatives& at, bool adopt) noexcept;
  void invalidate() noexcept;

  Bus<Port>& bus_;
  Config config_;
  Identity identity_{};
  bool initialized_{false};
  bool ready_{false};
  bool state_unknown_{false};
  bool line_busy_{false}; ///< The last scan stopped: a poll met a line that never went quiet.
  bool abandoned_{false};
  bool have_setpoint_{false};
  bool layout_known_{false};
  FrameLayout layout_{FrameLayout::Auto};
  uint8_t last_exception_{0};
  Reply last_reply_{};
  uint32_t last_setpoint_ms_{0};
  detail::Alternatives alt_{};
};

} // namespace alicat_basis2

// codeql::misra_deviation_next_line(MD-CPP-009)
#include "../src/alicat_basis2.ipp"

#endif // ALICAT_BASIS2_HPP
