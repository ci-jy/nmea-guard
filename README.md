# nmea-guard

nmea-guard is a small C++17 library that parses GPS NMEA 0183 sentences one byte at a time, without using the heap. A lot of simple parsers only check the checksum when `*hh` sits exactly at the end of the line. So when a serial read loop falls behind and drops bytes, damaged sentences get through as wrong positions. I wanted a parser that doesn't do that, and a way to measure how often it happens on real receiver logs.

It also includes `nmea-replay`, a command-line tool that runs recorded logs through the parser and prints JSON. There's also a fault injector, two libFuzzer targets and a test runner for a Cortex-M3 on QEMU. MIT licensed.

## Running it

You need CMake 3.16 or newer and a C++17 compiler. The receiver logs come from the [gpsd](https://gitlab.com/gpsd/gpsd) regression suite (BSD licence). They aren't committed, so you fetch them first:

```sh
python3 -m pip install -r requirements.txt
bash scripts/fetch_logs.sh          # gpsd's receiver logs -> data/gpsd/
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
python3 -m pytest -q
```

Fuzzing and the Cortex-M3 build need more packages. Those, plus the commands for them, are in [docs/setup.md](docs/setup.md). In firmware you feed the parser bytes from the UART and read the last complete fix:

```cpp
static nmea_guard::Parser gps;   // fixed size, no heap

void uart_rx_isr(uint8_t byte) {
    if (gps.feed(byte) == nmea_guard::Event::kSentence && gps.has_fix()) {
        const nmea_guard::Fix& fix = gps.fix();
        if (fix.valid()) publish(*fix.latitude, *fix.longitude);
    }
}

void uart_overrun_isr() { gps.reset(); }  // drop the partial sentence
```

The API and the `nmea-replay` options are in [docs/usage.md](docs/usage.md). What the tests cover is in [docs/testing.md](docs/testing.md).

## Does it help

This is the comparison I built it for. The script drops a run of 1 to 8 bytes from a quarter of the clean sentences in every log, which is roughly what a UART overrun does. Then it counts how many damaged sentences each parser still accepts. The naive parser in `tools/naive_parser.hpp` copies the line-based approach you see in a lot of example sketches.

```console
$ python3 scripts/overrun_eval.py --seed 1
{
  "logs": 103,
  "damaged": 2816,
  "guard_passed": 16,
  "naive_passed": 292,
  ...
}
```

`tools/corrupt.py` also flips bits, cuts reads short and removes line endings, and `docs/index.html` puts all of it on one page ([screenshot](docs/report.png)). The method is in [docs/overrun-eval.md](docs/overrun-eval.md) and [docs/corruption-eval.md](docs/corruption-eval.md).

## How it works

Framing is a small state machine: hunt for `$`, body, two checksum digits, line end. The checksum is the XOR of everything between `$` and the first `*`, wherever that `*` turns up. After it there must be exactly two hex digits and then CR or LF. Anything else is a rejection. There is no "no checksum, so accept it" path, and after a rejection the framer goes back to hunting for `$`.

One thing surprised me. Some gpsd logs contain `...*1E$GPRMC,...`, where a line ending got lost right after a complete checksum. Throwing away both sentences felt wrong, so the first is judged on its checksum and the `$` starts the next.

Fields are validated, not just split. A dropped byte that the checksum misses usually shortens or shifts a field, so minutes have to be below 60, dates have to be real days, and so on. Decimals are parsed without `strtod`, which gives the same double as Python's `float()`. That let the differential test against pynmea2 use a very tight tolerance. More in [docs/design.md](docs/design.md).

The same sources build unchanged for a Cortex-M3 with no FPU, and on QEMU all 797 test vectors decoded exactly as pynmea2 does. The first link was the real surprise, though. `std::string_view::substr` pulled in `abort()` and newlib's `_sbrk` through a libstdc++ range-check helper, so a library with no heap was quietly linking one. `firmware/cxx_support.cpp` swaps that helper for a trap, and the size report now fails if an allocator shows up. Sizes are in [docs/firmware.md](docs/firmware.md).

## Known issues

- XOR has blind spots. All 16 sentences that got past nmea-guard in the run above lost bytes that XOR to zero (like a pair of blank fields, `,,`) and were still well formed. Catching those would mean knowing each receiver's field layout, and receivers don't agree on it.
- Only GGA, RMC, GSA and VTG are decoded. Everything else is checksum-checked and reported as "other".
- QEMU checks the instruction set and the maths, not timing. I haven't measured whether `feed()` keeps up with a UART interrupt on real hardware.
- Over a quarter of the firmware is double-precision soft-float. Fixed-point output would shrink it but change the API.

Project period: 2026-08-23 to 2026-10-07.
