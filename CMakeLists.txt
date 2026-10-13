cmake_minimum_required(VERSION 3.16)
project(hostages LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
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
)
target_link_libraries(amiga PUBLIC m68k)

add_executable(hostages src/main.cpp)
target_link_libraries(hostages PRIVATE amiga SDL2::SDL2)
target_compile_definitions(hostages PRIVATE
  HOSTAGES_DATADIR="${CMAKE_INSTALL_FULL_DATADIR}/hostages")

# CPU conformance test against the SingleStepTests 68000 suite:
#   ./build/cpu_sst path/to/m68000/v1/*.json.bin
add_executable(cpu_sst tests/cpu_sst.cpp)
target_link_libraries(cpu_sst PRIVATE m68k)

install(TARGETS hostages RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
