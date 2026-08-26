# Tests

Run with `ctest --test-dir build --output-on-failure` and `python3 -m pytest -q` (after `scripts/fetch_logs.sh`).

## GoogleTest (`tests/*.cpp`)

72 tests covering the framer state machine, checksum edge cases (missing `*`, one hex digit, non-hex digits, extra characters after the checksum, a second `*`, a truncated sentence whose surviving `*hh` lands mid-line), talker IDs GP/GN/GL/GA/GB/BD/GQ, field validation, fix assembly and overflow recovery. One test drops each byte of a sentence in turn and checks that a different sentence never comes out.

## No heap

`test_no_heap.cpp` replaces global `operator new` with a counting one, feeds 1,000 sentences plus junk through the parser, and checks the count stays at zero. A separate ctest (`tests/check_symbols.cmake`) runs `nm` on `libnmea_guard.a` and fails if it references `malloc`, `free`, `operator new`, `__cxa_throw` or `typeinfo`. The only undefined symbols the library has are `memchr` and `__stack_chk_fail`.

## Differential against pynmea2 (`tests/test_differential.py`)

For each gpsd log, the sentences nmea-guard decodes have to be exactly the clean ones (good checksum, well-formed fields), as worked out by a separate Python implementation in `tests/nmea_reference.py`. Then every decoded field (time, position, quality, DOPs, PRNs, speeds, date, mode) has to match what pynmea2 gets from the same text, with a tolerance of 1e-12. That's 11,899 sentences: 3,261 GGA, 3,075 RMC, 3,294 GSA and 2,269 VTG.

## Replay (`tests/test_replay.py`)

`nmea-replay` processes all 173 downloaded logs, every byte is counted, and the per-file numbers add up to the totals.

## Overrun (`tests/test_overrun.py`)

Runs `scripts/overrun_eval.py` with seeds 1, 2 and 3. See [overrun-eval.md](overrun-eval.md).
