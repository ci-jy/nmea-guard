import json
import os
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
LOG_DIR = ROOT / "data" / "gpsd"


def _replay_binary() -> Path:
    env = os.environ.get("NMEA_REPLAY")
    if env:
        return Path(env)
    return ROOT / "build" / "nmea-replay"


@pytest.fixture(scope="session")
def replay():
    exe = _replay_binary()
    if not exe.exists():
        pytest.fail(f"{exe} not found; build first with: cmake -S . -B build && cmake --build build")

    def run(*args: str, check: bool = True, stdin: bytes | None = None) -> str:
        proc = subprocess.run([str(exe), *args], input=stdin, capture_output=True, check=False)
        if check and proc.returncode != 0:
            raise AssertionError(f"nmea-replay {' '.join(args)} failed: {proc.stderr.decode()}")
        return proc.stdout.decode("ascii")

    def summary(*args: str) -> dict:
        return json.loads(run(*args))

    run.summary = summary
    return run


@pytest.fixture(scope="session")
def gpsd_logs() -> list[Path]:
    logs = sorted(LOG_DIR.glob("*.log"))
    if not logs:
        pytest.fail("no gpsd logs in data/gpsd; run: bash scripts/fetch_logs.sh")
    return logs
