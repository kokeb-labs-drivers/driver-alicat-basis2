/**
 * @file rig.hpp
 * @brief Small helpers shared by the host tests: a started rig, and a view of the recorded wire.
 */
#ifndef ALICAT_BASIS2_TEST_RIG_HPP
#define ALICAT_BASIS2_TEST_RIG_HPP

#include <string>

#include "mock_basis2.hpp"
#include "test_harness.hpp"

namespace rig {

using alicat_basis2::Error;

/// A started ASCII or Modbus rig: `EnsureInitialized()` and `Begin()` have succeeded.
template <typename R>
[[nodiscard]] inline bool Start(R& r) {
  return r.drv.EnsureInitialized().ok() && r.drv.Begin().ok();
}

/// The recorded writes joined by `|`, without carriage returns.
[[nodiscard]] inline std::string Wire(const model::Instrument& m, std::size_t from = 0) {
  std::string s;
  for (std::size_t i = from; i < m.writes.size(); ++i) {
    std::string t = m.writes[i].text;
    for (char& c : t) {
      if (model::SameChar(c, '\r')) {
        c = '$';
      }
    }
    s += (i > from ? "|" : "") + t;
  }
  return s;
}

/// The last write without its CR.
[[nodiscard]] inline std::string Last(const model::Instrument& m) {
  std::string t = m.writes.empty() ? std::string() : m.writes.back().text;
  if (!t.empty() && (model::SameChar(t.back(), '\r'))) {
    t.pop_back();
  }
  return t;
}

inline constexpr int E(Error e) {
  return static_cast<int>(e);
}

} // namespace rig

#endif // ALICAT_BASIS2_TEST_RIG_HPP
