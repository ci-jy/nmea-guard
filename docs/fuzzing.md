# Fuzzing

```
sudo apt-get install clang libclang-rt-dev
bash scripts/fuzz.sh [SECONDS]
```

The script configures a separate build in `build-fuzz/` with Clang, AddressSanitizer, UBSan and libFuzzer coverage, all applied to the library itself as well as the harness (`-DNMEA_GUARD_BUILD_FUZZERS=ON`). Then it runs each target for half of `SECONDS` (default 60). It exits non-zero if a target crashes, trips a sanitizer or fails one of its checks. The input that did it is saved in `build-fuzz/artifacts/`.

## Targets

`fuzz/fuzz_parser.cpp` feeds arbitrary bytes to a `Parser`, then checks:

- every accepted sentence starts with `$`, fits the 120-byte buffer, has only printable characters, and really has a matching `*hh` at the end
- decoded latitudes and longitudes are finite and within ±90 / ±180, times and dates are in range, GSA has at most 12 PRNs
- `stats()` adds up: one count per byte, and decoded + other + rejected equals the number of events
- whatever state the input leaves the parser in, `reset()` followed by a good GGA gives back exactly that GGA. The same has to work with just a CR/LF and no `reset()`. This is the "never merge into the next sentence" promise.

`fuzz/fuzz_decode.cpp` gives arbitrary text straight to `decode()`, skipping framing. Through the parser, the fuzzer would first have to get the XOR right before any field gets looked at. This target checks the same value ranges.

## Corpus

`fuzz/corpus/<target>/` holds a few hand-made seeds: one of each decoded sentence, a full epoch, a lost line ending, an overflow and some binary noise. If the gpsd logs have been fetched, the script also seeds the parser with the first 2 KB of every log and the decoder with up to 2,000 distinct sentence bodies. New inputs go to `build-fuzz/corpus/`, so the next run picks up where the last one stopped.

A normal (GCC) build compiles both targets a second time against `fuzz/replay_main.cpp`, a small `main()` that runs every file in the committed corpus once. ctest runs these as `fuzz_parser_corpus` and `fuzz_decode_corpus`. If a crash ever turns up, the input goes into `fuzz/corpus/<target>/` and stays there as a regression test.

## A run

```console
$ bash scripts/fuzz.sh 60
fuzz_parser: 30s with ASan+UBSan
  ok: 503228 inputs, 495 coverage points, corpus 3060 files
fuzz_decode: 30s with ASan+UBSan
  ok: 17447385 inputs, 370 coverage points, corpus 892 files
```

Nothing has turned up so far. The parser target is much slower per input because inputs go up to 4 KB, every one of them is fed through twice, and ASan checks every buffer access.
