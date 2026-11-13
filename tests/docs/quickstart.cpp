/**
 * @file quickstart.cpp
 * @brief The Quickstart page's example, compiled by the host build (never run).
 */
#include "my_port.hpp"

// --8<-- [start:quickstart]
#include "alicat_basis2.hpp"

int read_flow(MyPort& port, double& flow) {
  alicat_basis2::Bus<MyPort> bus(port);
  alicat_basis2::Basis2<MyPort> meter(bus); // unit ID 'A', 38400 baud, ASCII: the factory settings

  if (auto r = meter.EnsureInitialized(); !r) { // 1. the port comes up; no traffic yet
    return static_cast<int>(r.error);
  }
  if (auto r = meter.Begin();
      !r) { // 2. learn the instrument: Timeout means check wiring and unit ID
    return static_cast<int>(r.error);
  }
  const auto f = meter.ReadFlow(); // 3. one poll
  if (!f) {
    return static_cast<int>(f.error);
  }
  flow = f.value.ToDouble(); // in the instrument's flow unit
  return 0;
}
// --8<-- [end:quickstart]
