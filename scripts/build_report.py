#!/usr/bin/env python3
"""Build docs/index.html, the one-page report, from the committed results.

Every number on the page comes from results/rejection.json (the fault
injection run, scripts/rejection_eval.py) or results/firmware-size.json (the
Cortex-M3 build and QEMU run, make -C firmware qemu-test). The page is a
single static file with inline CSS, so it can be opened straight from the
repository or served by GitHub Pages from docs/.

    python3 scripts/build_report.py            # rewrite docs/index.html
    python3 scripts/build_report.py --check    # exit 1 if it is out of date
"""

from __future__ import annotations

import argparse
import json
import sys
from html import escape
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REJECTION = ROOT / "results" / "rejection.json"
FIRMWARE = ROOT / "results" / "firmware-size.json"
OUT = ROOT / "docs" / "index.html"

FAULT_LABELS = {
    "dropped_chars": ("Dropped bytes", "1 to 3 bytes lost from the line, as when the UART FIFO fills"),
    "truncated_read": ("Overrun", "the line is cut and the stream resumes inside the next one"),
    "bit_flip": ("Bit flips", "1 or 2 bits flipped anywhere, line ending included"),
    "missing_line_end": ("Lost line end", "CR/LF removed, so two sentences run together"),
}

PART_LABELS = {
    "nmea_guard": "nmea-guard library",
    "soft_float": "libgcc soft-float (double maths)",
    "libc": "newlib-nano (memcpy, memchr, ...)",
    "startup_and_app": "startup, UART loop, Parser object",
}

CSS = """
body { font: 16px/1.5 system-ui, -apple-system, "Segoe UI", sans-serif; color: #1f2328;
       max-width: 52rem; margin: 2rem auto; padding: 0 1rem; }
h1 { font-size: 1.6rem; margin-bottom: .2rem; }
h2 { font-size: 1.2rem; margin-top: 2.2rem; border-bottom: 1px solid #d0d7de; padding-bottom: .3rem; }
p.lead { color: #57606a; margin-top: 0; }
table { border-collapse: collapse; width: 100%; margin: .8rem 0; font-variant-numeric: tabular-nums; }
th, td { padding: .35rem .6rem; border-bottom: 1px solid #d0d7de; text-align: right; white-space: nowrap; }
td.note { white-space: normal; }
th:first-child, td:first-child { text-align: left; }
th { background: #f6f8fa; font-weight: 600; }
td.note { text-align: left; color: #57606a; font-size: .9rem; }
.bar { display: inline-block; height: .7rem; vertical-align: middle; border-radius: 2px; }
.guard { background: #1f6feb; }
.naive { background: #d1242f; }
code { font-size: .9em; background: #f6f8fa; padding: .1rem .3rem; border-radius: 4px; }
footer { margin-top: 3rem; color: #57606a; font-size: .85rem; }
"""


def n(v: int) -> str:
    return f"{v:,}"


def pct(passed: int, damaged: int) -> str:
    return f"{100 * (1 - passed / damaged):.2f}%"


def bar(cls: str, passed: int, worst: int) -> str:
    width = 0 if worst == 0 else max(1, round(120 * passed / worst)) if passed else 0
    return f'<span class="bar {cls}" style="width:{width}px"></span>'


def rejection_section(rej: dict) -> str:
    worst = max(f["naive_passed"] for f in rej["faults"].values())
    rows = []
    for key, f in rej["faults"].items():
        label, note = FAULT_LABELS[key]
        rows.append(
            f"<tr><td>{escape(label)}</td><td>{n(f['damaged'])}</td>"
            f"<td>{n(f['guard_passed'])} {bar('guard', f['guard_passed'], worst)}</td>"
            f"<td>{n(f['naive_passed'])} {bar('naive', f['naive_passed'], worst)}</td>"
            f"<td>{pct(f['guard_passed'], f['damaged'])}</td><td>{pct(f['naive_passed'], f['damaged'])}</td></tr>"
            f'<tr><td class="note" colspan="6">{escape(note)}</td></tr>')
    a = rej["all_faults"]
    rows.append(
        f"<tr><th>All faults</th><th>{n(a['damaged'])}</th><th>{n(a['guard_passed'])}</th>"
        f"<th>{n(a['naive_passed'])}</th><th>{pct(a['guard_passed'], a['damaged'])}</th>"
        f"<th>{pct(a['naive_passed'], a['damaged'])}</th></tr>")
    seeds = ", ".join(str(s) for s in rej["seeds"])
    recovered = rej["faults"]["missing_line_end"]
    return f"""
<h2 id="rejection">Damaged sentences that got through</h2>
<p>A quarter of the clean GGA, RMC, GSA and VTG sentences in {rej['logs']} gpsd receiver logs were damaged
with one fault at a time (seeds {seeds}), and each damaged log was replayed through nmea-guard and through a
naive line parser that only checks <code>*hh</code> at the end of a line. A damaged sentence "got through"
if the parser accepted it as a position, time or speed.</p>
<table>
<tr><th>Fault</th><th>Damaged</th><th>nmea-guard accepted</th><th>naive accepted</th>
<th>nmea-guard rejected</th><th>naive rejected</th></tr>
{''.join(rows)}
</table>
<p>When only the line end is lost, nmea-guard still decodes the first of the two run-together sentences on its
checksum: {n(recovered['guard_recovered'])} of {n(recovered['damaged'])} came back, against
{n(recovered['naive_recovered'])} for the naive parser. Nearly everything that got past nmea-guard had lost
bytes or flipped bits that cancel out under XOR and left well-formed fields behind; the details are in
<a href="corruption-eval.md">corruption-eval.md</a>.</p>
"""


def firmware_section(fw: dict) -> str:
    p = fw["probe"]
    rows = "".join(
        f"<tr><td>{escape(PART_LABELS[k])}</td><td>{n(v)}</td><td>{n(fw['ram_by_part'][k])}</td></tr>"
        for k, v in fw["flash_by_part"].items())
    files = "".join(f"<tr><td><code>src/{escape(k)}.cpp</code></td><td>{n(v)}</td></tr>"
                    for k, v in fw["library_flash_by_file"].items())
    q = fw["qemu"]
    run = ""
    if q:
        run = f"""
<p>On QEMU's <code>mps2-an385</code> board the runner fed {n(q['vectors'])} test vectors from the gpsd logs
(clean sentences, other types, and copies with dropped bytes or flipped bits) to one Parser, byte by byte
with CR/LF, and {n(q['vectors'] - q['failed'])} produced the expected event. All {n(q['values_compared'])}
decoded numbers matched pynmea2, {n(q['values_bit_exact'])} of them to the last bit, even though the
Cortex-M3 does its double arithmetic in software. {q['scenarios_passed']} of {q['scenarios']} scenarios
passed (buffer overflow, reset after an overrun, two sentences run together, a fix built from one epoch).</p>
<table>
<tr><th>Measured on the target</th><th>Bytes</th></tr>
<tr><td><code>sizeof(nmea_guard::Parser)</code></td><td>{n(q['sizeof_parser'])}</td></tr>
<tr><td>Stack used while feeding every vector</td><td>{n(q['feed_stack_bytes'])}</td></tr>
</table>"""
    return f"""
<h2 id="firmware">Cortex-M3 build</h2>
<p>The size probe is the smallest realistic firmware: it polls the board's UART, feeds every byte to a static
Parser and keeps the last valid position. Built with {escape(fw['compiler'])}, it needs
{n(p['flash_bytes'])} bytes of flash and {n(p['ram_bytes'])} bytes of RAM, plus the stack. No heap is linked
in at all.</p>
<table>
<tr><th>Part of the image</th><th>Flash</th><th>RAM</th></tr>
{rows}
<tr><th>Total</th><th>{n(p['flash_bytes'])}</th><th>{n(p['ram_bytes'])}</th></tr>
</table>
<table>
<tr><th>Library flash by file</th><th>Bytes</th></tr>
{files}
</table>
{run}
"""


def render(rej: dict, fw: dict) -> str:
    return f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>nmea-guard: rejection and firmware results</title>
<style>{CSS}</style>
</head>
<body>
<h1>nmea-guard results</h1>
<p class="lead">A heap-free C++17 NMEA 0183 parser that rejects garbled GPS sentences and recovers from serial
overruns. This page is generated by <code>scripts/build_report.py</code> from the files in
<code>results/</code>.</p>
{rejection_section(rej)}
{firmware_section(fw)}
<footer>Receiver logs: the <a href="https://gitlab.com/gpsd/gpsd">gpsd</a> project's regression suite
(BSD licence). Method: <a href="corruption-eval.md">corruption-eval.md</a> and
<a href="firmware.md">firmware.md</a>.</footer>
</body>
</html>
"""


def build() -> str:
    return render(json.loads(REJECTION.read_text()), json.loads(FIRMWARE.read_text()))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--check", action="store_true", help="compare with docs/index.html instead of writing")
    args = ap.parse_args()
    html = build()
    if args.check:
        if not OUT.exists() or OUT.read_text() != html:
            print("docs/index.html is out of date; rerun scripts/build_report.py", file=sys.stderr)
            return 1
        return 0
    OUT.write_text(html)
    print(f"wrote {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
