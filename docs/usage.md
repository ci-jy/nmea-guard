# Usage

## Using the library in another project

Either `add_subdirectory()` this repo and link `nmea_guard::nmea_guard`, or copy the files from `src/` and the headers from `include/` into your firmware tree. The library is built with `-fno-exceptions -fno-rtti -Wconversion`.

## Parser API

`#include "nmea_guard/parser.hpp"`

- `Event feed(uint8_t byte)` takes one byte and returns:
  - `kNone` while a sentence is still coming in
  - `kSentence` for a decoded GGA, RMC, GSA or VTG (read it with `sentence()`)
  - `kOther` for a checksum-valid sentence of a type that isn't decoded (GSV, ZDA, GLL, proprietary ones)
  - `kRejected`, with the reason in `last_frame_error()` or `last_decode_error()` (and `last_bad_field()` for field errors)
- `Event flush()` ends the input: terminates a final unterminated line and closes the epoch in progress.
- `reset()` drops a partial sentence, for example after a UART overrun.
- `fix()` is the last complete epoch, `current_fix()` the one being built, `has_fix()` says whether there is a complete one yet.
- `raw()` is the text of the current sentence, `stats()` holds running counts.

Fields that are blank in the sentence come back as empty `std::optional`s.

The sentence buffer is 120 bytes. NMEA allows 82, but real receivers go over that.

### Epochs

GGA and RMC carry the UTC time. A new time closes the previous epoch, and GSA and VTG join whichever epoch is open. The RMC date carries over between epochs, because some receivers send RMC less often than GGA.

## nmea-replay

```
nmea-replay [--parser guard|naive] [--sentences | --accepted] FILE...
```

- `--parser guard|naive` picks nmea-guard (default) or the naive baseline from `tools/naive_parser.hpp`.
- `--sentences` prints one JSON object per decoded sentence (JSON Lines). Only works with the guard parser.
- `--accepted` prints the raw text of every accepted sentence, one per line.
- `-` as a file name reads from stdin.

Without `--sentences` or `--accepted` it prints a summary: an entry per file and a `total`.

```console
$ ./build/nmea-replay --sentences data/gpsd/bu353-glonass.log | sed -n 3p
{"file":"data/gpsd/bu353-glonass.log","raw":"$GNRMC,081411.000,A,5541.1955,N,03737.2872,E,0.60,301.29,260514,,,A*74","talker":"GN","type":"RMC","time":"08:14:11.000000","status":"A","latitude":55.686591666666665,"longitude":37.621453333333335,"speed_knots":0.6,"course_deg":301.29,"date":{"day":26,"month":5,"year":14},"magnetic_variation_deg":null,"mode":"A"}
```

```console
$ ./build/nmea-replay data/gpsd/*.log | tail -2 | head -1
  "total": {"files":173,"bytes":3999727,"accepted":11899,"other":16177,"rejected":7626,"rejected_by_reason":{"bad_address":422,"bad_character":6529,"bad_field":45,"checksum_mismatch":102,"field_count":1,"interrupted":255,"malformed_checksum":72,"missing_checksum":159,"overflow":41},"accepted_by_type":{"GGA":3261,"GSA":3294,"RMC":3075,"VTG":2269},"accepted_by_talker":{"BD":180,"GL":62,"GN":4496,"GP":7026,"IM":51,"IN":33,"QZ":51},"epochs":3138,"valid_fixes":2827}
```

Each per-file entry has the same counts plus `last_fix`, the last complete epoch with GGA, RMC, GSA and VTG merged. Most of the `bad_character` rejections come from gpsd logs that are mostly binary (SiRF, u-blox UBX) with a bit of NMEA mixed in.
