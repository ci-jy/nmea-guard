cmake_minimum_required(VERSION 3.16)
project(nmea_guard VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(NMEA_GUARD_BUILD_TOOLS "Build the nmea-replay CLI" ON)
option(NMEA_GUARD_BUILD_TESTS "Build the GoogleTest suite" ON)
option(NMEA_GUARD_BUILD_FUZZERS "Build the libFuzzer targets in fuzz/ (Clang only)" OFF)

if(NMEA_GUARD_BUILD_FUZZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "NMEA_GUARD_BUILD_FUZZERS needs Clang; see scripts/fuzz.sh")
  endif()
  # Everything, the library included, gets ASan, UBSan and fuzzer coverage.
  # UBSan's vptr check needs RTTI, which the library is built without.
  add_compile_options(-fsanitize=address,undefined,fuzzer-no-link
                      -fno-sanitize=vptr -fno-sanitize-recover=all -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined -fno-sanitize=vptr)
endif()

# The library itself is written for firmware: no exceptions, no RTTI, and
# nothing that touches the heap.
add_library(nmea_guard
  src/framer.cpp
  src/fields.cpp
  src/sentences.cpp
  src/fix.cpp
  src/parser.cpp
)
add_library(nmea_guard::nmea_guard ALIAS nmea_guard)
target_include_directories(nmea_guard PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(nmea_guard PRIVATE
    -fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

if(NMEA_GUARD_BUILD_TOOLS)
  add_executable(nmea-replay tools/nmea_replay.cpp)
  target_link_libraries(nmea-replay PRIVATE nmea_guard)
  target_include_directories(nmea-replay PRIVATE tools)
endif()

if(NMEA_GUARD_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(NMEA_GUARD_BUILD_TESTS OR NMEA_GUARD_BUILD_FUZZERS)
  add_subdirectory(fuzz)
endif()
