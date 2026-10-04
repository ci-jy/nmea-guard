# How the parser works

## Framing

The framer is a five-state machine: hunt for `$`, body, first checksum digit, second checksum digit, line end.

- The checksum is the XOR of every byte between `$` and the first `*`, wherever that `*` turns up.
- After the `*` there must be exactly two hex digits and then CR or LF. A missing `*`, one digit, non-hex digits, extra characters or a second `*` are all rejections. There is no "no checksum, so accept it" path.
- After any rejection the framer goes back to hunting for `$`, so the tail of a broken sentence can't turn into the start of the next one.
- If a `$` arrives straight after a complete checksum (`...*1E$GPRMC,...`), the first sentence is judged on its checksum and the `$` starts the next one. Several gpsd logs contain this, where a line ending was lost.
- The sentence buffer is 120 bytes. NMEA allows 82, but real receivers go over that. A longer line is rejected as `overflow` and the framer recovers at the next `$`.

## Field validation

Fields are checked as well as split:

- latitude needs whole degrees and two digits of minutes, and minutes must be below 60 (the same for longitude)
- dates have to be real calendar days
- the field count has to match the sentence type

A dropped byte that the checksum misses usually shortens or shifts a field, and these checks catch most of those. They also reject placeholder positions like `8960.0000,N` that some receivers send before they have a fix, even though their checksum is correct.

## Decimals

Decimals are parsed without `strtod`, as an integer mantissa divided once by a power of ten. That gives the same double as Python's `float()`, so the differential test against pynmea2 uses a tolerance of 1e-12. On the Cortex-M3, libgcc's soft-float rounds the same way SSE does on x86, so the values match bit for bit there too.

## Smaller limitations

- The fault injector only removes or flips bytes, it never inserts them. A doubled character or noise between two sentences isn't measured. The fuzzer does insert bytes, but it only checks that nothing crashes.
- The flash figures are from arm-none-eabi-gcc 14.2.1. CI uses Ubuntu's older package and only checks that the QEMU run passes, not that the sizes match.
- There's no RTOS port. Calling `feed()` from an ISR and reading `fix()` from a task needs your own locking or a copy.
- I haven't measured how many cycles `feed()` takes per byte, or whether it keeps up at 115200 baud.
