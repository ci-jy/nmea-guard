#!/usr/bin/env python3
"""Generate firmware/vectors.inc, the test vectors for the bare-metal runner.

Picks sentences from the gpsd logs (a seeded handful per log) and, for some of
them, a damaged copy with bytes dropped or bits flipped. Each vector is one
line, fed to the parser with CR/LF, plus what the parser has to make of it:

  - the verdict comes from tests/nmea_reference.py (clean, other, rejected)
  - the decoded values come from pynmea2

Neither looks at the C++ code, so the QEMU run checks the Cortex-M3 build
against the same independent references as the host differential test.

    python3 scripts/gen_vectors.py [--per-log N] [--seed S] [-o firmware/vectors.inc]
    python3 scripts/gen_vectors.py --check     # exit 1 if the file is stale
"""

from __future__ import annotations

import argparse
import random
import re
import sys
from pathlib import Path

import pynmea2

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tests"))
sys.path.insert(0, str(ROOT / "tools"))

from corrupt import drop_chars, flip_bits  # noqa: E402
from nmea_reference import DECODED_TYPES, framed_sentences, is_clean, sentence_type  # noqa: E402

LOG_DIR = ROOT / "data" / "gpsd"
OUT = ROOT / "firmware" / "vectors.inc"

# Verdict codes, mirrored by enum class Expect in firmware/vectors.hpp.
REJECTED, OTHER = 0, 1
TYPE_CODE = {"GGA": 2, "RMC": 3, "GSA": 4, "VTG": 5}

_SINGLE = re.compile(rb"^\$[ -~]*$")  # one sentence, printable, nothing before '$'


def _raw(msg, name: str) -> str:
    names = [f[1] for f in msg.fields]
    i = names.index(name)
    return msg.data[i] if i < len(msg.data) else ""


def _num(s: str):
    return float(s) if s else None


def _int(s: str):
    return int(s) if s else None


def _position(msg):
    lat = msg.latitude if _raw(msg, "lat") else None
    lon = msg.longitude if _raw(msg, "lon") else None
    return lat, lon


def _time_ms(msg):
    ts = msg.timestamp
    if ts is None:
        return None
    # Milliseconds are enough to tell a wrong time apart; the parser keeps
    # microseconds and the runner divides.
    return ((ts.hour * 60 + ts.minute) * 60 + ts.second) * 1000 + ts.microsecond // 1000


def expected_values(text: str, kind: str):
    """(time_ms, four doubles, two ints) as the runner's Vector holds them."""
    msg = pynmea2.parse(text, check=True)
    if kind == "GGA":
        lat, lon = _position(msg)
        return (_time_ms(msg), [lat, lon, _num(_raw(msg, "altitude")), _num(_raw(msg, "horizontal_dil"))],
                [_int(_raw(msg, "gps_qual")), _int(_raw(msg, "num_sats"))])
    if kind == "RMC":
        lat, lon = _position(msg)
        d = msg.datestamp
        date = d.day * 10000 + d.month * 100 + d.year % 100 if d else None
        status = _raw(msg, "status")
        return (_time_ms(msg), [lat, lon, _num(_raw(msg, "spd_over_grnd")), _num(_raw(msg, "true_course"))],
                [date, ord(status) if status else None])
    if kind == "GSA":
        prns = sum(1 for i in range(1, 13) if _raw(msg, f"sv_id{i:02d}"))
        return (None, [_num(_raw(msg, "pdop")), _num(_raw(msg, "hdop")), _num(_raw(msg, "vdop")), None],
                [_int(_raw(msg, "mode_fix_type")), prns])
    if kind == "VTG":
        mode = _raw(msg, "faa_mode")
        return (None, [_num(_raw(msg, "true_track")), _num(_raw(msg, "mag_track")),
                       _num(_raw(msg, "spd_over_grnd_kts")), _num(_raw(msg, "spd_over_grnd_kmph"))],
                [ord(mode) if mode else None, None])
    raise ValueError(kind)


def verdict(line: bytes):
    """What the parser must report for `line` on its own, or None if the
    line is not a single printable sentence (those are left out)."""
    if not _SINGLE.match(line) or line.count(b"$") != 1:
        return None
    framed = list(framed_sentences(line))
    if not framed:
        return REJECTED, None
    text, body = framed[0]
    addr = body.split(",", 1)[0]
    kind = sentence_type(body)
    if kind is not None:
        return (TYPE_CODE[kind], kind) if is_clean(body) else (REJECTED, None)
    # A checksum-valid sentence of another type. The parser reports it as
    # "other" when the address is five letters or digits (proprietary 'P'
    # ones included) and rejects it otherwise; leave the odd addresses to the
    # host tests.
    if re.fullmatch(r"[A-Z0-9]{5}", addr) and addr[2:] not in DECODED_TYPES:
        return OTHER, None
    return None


def c_string(line: bytes) -> str:
    return '"' + line.decode("ascii").replace("\\", "\\\\").replace('"', '\\"').replace("?", "\\?") + '"'


def c_double(v) -> str:
    return "0.0" if v is None else repr(float(v))


def c_int(v) -> str:
    return "-1" if v is None else str(v)


def render(line: bytes, code: int, kind: str | None) -> str:
    if kind is None:
        return f"    {{{c_string(line)}, {code}, -1, 0, {{0.0, 0.0, 0.0, 0.0}}, {{-1, -1}}}},"
    time_ms, nums, ints = expected_values(line.decode("ascii"), kind)
    mask = sum(1 << i for i, v in enumerate(nums) if v is not None)
    return (f"    {{{c_string(line)}, {code}, {c_int(time_ms)}, {mask}, "
            f"{{{', '.join(c_double(v) for v in nums)}}}, {{{', '.join(c_int(v) for v in ints)}}}}},")


def collect(per_log: int, seed: int):
    rng = random.Random(seed)
    vectors = []
    for log in sorted(LOG_DIR.glob("*.log")):
        lines = []
        for raw in log.read_bytes().splitlines():
            if verdict(raw) is not None:
                lines.append(raw)
        if not lines:
            continue
        decoded = [l for l in lines if verdict(l)[0] >= 2]
        others = [l for l in lines if verdict(l)[0] < 2]
        picked = rng.sample(decoded, min(per_log, len(decoded)))
        picked += rng.sample(others, min(max(1, per_log // 4), len(others)))
        for line in picked:
            vectors.append(line)
            if verdict(line)[0] < 2:
                continue
            # A damaged copy of each decoded sentence: what an overrun or
            # noise on the line leaves behind.
            damage = drop_chars if rng.random() < 0.5 else flip_bits
            bad = damage(line, rng)
            if verdict(bad) is not None:
                vectors.append(bad)
    # The same sentence can turn up in several logs.
    seen, unique = set(), []
    for v in vectors:
        if v not in seen:
            seen.add(v)
            unique.append(v)
    return unique


def generate(per_log: int, seed: int) -> str:
    vectors = collect(per_log, seed)
    counts = {REJECTED: 0, OTHER: 0, **{c: 0 for c in TYPE_CODE.values()}}
    rows = []
    for line in vectors:
        code, kind = verdict(line)
        counts[code] += 1
        rows.append(render(line, code, kind))
    summary = (f"// {len(rows)} vectors: {counts[TYPE_CODE['GGA']]} GGA, {counts[TYPE_CODE['RMC']]} RMC, "
               f"{counts[TYPE_CODE['GSA']]} GSA, {counts[TYPE_CODE['VTG']]} VTG, "
               f"{counts[OTHER]} other, {counts[REJECTED]} rejected")
    header = [
        "// Generated by scripts/gen_vectors.py from the gpsd logs; do not edit.",
        f"// python3 scripts/gen_vectors.py --per-log {per_log} --seed {seed}",
        summary,
        "// {line, expect, time_ms, mask, {values}, {ints}}: see firmware/vectors.hpp",
    ]
    return "\n".join(header + rows) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--per-log", type=int, default=4, help="decoded sentences to pick per log")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("-o", "--output", type=Path, default=OUT)
    ap.add_argument("--check", action="store_true", help="compare with the existing file instead of writing")
    args = ap.parse_args()

    if not any(LOG_DIR.glob("*.log")):
        print("no gpsd logs in data/gpsd; run: bash scripts/fetch_logs.sh", file=sys.stderr)
        return 1
    text = generate(args.per_log, args.seed)
    if args.check:
        current = args.output.read_text() if args.output.exists() else ""
        if current != text:
            print(f"{args.output} is out of date; rerun scripts/gen_vectors.py", file=sys.stderr)
            return 1
        return 0
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text)
    print(text.splitlines()[2].removeprefix("// "))
    return 0


if __name__ == "__main__":
    sys.exit(main())
