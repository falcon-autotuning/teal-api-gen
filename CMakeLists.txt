cmake_minimum_required(VERSION 3.20)

project(
  teal-api-gen
  VERSION 1.0.0
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build Options
option(BUILD_TESTS "Build tests" OFF)
option(USE_CCACHE "Enable ccache or sccache if available" ON)
option(ENABLE_PCH "Enable target_precompile_headers to speed up builds" ON)

# Windows-specific definitions
if(WIN32)
  add_compile_definitions(NOMINMAX)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# Try to auto-detect a compiler launcher (prefer sccache if present)
if(USE_CCACHE)
  find_program(SCCACHE_PROGRAM sccache)
  if(SCCACHE_PROGRAM)
    message(STATUS "Using sccache: ${SCCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER ${SCCACHE_PROGRAM})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${SCCACHE_PROGRAM})
  else()
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
      message(STATUS "Using ccache: ${CCACHE_PROGRAM}")
      set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
      set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
    else()
      message(STATUS "No ccache/sccache found (fine, builds will just be normal).")
    endif()
  endif()
endif()

find_package(yaml-cpp CONFIG REQUIRED)

# Core library
add_library(teal-api-gen SHARED
  src/convert.cpp
)

target_include_directories(teal-api-gen
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated  # Add generated headers
)

target_link_libraries(teal-api-gen
  PUBLIC
    yaml-cpp::yaml-cpp
)
target_compile_definitions(teal-api-gen PRIVATE teal_api_EXPORTS)

# Compiler warnings
if(MSVC)
  target_compile_options(teal-api-gen PRIVATE /W4)
else()
  target_compile_options(teal-api-gen PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Tests
if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# CLI Executable
add_executable(teal-api-gen-cli src/cli.cpp)
target_link_libraries(teal-api-gen-cli PRIVATE teal-api-gen)

# ── Install ───────────────────────────────────────────────────────────────────
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS teal-api-gen
  EXPORT teal-api-gen-targets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install CLI separately WITHOUT exporting it
install(TARGETS teal-api-gen-cli
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/teal-api-gen
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING
    PATTERN "*.hpp"
    PATTERN "*.h"
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/teal-api-gen-config-version.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY AnyNewerVersion
)

install(EXPORT teal-api-gen-targets
  FILE      teal-api-gen-targets.cmake
  NAMESPACE teal-api-gen::
  DESTINATION ${CMAKE_INSTALL_DATADIR}/teal-api-gen
)
configure_package_config_file(
  cmake/teal-api-gen-config.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/teal-api-gen-config.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_DATADIR}/teal-api-gen
)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/teal-api-gen-config.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/teal-api-gen-config-version.cmake"
  DESTINATION ${CMAKE_INSTALL_DATADIR}/teal-api-gen
)
