/**
 * @file enum_last_non_enum.cpp
 * @brief Must NOT compile: `detail::EnumLast` names the last enumerator of an enumeration, so an
 * integer type does not satisfy its constraint (handbook KB-CPP20-005, -026).
 *
 * EXPECT: Enumeration
 */
#include "alicat_basis2_detail.hpp"

int main() {
  const alicat_basis2::detail::EnumLast<int>* last = nullptr;
  return last == nullptr ? 0 : 1;
}
