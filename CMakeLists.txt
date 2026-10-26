# SPDX-FileCopyrightText: 2025–2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.16)

# Source of truth: top-level VERSION (e.g. "0.1.0-dev"), or -DPROJECT_VERSION_FULL=...
# from packaging (Nix appends .<revCount>+g<rev> for -dev builds).
if(NOT DEFINED PROJECT_VERSION_FULL)
  file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" PROJECT_VERSION_FULL LIMIT_COUNT 1)
endif()
# project() needs a numeric version; strip -dev / +git suffixes.
string(REGEX MATCH "^[0-9]+(\\.[0-9]+)*" _project_version_cmake "${PROJECT_VERSION_FULL}")
if(_project_version_cmake STREQUAL "")
  set(_project_version_cmake "0.0.0")
endif()

project(hostages VERSION ${_project_version_cmake} LANGUAGES CXX)

message(STATUS "Hostages version: ${PROJECT_VERSION_FULL} (CMake ${PROJECT_VERSION})")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SDL2 REQUIRED)
include(GNUInstallDirs)

add_library(m68k STATIC
  src/m68k/cpu.cpp
  src/m68k/cpu.hpp
  src/m68k/ops.hpp
)
target_include_directories(m68k PUBLIC src)

add_library(amiga STATIC
  src/amiga/machine.cpp
  src/amiga/machine.hpp
  src/amiga/custom.cpp
  src/amiga/cia.cpp
  src/amiga/cia.hpp
  src/amiga/disk.cpp
  src/amiga/disk.hpp
  src/amiga/hle.cpp
  src/amiga/hle.hpp
  src/amiga/hunk.hpp
)
target_link_libraries(amiga PUBLIC m68k)

add_executable(hostages src/main.cpp)
target_link_libraries(hostages PRIVATE amiga SDL2::SDL2)
target_compile_definitions(hostages PRIVATE
  HOSTAGES_DATADIR="${CMAKE_INSTALL_FULL_DATADIR}/hostages"
  HOSTAGES_VERSION="${PROJECT_VERSION_FULL}"
)

# CPU conformance test against the SingleStepTests 68000 suite:
#   ./build/cpu_sst path/to/m68000/v1/*.json.bin
add_executable(cpu_sst tests/cpu_sst.cpp)
target_link_libraries(cpu_sst PRIVATE m68k)

install(TARGETS hostages RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
