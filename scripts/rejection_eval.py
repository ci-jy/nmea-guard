#!/usr/bin/env python3
"""Compare nmea-guard with the naive baseline on each kind of serial fault.

For every fault type in tools/corrupt.py and every seed, the gpsd logs are
damaged and replayed through both parsers. Two things are counted per parser:

  passed       accepted sentences whose text never appeared in the original
               log, i.e. corrupted sentences that got through
  intact_lost  undamaged sentences the parser accepted on the original log
               but not on the damaged one (collateral loss around a fault;
               the damaged sentences themselves are not counted)
  recovered    damaged sentences still accepted with their original text,
               e.g. when only the line ending was lost

    python3 scripts/rejection_eval.py [--seeds 1 2 3] [--rate P] [-o results/rejection.json]
"""

from __future__ import annotations

import argparse
import json
import os
import random
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tests"))

from corrupt import FAULTS, corrupt  # noqa: E402
from nmea_reference import segments  # noqa: E402

PARSERS = ("guard", "naive")


def accepted(replay: Path, parser: str, path: Path) -> list[str]:
    proc = subprocess.run([str(replay), "--parser", parser, "--accepted", str(path)],
                          capture_output=True, check=True)
    return proc.stdout.decode("latin1").splitlines()


def _pct(part: int, whole: int) -> float:
    return round(100.0 * part / whole, 2) if whole else 0.0


def evaluate_fault(logs: list[Path], replay: Path, fault: str, seed: int, rate: float,
                   baseline: dict[tuple[str, str], Counter]) -> dict:
    # One generator per (fault, seed) so each row can be reproduced alone.
    rng = random.Random(f"{fault}:{seed}")
    row = {"fault": fault, "seed": seed, "logs": 0, "damaged": 0}
    for p in PARSERS:
        row[f"{p}_passed"] = 0
        row[f"{p}_intact_lost"] = 0
        row[f"{p}_recovered"] = 0
    with tempfile.TemporaryDirectory(prefix="nmea-reject-") as tmp:
        for log in logs:
            data = log.read_bytes()
            damaged_data, damaged = corrupt(data, fault, rng, rate)
            if not damaged:
                continue
            path = Path(tmp) / log.name
            path.write_bytes(damaged_data)
            known = {chunk.decode("latin1") for chunk in segments(data)}
            row["logs"] += 1
            row["damaged"] += len(damaged)
            for p in PARSERS:
                got = accepted(replay, p, path)
                row[f"{p}_passed"] += sum(1 for s in got if s not in known)
                intact = baseline[(p, log.name)] - Counter(damaged)
                still = Counter(s for s in got if s in known)
                row[f"{p}_intact_lost"] += sum((intact - still).values())
                row[f"{p}_recovered"] += sum((still - intact).values())
    return row


def summarize(rows: list[dict]) -> dict:
    out = {"damaged": sum(r["damaged"] for r in rows)}
    for p in PARSERS:
        passed = sum(r[f"{p}_passed"] for r in rows)
        out[f"{p}_passed"] = passed
        out[f"{p}_rejected_pct"] = _pct(out["damaged"] - passed, out["damaged"])
        out[f"{p}_intact_lost"] = sum(r[f"{p}_intact_lost"] for r in rows)
        out[f"{p}_recovered"] = sum(r[f"{p}_recovered"] for r in rows)
    return out


def evaluate(logs: list[Path], replay: Path, seeds: list[int], rate: float,
             faults: tuple[str, ...] = FAULTS) -> dict:
    # Undamaged sentences each parser accepts on the original logs.
    baseline = {(p, log.name): Counter(accepted(replay, p, log)) for p in PARSERS for log in logs}
    result = {"rate": rate, "seeds": seeds, "logs": len(logs), "faults": {}}
    every = []
    for fault in faults:
        rows = [evaluate_fault(logs, replay, fault, seed, rate, baseline) for seed in seeds]
        every += rows
        result["faults"][fault] = {**summarize(rows), "per_seed": rows}
    result["all_faults"] = summarize(every)
    return result


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("logs", nargs="*", type=Path)
    ap.add_argument("--seeds", type=int, nargs="+", default=[1, 2, 3])
    ap.add_argument("--rate", type=float, default=0.25, help="fraction of clean sentences to damage")
    ap.add_argument("--fault", choices=FAULTS, action="append", help="only this fault (repeatable)")
    ap.add_argument("--replay", type=Path, default=Path(os.environ.get("NMEA_REPLAY", ROOT / "build" / "nmea-replay")))
    ap.add_argument("-o", "--output", type=Path, help="also write the JSON here")
    args = ap.parse_args()
    logs = args.logs or sorted((ROOT / "data" / "gpsd").glob("*.log"))
    if not logs:
        print("no logs; run scripts/fetch_logs.sh first", file=sys.stderr)
        return 1
    result = evaluate(logs, args.replay, args.seeds, args.rate, tuple(args.fault or FAULTS))
    text = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text)
    sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
