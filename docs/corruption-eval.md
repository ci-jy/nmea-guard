# Fault injection by type

```
python3 tools/corrupt.py --fault FAULT [--seed N] [--rate R] LOG [-o OUT]
python3 scripts/rejection_eval.py [--seeds N ...] [--rate R] [--fault FAULT ...] [-o FILE] [LOG...]
```

`tools/corrupt.py` damages one log with one kind of fault and prints a one-line JSON summary to stderr. `scripts/rejection_eval.py` runs every fault over every `data/gpsd/*.log` for each seed, replays the damaged logs through both parsers, and prints (or writes) the JSON that is committed as `results/rejection.json`.

## Faults

The injector only damages clean GGA, RMC, GSA and VTG sentences (the ones `tests/nmea_reference.py` calls clean), a seeded fraction `--rate` of them (default 0.25). Every byte of the line can be hit, including `$`, `*`, the checksum and the CR/LF.

| fault | what happens to the line | where it comes from |
|---|---|---|
| `dropped_chars` | 1 to 3 single bytes deleted at scattered positions | the UART FIFO was full for a character time |
| `truncated_read` | cut at a random point, and the stream resumes at a random point in the next line | the read loop fell behind and a whole buffer was lost |
| `bit_flip` | 1 or 2 bits flipped, never the same bit twice | line noise, a baud rate slightly off |
| `missing_line_end` | the CR/LF is removed, so the next line follows directly | a lost line ending on its own |

`truncated_read` damages two lines at once when the resume point is past the start of the next line. Both count as damaged.

## What is counted

For each parser, `nmea-replay --parser guard|naive --accepted` lists the sentences it accepted, and they are compared with the text of the original log:

- `passed`: accepted sentences whose text never appeared in the original. These are corrupted sentences that got through. `rejected_pct` is `(damaged - passed) / damaged`.
- `recovered`: damaged lines that were still accepted with their original text, for example when only the line ending was hit.
- `intact_lost`: sentences that were not damaged, that the parser accepts on the original log, but that it lost on the damaged one. This is collateral damage, like a fault in one line taking the next one with it.

The random generator is seeded with the fault name and the seed, so a single row can be rerun on its own and gives the same numbers.

## Results

From `results/rejection.json`: seeds 1, 2 and 3, rate 0.25, all 173 logs. Parentheses give the number of corrupted sentences that got through.

| fault | damaged | nmea-guard rejected | naive rejected | guard recovered | naive recovered |
|---|---:|---:|---:|---:|---:|
| dropped_chars | 8,475 | 99.37% (53) | 87.21% (1,084) | 110 | 78 |
| truncated_read | 14,141 | 99.96% (6) | 94.12% (832) | 84 | 81 |
| bit_flip | 8,359 | 99.46% (45) | 91.10% (744) | 38 | 49 |
| missing_line_end | 8,327 | 100.00% (0) | 99.95% (4) | 8,281 | 296 |
| all | 39,302 | 99.74% (104) | 93.22% (2,664) | 8,513 | 504 |

`intact_lost` is 1 for nmea-guard and 13 for the naive parser over all 39,302 faults.

Things I noticed going through the sentences that got past nmea-guard:

- **bit_flip**: 42 of the 45 are pairs of flips that cancel out in the XOR. A single flipped bit always changes it, but the same bit flipped in two different bytes (`0` to `1` in one field, `1` to `0` in another) doesn't, and a flip in the body can be matched by one in a checksum digit. The other 3 flipped a checksum letter to lower case (`*1B` to `*1b`). That's still a correct checksum and the data is unchanged, but the text differs from the original, so the count includes them.
- **dropped_chars** and **truncated_read**: same blind spot as in the [overrun comparison](overrun-eval.md). The bytes that went missing XOR to zero and the fields were still well formed.
- **missing_line_end**: nmea-guard gets back 8,281 of the 8,327 sentences, because `...*hh$G...` ends the first sentence on its checksum. The naive parser starts again at the `$` and throws away the first sentence. When the next line doesn't start with `$` (a `!AIVDM` line, say), the naive parser glues the two together and accepts the result without a checksum. That's where its 4 come from.

`tests/test_corruption.py` checks that a fresh run reproduces `results/rejection.json` exactly. It also runs seed 11 and requires at least 99% rejection for every fault.
