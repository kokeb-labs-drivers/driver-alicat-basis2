/**
 * @file alicat_basis2_registers.hpp
 * @brief ALICAT_BASIS2 Modbus register map, magic values, firmware gates and unit tables (M pp.18
 * to 27, p.31).
 * @copyright Copyright (c) 2026 The driver-alicat-basis2 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * Registers are 16 bits, big-endian on the wire. A firmware value is `256a + 16b + c` (register
 * 25).
 */
#ifndef ALICAT_BASIS2_REGISTERS_HPP
#define ALICAT_BASIS2_REGISTERS_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "alicat_basis2_detail.hpp"
#include "alicat_basis2_types.hpp"

/** @brief The ALICAT_BASIS2 Modbus register map. */
namespace alicat_basis2::reg {

/**
 * @defgroup alicat_basis2_registers Register map
 * @brief Addresses, magic values and firmware gates.
 * @{
 */

inline constexpr uint16_t kBaud = 21;     ///< R/W baud code 0 to 5 (M p.25).
inline constexpr uint16_t kFirmware = 25; ///< R firmware, 256a + 16b + c.
inline constexpr uint16_t kSerial = 26;   ///< R six registers, two characters each (26 to 31).
inline constexpr uint16_t kSensorOffset =
    32; ///< R flow sensor offset (ADC integer defined as zero).
inline constexpr uint16_t kFullScaleSccm = 35; ///< R two registers, full scale in SCCM.
inline constexpr uint16_t kTare = 39;          ///< W 0xAA55 tares.
inline constexpr uint16_t kModbusAddress = 45; ///< R/W 1 to 247.
inline constexpr uint16_t kUnitId = 46;        ///< R/W ASCII 65 to 90.
inline constexpr uint16_t kFullScale = 47;   ///< R two registers, full scale in flow units x 1000.
inline constexpr uint16_t kFlowUnit = 49;    ///< R flow unit code.
inline constexpr uint16_t kTareSamples = 51; ///< R/W 1 to 65535, about 2.5 ms each.
inline constexpr uint16_t kRefTemperature = 52; ///< R/W 0 to 3000, C x 100.
inline constexpr uint16_t kTotalReset = 53;     ///< W 0xAA55 resets the total volume.
inline constexpr uint16_t kTotalLimit = 54;     ///< R/W 0 to 3.
inline constexpr uint16_t kAveraging = 55;      ///< R/W 0 to 2500 ms.
inline constexpr uint16_t kProtocol = 56;       ///< R/W 0 BASIS 2, 1 BASIS 1.
inline constexpr uint16_t kFactoryRestore = 80; ///< W 0x5214.
inline constexpr uint16_t kGasTable = 81;       ///< R 60 registers: twelve entries of five.
inline constexpr uint16_t kWatchdog = 514;      ///< R/W 0 to 5000 ms.
inline constexpr uint16_t kAutotare = 515;      ///< R/W 0 or 1.
inline constexpr uint16_t kSetpointType = 516;  ///< R/W 0 analog, 1 saved, 2 unsaved.
inline constexpr uint16_t kGainP = 519;         ///< R/W 0 to 65535.
inline constexpr uint16_t kGainI = 520;         ///< R/W 0 to 65535.
inline constexpr uint16_t kBatch = 521;         ///< R/W two registers.
inline constexpr uint16_t kRamp = 524;          ///< R/W two registers, % of FS per ms x 10^7.
inline constexpr uint16_t kSetpoint = 2053;     ///< R/W two registers, signed, /1000.
inline constexpr uint16_t kGas = 2100;    ///< R/W gas number; start of the instantaneous block.
inline constexpr uint16_t kStatus = 2101; ///< R status bits.
inline constexpr uint16_t kTemperature = 2102;    ///< R signed, /100.
inline constexpr uint16_t kFlow = 2103;           ///< R signed, scaled by the factory decimals.
inline constexpr uint16_t kTotal = 2104;          ///< R two registers, unsigned.
inline constexpr uint16_t kSetpointRead = 2106;   ///< R current setpoint.
inline constexpr uint16_t kValveDrive = 2107;     ///< R /100.
inline constexpr uint16_t kBatchRemaining = 2108; ///< R two registers, unsigned.
inline constexpr uint16_t kTrigger = 4200;        ///< R/W trigger bits 0 to 7.
inline constexpr uint16_t kSamples = 4201; ///< R/W samples in a measurement; a write starts it.
inline constexpr uint16_t kMeasureBlock =
    4202; ///< R 4202 to 4209: min/max/avg of the current window.
inline constexpr uint16_t kPreviousBlock = 4210; ///< R 4210 to 4215: the previous window.
inline constexpr uint16_t kPreviousAvgFlow =
    4214; ///< R signed 32-bit, 4214 high word first (A25); reading 4214 may trigger (A17).

inline constexpr uint16_t kMagicAction = 0xAA55;  ///< Tare and total reset value.
inline constexpr uint16_t kMagicRestore = 0x5214; ///< Factory restore value.

/** @brief Largest register count of one read the driver issues. */
inline constexpr uint16_t kMaxRead = 60;
/** @brief Milliseconds per measurement or tare sample, times ten (about 2.5 ms, M p.22, p.26). */
inline constexpr uint32_t kSampleTenthMs = 25;

/** @brief Firmware gates, `256a + 16b + c` (M pp.21 to 27). */
inline constexpr uint16_t kFw210 = 0x210; ///< 2.1.0: register 2053.
inline constexpr uint16_t kFw222 = 0x222; ///< 2.2.2: baud 57600 and 115200.
inline constexpr uint16_t kFw240 = 0x240; ///< 2.4.0: regs 45 to 48, 51, 52, 519, 520.
inline constexpr uint16_t kFw255 = 0x255; ///< 2.5.5: register 53.
inline constexpr uint16_t kFw300 =
    0x300; ///< 3.0.0: block 2100 to 2109, 4200 to 4215, 54 to 56, 521, 524.
inline constexpr uint16_t kFw305 = 0x305; ///< 3.0.5: gas table and `GS *`.

/** @brief How a register may be accessed. */
enum class Access : uint8_t {
  Read = 1,      ///< Function 3 only.
  Write = 2,     ///< Function 6 or 16 only.
  ReadWrite = 3, ///< Both.
};

/** @brief One row of the register table: the only registers the driver touches. */
struct RegisterSpec {
  uint16_t address;   ///< First register.
  uint16_t count;     ///< Number of registers of the item.
  Access access;      ///< Allowed functions.
  uint16_t min_fw;    ///< Firmware gate, 0 for none.
  uint32_t min_value; ///< Lowest value a write may carry (32-bit items use the combined value).
  uint32_t max_value; ///< Highest value a write may carry.
  uint32_t magic;     ///< When not 0xFFFFFFFF the only value a write may carry.
};

/** @brief Marker for "no magic value". */
inline constexpr uint32_t kNoMagic = 0xFFFF'FFFFU;

/** @brief Number of rows of `kRegisters`. */
inline constexpr std::size_t kRegisterRows = 39;

/** @brief The register table (M pp.18 to 27). Reads may span consecutive rows; writes must match
 * one row. */
inline constexpr std::array<RegisterSpec, kRegisterRows> kRegisters{{{.address = 21,
                                                                      .count = 1,
                                                                      .access = Access::ReadWrite,
                                                                      .min_fw = 0,
                                                                      .min_value = 0,
                                                                      .max_value = 5,
                                                                      .magic = kNoMagic},
                                                                     {.address = 25,
                                                                      .count = 1,
                                                                      .access = Access::Read,
                                                                      .min_fw = 0,
                                                                      .min_value = 0,
                                                                      .max_value = 0,
                                                                      .magic = kNoMagic},
                                                                     {.address = 26,
                                                                      .count = 6,
                                                                      .access = Access::Read,
                                                                      .min_fw = 0,
                                                                      .min_value = 0,
                                                                      .max_value = 0,
                                                                      .magic = kNoMagic},
                                                                     {.address = 32,
                                                                      .count = 1,
                                                                      .access = Access::Read,
                                                                      .min_fw = 0,
                                                                      .min_value = 0,
                                                                      .max_value = 0,
                                                                      .magic = kNoMagic},
                                                                     {.address = 35,
                                                                      .count = 2,
                                                                      .access = Access::Read,
                                                                      .min_fw = 0,
                                                                      .min_value = 0,
                                                                      .max_value = 0,
                                                                      .magic = kNoMagic},
                                                                     {.address = 39,
                                                                      .count = 1,
                                                                      .access = Access::Write,
                                                                      .min_fw = 0,
                                                                      .min_value = 0,
                                                                      .max_value = 0xAA55,
                                                                      .magic = 0xAA55},
                                                                     {.address = 45,
                                                                      .count = 1,
                                                                      .access = Access::ReadWrite,
                                                                      .min_fw = kFw240,
                                                                      .min_value = 1,
                                                                      .max_value = 247,
                                                                      .magic = kNoMagic},
                                                                     {.address = 46,
                                                                      .count = 1,
                                                                      .access = Access::ReadWrite,
                                                                      .min_fw = kFw240,
                                                                      .min_value = 65,
                                                                      .max_value = 90,
                                                                      .magic = kNoMagic},
                                                                     {.address = 47,
                                                                      .count = 2,
                                                                      .access = Access::Read,
                                                                      .min_fw = kFw240,
                                                                      .min_value = 0,
                                                                      .max_value = 0,
                                                                      .magic = kNoMagic},
                                                                     {.address = 49,
                                                                      .count = 1,
                                                                      .access = Access::Read,
                                                                      .min_fw = 0,
                                                                      .min_value = 0,
                                                                      .max_value = 0,
                                                                      .magic = kNoMagic},
                                                                     {.address = 51,
                                                                      .count = 1,
                                                                      .access = Access::ReadWrite,
                                                                      .min_fw = kFw240,
                                                                      .min_value = 1,
                                                                      .max_value = 65535,
                                                                      .magic = kNoMagic},
                                                                     {.address = 52,
                                                                      .count = 1,
                                                                      .access = Access::ReadWrite,
                                                                      .min_fw = kFw240,
                                                                      .min_value = 0,
                                                                      .max_value = 3000,
                                                                      .magic = kNoMagic},
                                                                     {.address = 53,
                                                                      .count = 1,
                                                                      .access = Access::Write,
                                                                      .min_fw = kFw255,
                                                                      .min_value = 0,
                                                                      .max_value = 0xAA55,
                                                                      .magic = 0xAA55},
                                                                     {.address = 54,
                                                                      .count = 1,
                                                                      .access = Access::ReadWrite,
                                                                      .min_fw = kFw300,
                                                                      .min_value = 0,
                                                                      .max_value = 3,
                                                                      .magic = kNoMagic},
                                                                     {.address = 55,
                                                                      .count = 1,
                                                                      .access = Access::ReadWrite,
                                                                      .min_fw = kFw300,
                                                                      .min_value = 0,
                                                                      .max_value = 2500,
                                                                      .magic = kNoMagic},
                                                                     {.address = 56,
                                                                      .count = 1,
                                                                      .access = Access::ReadWrite,
                                                                      .min_fw = kFw300,
                                                                      .min_value = 0,
                                                                      .max_value = 1,
                                                                      .magic = kNoMagic},
                                                                     {.address = 80,
                                                                      .count = 1,
                                                                      .access = Access::Write,
                                                                      .min_fw = 0,
                                                                      .min_value = 0,
                                                                      .max_value = 0x5214,
                                                                      .magic = 0x5214},
                                                                     {.address = 81,
                                                                      .count = 60,
                                                                      .access = Access::Read,
                                                                      .min_fw = kFw305,
                                                                      .min_value = 0,
                                                                      .max_value = 0,
                                                                      .magic = kNoMagic},
                                                                     {.address = 514,
                                                                      .count = 1,
                                                                      .access = Access::ReadWrite,
                                                                      .min_fw = 0,
                                                                      .min_value = 0,
                                                                      .max_value = 5000,
                                                                      .magic = kNoMagic},
                                                                     {.address = 515,
                                                                      .count = 1,
                                                                      .access = Access::ReadWrite,
                                                                      .min_fw = 0,
                                                                      .min_value = 0,
                                                                      .max_value = 1,
                                                                      .magic = kNoMagic},
                                                                     {.address = 516,
                                                                      .count = 1,
                                                                      .access = Access::ReadWrite,
                                                                      .min_fw = 0,
                                                                      .min_value = 0,
                                                                      .max_value = 2,
                                                                      .magic = kNoMagic},
                                                                     {.address = 519,
                                                                      .count = 1,
                                                                      .access = Access::ReadWrite,
                                                                      .min_fw = kFw240,
                                                                      .min_value = 0,
                                                                      .max_value = 65535,
                                                                      .magic = kNoMagic},
                                                                     {.address = 520,
                                                                      .count = 1,
                                                                      .access = Access::ReadWrite,
                                                                      .min_fw = kFw240,
                                                                      .min_value = 0,
                                                                      .max_value = 65535,
                                                                      .magic = kNoMagic},
                                                                     {.address = 521,
                                                                      .count = 2,
                                                                      .access = Access::ReadWrite,
                                                                      .min_fw = kFw300,
                                                                      .min_value = 0,
                                                                      .max_value = 0xFFFF'FFFFU,
                                                                      .magic = kNoMagic},
                                                                     {.address = 524,
                                                                      .count = 2,
                                                                      .access = Access::ReadWrite,
                                                                      .min_fw = kFw300,
                                                                      .min_value = 0,
                                                                      .max_value = 1'000'000'000U,
                                                                      .magic = kNoMagic},
                                                                     {.address = 2053,
                                                                      .count = 2,
                                                                      .access = Access::ReadWrite,
                                                                      .min_fw = kFw210,
                                                                      .min_value = 0,
                                                                      .max_value = 0x7FFF'FFFFU,
                                                                      .magic = kNoMagic},
                                                                     {.address = 2100,
                                                                      .count = 1,
                                                                      .access = Access::ReadWrite,
                                                                      .min_fw = kFw300,
                                                                      .min_value = 0,
                                                                      .max_value = 255,
                                                                      .magic = kNoMagic},
                                                                     {.address = 2101,
                                                                      .count = 1,
                                                                      .access = Access::Read,
                                                                      .min_fw = kFw300,
                                                                      .min_value = 0,
                                                                      .max_value = 0,
                                                                      .magic = kNoMagic},
                                                                     {.address = 2102,
                                                                      .count = 1,
                                                                      .access = Access::Read,
                                                                      .min_fw = kFw300,
                                                                      .min_value = 0,
                                                                      .max_value = 0,
                                                                      .magic = kNoMagic},
                                                                     {.address = 2103,
                                                                      .count = 1,
                                                                      .access = Access::Read,
                                                                      .min_fw = kFw300,
                                                                      .min_value = 0,
                                                                      .max_value = 0,
                                                                      .magic = kNoMagic},
                                                                     {.address = 2104,
                                                                      .count = 2,
                                                                      .access = Access::Read,
                                                                      .min_fw = kFw300,
                                                                      .min_value = 0,
                                                                      .max_value = 0,
                                                                      .magic = kNoMagic},
                                                                     {.address = 2106,
                                                                      .count = 1,
                                                                      .access = Access::Read,
                                                                      .min_fw = kFw300,
                                                                      .min_value = 0,
                                                                      .max_value = 0,
                                                                      .magic = kNoMagic},
                                                                     {.address = 2107,
                                                                      .count = 1,
                                                                      .access = Access::Read,
                                                                      .min_fw = kFw300,
                                                                      .min_value = 0,
                                                                      .max_value = 0,
                                                                      .magic = kNoMagic},
                                                                     {.address = 2108,
                                                                      .count = 2,
                                                                      .access = Access::Read,
                                                                      .min_fw = kFw300,
                                                                      .min_value = 0,
                                                                      .max_value = 0,
                                                                      .magic = kNoMagic},
                                                                     {.address = 4200,
                                                                      .count = 1,
                                                                      .access = Access::ReadWrite,
                                                                      .min_fw = kFw300,
                                                                      .min_value = 0,
                                                                      .max_value = 7,
                                                                      .magic = kNoMagic},
                                                                     {.address = 4201,
                                                                      .count = 1,
                                                                      .access = Access::ReadWrite,
                                                                      .min_fw = kFw300,
                                                                      .min_value = 1,
                                                                      .max_value = 65535,
                                                                      .magic = kNoMagic},
                                                                     {.address = 4202,
                                                                      .count = 8,
                                                                      .access = Access::Read,
                                                                      .min_fw = kFw300,
                                                                      .min_value = 0,
                                                                      .max_value = 0,
                                                                      .magic = kNoMagic},
                                                                     {.address = 4210,
                                                                      .count = 4,
                                                                      .access = Access::Read,
                                                                      .min_fw = kFw300,
                                                                      .min_value = 0,
                                                                      .max_value = 0,
                                                                      .magic = kNoMagic},
                                                                     {.address = 4214,
                                                                      .count = 2,
                                                                      .access = Access::Read,
                                                                      .min_fw = kFw300,
                                                                      .min_value = 0,
                                                                      .max_value = 0,
                                                                      .magic = kNoMagic}}};

/** @brief Flow unit labels in the order of register 49 (M p.26, p.31). */
inline constexpr std::array<const char*, 20> kFlowUnitLabels{
    "SCCM", "NCCM", "SLPM",  "NLPM",  "SmL/s", "NmL/s", "SmL/m", "NmL/m", "SL/h", "NL/h",
    "SCCS", "NCCS", "Sm3/h", "Nm3/h", "Sm3/d", "Nm3/d", "SCIM",  "SCFM",  "SCFH", "SCFD"};

/** @brief The flow unit of a Modbus code or label. @{ */
/** @param code Register 49 value. @return The unit, or `Other` for a code of 20 or more. */
[[nodiscard]] constexpr FlowUnit FlowUnitFromCode(uint16_t code) noexcept {
  return (code < 20U) ? static_cast<FlowUnit>(code) : FlowUnit::Other;
}
/** @} */

/** @} */

} // namespace alicat_basis2::reg

#endif // ALICAT_BASIS2_REGISTERS_HPP
