// The little the runner needs from wherever it runs: a way to print and a
// way to stop with a status. firmware/semihost.cpp implements it over ARM
// semihosting for QEMU, host_io.cpp over stdio for the host build.
#pragma once

#include <cstddef>
#include <cstdint>

namespace runner {

void write(const char* text);
[[noreturn]] void finish(int status);

// Bytes of stack below the caller that `fn` touched, or 0 where that can't be
// measured (the host build).
std::size_t stack_used(void (*fn)(void*), void* arg);

}  // namespace runner
