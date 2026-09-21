# nmea-guard

nmea-guard is a small C++17 library that parses GPS NMEA 0183 sentences one byte at a time, without touching the heap. It's strict about checksums, so when a serial read loop falls behind and drops bytes, the damaged sentences get rejected instead of turning into a wrong position. I wrote it because a lot of simple parsers only check the checksum when `*hh` sits exactly at the end of the line, and I wanted to see how much broken data that lets through on real receiver logs.

There's also `nmea-replay`, a command-line tool that runs recorded logs through the parser and prints JSON, a seeded fault injector (`tools/corrupt.py`) that damages those logs the way a serial link does, and two libFuzzer targets that run under AddressSanitizer. The code is MIT licensed.

## Running it

You need CMake 3.16 or newer and a C++17 compiler. GoogleTest is used from the system if it's installed, otherwise CMake downloads it. The Python tests need pynmea2 and pytest, and fuzzing needs Clang.

```sh
sudo apt-get install cmake g++ libgtest-dev python3-pip clang libclang-rt-dev
python3 -m pip install -r requirements.txt

bash scripts/fetch_logs.sh          # gpsd's receiver logs -> data/gpsd/
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
python3 -m pytest -q
bash scripts/fuzz.sh 60             # 30 s per fuzz target, ASan + UBSan
```

The logs come from the [gpsd](https://gitlab.com/gpsd/gpsd) regression suite (BSD licence). The fetch script downloads them, they aren't committed here. In firmware, you feed the parser bytes from the UART and read the last complete fix:

```cpp
#include "nmea_guard/parser.hpp"

static nmea_guard::Parser gps;   // fixed size, no heap

void uart_rx_isr(uint8_t byte) {
    if (gps.feed(byte) == nmea_guard::Event::kSentence && gps.has_fix()) {
        const nmea_guard::Fix& fix = gps.fix();   // last complete epoch
        if (fix.valid()) publish(*fix.latitude, *fix.longitude);
    }
}

void uart_overrun_isr() { gps.reset(); }  // drop the partial sentence
```

The rest of the API and the `nmea-replay` options are in [docs/usage.md](docs/usage.md). What the tests cover is in [docs/testing.md](docs/testing.md).

## Does it help

This is the comparison I built it for. The script drops a run of 1 to 8 bytes from a quarter of the clean sentences in every log (roughly what a UART overrun does) and counts how many damaged sentences each parser still accepts:

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

The naive parser in `tools/naive_parser.hpp` is modelled on the line-based code in a lot of example sketches. Other seeds and the exact method are in [docs/overrun-eval.md](docs/overrun-eval.md).

Overruns aren't the only thing that goes wrong on a serial line, so `tools/corrupt.py` injects four kinds of fault, each with a seed so it can be repeated:

```console
$ python3 tools/corrupt.py --fault bit_flip --seed 4 --rate 0.5 data/gpsd/bu353-glonass.log -o flipped.log
{"log": "data/gpsd/bu353-glonass.log", "fault": "bit_flip", "seed": 4, "rate": 0.5, "damaged": 35}
$ diff data/gpsd/bu353-glonass.log flipped.log | tr -d '\r' | sed -n '2p;4p;6p;8p'
< $GNGSA,A,3,03,19,27,13,,,,,,,,,2.75,2.56,1.00*10
< $GNRMC,081411.000,A,5541.1955,N,03737.2872,E,0.60,301.29,260514,,,A*74
> $GNGSA,A,3,03,19,27,13,,,,,,,,,2.75,2.56,1.00*30
> $GNRMC,081411.000,A,5541.1955,N,03737/2872,E,0.60,301.29,260514,,,A*74
```

The first sentence lost a bit in its checksum, the second one in its longitude (a `.` turned into `/`) and in the CR, which became a tab.

`scripts/rejection_eval.py` runs all four over every log with seeds 1 to 3 and writes [results/rejection.json](results/rejection.json):

| fault | damaged sentences | nmea-guard rejected | naive rejected |
|---|---:|---:|---:|
| 1 to 3 dropped bytes | 8,475 | 99.37% (53 got through) | 87.21% (1,084) |
| overrun, read resumes in the next line | 14,141 | 99.96% (6) | 94.12% (832) |
| 1 or 2 flipped bits | 8,359 | 99.46% (45) | 91.10% (744) |
| lost CR/LF | 8,327 | 100% (0) | 99.95% (4) |

The lost-line-end row hides the bigger difference: nmea-guard still gets 8,281 of those 8,327 sentences out intact, the naive parser 296. More on that and on what got through in [docs/corruption-eval.md](docs/corruption-eval.md).

## How it works

Framing is a five-state machine: hunt for `$`, body, first checksum digit, second digit, line end. The checksum is the XOR of everything between `$` and the first `*`, wherever that `*` lands, and after it there have to be exactly two hex digits and then CR or LF. Anything else is a rejection, never "no checksum, accept it". After a rejection the framer goes back to hunting for `$`, so the tail of a broken sentence can't become the start of the next one.

There was one case I didn't expect. Some gpsd logs contain `...*1E$GPRMC,...`, where the line ending got lost after a complete checksum. Throwing away both sentences seemed wrong, so the first one is judged on its checksum and the `$` starts the next.

Fields are validated, not just split on commas. Latitude has to be exactly `ddmm.mmm`, minutes below 60, dates real calendar days. A dropped byte that the checksum misses usually shortens or shifts a field, and this catches most of those. It also rejects placeholder positions like `8960.0000,N` that some receivers send before they have a fix. Those have a correct checksum, and pynmea2 happily accepts them.

Two libFuzzer targets throw arbitrary bytes at the parser and arbitrary text at the field decoder under ASan and UBSan. Apart from crashes, they check that every accepted sentence really has a matching checksum, that values stay in range, and that a `reset()` or a line end always brings the next good sentence through whole. A 60-second run covers about 500,000 inputs through the parser and 17 million through the decoder ([docs/fuzzing.md](docs/fuzzing.md)). The committed corpus is replayed by ctest in normal builds too.

Decimals are parsed without `strtod`, as an integer mantissa divided once by a power of ten. That gives the same double as Python's `float()`, so the differential test against pynmea2 can compare values exactly. GGA, RMC, GSA and VTG for the same time are merged into one fix per epoch.

## Known issues

- XOR has blind spots. All 16 sentences that got past nmea-guard in the seed-1 overrun run lost bytes that XOR to zero, like a pair of blank fields (`,,`), and the fields after them were still well formed. With bit flips, it's two flips that cancel each other out. Catching those would mean knowing each receiver's exact field layout, and receivers don't agree on that.
- Only GGA, RMC, GSA and VTG are decoded. Everything else is framed and checksum-checked, then reported as "other".
- The fault injector only removes or flips bytes. It never inserts them, so a doubled character or noise between two sentences isn't covered. The fuzzer does insert bytes, but it only checks that nothing breaks and doesn't count how many got through.
- The fuzz runs are short (a minute in the acceptance run), and libFuzzer's coverage feedback has a hard time producing a matching XOR checksum. That's the reason for the separate decoder target.
- I've only built and run it on Linux x86-64. Nothing in the code is host-specific, but there's no Cortex-M build yet. Next I'd like an `arm-none-eabi-gcc` build running the tests under QEMU.

Project period: 2026-08-23 to 2026-08-26.
