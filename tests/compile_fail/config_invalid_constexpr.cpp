/**
 * @file config_invalid_constexpr.cpp
 * @brief Must NOT compile: a configuration that `Validate()` rejects fails the build when it is
 * checked at compile time (handbook KB-CPP20-013).
 *
 * EXPECT: static assertion failed|static_assert
 */
#include "alicat_basis2_types.hpp"

namespace {
constexpr alicat_basis2::Config kBoard{.unit_id = '@'}; // the unit ID is a letter A to Z (M p.10)
static_assert(alicat_basis2::Validate(kBoard) == alicat_basis2::Error::Ok,
              "the unit ID must be A to Z");
} // namespace

int main() {
  return static_cast<int>(kBoard.unit_id);
}
