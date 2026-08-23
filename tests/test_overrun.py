"""Simulated UART overruns on the real gpsd logs: what slips through.

See scripts/overrun_eval.py for the corruption model.
"""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))

from overrun_eval import evaluate  # noqa: E402


@pytest.mark.parametrize("seed", [1, 2, 3])
def test_guard_rejects_overrun_damage_far_better_than_naive(replay, gpsd_logs, seed):
    r = evaluate(gpsd_logs, replay.path, seed=seed, rate=0.25)
    print(f"\nseed {seed}: {r['damaged']} damaged sentences in {r['logs']} logs; "
          f"guard rejected {r['guard_rejected_pct']:.2f}%, naive {r['naive_rejected_pct']:.2f}%")
    assert r["logs"] > 90
    assert r["damaged"] > 2000
    # The residue is the XOR checksum's blind spot: dropping an even run of
    # identical bytes (",," or "00") leaves the checksum unchanged.
    assert r["guard_rejected_pct"] >= 99.0
    assert r["naive_passed"] > 10 * r["guard_passed"]
