"""The replay CLI over every downloaded gpsd log."""

import json


def test_replays_every_downloaded_log(replay, gpsd_logs):
    out = replay.summary(*map(str, gpsd_logs))
    assert out["parser"] == "guard"
    assert len(out["files"]) == len(gpsd_logs)
    total = out["total"]
    assert total["files"] == len(gpsd_logs)
    # Per-file counts add up to the totals.
    for key in ("bytes", "accepted", "other", "rejected", "epochs", "valid_fixes"):
        assert sum(f[key] for f in out["files"]) == total[key], key
    assert sum(total["accepted_by_type"].values()) == total["accepted"]
    assert sum(total["rejected_by_reason"].values()) == total["rejected"]
    # Every byte of every log was fed through.
    assert total["bytes"] == sum(p.stat().st_size for p in gpsd_logs)
    # GP, GN and GL talkers all occur in the gpsd corpus.
    for talker in ("GP", "GN", "GL"):
        assert total["accepted_by_talker"].get(talker, 0) > 0, talker
    assert total["valid_fixes"] > 1000


def test_fix_tracks_receiver_losing_lock(replay, gpsd_logs):
    # This BU-353 capture has a fix in the middle and loses it at the end:
    # the last epoch keeps the date and time but reports no position.
    log = next(p for p in gpsd_logs if p.name == "bu353-glonass.log")
    f = replay.summary(str(log))["files"][0]
    assert f["rejected"] == 0
    assert f["epochs"] == 20 and f["valid_fixes"] == 9
    fix = f["last_fix"]
    assert fix["valid"] is False
    assert fix["latitude"] is None and fix["fix_type"] == 1
    assert fix["time"] == "08:14:29.000000"
    assert fix["date"] == {"day": 26, "month": 5, "year": 14}


def test_naive_parser_summary(replay, gpsd_logs):
    out = replay.summary("--parser", "naive", *map(str, gpsd_logs[:5]))
    assert out["parser"] == "naive"
    assert set(out["total"]) == {"files", "bytes", "accepted"}


def test_reads_stdin_and_counts_rejections(replay):
    data = (b"$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n"
            b"$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,\r\n"
            b"$GPGGA,123519,07.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n")
    out = json.loads(replay("-", stdin=data))
    total = out["total"]
    assert total["accepted"] == 1
    assert total["rejected_by_reason"] == {"missing_checksum": 1, "checksum_mismatch": 1}


def test_missing_file_fails(replay):
    out = replay("does-not-exist.log", check=False)
    assert '"files": [' in out
