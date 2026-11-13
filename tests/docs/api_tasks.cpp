/**
 * @file api_tasks.cpp
 * @brief The API guide's examples, compiled by the host build (never run).
 */
#include <cstdio>

#include "alicat_basis2.hpp"
#include "my_port.hpp"

using Meter = alicat_basis2::Basis2<MyPort>;

// --8<-- [start:lifecycle]
alicat_basis2::Error BringUp(Meter& meter) {
  // EnsureInitialized() starts the port; Begin() learns the instrument (identity, full scale,
  // firmware). Both can be called again: after a failure they start from the beginning.
  auto r = meter.EnsureInitialized();
  if (r) {
    r = meter.Begin();
  }
  if (!r) {
    std::printf("BASIS 2 start-up: %s\n", alicat_basis2::ToString(r.error));
  }
  return r.error;
}
// --8<-- [end:lifecycle]

// --8<-- [start:setpoint]
alicat_basis2::Error RampTo(Meter& meter, double flow) {
  // The unsaved source never wears the non-volatile memory; the saved one is rate limited.
  if (auto r = meter.SetSetpointSource(alicat_basis2::SetpointSource::UnsavedDigital); !r) {
    return r.error;
  }
  return meter.SetSetpoint(flow).error; // read back and compared when no ramp is active
}
// --8<-- [end:setpoint]

// --8<-- [start:guarded]
alicat_basis2::Error MoveToUnit(Meter& meter) {
  // Commands that can strand the instrument need a token that names the consequence.
  return meter.ChangeUnitId('B', alicat_basis2::ConfirmUnitIdChange{}).error;
}
// --8<-- [end:guarded]

// --8<-- [start:errors]
const char* Describe(alicat_basis2::Error e) {
  switch (e) {
  case alicat_basis2::Error::Timeout:
    return "no reply: check wiring, unit ID, baud rate and RS-485 direction control";
  case alicat_basis2::Error::DeviceIdMismatch:
    return "another unit answered: check the unit ID or Modbus address";
  case alicat_basis2::Error::Crc:
    return "line noise: check cabling and termination";
  default:
    return alicat_basis2::ToString(e);
  }
}
// --8<-- [end:errors]

// --8<-- [start:recover]
void Resynchronise(Meter& meter) {
  // After an unexplained failure: find the instrument again (scans unit IDs or Modbus addresses).
  if (auto r = meter.Recover(); !r) {
    std::printf("BASIS 2 recover: %s\n", alicat_basis2::ToString(r.error));
  }
}
// --8<-- [end:recover]
