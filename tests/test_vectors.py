"""The firmware test vectors (firmware/vectors.inc) and how they are made.

The runner itself is checked by ctest (firmware_vectors_host) and on QEMU by
`make -C firmware qemu-test`; this checks what it is checked against.
"""

import re
import subprocess
import sys
from pathlib import Path

import pytest

from gen_vectors import OTHER, REJECTED, TYPE_CODE, verdict

ROOT = Path(__file__).resolve().parent.parent
VECTORS = ROOT / "firmware" / "vectors.inc"


def test_committed_vectors_match_the_logs(gpsd_logs):
    proc = subprocess.run([sys.executable, str(ROOT / "scripts" / "gen_vectors.py"), "--check"],
                          capture_output=True, text=True)
    assert proc.returncode == 0, proc.stderr


def test_vectors_cover_every_outcome():
    counts = {}
    for code in re.findall(r'^    \{".*", (\d), ', VECTORS.read_text(), re.M):
        counts[int(code)] = counts.get(int(code), 0) + 1
    assert set(counts) == {REJECTED, OTHER, *TYPE_CODE.values()}
    assert sum(counts.values()) > 500
    assert all(n >= 30 for n in counts.values()), counts


@pytest.mark.parametrize("line, expected", [
    (b"$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47", (TYPE_CODE["GGA"], "GGA")),
    (b"$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*48", (REJECTED, None)),
    (b"$GPGGA,123519,4807.038,N,0113.000,E,1,08,0.9,545.4,M,46.9,M,,*41", (REJECTED, None)),
    (b"$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,", (REJECTED, None)),
    (b"$GPGSV,1,1,00*79", (OTHER, None)),
    # Not a single printable sentence: left out of the vectors.
    (b"junk$GPGSV,1,1,00*79", None),
    (b"$GPGSV,1,1,00*79$GPGSV,1,1,00*79", None),
])
def test_verdicts(line, expected):
    assert verdict(line) == expected
