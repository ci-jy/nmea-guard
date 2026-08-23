"""Differential test: nmea-guard against pynmea2 on real gpsd receiver logs.

For every log, the set of sentences nmea-guard decodes must be exactly the
clean sentences (see nmea_reference.py), and on each of them every decoded
field must agree with what pynmea2 extracts from the same text.
"""

import json
import math
from collections import Counter

import pynmea2
import pytest

from nmea_reference import framed_sentences, is_clean


def raw_field(msg, name):
    names = [f[1] for f in msg.fields]
    i = names.index(name)
    return msg.data[i] if i < len(msg.data) else ""


def num(s):
    return float(s) if s not in ("", None) else None


def assert_close(ours, theirs, what, raw):
    if theirs is None:
        assert ours is None, f"{what}: ours={ours} pynmea2=None in {raw}"
        return
    assert ours is not None, f"{what}: ours=None pynmea2={theirs} in {raw}"
    assert math.isclose(ours, theirs, rel_tol=0, abs_tol=1e-12), f"{what}: ours={ours} pynmea2={theirs} in {raw}"


def assert_time(ours, msg, raw):
    ts = msg.timestamp
    if ts is None:
        assert ours is None, raw
        return
    expected = f"{ts.hour:02d}:{ts.minute:02d}:{ts.second:02d}.{ts.microsecond:06d}"
    assert ours == expected, f"time: ours={ours} pynmea2={expected} in {raw}"


def assert_position(rec, msg, lat_field, lon_field, raw):
    # pynmea2 reports 0.0 for a blank position; nmea-guard reports null.
    if raw_field(msg, lat_field):
        assert_close(rec["latitude"], msg.latitude, "latitude", raw)
    else:
        assert rec["latitude"] is None, raw
    if raw_field(msg, lon_field):
        assert_close(rec["longitude"], msg.longitude, "longitude", raw)
    else:
        assert rec["longitude"] is None, raw


def compare(rec, msg):
    raw = rec["raw"]
    assert rec["type"] == msg.sentence_type
    assert rec["talker"] == msg.talker
    kind = rec["type"]
    if kind == "GGA":
        assert_time(rec["time"], msg, raw)
        assert_position(rec, msg, "lat", "lon", raw)
        assert rec["quality"] == (int(msg.gps_qual) if raw_field(msg, "gps_qual") else None), raw
        assert rec["satellites"] == (int(raw_field(msg, "num_sats")) if raw_field(msg, "num_sats") else None), raw
        assert_close(rec["hdop"], num(raw_field(msg, "horizontal_dil")), "hdop", raw)
        assert_close(rec["altitude_m"], num(raw_field(msg, "altitude")), "altitude", raw)
        assert_close(rec["geoid_separation_m"], num(raw_field(msg, "geo_sep")), "geo_sep", raw)
        assert_close(rec["dgps_age_s"], num(raw_field(msg, "age_gps_data")), "age", raw)
    elif kind == "RMC":
        assert_time(rec["time"], msg, raw)
        assert rec["status"] == (raw_field(msg, "status") or None), raw
        assert_position(rec, msg, "lat", "lon", raw)
        assert_close(rec["speed_knots"], num(raw_field(msg, "spd_over_grnd")), "speed", raw)
        assert_close(rec["course_deg"], num(raw_field(msg, "true_course")), "course", raw)
        d = msg.datestamp
        if d is None:
            assert rec["date"] is None, raw
        else:
            assert rec["date"] == {"day": d.day, "month": d.month, "year": d.year % 100}, raw
        var = num(raw_field(msg, "mag_variation"))
        if var is not None and raw_field(msg, "mag_var_dir") == "W":
            var = -var
        assert_close(rec["magnetic_variation_deg"], var, "mag_variation", raw)
        assert rec["mode"] == (raw_field(msg, "mode_indicator") or None), raw
    elif kind == "GSA":
        assert rec["selection"] == (raw_field(msg, "mode") or None), raw
        assert rec["fix_type"] == (int(raw_field(msg, "mode_fix_type")) if raw_field(msg, "mode_fix_type") else None), raw
        prns = [int(raw_field(msg, f"sv_id{i:02d}")) for i in range(1, 13) if raw_field(msg, f"sv_id{i:02d}")]
        assert rec["prns"] == prns, raw
        for key, name in (("pdop", "pdop"), ("hdop", "hdop"), ("vdop", "vdop")):
            assert_close(rec[key], num(raw_field(msg, name)), key, raw)
    elif kind == "VTG":
        assert_close(rec["course_true_deg"], num(raw_field(msg, "true_track")), "true_track", raw)
        assert_close(rec["course_magnetic_deg"], num(raw_field(msg, "mag_track")), "mag_track", raw)
        assert_close(rec["speed_knots"], num(raw_field(msg, "spd_over_grnd_kts")), "kts", raw)
        assert_close(rec["speed_kmh"], num(raw_field(msg, "spd_over_grnd_kmph")), "kmph", raw)
        assert rec["mode"] == (raw_field(msg, "faa_mode") or None), raw
    else:
        pytest.fail(f"unexpected type {kind}")


def test_decoded_fields_match_pynmea2_on_every_clean_sentence(replay, gpsd_logs):
    compared = Counter()
    for log in gpsd_logs:
        clean = Counter(text for text, body in framed_sentences(log.read_bytes()) if is_clean(body))
        records = [json.loads(line) for line in replay("--sentences", str(log)).splitlines()]
        decoded = Counter(r["raw"] for r in records)

        missed = clean - decoded
        extra = decoded - clean
        assert not missed, f"{log.name}: clean sentences not decoded: {list(missed)[:3]}"
        assert not extra, f"{log.name}: decoded sentences that are not clean: {list(extra)[:3]}"

        for rec in records:
            compare(rec, pynmea2.parse(rec["raw"], check=True))
            compared[rec["type"]] += 1

    total = sum(compared.values())
    print(f"\ncompared {total} clean sentences with pynmea2: {dict(compared)}")
    # The pinned gpsd release has thousands of each type; a collapse here
    # means the fetch or the framer broke, not that everything agreed.
    assert total > 10000
    assert all(compared[t] > 1000 for t in ("GGA", "RMC", "GSA", "VTG"))


def test_checksum_valid_but_malformed_sentences_are_rejected(replay, gpsd_logs):
    """Real receivers do emit sentences with a correct checksum and impossible
    fields (89 degrees 60 minutes, a 360-degree latitude, a date of '2208-1').
    nmea-guard rejects them; this pins down that it is a small, known set."""
    malformed = []
    for log in gpsd_logs:
        for text, body in framed_sentences(log.read_bytes()):
            if not is_clean(body) and body[2:5] in ("GGA", "RMC", "GSA", "VTG") and body[0] != "P":
                malformed.append((log.name, text))
    print(f"\n{len(malformed)} checksum-valid but malformed sentences")
    for name, text in malformed[:5]:
        print(f"  {name}: {text}")
    assert 0 < len(malformed) < 200
