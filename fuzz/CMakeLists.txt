# Two entry points per target:
#   fuzz_<name>         libFuzzer binary, only with NMEA_GUARD_BUILD_FUZZERS=ON
#                       and Clang (scripts/fuzz.sh sets this up)
#   fuzz_<name>_replay  runs the committed corpus once, in any build, as a test
set(NMEA_GUARD_FUZZ_TARGETS parser decode)

foreach(name IN LISTS NMEA_GUARD_FUZZ_TARGETS)
  if(NMEA_GUARD_BUILD_FUZZERS)
    add_executable(fuzz_${name} fuzz_${name}.cpp)
    target_link_libraries(fuzz_${name} PRIVATE nmea_guard)
    target_link_options(fuzz_${name} PRIVATE -fsanitize=fuzzer)
  endif()

  if(NMEA_GUARD_BUILD_TESTS)
    add_executable(fuzz_${name}_replay fuzz_${name}.cpp replay_main.cpp)
    target_link_libraries(fuzz_${name}_replay PRIVATE nmea_guard)
    add_test(NAME fuzz_${name}_corpus
      COMMAND fuzz_${name}_replay ${CMAKE_CURRENT_SOURCE_DIR}/corpus/${name})
  endif()
endforeach()
