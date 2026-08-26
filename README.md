# nmea-guard

nmea-guard is a small C++17 library that parses GPS NMEA 0183 sentences one byte at a time, without touching the heap. It's strict about checksums, so when a serial read loop falls behind and drops bytes, the damaged sentences get rejected instead of turning into a wrong position. I wrote it because a lot of simple parsers only check the checksum when `*hh` sits exactly at the end of the line, and I wanted to measure how much broken data that lets through on real receiver logs.

It comes with `nmea-replay`, a command-line tool that runs recorded logs through the parser and prints JSON.

## Running it

You need CMake 3.16 or newer and a C++17 compiler. GoogleTest is taken from the system if it's installed, otherwise CMake downloads it. The Python tests need `pynmea2` and `pytest`.

```sh
sudo apt-get install cmake g++ libgtest-dev python3-pip
python3 -m pip install -r requirements.txt

bash scripts/fetch_logs.sh          # gpsd's receiver logs -> data/gpsd/
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
python3 -m pytest -q
```

The logs come from the gpsd regression suite and aren't committed here. In firmware, you feed it bytes from the UART and read the last complete fix:

```cpp
#include "nmea_guard/parser.hpp"

static nmea_guard::Parser gps;   // 1144 bytes on x86-64, no heap

void uart_rx_isr(uint8_t byte) {
    if (gps.feed(byte) == nmea_guard::Event::kSentence && gps.has_fix()) {
        const nmea_guard::Fix& fix = gps.fix();   // last complete epoch
        if (fix.valid()) publish(*fix.latitude, *fix.longitude);
    }
}

void uart_overrun_isr() { gps.reset(); }  // drop the partial sentence
```

The main result is this comparison. The script drops a run of 1 to 8 bytes from a quarter of the clean sentences in every log (roughly what a UART overrun does) and counts how many damaged sentences each parser still accepts:

```console
$ python3 scripts/overrun_eval.py --seed 1
{
  "logs": 103,
  "damaged": 2816,
  "guard_passed": 16,
  "naive_passed": 292,
  "guard_rejected_pct": 99.43181818181819,
  "naive_rejected_pct": 89.63068181818181,
  "seed": 1,
  "rate": 0.25
}
```

The naive parser in `tools/naive_parser.hpp` is modelled on the line-based code in a lot of example sketches.

Details on the API and `nmea-replay`: [docs/usage.md](docs/usage.md)
How the overrun comparison works, with other seeds: [docs/overrun-eval.md](docs/overrun-eval.md)
What the tests cover: [docs/testing.md](docs/testing.md)

## How it works

Framing is a five-state machine: hunt for `$`, body, first checksum digit, second digit, line end. The checksum is the XOR of everything between `$` and the first `*`, wherever that `*` lands, and after it there have to be exactly two hex digits and then CR or LF. Anything else is a rejection, never "no checksum, accept it". After any rejection the framer goes back to hunting for `$`, so the tail of a broken sentence can't become the start of the next one.

There was one exception I didn't expect. Some gpsd logs contain `...*1E$GPRMC,...`, where the line ending got lost after a complete checksum. Rejecting both sentences there seemed wrong, so the first one is judged on its checksum and the `$` starts the next.

Fields are validated, not just split on commas. Latitude has to be exactly `ddmm.mmm`, minutes below 60, dates real calendar days, and so on. A dropped byte that the checksum misses usually shortens or shifts a field, and this catches most of those. It also rejects placeholder positions some receivers send before they have a fix, like `8960.0000,N`, which have a correct checksum and which pynmea2 happily accepts.

Decimals are parsed without `strtod`: an integer mantissa divided once by a power of ten. That gives the same double Python's `float()` does, so the differential test against pynmea2 can compare exactly instead of "close enough". GGA, RMC, GSA and VTG for the same time get merged into one fix per epoch.

## Known issues

- The XOR checksum has blind spots. All 16 sentences that got past nmea-guard in the seed-1 run lost bytes that XOR to zero, like a pair of blank fields (`,,`), and still had well-formed fields afterwards. Catching those would mean knowing each receiver's exact field layout, and receivers don't agree on that.
- Only GGA, RMC, GSA and VTG are decoded. Everything else is framed and checksum-checked, then reported as "other".
- I've only built and run it on Linux x86-64. Nothing in the code is host-specific, but there's no Cortex-M build yet. That, an `arm-none-eabi-gcc` build running the tests under QEMU, and a fuzzer that also flips bits and inserts bytes are what I'd do next.

## Credits

The receiver logs are from the [gpsd project](https://gitlab.com/gpsd/gpsd) regression suite (BSD licence) and are downloaded by the fetch script, not redistributed. pynmea2 is only used as the reference in the differential test. nmea-guard itself is MIT licensed (see `LICENSE`).

Project period: 2026-08-23 to 2026-08-26.
