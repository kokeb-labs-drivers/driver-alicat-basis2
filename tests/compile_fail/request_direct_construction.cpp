/**
 * @file request_direct_construction.cpp
 * @brief Must NOT compile: a `Request` comes only from the grammar's factory functions, so no code
 * can put bytes outside the grammar on the wire by building one by hand.
 *
 * EXPECT: RequestData
 */
#include "alicat_basis2.hpp"

int main() {
  alicat_basis2::detail::RequestData data{};
  const alicat_basis2::Request request{data};
  return static_cast<int>(request.Bytes().size());
}
