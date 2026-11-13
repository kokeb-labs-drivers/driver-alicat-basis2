/**
 * @file test_kconfig.cpp
 * @brief KconfigDefaults(): the defaults without Kconfig, and menuconfig values when they are set.
 *
 * Built twice: without CONFIG_ macros (KCONFIG_SET undefined) and with the values an sdkconfig.h
 * would define (KCONFIG_SET=1, see tests/CMakeLists.txt).
 */
#include "alicat_basis2_kconfig.hpp"
#include "test_harness.hpp"

#ifdef KCONFIG_SET
TEST_CASE("Kconfig values reach the Config") {
  constexpr alicat_basis2::Config kC = alicat_basis2::KconfigDefaults();
  CHECK(kC.protocol == alicat_basis2::Protocol::ModbusRtu);
  CHECK_EQ(kC.baud, 115200U);
  CHECK_EQ(kC.processing_ms, 80U);
  CHECK_EQ(kC.slow_command_ms, 1500U);
  CHECK_EQ(kC.read_retries, 2U);
  CHECK(kC.verify_setpoint == alicat_basis2::Config{}.verify_setpoint); // not a Kconfig option
}
#else
TEST_CASE("Without Kconfig, KconfigDefaults() equals Config{}") {
  constexpr alicat_basis2::Config kC = alicat_basis2::KconfigDefaults();
  constexpr alicat_basis2::Config kD{};
  CHECK(kC.protocol == kD.protocol);
  CHECK_EQ(kC.baud, kD.baud);
  CHECK_EQ(kC.processing_ms, kD.processing_ms);
  CHECK_EQ(kC.slow_command_ms, kD.slow_command_ms);
  CHECK_EQ(kC.read_retries, kD.read_retries);
  CHECK(alicat_basis2::Validate(kC) == alicat_basis2::Error::Ok);
}
#endif
