# nmea-guard

I dropped runs of 1 to 8 bytes from 2816 GPS sentences in 103 real receiver logs, which is roughly what a UART overrun does. A typical line-based NMEA parser still accepted 292 of the damaged sentences as valid. nmea-guard accepted 16. It's a small C++17 library that parses NMEA 0183 one byte at a time without using the heap, so it can run inside a UART interrupt on a microcontroller.

Most of that gap comes from one shortcut. A lot of simple parsers only check the checksum when `*hh` sits exactly at the end of the line. If it doesn't, they assume the receiver didn't send one. Drop the wrong bytes and a broken sentence turns into a wrong position.

## Reproducing the comparison

You need CMake 3.16 or newer and a C++17 compiler. The receiver logs come from the [gpsd](https://gitlab.com/gpsd/gpsd) regression suite (BSD licence). They aren't committed, so fetch them first:

```sh
python3 -m pip install -r requirements.txt
bash scripts/fetch_logs.sh          # gpsd's receiver logs -> data/gpsd/
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
python3 -m pytest -q
```

Then run the overrun script. The naive parser it compares against is in `tools/naive_parser.hpp` and copies the line-based approach from a lot of example sketches.

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

`tools/corrupt.py` adds other faults too (bit flips, short reads, missing line endings), and `docs/index.html` puts every result on one page ([screenshot](docs/report.png)). How the damage is chosen is in [docs/overrun-eval.md](docs/overrun-eval.md) and [docs/corruption-eval.md](docs/corruption-eval.md). Fuzzing and the Cortex-M3 build need extra packages, listed in [docs/setup.md](docs/setup.md).

## Using it in firmware

Feed it bytes from the UART and read the last complete fix:

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

There's also `nmea-replay`, a command-line tool that runs a recorded log through the parser and prints JSON. The API and its options are in [docs/usage.md](docs/usage.md), and what the tests cover is in [docs/testing.md](docs/testing.md). MIT licensed.

## Inside the parser

Framing is a small state machine: hunt for `$`, read the body, two checksum digits, line end. The checksum is the XOR of everything between `$` and the first `*`, wherever that `*` turns up. After it there have to be exactly two hex digits and then CR or LF, or the sentence is rejected. There's no "no checksum, so accept it" path. After a rejection it goes back to hunting for `$`.

Some gpsd logs surprised me. They contain `...*1E$GPRMC,...`, where a line ending got lost right after a complete checksum. Throwing away both sentences felt wrong, so the first one is judged on its checksum and the `$` starts the next.

Fields get validated, not just split. A dropped byte that slips past the checksum usually shortens or shifts a field, so minutes must be below 60, dates must be real days, and so on. Decimals are parsed without `strtod` and come out as the same double Python's `float()` gives, which let the differential test against pynmea2 use a very tight tolerance. Details are in [docs/design.md](docs/design.md).

The same sources build unchanged for a Cortex-M3 with no FPU. On QEMU all 797 test vectors decoded exactly as pynmea2 does. Getting there was the hard part. `std::string_view::substr` pulled in `abort()` and newlib's `_sbrk` through a libstdc++ range-check helper, so a library with no heap was quietly linking one anyway. `firmware/cxx_support.cpp` replaces that helper with a trap, and the size report now fails if an allocator shows up. Sizes are in [docs/firmware.md](docs/firmware.md).

## What it still misses

- XOR has blind spots. Every one of the 16 sentences that got through in the run above lost bytes that XOR to zero (a pair of blank fields, `,,`, for example) and was still well formed. Catching those needs each receiver's field layout, and receivers don't agree on it.
- Only GGA, RMC, GSA and VTG are decoded. Everything else is checksum-checked and reported as "other".
- QEMU checks the instruction set and the maths, not timing. I haven't measured whether `feed()` keeps up with a UART interrupt on real hardware.
- Over a quarter of the firmware is double-precision soft-float. Fixed-point output would shrink it, but it would change the API.

Project period: 2026-08-23 to 2026-10-13.
