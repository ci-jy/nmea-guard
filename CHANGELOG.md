# Changelog

## 0.2.0 - Corruption injection and fuzzing

- `tools/corrupt.py`: seeded fault injector for the gpsd logs with four serial faults (dropped bytes, overrun truncation into the next line, bit flips, lost CR/LF).
- `scripts/rejection_eval.py` and `results/rejection.json`: rejection per fault type for nmea-guard and the naive baseline, plus sentences recovered and collateral losses. Over 39,302 damaged sentences, nmea-guard rejects 99.74% and the naive parser 93.22%.
- `fuzz/`: libFuzzer targets for the parser and the field decoder, with invariant checks. `scripts/fuzz.sh SECONDS` runs them under ASan and UBSan. ctest replays the committed corpus in every build.
- `Parser::reset()`, which the README's overrun example already called but which only existed on `Framer`.
- Fixed a `-Wsign-conversion` warning under Clang.

## 0.1.0 - Heap-free parser, replay CLI and differential tests

- Byte-at-a-time framer, strict field decoding and fix assembly for GGA, RMC, GSA and VTG, with no heap, exceptions or RTTI.
- `nmea-replay` CLI, gpsd log fetch script, GoogleTest suite, differential test against pynmea2 and the overrun comparison against a naive parser.
