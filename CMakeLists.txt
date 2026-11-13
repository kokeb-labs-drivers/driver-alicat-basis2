#===============================================================================
# ALICAT_BASIS2 driver: root CMake
#===============================================================================
# One file, two build systems:
#
# - ESP-IDF (ESP_PLATFORM): this folder is an ESP-IDF component. Point
#   EXTRA_COMPONENT_DIRS at it, check it out under components/, or add it as a
#   component-manager git dependency. menuconfig reads _config/Kconfig.
# - Any other CMake build (host, STM32, RP2040, nRF, ...): one INTERFACE target,
#   alicat_basis2, consumed as alicat_basis2::alicat_basis2 both through add_subdirectory() and
#   through find_package(alicat_basis2) after install.
#
#   cmake -S . -B build -DALICAT_BASIS2_BUILD_TESTS=ON
#   cmake --build build && ctest --test-dir build --output-on-failure
#===============================================================================
cmake_minimum_required(VERSION 3.16)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/alicat_basis2_build_settings.cmake)

#===============================================================================
# ESP-IDF component
#===============================================================================
if(ESP_PLATFORM)
    idf_component_register(
        SRCS ${ALICAT_BASIS2_SOURCE_FILES}
        INCLUDE_DIRS ${ALICAT_BASIS2_PUBLIC_INCLUDE_DIRS}
        REQUIRES ${ALICAT_BASIS2_IDF_REQUIRES}
        KCONFIG ${CMAKE_CURRENT_LIST_DIR}/_config/Kconfig
    )
    if(ALICAT_BASIS2_SOURCE_FILES)
        target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
    else()
        target_compile_features(${COMPONENT_LIB} INTERFACE cxx_std_20)
    endif()
    return()
endif()

project(alicat_basis2_driver
    VERSION ${ALICAT_BASIS2_VERSION}
    DESCRIPTION "C++20 driver for the Alicat BASIS 2 gas mass flow meters and controllers (B and BC series) over serial, with the ASCII protocol and Modbus RTU."
    LANGUAGES CXX)

include(GNUInstallDirs)

# ISO C++20 for every target this project builds (tests, fuzzers, examples), never the GNU dialect
# that CMake picks by default (handbook KB-CPP-001, KB-CPP20-001). CXX_EXTENSIONS does not travel
# through an INTERFACE target, so each folder that builds a target sets it as well.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The installed layout keeps the source tree's inc/ + src/ pair, because alicat_basis2.hpp includes
# "../src/alicat_basis2.ipp" relative to itself:
#   <prefix>/include/alicat_basis2/inc/*.hpp, alicat_basis2_version.h
#   <prefix>/include/alicat_basis2/src/alicat_basis2.ipp
set(ALICAT_BASIS2_INSTALL_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}/alicat_basis2/inc")

#===============================================================================
# Target
#===============================================================================
add_library(${ALICAT_BASIS2_TARGET_NAME} INTERFACE)
target_include_directories(${ALICAT_BASIS2_TARGET_NAME}
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/inc>
        $<BUILD_INTERFACE:${ALICAT_BASIS2_VERSION_HEADER_DIR}>
        $<INSTALL_INTERFACE:${ALICAT_BASIS2_INSTALL_INCLUDEDIR}>
)
target_compile_features(${ALICAT_BASIS2_TARGET_NAME} INTERFACE cxx_std_20)

# alicat_basis2::alicat_basis2 everywhere: the alias for add_subdirectory(), EXPORT_NAME for the installed package.
add_library(alicat_basis2::alicat_basis2 ALIAS ${ALICAT_BASIS2_TARGET_NAME})
set_target_properties(${ALICAT_BASIS2_TARGET_NAME} PROPERTIES EXPORT_NAME alicat_basis2)

option(ALICAT_BASIS2_ENABLE_WARNINGS "Add -Wall -Wextra -Wpedantic to every target that links alicat_basis2::alicat_basis2" OFF)
if(ALICAT_BASIS2_ENABLE_WARNINGS)
    target_compile_options(${ALICAT_BASIS2_TARGET_NAME} INTERFACE -Wall -Wextra -Wpedantic)
endif()

#===============================================================================
# Host tests, documentation snippets, install check (tests/)
#===============================================================================
option(ALICAT_BASIS2_BUILD_TESTS "Build the host tests in tests/" OFF)
option(ALICAT_BASIS2_BUILD_FUZZERS "Also build the libFuzzer targets in tests/fuzz/ (Clang only)" OFF)
if(ALICAT_BASIS2_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

#===============================================================================
# Install and package export
#===============================================================================
include(CMakePackageConfigHelpers)

install(TARGETS ${ALICAT_BASIS2_TARGET_NAME} EXPORT alicat_basis2Targets
    INCLUDES DESTINATION ${ALICAT_BASIS2_INSTALL_INCLUDEDIR})

install(DIRECTORY inc/ DESTINATION ${ALICAT_BASIS2_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h")
install(FILES "${ALICAT_BASIS2_VERSION_HEADER}" DESTINATION ${ALICAT_BASIS2_INSTALL_INCLUDEDIR})
install(DIRECTORY src/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/alicat_basis2/src
    FILES_MATCHING PATTERN "*.ipp")

install(EXPORT alicat_basis2Targets
    FILE alicat_basis2Targets.cmake
    NAMESPACE alicat_basis2::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/alicat_basis2)

configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/cmake/alicat_basis2Config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/alicat_basis2Config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/alicat_basis2)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/alicat_basis2ConfigVersion.cmake"
    VERSION ${ALICAT_BASIS2_VERSION}
    COMPATIBILITY SameMajorVersion
    ARCH_INDEPENDENT)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/alicat_basis2Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/alicat_basis2ConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/alicat_basis2)
