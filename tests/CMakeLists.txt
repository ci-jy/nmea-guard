find_package(GTest QUIET)
if(NOT GTest_FOUND)
  include(FetchContent)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
  add_library(GTest::gtest_main ALIAS gtest_main)
endif()

include(GoogleTest)

add_executable(nmea_guard_tests
  test_framer.cpp
  test_fields.cpp
  test_sentences.cpp
  test_fix.cpp
  test_overflow.cpp
)
target_link_libraries(nmea_guard_tests PRIVATE nmea_guard GTest::gtest_main)
gtest_discover_tests(nmea_guard_tests)

# Separate binary because it replaces the global allocator to count calls.
add_executable(nmea_guard_heap_test test_no_heap.cpp)
target_link_libraries(nmea_guard_heap_test PRIVATE nmea_guard GTest::gtest_main)
gtest_discover_tests(nmea_guard_heap_test)

# Link-level proof of the no-heap / no-exceptions / no-RTTI promise.
if(CMAKE_NM)
  add_test(NAME library_has_no_heap_or_exception_symbols
    COMMAND ${CMAKE_COMMAND} -DNM=${CMAKE_NM} -DLIB=$<TARGET_FILE:nmea_guard>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_symbols.cmake)
endif()

# The bare-metal test-vector runner from firmware/, built for the host. The
# same source runs on QEMU's Cortex-M3 with `make -C firmware qemu-test`.
add_executable(firmware_runner_host
  ${PROJECT_SOURCE_DIR}/firmware/runner.cpp
  ${PROJECT_SOURCE_DIR}/firmware/vectors.cpp
  ${PROJECT_SOURCE_DIR}/firmware/host_io.cpp)
target_link_libraries(firmware_runner_host PRIVATE nmea_guard)
add_test(NAME firmware_vectors_host COMMAND firmware_runner_host)
