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
