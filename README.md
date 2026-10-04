# nmea-guard

nmea-guard is a small C++17 library that parses GPS NMEA 0183 sentences one byte at a time, without using the heap. A lot of simple parsers only check the checksum when `*hh` sits exactly at the end of the line, so when a serial read loop falls behind and drops bytes, damaged sentences get through as wrong positions. I wanted a parser that doesn't do that, and a way to measure how often it happens on real receiver logs.

Alongside the library there's `nmea-replay`, a command-line tool that runs recorded logs through the parser and prints JSON, plus a seeded fault injector, two libFuzzer targets and a bare-metal runner for a Cortex-M3 on QEMU. MIT licensed.

## Running it

You need CMake 3.16 or newer and a C++17 compiler. CMake uses the system GoogleTest if you have it and downloads it if you don't. The Python tests need pynmea2 and pytest, fuzzing needs Clang, and the firmware build needs the Arm GNU toolchain and QEMU.

```sh
sudo apt-get install cmake g++ libgtest-dev python3-pip clang libclang-rt-dev \
    gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib qemu-system-arm
python3 -m pip install -r requirements.txt

bash scripts/fetch_logs.sh          # gpsd's receiver logs -> data/gpsd/
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
python3 -m pytest -q
bash scripts/fuzz.sh 60             # 30 s per fuzz target, ASan + UBSan
make -C firmware qemu-test          # Cortex-M3 test vectors on QEMU, then flash/RAM sizes
```

The logs come from the [gpsd](https://gitlab.com/gpsd/gpsd) regression suite (BSD licence). They aren't committed, the fetch script downloads them. In firmware you feed the parser bytes from the UART and read the last complete fix:

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

The API and the `nmea-replay` options are in [docs/usage.md](docs/usage.md), and what the tests cover is in [docs/testing.md](docs/testing.md).

## Does it help

This is the comparison I built it for. The script drops a run of 1 to 8 bytes from a quarter of the clean sentences in every log, which is roughly what a UART overrun does, then counts how many damaged sentences each parser still accepts. The naive parser in `tools/naive_parser.hpp` copies the line-based approach you see in a lot of example sketches.

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

Overruns aren't the only thing that goes wrong on a serial line, so `tools/corrupt.py` also flips bits, cuts reads short and removes line endings. Across all four faults nmea-guard rejects 99.74% of damaged sentences and the naive parser 93.22%. The method and other seeds are in [docs/overrun-eval.md](docs/overrun-eval.md) and [docs/corruption-eval.md](docs/corruption-eval.md), and `docs/index.html` puts everything on one page, generated from `results/`.

## How it works

Framing is a five-state machine: hunt for `$`, body, two checksum digits, line end. The checksum is the XOR of everything between `$` and the first `*`, wherever that `*` turns up, and after it there must be exactly two hex digits and then CR or LF. Anything else is a rejection, never "no checksum, so accept it". After a rejection the framer goes back to hunting for `$`, so the tail of a broken sentence can't become the start of the next one.

One thing surprised me. Some gpsd logs contain `...*1E$GPRMC,...`, where a line ending got lost right after a complete checksum. Throwing away both sentences felt wrong, so the first is judged on its checksum and the `$` starts the next.

Fields are validated, not just split. A dropped byte that the checksum misses usually shortens or shifts a field, so minutes have to be below 60, dates have to be real days, and so on. That catches most of them. Decimals are parsed without `strtod`, which gives the same double as Python's `float()` and lets the differential test against pynmea2 use a very tight tolerance. More detail in [docs/design.md](docs/design.md).

The same sources build unchanged for QEMU's `mps2-an385` board, a Cortex-M3 with no FPU. A runner streams 797 test vectors through one `Parser`, with expected results taken from a Python reference and pynmea2 rather than the C++ code, and every decoded value matched exactly even with software floating point. A minimal UART-to-fix firmware is about 8 KB of flash and 1.1 KB of RAM. The first link was the real surprise: `std::string_view::substr` pulled in `abort()` and newlib's `_sbrk` through a libstdc++ range-check helper, so a library with no heap was quietly linking one. `firmware/cxx_support.cpp` swaps that helper for a trap, and the size report now fails if any allocator or exception symbol shows up ([docs/firmware.md](docs/firmware.md)).

## Known issues

- XOR has blind spots. All 16 sentences that got past nmea-guard in the seed-1 run lost bytes that XOR to zero (a pair of blank fields, `,,`) and the rest was still well formed. Catching those would mean knowing each receiver's field layout, and receivers don't agree on it.
- Only GGA, RMC, GSA and VTG are decoded. Everything else is framed, checksum-checked and reported as "other".
- QEMU checks the instruction set and the maths, not timing. I haven't measured whether `feed()` keeps up with a UART interrupt on real hardware.
- Over a quarter of the firmware is double-precision soft-float. Fixed-point output would shrink it but change the API.

A few smaller limitations are listed at the end of [docs/design.md](docs/design.md).

Project period: 2026-08-23 to 2026-10-07.
