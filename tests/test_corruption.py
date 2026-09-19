"""The seeded fault injector, and the per-fault comparison it feeds.

See tools/corrupt.py for the fault models and scripts/rejection_eval.py for
how rejections are counted.
"""

import json
import random
from pathlib import Path

import pytest

from corrupt import FAULTS, corrupt, split_lines, truncate_read
from rejection_eval import evaluate

ROOT = Path(__file__).resolve().parent.parent

SAMPLE = (b"# comment line, never touched\n"
          b"$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n"
          b"$GPGSV,3,1,11,03,03,111,00,04,15,270,00,06,01,010,00,13,06,292,00*74\r\n"
          b"$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n"
          b"$GPVTG,054.7,T,034.4,M,005.5,N,010.2,K*48\r\n") * 40


def _bits(a: bytes, b: bytes) -> int:
    return sum(bin(x ^ y).count("1") for x, y in zip(a, b))


@pytest.mark.parametrize("fault", FAULTS)
def test_same_seed_same_damage(fault):
    a = corrupt(SAMPLE, fault, random.Random(7), 0.5)
    b = corrupt(SAMPLE, fault, random.Random(7), 0.5)
    c = corrupt(SAMPLE, fault, random.Random(8), 0.5)
    assert a == b
    assert a[0] != c[0]
    assert a[0] != SAMPLE and a[1]


@pytest.mark.parametrize("fault", FAULTS)
def test_rate_zero_leaves_log_alone(fault):
    assert corrupt(SAMPLE, fault, random.Random(1), 0.0) == (SAMPLE, [])


@pytest.mark.parametrize("fault", ["dropped_chars", "bit_flip", "missing_line_end"])
def test_only_clean_decoded_sentences_are_damaged(fault):
    out, damaged = corrupt(SAMPLE, fault, random.Random(3), 1.0)
    # Comments and GSV are not targets; with rate 1 every GGA/RMC/VTG is,
    # except that the file's last line has no next line to lose its end into.
    assert len(damaged) == 3 * 40 - (fault == "missing_line_end")
    assert not any("GSV" in d for d in damaged)
    assert out.count(b"# comment line") == 40
    assert out.count(SAMPLE.splitlines(keepends=True)[2]) == 40


def test_dropped_chars_removes_one_to_three_bytes_per_sentence():
    rng = random.Random(5)
    for line in split_lines(SAMPLE)[1:5]:
        if b"GSV" in line:
            continue
        for _ in range(50):
            out, damaged = corrupt(line, "dropped_chars", rng, 1.0)
            assert 1 <= len(line) - len(out) <= 3
            # What is left is the original with bytes taken out, in order.
            it = iter(line)
            assert all(b in it for b in out)


def test_bit_flip_changes_one_or_two_bits():
    rng = random.Random(9)
    line = split_lines(SAMPLE)[1]
    seen = set()
    for _ in range(200):
        out, _ = corrupt(line, "bit_flip", rng, 1.0)
        assert len(out) == len(line)
        seen.add(_bits(out, line))
    assert seen == {1, 2}


def test_missing_line_end_glues_the_next_line_on():
    out, damaged = corrupt(SAMPLE, "missing_line_end", random.Random(2), 0.5)
    assert SAMPLE.count(b"\n") - out.count(b"\n") == len(damaged)
    assert out.replace(b"\r", b"").replace(b"\n", b"") == SAMPLE.replace(b"\r", b"").replace(b"\n", b"")
    assert b"*47$GPGSV" in out or b"*6A$GPVTG" in out or b"*48# comment" in out


def test_truncated_read_joins_head_of_one_line_to_tail_of_next():
    rng = random.Random(4)
    a, b = split_lines(SAMPLE)[1:3]
    for _ in range(200):
        merged, hit_next = truncate_read(a, b, rng)
        assert len(merged) < len(a) + len(b)
        cut = next(i for i in range(len(a) + 1) if i == len(a) or merged[i] != a[i])
        assert cut >= 1
        tail = merged[cut:] if merged[cut:] else b""
        assert b.endswith(tail)
        assert hit_next == (tail != b)


def test_truncated_read_counts_both_lines_it_hit():
    out, damaged = corrupt(SAMPLE, "truncated_read", random.Random(6), 1.0)
    assert len(out) < len(SAMPLE)
    assert any("GSV" in d for d in damaged)


def test_rejection_results_match_committed_file(replay, gpsd_logs):
    committed = json.loads((ROOT / "results" / "rejection.json").read_text())
    r = evaluate(gpsd_logs, replay.path, committed["seeds"], committed["rate"])
    assert r == committed


def test_guard_rejects_each_fault_type_better_than_naive(replay, gpsd_logs):
    r = evaluate(gpsd_logs, replay.path, [11], 0.25)
    for fault, row in r["faults"].items():
        print(f"\n{fault}: {row['damaged']} damaged; guard rejected {row['guard_rejected_pct']}%, "
              f"naive {row['naive_rejected_pct']}%")
        assert row["damaged"] > 2000, fault
        assert row["guard_rejected_pct"] >= 99.0, fault
        assert row["guard_passed"] <= row["naive_passed"], fault
        # A fault never costs the guard parser an undamaged neighbour.
        assert row["guard_intact_lost"] <= 2, fault
    lost_end = r["faults"]["missing_line_end"]
    # A lost CR/LF after a complete checksum costs nmea-guard nothing.
    assert lost_end["guard_recovered"] >= 0.99 * lost_end["damaged"]
    assert r["all_faults"]["naive_passed"] > 10 * r["all_faults"]["guard_passed"]
