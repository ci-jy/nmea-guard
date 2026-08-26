# Overrun comparison

```
python3 scripts/overrun_eval.py [--seed N] [--rate R] [--replay PATH] [LOG...]
```

- `--seed` (default 1) seeds the random damage.
- `--rate` (default 0.25) is the fraction of clean sentences to damage.
- `--replay` is the path to `nmea-replay` (default `build/nmea-replay`, or `$NMEA_REPLAY`).
- With no logs given it uses every `data/gpsd/*.log`.

For each log it takes the chosen share of clean GGA/RMC/GSA/VTG sentences and drops a run of 1 to 8 consecutive bytes from each one. The run can start anywhere, including the `$`, the checksum or the line ending. The damaged stream goes through both parsers (`nmea-replay --parser guard|naive --accepted`). Any accepted sentence that never appeared in the original log counts as damage that got through.

The naive baseline (`tools/naive_parser.hpp`) checks the checksum only when `*hh` closes the line, and otherwise assumes the receiver just didn't send one.

`tests/test_overrun.py` runs seeds 1, 2 and 3:

| seed | logs | damaged sentences | nmea-guard rejected | naive rejected |
|-----:|-----:|------------------:|--------------------:|---------------:|
| 1 | 103 | 2816 | 99.43% (16 got through) | 89.63% (292 got through) |
| 2 | 104 | 2873 | 99.44% | 89.87% |
| 3 | 102 | 2766 | 99.46% | 89.26% |
