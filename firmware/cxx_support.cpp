// The one piece of libstdc++ the library reaches for: string_view::substr()
// checks its position and, with exceptions off, would still pull in
// libstdc++'s throw helper and newlib's abort(), signal and sbrk code. The
// parser never passes a position past the end, so replace the helper with a
// trap; startup.c turns the fault into a failed run.
namespace std {

[[noreturn]] void __throw_out_of_range_fmt(const char*, ...) { __builtin_trap(); }

}  // namespace std
