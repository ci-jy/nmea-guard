#!/usr/bin/env python3
"""Replay gpsd logs with simulated UART overruns and count what gets through.

A serial overrun loses a run of consecutive bytes. For a seeded fraction of
the clean GGA/RMC/GSA/VTG sentences in each log, this drops 1..MAX_RUN bytes
at a random offset (the '$', the checksum and the line end are all fair
game), then feeds the damaged stream to nmea-replay with the guard parser and
with the naive baseline. Any accepted sentence whose text never appeared in
the original log is a corrupted sentence that got through.

    python3 scripts/overrun_eval.py [--seed N] [--rate P] [--replay PATH] [LOG...]
"""

from __future__ import annotations

import argparse
import json
import os
import random
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tests"))

from nmea_reference import framed_sentences, is_clean, segments  # noqa: E402

MAX_RUN = 8


def corrupt(data: bytes, rng: random.Random, rate: float) -> tuple[bytes, int]:
    """Return the damaged log and how many sentences were damaged."""
    clean = {text.encode() for text, body in framed_sentences(data) if is_clean(body)}
    out = bytearray()
    damaged = 0
    # Keep line endings attached so a drop can swallow them too.
    for line in re.split(rb"(?<=\n)", data):
        stripped = line.rstrip(b"\r\n")
        if stripped in clean and rng.random() < rate:
            start = rng.randrange(len(line))
            run = rng.randint(1, MAX_RUN)
            line = line[:start] + line[start + run:]
            damaged += 1
        out += line
    return bytes(out), damaged


def original_lines(data: bytes) -> set[str]:
    return {chunk.decode("latin1") for chunk in segments(data)}


def accepted(replay: Path, parser: str, path: Path) -> list[str]:
    proc = subprocess.run([str(replay), "--parser", parser, "--accepted", str(path)],
                          capture_output=True, check=True)
    return proc.stdout.decode("latin1").splitlines()


def evaluate(logs: list[Path], replay: Path, seed: int, rate: float) -> dict:
    rng = random.Random(seed)
    totals = {"logs": 0, "damaged": 0, "guard_passed": 0, "naive_passed": 0}
    with tempfile.TemporaryDirectory(prefix="nmea-overrun-") as tmp:
        for log in logs:
            data = log.read_bytes()
            damaged_data, damaged = corrupt(data, rng, rate)
            if damaged == 0:
                continue
            path = Path(tmp) / log.name
            path.write_bytes(damaged_data)
            known = original_lines(data)
            totals["logs"] += 1
            totals["damaged"] += damaged
            for parser in ("guard", "naive"):
                passed = [s for s in accepted(replay, parser, path) if s not in known]
                totals[f"{parser}_passed"] += len(passed)
    n = totals["damaged"]
    totals["guard_rejected_pct"] = 100.0 * (n - totals["guard_passed"]) / n if n else 0.0
    totals["naive_rejected_pct"] = 100.0 * (n - totals["naive_passed"]) / n if n else 0.0
    totals["seed"] = seed
    totals["rate"] = rate
    return totals


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("logs", nargs="*", type=Path)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--rate", type=float, default=0.25, help="fraction of clean sentences to damage")
    ap.add_argument("--replay", type=Path, default=Path(os.environ.get("NMEA_REPLAY", ROOT / "build" / "nmea-replay")))
    args = ap.parse_args()
    logs = args.logs or sorted((ROOT / "data" / "gpsd").glob("*.log"))
    if not logs:
        print("no logs; run scripts/fetch_logs.sh first", file=sys.stderr)
        return 1
    print(json.dumps(evaluate(logs, args.replay, args.seed, args.rate), indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
