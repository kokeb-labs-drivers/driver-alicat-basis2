/**
 * @file guarded_without_token.cpp
 * @brief Must NOT compile: FACTORY RESTORE can strand an instrument, so the call needs its
 * confirmation token and a call without it is a build error.
 *
 * EXPECT: FactoryRestore
 */
#include "mock_basis2.hpp"

int main() {
  model::FullRig rig;
  const auto r = rig.drv.FactoryRestore();
  return static_cast<int>(r.error);
}
