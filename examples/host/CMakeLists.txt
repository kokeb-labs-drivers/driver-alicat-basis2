# =============================================================================
# ALICAT_BASIS2 driver: host example (any PC, no hardware)
# =============================================================================
# The driver against a simulated ALICAT_BASIS2 (the register model the host tests
# use, tests/mock_basis2.hpp) through the host port in this folder.
#
#   cmake -S examples/host -B build-example
#   cmake --build build-example && ./build-example/alicat_basis2_host_example
#
# The host tests also build this folder and run the example (CTest
# alicat_basis2_host_example), so it always compiles against the current headers.
# =============================================================================
cmake_minimum_required(VERSION 3.16)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  project(alicat_basis2_host_example LANGUAGES CXX)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. alicat_basis2)
endif()

# ISO C++20, also when this folder is built on its own (handbook KB-CPP20-001).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(alicat_basis2_host_example main.cpp)
target_link_libraries(alicat_basis2_host_example PRIVATE alicat_basis2::alicat_basis2)
target_include_directories(alicat_basis2_host_example PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/../../tests)
target_compile_features(alicat_basis2_host_example PRIVATE cxx_std_20)
target_compile_options(alicat_basis2_host_example PRIVATE -fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic
  -pedantic-errors)
