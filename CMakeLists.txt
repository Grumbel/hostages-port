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

# --- SDL2 ---
if(EMSCRIPTEN)
  if(SDL2_ROOT)
    if(NOT EXISTS "${SDL2_ROOT}/lib/libSDL2.a" OR NOT EXISTS "${SDL2_ROOT}/include/SDL2/SDL.h")
      message(FATAL_ERROR "SDL2_ROOT=${SDL2_ROOT} has no lib/libSDL2.a or include/SDL2/SDL.h")
    endif()
    add_library(SDL2::SDL2 STATIC IMPORTED)
    set_target_properties(SDL2::SDL2 PROPERTIES
      IMPORTED_LOCATION "${SDL2_ROOT}/lib/libSDL2.a"
      INTERFACE_INCLUDE_DIRECTORIES "${SDL2_ROOT}/include/SDL2")
  else()
    add_library(SDL2::SDL2 INTERFACE IMPORTED)
    set_target_properties(SDL2::SDL2 PROPERTIES
      INTERFACE_COMPILE_OPTIONS "-sUSE_SDL=2"
      INTERFACE_LINK_OPTIONS "-sUSE_SDL=2")
  endif()
else()
  find_package(SDL2 QUIET CONFIG)
  if(NOT TARGET SDL2::SDL2)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET GLOBAL sdl2)
    add_library(SDL2::SDL2 ALIAS PkgConfig::SDL2)
  endif()
endif()

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

if(HOSTAGES_CXXABI_SHIM)
  target_sources(hostages PRIVATE "${HOSTAGES_CXXABI_SHIM}")
endif()
target_compile_definitions(hostages PRIVATE
  HOSTAGES_VERSION="${PROJECT_VERSION_FULL}"
)
if(NOT EMSCRIPTEN AND NOT ANDROID)
  target_compile_definitions(hostages PRIVATE
    HOSTAGES_DATADIR="${CMAKE_INSTALL_FULL_DATADIR}/hostages")
else()
  # Packaged assets are looked up relative to the executable / virtual FS.
  target_compile_definitions(hostages PRIVATE HOSTAGES_DATADIR=".")
endif()

if(WIN32)
  if(TARGET SDL2::SDL2main)
    target_link_libraries(hostages PRIVATE SDL2::SDL2main)
  endif()
  if(MINGW)
    target_link_options(hostages PRIVATE -static-libgcc -static-libstdc++)
  endif()
endif()

if(EMSCRIPTEN)
  set_target_properties(hostages PROPERTIES
    OUTPUT_NAME "hostages"
    SUFFIX ".html")
  target_link_options(hostages PRIVATE
    "SHELL:-s ALLOW_MEMORY_GROWTH=1"
    "SHELL:-s FORCE_FILESYSTEM=1"
    "SHELL:-s EXIT_RUNTIME=0"
    "SHELL:-s ASSERTIONS=0"
    "SHELL:-s MAX_WEBGL_VERSION=0"
    "SHELL:-s ENVIRONMENT=web"
    "SHELL:-lidbfs.js"
    "SHELL:--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/mk/wasm/shell.html"
  )
  # Asyncify not required: step_frame returns each frame.
endif()

# CPU conformance test against the SingleStepTests 68000 suite:
#   ./build/cpu_sst path/to/m68000/v1/*.json.bin
if(NOT EMSCRIPTEN AND NOT ANDROID)
  add_executable(cpu_sst tests/cpu_sst.cpp)
  target_link_libraries(cpu_sst PRIVATE m68k)
endif()

if(NOT EMSCRIPTEN AND NOT ANDROID)
  install(TARGETS hostages RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES data/hostages.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES data/icons/hicolor/scalable/apps/hostages.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
  install(FILES data/metainfo/org.grumbel.Hostages.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/man/hostages.6")
    install(FILES man/hostages.6 DESTINATION ${CMAKE_INSTALL_MANDIR}/man6)
  endif()
endif()

if(EMSCRIPTEN)
  install(TARGETS hostages RUNTIME DESTINATION .)
  # emscripten also emits .js / .wasm next to the .html
  install(CODE [[
    file(GLOB _em_extra "${CMAKE_BINARY_DIR}/hostages.js" "${CMAKE_BINARY_DIR}/hostages.wasm"
                        "${CMAKE_BINARY_DIR}/hostages.html")
    foreach(f ${_em_extra})
      file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}" TYPE FILE FILES "${f}")
    endforeach()
  ]])
endif()
