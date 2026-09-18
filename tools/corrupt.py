#!/usr/bin/env python3
"""Seeded serial-fault injector for recorded NMEA logs.

Picks a seeded fraction of the clean GGA/RMC/GSA/VTG sentences in a log and
damages each one with a single kind of fault, the way a UART link actually
goes wrong:

  dropped_chars     1 to 3 single bytes lost at scattered positions
                    (receiver FIFO full for one character time)
  truncated_read    the read loop overran: the sentence is cut at a random
                    point and the stream resumes somewhere inside the next
                    line, so the head of one sentence meets the tail of the
                    next
  bit_flip          1 or 2 bits flipped anywhere in the line (noise, baud
                    drift)
  missing_line_end  the CR/LF is lost and the next line follows directly

Any byte of the line is fair game, including '$', '*', the checksum digits
and the line ending. The same seed always produces the same damage.

    python3 tools/corrupt.py --fault bit_flip [--seed N] [--rate P] LOG [-o OUT]
"""

from __future__ import annotations

import argparse
import json
import random
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tests"))

from nmea_reference import framed_sentences, is_clean  # noqa: E402

FAULTS = ("dropped_chars", "truncated_read", "bit_flip", "missing_line_end")


def split_lines(data: bytes) -> list[bytes]:
    """Lines with their endings kept, so a fault can hit the CR/LF too."""
    return [line for line in re.split(rb"(?<=\n)", data) if line]


def _ending(line: bytes) -> int:
    return len(line) - len(line.rstrip(b"\r\n"))


def drop_chars(line: bytes, rng: random.Random) -> bytes:
    count = min(rng.randint(1, 3), len(line))
    gone = set(rng.sample(range(len(line)), count))
    return bytes(b for i, b in enumerate(line) if i not in gone)


def flip_bits(line: bytes, rng: random.Random) -> bytes:
    out = bytearray(line)
    # Two flips never land on the same bit, which would undo the damage.
    bits = rng.sample(range(len(line) * 8), rng.randint(1, 2))
    for bit in bits:
        out[bit // 8] ^= 1 << (bit % 8)
    return bytes(out)


def truncate_read(line: bytes, following: bytes, rng: random.Random) -> tuple[bytes, bool]:
    """Cut `line` at a random point and resume inside `following`.

    Returns the merged bytes and whether `following` lost anything. At least
    one byte of `line` survives and at least one byte is lost overall.
    """
    cut = rng.randrange(1, len(line))
    resume = rng.randrange(0, len(following))
    return line[:cut] + following[resume:], resume > 0


def drop_line_end(line: bytes) -> bytes:
    return line[: len(line) - _ending(line)]


def corrupt(data: bytes, fault: str, rng: random.Random, rate: float) -> tuple[bytes, list[str]]:
    """Damage a seeded share of the clean sentences in `data`.

    Returns the damaged log and the original text (without line ending) of
    every line that lost or changed a byte.
    """
    if fault not in FAULTS:
        raise ValueError(f"unknown fault {fault!r}")
    clean = {text.encode() for text, body in framed_sentences(data) if is_clean(body)}
    lines = split_lines(data)
    out = bytearray()
    damaged: list[str] = []
    i = 0
    while i < len(lines):
        line = lines[i]
        i += 1
        if line.rstrip(b"\r\n") not in clean or rng.random() >= rate:
            out += line
            continue
        if fault == "dropped_chars":
            out += drop_chars(line, rng)
        elif fault == "bit_flip":
            out += flip_bits(line, rng)
        elif fault == "missing_line_end":
            if _ending(line) == 0 or i == len(lines):
                out += line  # last line of the file: nothing to run into
                continue
            out += drop_line_end(line)
        else:  # truncated_read
            if i == len(lines):
                out += line
                continue
            merged, hit_next = truncate_read(line, lines[i], rng)
            out += merged
            i += 1
            if hit_next:
                damaged.append(lines[i - 1].rstrip(b"\r\n").decode("latin1"))
        damaged.append(line.rstrip(b"\r\n").decode("latin1"))
    return bytes(out), damaged


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("log", type=Path)
    ap.add_argument("--fault", choices=FAULTS, required=True)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--rate", type=float, default=0.25, help="fraction of clean sentences to damage")
    ap.add_argument("-o", "--output", type=Path, help="write the damaged log here (default stdout)")
    args = ap.parse_args()

    damaged_data, damaged = corrupt(args.log.read_bytes(), args.fault, random.Random(args.seed), args.rate)
    if args.output:
        args.output.write_bytes(damaged_data)
    else:
        sys.stdout.buffer.write(damaged_data)
    print(json.dumps({"log": str(args.log), "fault": args.fault, "seed": args.seed,
                      "rate": args.rate, "damaged": len(damaged)}), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
