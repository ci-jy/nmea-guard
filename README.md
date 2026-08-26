# nmea-guard

A small C++17 library that parses GPS NMEA 0183 sentences one byte at a time,
without touching the heap. It is strict about checksums, so when your serial
read loop falls behind and drops bytes, the damaged sentences get rejected
and never turn into a wrong position. It also comes with `nmea-replay`, a
command-line tool that runs recorded receiver logs through the parser and
prints a JSON report.

```console
$ ./build/nmea-replay --sentences data/gpsd/bu353-glonass.log | sed -n 3p
{"file":"data/gpsd/bu353-glonass.log","raw":"$GNRMC,081411.000,A,5541.1955,N,03737.2872,E,0.60,301.29,260514,,,A*74","talker":"GN","type":"RMC","time":"08:14:11.000000","status":"A","latitude":55.686591666666665,"longitude":37.621453333333335,"speed_knots":0.6,"course_deg":301.29,"date":{"day":26,"month":5,"year":14},"magnetic_variation_deg":null,"mode":"A"}
```

It's meant for people writing firmware for trackers, telematics boxes or
navigation devices, and for robotics and hobby teams who wire a GPS module
to a UART and want to trust what comes out of it.

## Why

A GPS module sends a steady stream of text at 9600 baud or more. If the
microcontroller is busy and the UART buffer overruns, characters go missing
from the middle of sentences. You can see what that looks like in
[this Particle Asset Tracker thread](https://community.particle.io/t/default-example-for-asset-tracker-gps-prints-erratic-nmea-sentences-to-serial/21356),
where the example firmware prints chopped-up and run-together sentences.
Every sentence carries a checksum that should catch this. The trouble is that
a lot of simple parsers only check it when `*hh` is exactly where they expect
it, at the end of the line. When the tail of a sentence gets lost, or two
sentences run together, the checksum isn't checked at all and the broken
sentence goes through.

I wanted to measure how big that gap is on real receiver output, and to
write a parser that closes it while still fitting in a few hundred bytes of
RAM.

## Install

You need CMake 3.16 or newer and a C++17 compiler. GoogleTest comes from the
system if it's installed (`libgtest-dev` on Debian/Ubuntu); otherwise CMake
downloads it. The Python tests need `pynmea2` and `pytest`.

```sh
sudo apt-get install cmake g++ libgtest-dev python3-pip
python3 -m pip install -r requirements.txt

bash scripts/fetch_logs.sh          # gpsd's receiver logs -> data/gpsd/
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
python3 -m pytest -q
```

`fetch_logs.sh` downloads the `test/daemon` logs from the gpsd
`release-3.25` tag (173 files, about 4 MB). The logs aren't committed to
this repo. To use the library in another CMake project, either
`add_subdirectory()` this repo and link `nmea_guard::nmea_guard`, or just
drop the five files from `src/` and the headers from `include/` into your
firmware tree.

## Usage

### In firmware

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

`feed()` returns `kSentence` for a decoded GGA/RMC/GSA/VTG, `kOther` for a
valid sentence of some other type (GSV, ZDA, proprietary ones), and
`kRejected` with a reason in `last_frame_error()` or `last_decode_error()`.
Fields that are blank in the sentence come back as empty `std::optional`s.

### Summarise a set of logs

```console
$ ./build/nmea-replay data/gpsd/*.log | tail -2 | head -1
  "total": {"files":173,"bytes":3999727,"accepted":11899,"other":16177,"rejected":7626,"rejected_by_reason":{"bad_address":422,"bad_character":6529,"bad_field":45,"checksum_mismatch":102,"field_count":1,"interrupted":255,"malformed_checksum":72,"missing_checksum":159,"overflow":41},"accepted_by_type":{"GGA":3261,"GSA":3294,"RMC":3075,"VTG":2269},"accepted_by_talker":{"BD":180,"GL":62,"GN":4496,"GP":7026,"IM":51,"IN":33,"QZ":51},"epochs":3138,"valid_fixes":2827}
```

Each file also gets its own entry with the same counts plus `last_fix`, the
last complete epoch with GGA, RMC, GSA and VTG merged together. Most of the
`bad_character` rejections come from the gpsd logs that are mostly binary
(SiRF, u-blox UBX) with some NMEA mixed in. `-` reads from stdin.

### Compare with a naive parser under simulated overruns

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

This takes a quarter of the clean GGA/RMC/GSA/VTG sentences in every log and
drops a run of 1 to 8 consecutive bytes from each one, which is what a UART
overrun does. The run can start anywhere, including the `$`, the checksum or
the line ending. Then it feeds the damaged stream to both parsers
(`nmea-replay --parser guard|naive --accepted`). Any sentence a parser
accepts that never appeared in the original log counts as damage that got
through. `tests/test_overrun.py` runs this with seeds 1, 2 and 3:

| seed | logs | damaged sentences | nmea-guard rejected | naive rejected |
|-----:|-----:|------------------:|--------------------:|---------------:|
| 1 | 103 | 2816 | 99.43% (16 got through) | 89.63% (292 got through) |
| 2 | 104 | 2873 | 99.44% | 89.87% |
| 3 | 102 | 2766 | 99.46% | 89.26% |

The naive baseline is `tools/naive_parser.hpp`. It's modelled on the
line-based code you find in many example sketches: it checks the checksum
only when `*hh` closes the line, and otherwise assumes the receiver just
didn't send one.

## How it's tested

- **GoogleTest** (`tests/*.cpp`, 72 tests): the framer state machine, every
  checksum edge case I could think of (missing `*`, one hex digit, non-hex
  digits, extra characters after the checksum, a second `*`, a truncated
  sentence whose surviving `*hh` lands mid-line), talker IDs GP/GN/GL/GA/GB/BD/GQ,
  field validation, fix assembly, and overflow recovery. One test drops each
  byte of a sentence in turn and checks that a different sentence never comes
  out.
- **No heap**: `test_no_heap.cpp` swaps in a counting global `operator new`
  and feeds 1,000 sentences plus junk through the parser, and the count stays
  at zero. A separate ctest runs `nm` on `libnmea_guard.a` and fails if it
  references `malloc`, `operator new`, `__cxa_throw` or `typeinfo`. The only
  undefined symbols the library has are `memchr` and `__stack_chk_fail`.
- **Differential against pynmea2** (`tests/test_differential.py`): for each
  gpsd log, the sentences nmea-guard decodes have to be exactly the clean
  ones, i.e. good checksum and well-formed fields. That set is worked out by
  a separate Python implementation in `tests/nmea_reference.py`. Then every
  decoded field (time, position, quality, DOPs, PRNs, speeds, date, mode) has
  to match what pynmea2 gets from the same text. That's 11,899 sentences:
  3,261 GGA, 3,075 RMC, 3,294 GSA and 2,269 VTG, all identical.
- **Replay** (`tests/test_replay.py`): the CLI processes all 173 downloaded
  logs, every byte gets counted, and the per-file numbers add up to the totals.

## Design notes

- **Framing is a five-state machine**: hunt for `$`, body, first checksum
  digit, second digit, line end. The checksum is the XOR of everything
  between `$` and the first `*`, wherever that `*` falls. After the `*` there
  have to be exactly two hex digits and then CR or LF. Anything else is
  `malformed_checksum`, never "no checksum, accept it".
- **No merging after damage.** Every rejection (overflow, a control byte, a
  `$` arriving mid-sentence) sends the framer back to hunting for `$`. The
  tail of a broken sentence can't be read as the start of the next one. The
  one exception is a real pattern in the gpsd logs, `...*1E$GPRMC,...`, where
  the line ending was lost *after* a complete checksum. In that case the
  first sentence is judged on its checksum and the `$` starts the next one.
- **Fields are validated, not just split.** Latitude has to be exactly
  `ddmm.mmm` and longitude `dddmm.mmm`, minutes below 60, times `hhmmss[.s]`
  with values in range, dates real calendar days, unit letters the expected
  letter. A lost byte that the checksum misses usually shifts or shortens a
  field, and this catches it. It also rejects the placeholder positions some
  receivers send before they have a fix, like `8960.0000,N` (89°60') or
  `36000.0000,N`. There are 46 such sentences across the logs, all with a
  correct checksum, and pynmea2 accepts them.
- **No `strtod`.** Decimals are read as an integer mantissa and divided once
  by a power of ten. That gives the correctly rounded double, the same value
  Python's `float()` returns, so the differential test compares exactly
  (tolerance 1e-12) rather than "close enough". It also keeps locale and
  `errno` out of firmware.
- **One fix per epoch.** GGA and RMC carry the UTC time. A new time closes
  the previous epoch, and GSA/VTG join whichever epoch is open. The RMC date
  carries over between epochs because some receivers send RMC less often
  than GGA.
- The library is built with `-fno-exceptions -fno-rtti -Wconversion`. The
  buffer is 120 bytes. NMEA allows 82, but real receivers go over that.

## Limitations

- **The XOR checksum can't see everything.** The 16 sentences that got past
  nmea-guard in the seed-1 run all lost a run of bytes that XORs to zero,
  so the checksum didn't change. Typical cases are a pair of blank fields
  (`,,`) or two zeros out of a padded number. Each sentence still had
  well-formed fields afterwards. The only way to
  catch those would be to know each receiver's exact field layout, and real
  receivers don't agree on that (ch-4701 sends more than 12 PRN slots in
  GSA, for example).
- Only GGA, RMC, GSA and VTG are decoded. GSV, ZDA, GLL and proprietary
  sentences get framed and checksum-checked, then reported as `other`.
- I've only run it on Linux/x86-64 so far. The code avoids anything
  host-specific, but I haven't built it for a Cortex-M target yet.
- Some gpsd logs are binary-protocol captures. `nmea-replay` reads them fine,
  but there's nothing in them for it to decode.

What comes next: a seeded corruption injector and a fuzzer on top of
`scripts/overrun_eval.py` (bit flips and inserted bytes, not just drops), an
`arm-none-eabi-gcc` build running the tests under QEMU, and CI that publishes
the overrun numbers as a report.

## Credits

The receiver logs are from the [gpsd project](https://gitlab.com/gpsd/gpsd)
regression suite (BSD licence, copyright the GPSD project and the people who
submitted them). They're downloaded at build time, not redistributed.
pynmea2 is used only as the reference in the differential test. nmea-guard's
own code is under the MIT licence (see `LICENSE`).
