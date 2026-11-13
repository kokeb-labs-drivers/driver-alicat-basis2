#===============================================================================
# ALICAT_BASIS2 driver: build settings
#===============================================================================
# Read by the root CMakeLists.txt in both of its modes (plain CMake and the
# ESP-IDF component): target name, version, include directories, sources and
# ESP-IDF requirements. This file is the single source
# of the version; tests/CMakeLists.txt checks every other copy against it.
#===============================================================================

include_guard(GLOBAL)

set(ALICAT_BASIS2_TARGET_NAME "alicat_basis2")

#===============================================================================
# Version (single source of truth)
#===============================================================================
set(ALICAT_BASIS2_VERSION_MAJOR 0)
set(ALICAT_BASIS2_VERSION_MINOR 1)
set(ALICAT_BASIS2_VERSION_PATCH 0)
set(ALICAT_BASIS2_VERSION "${ALICAT_BASIS2_VERSION_MAJOR}.${ALICAT_BASIS2_VERSION_MINOR}.${ALICAT_BASIS2_VERSION_PATCH}")

#===============================================================================
# Generated version header (build tree, never the source tree)
#===============================================================================
set(ALICAT_BASIS2_VERSION_TEMPLATE "${CMAKE_CURRENT_LIST_DIR}/../inc/alicat_basis2_version.h.in")
set(ALICAT_BASIS2_VERSION_HEADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/alicat_basis2_generated")
set(ALICAT_BASIS2_VERSION_HEADER "${ALICAT_BASIS2_VERSION_HEADER_DIR}/alicat_basis2_version.h")

# The directory must exist before ESP-IDF's idf_component_register() checks the include paths.
file(MAKE_DIRECTORY "${ALICAT_BASIS2_VERSION_HEADER_DIR}")
configure_file("${ALICAT_BASIS2_VERSION_TEMPLATE}" "${ALICAT_BASIS2_VERSION_HEADER}" @ONLY)

#===============================================================================
# Public include directories, sources, ESP-IDF requirements
#===============================================================================
set(ALICAT_BASIS2_PUBLIC_INCLUDE_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../inc"
    "${ALICAT_BASIS2_VERSION_HEADER_DIR}"
)

# Header-only: the template implementation is src/alicat_basis2.ipp, included by inc/alicat_basis2.hpp.
set(ALICAT_BASIS2_SOURCE_FILES)

set(ALICAT_BASIS2_IDF_REQUIRES)
