"""docs/index.html, the report page, and the results it is built from.

The page has to be what scripts/build_report.py makes of results/ right now,
every figure on it has to agree with those files, and the files have to
agree with themselves. If `make -C firmware qemu-test` has been run, its
fresh result has to agree with the committed one too.
"""

import json
import re
from html.parser import HTMLParser
from pathlib import Path

import pytest

from build_report import FAULT_LABELS, PART_LABELS, build

ROOT = Path(__file__).resolve().parent.parent
PAGE = ROOT / "docs" / "index.html"
REJECTION = json.loads((ROOT / "results" / "rejection.json").read_text())
FIRMWARE = json.loads((ROOT / "results" / "firmware-size.json").read_text())
FRESH_FIRMWARE = ROOT / "firmware" / "build" / "firmware-size.json"

VOID = {"meta", "br", "img", "link", "hr", "input"}


class Page(HTMLParser):
    def __init__(self):
        super().__init__()
        self.stack, self.errors, self.links, self.sources = [], [], [], []
        self.text, self.rows, self._row = [], [], None

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag == "a":
            self.links.append(attrs.get("href", ""))
        for key in ("src", "srcset"):
            if key in attrs:
                self.sources.append(attrs[key])
        if tag == "link":
            self.sources.append(attrs.get("href", ""))
        if tag == "tr":
            self._row = []
        if tag not in VOID:
            self.stack.append(tag)

    def handle_endtag(self, tag):
        if not self.stack or self.stack[-1] != tag:
            self.errors.append(f"</{tag}> closes <{self.stack[-1] if self.stack else None}>")
            return
        self.stack.pop()
        if tag == "tr" and self._row is not None:
            self.rows.append(self._row)
            self._row = None

    def handle_data(self, data):
        self.text.append(data)
        if self._row is not None and self.stack and self.stack[-1] in ("td", "th", "code") and data.strip():
            self._row.append(data.strip())


@pytest.fixture(scope="module")
def page():
    p = Page()
    p.feed(PAGE.read_text())
    p.close()
    return p


def row(page, label):
    matches = [r for r in page.rows if r and r[0] == label]
    assert len(matches) == 1, f"expected one row for {label!r}, found {matches}"
    return matches[0]


def num(s):
    return int(s.replace(",", ""))


def test_page_is_up_to_date():
    assert PAGE.read_text() == build(), "rerun: python3 scripts/build_report.py"


def test_page_is_well_formed_and_self_contained(page):
    assert not page.errors
    assert page.stack == []
    # Opens from a checkout with no network: no scripts, images or stylesheets.
    assert page.sources == []
    assert "<script" not in PAGE.read_text()
    for href in page.links:
        if href.startswith("https://"):
            continue
        assert (PAGE.parent / href).is_file(), f"broken link {href}"


def test_rejection_table_matches_results(page):
    for key, label in ((k, v[0]) for k, v in FAULT_LABELS.items()):
        f = REJECTION["faults"][key]
        damaged, guard, naive, guard_pct, naive_pct = row(page, label)[1:]
        assert (num(damaged), num(guard), num(naive)) == (f["damaged"], f["guard_passed"], f["naive_passed"])
        assert guard_pct == f"{f['guard_rejected_pct']:.2f}%"
        assert naive_pct == f"{f['naive_rejected_pct']:.2f}%"
    a = REJECTION["all_faults"]
    total = row(page, "All faults")
    assert [num(x) for x in total[1:4]] == [a["damaged"], a["guard_passed"], a["naive_passed"]]
    assert total[4] == f"{a['guard_rejected_pct']:.2f}%"


def test_rejection_results_add_up():
    faults = REJECTION["faults"].values()
    for key in ("damaged", "guard_passed", "naive_passed", "guard_recovered", "naive_recovered"):
        assert sum(f[key] for f in faults) == REJECTION["all_faults"][key]
    for f in faults:
        assert sum(s["damaged"] for s in f["per_seed"]) == f["damaged"]
        assert f["guard_passed"] <= f["naive_passed"]


def test_firmware_table_matches_results(page):
    for key, label in PART_LABELS.items():
        flash, ram = row(page, label)[1:]
        assert (num(flash), num(ram)) == (FIRMWARE["flash_by_part"][key], FIRMWARE["ram_by_part"][key])
    total = row(page, "Total")
    assert (num(total[1]), num(total[2])) == (FIRMWARE["probe"]["flash_bytes"], FIRMWARE["probe"]["ram_bytes"])
    q = FIRMWARE["qemu"]
    assert num(row(page, "sizeof(nmea_guard::Parser)")[1]) == q["sizeof_parser"]
    text = " ".join(" ".join(page.text).split())
    assert f"fed {q['vectors']:,} test vectors" in text
    assert f"and {q['vectors'] - q['failed']:,} produced the expected event" in text


def test_firmware_results_are_consistent():
    p = FIRMWARE["probe"]
    assert sum(FIRMWARE["flash_by_part"].values()) == p["flash_bytes"]
    assert sum(FIRMWARE["ram_by_part"].values()) == p["ram_bytes"]
    assert sum(FIRMWARE["library_flash_by_file"].values()) == FIRMWARE["flash_by_part"]["nmea_guard"]
    # Linker padding only: the parts account for the whole image.
    assert 0 <= p["image_flash_bytes"] - p["flash_bytes"] < 64
    assert FIRMWARE["heap_or_exception_symbols"] == []
    # The library itself owns no RAM; the Parser object is the application's.
    assert FIRMWARE["ram_by_part"]["nmea_guard"] == 0
    assert FIRMWARE["ram_by_part"]["startup_and_app"] >= FIRMWARE["qemu"]["sizeof_parser"]


def test_qemu_run_passed_on_every_committed_vector():
    q = FIRMWARE["qemu"]
    header = (ROOT / "firmware" / "vectors.inc").read_text().splitlines()[2]
    committed = int(re.match(r"// (\d+) vectors", header).group(1))
    assert q["passed"] and q["failed"] == 0
    assert q["vectors"] == committed
    assert q["scenarios_passed"] == q["scenarios"] == 4
    assert q["values_bit_exact"] <= q["values_compared"]


def test_fresh_firmware_run_agrees_with_committed_results():
    if not FRESH_FIRMWARE.exists():
        pytest.skip("no firmware build; run: make -C firmware qemu-test")
    fresh = json.loads(FRESH_FIRMWARE.read_text())
    assert fresh["qemu"] is not None and fresh["qemu"]["passed"]
    assert fresh["heap_or_exception_symbols"] == []
    # Sizes move with the compiler version; what the run checked must not.
    for key in ("vectors", "failed", "values_compared", "scenarios_passed", "scenarios"):
        assert fresh["qemu"][key] == FIRMWARE["qemu"][key], key
