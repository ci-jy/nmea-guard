// Host side of platform.hpp, so the same runner can be checked with ctest
// before it goes anywhere near QEMU.
#include <cstdio>
#include <cstdlib>

#include "platform.hpp"

namespace runner {

void write(const char* text) { std::fputs(text, stdout); }

void finish(int status) {
    std::fflush(stdout);
    std::exit(status);
}

std::size_t stack_used(void (*fn)(void*), void* arg) {
    fn(arg);
    return 0;
}

}  // namespace runner
