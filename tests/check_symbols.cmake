# Fails if the parser library refers to the heap, C++ exceptions or RTTI.
# Run as: cmake -DNM=<nm> -DLIB=<libnmea_guard.a> -P check_symbols.cmake
execute_process(COMMAND ${NM} -C --undefined-only ${LIB}
                OUTPUT_VARIABLE undefined RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "nm failed on ${LIB}")
endif()
foreach(pattern
    "malloc" "calloc" "realloc" "free$" "operator new" "operator delete"
    "__cxa_throw" "__cxa_allocate_exception" "__gxx_personality" "typeinfo" "dynamic_cast")
  string(REGEX MATCH "[^\n]*${pattern}[^\n]*" hit "${undefined}")
  if(hit)
    message(FATAL_ERROR "library references forbidden symbol: ${hit}")
  endif()
endforeach()
message(STATUS "no heap, exception or RTTI symbols in ${LIB}")
