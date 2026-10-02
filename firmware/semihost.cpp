// ARM semihosting for Cortex-M: the debugger (here QEMU with
// -semihosting-config enable=on) traps "bkpt 0xAB" and performs the call in
// r0 with the argument block in r1.
#include "platform.hpp"

namespace runner {
namespace {

constexpr std::uint32_t kSysWrite0 = 0x04;
constexpr std::uint32_t kSysExit = 0x18;
constexpr std::uint32_t kSysExitExtended = 0x20;
constexpr std::uint32_t kApplicationExit = 0x20026;

std::uint32_t semihost(std::uint32_t op, const void* arg) {
    register std::uint32_t r0 __asm__("r0") = op;
    register const void* r1 __asm__("r1") = arg;
    __asm__ volatile("bkpt 0xAB" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}

}  // namespace

void write(const char* text) { semihost(kSysWrite0, text); }

void finish(int status) {
    // SYS_EXIT_EXTENDED passes the status through as QEMU's exit code.
    const std::uint32_t block[2] = {kApplicationExit, static_cast<std::uint32_t>(status)};
    semihost(kSysExitExtended, block);
    semihost(kSysExit, reinterpret_cast<const void*>(kApplicationExit));
    for (;;) {
    }
}

// Paint the stack below the current frame, run `fn`, and see how far down
// the paint was overwritten. Nothing else runs (no interrupts are enabled),
// so everything below sp belongs to `fn`.
std::size_t stack_used(void (*fn)(void*), void* arg) {
    constexpr std::size_t kWindow = 8192;
    constexpr std::uint32_t kPaint = 0xA5A5A5A5u;
    std::uint32_t sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    // Leave 64 bytes for this function's own calls into fn.
    volatile std::uint32_t* top = reinterpret_cast<std::uint32_t*>((sp - 64) & ~3u);
    volatile std::uint32_t* bottom = top - kWindow / 4;
    for (volatile std::uint32_t* p = bottom; p < top; ++p) *p = kPaint;
    fn(arg);
    volatile std::uint32_t* p = bottom;
    while (p < top && *p == kPaint) ++p;
    return static_cast<std::size_t>(sp - reinterpret_cast<std::uintptr_t>(p));
}

}  // namespace runner

// For startup.c.
extern "C" void platform_write(const char* text) { runner::write(text); }
extern "C" [[noreturn]] void platform_exit(int status) { runner::finish(status); }
