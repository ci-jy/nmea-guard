#!/usr/bin/env python3
"""Flash and RAM report for the Cortex-M3 build, plus the QEMU run's result.

Reads firmware/build/probe.map (the size probe: a UART polling loop feeding a
static Parser) and attributes every input section the linker kept to the
part of the image it came from: the nmea-guard library, libgcc's soft-float
routines, newlib, or the probe's own startup and main loop. Then folds in
the RESULT line from firmware/build/qemu.log, if the runner has been run.

    python3 scripts/firmware_report.py [--build firmware/build] [-o OUT.json]

`make -C firmware size` and `make -C firmware qemu-test` call it.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

FLASH_SECTIONS = {".isr_vector", ".text", ".ARM.exidx"}
LOADED_RAM_SECTIONS = {".data"}  # in RAM, with its initial values in flash
RAM_SECTIONS = {".bss"}

_OUTPUT = re.compile(r"^(\.[\w.]+)\s+0x[0-9a-f]+\s+0x([0-9a-f]+)")
_INPUT = re.compile(r"^ (\.[\w.$]+|COMMON)?\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$")
_INPUT_NAME_ONLY = re.compile(r"^ (\.[\w.$]+|COMMON)$")


def part_of(path: str) -> str:
    if "/lib/" in path and path.startswith(("build/lib/", "./build/lib/")):
        return "nmea_guard"
    if "libgcc.a" in path:
        return "soft_float"
    if "libc" in path or "libnosys" in path:
        return "libc"
    return "startup_and_app"


def library_object(path: str) -> str | None:
    m = re.search(r"build/lib/(\w+)\.o$", path)
    return m.group(1) if m else None


def parse_map(text: str):
    """Sizes per (output section, input file) from a GNU ld map file."""
    body = text.split("Linker script and memory map", 1)[1]
    section = None
    pending_name = False
    sizes: list[tuple[str, str, int]] = []
    output_sizes: dict[str, int] = {}
    for line in body.splitlines():
        m = _OUTPUT.match(line)
        if m:
            section = m.group(1)
            output_sizes[section] = int(m.group(2), 16)
            pending_name = False
            continue
        if _INPUT_NAME_ONLY.match(line):
            pending_name = True
            continue
        m = _INPUT.match(line)
        if m and section is not None and (m.group(1) or pending_name):
            size = int(m.group(3), 16)
            if size:
                sizes.append((section, m.group(4).strip(), size))
        pending_name = False
    return sizes, output_sizes


def read_result(log: Path) -> dict | None:
    if not log.exists():
        return None
    m = re.search(r"^RESULT (.*)$", log.read_text(errors="replace"), re.M)
    if not m:
        return None
    fields = dict(kv.split("=", 1) for kv in m.group(1).split())
    passed, total = (int(x) for x in fields["scenarios"].split("/"))
    return {
        "vectors": int(fields["vectors"]),
        "failed": int(fields["failed"]),
        "values_compared": int(fields["values"]),
        "values_bit_exact": int(fields["bit_exact"]),
        "scenarios_passed": passed,
        "scenarios": total,
        "sizeof_parser": int(fields["sizeof_parser"]),
        "feed_stack_bytes": int(fields["stack_bytes"]),
        "passed": "PASS" in log.read_text(errors="replace").splitlines()[-1:],
    }


def compiler_version(cxx: str) -> str:
    try:
        out = subprocess.run([cxx, "-dumpversion"], capture_output=True, text=True, check=True).stdout
        return out.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


# Anything here in the linked image would mean the no-heap, no-exceptions
# promise broke somewhere between the library and the toolchain.
FORBIDDEN = re.compile(r"^(malloc|free|calloc|realloc|_sbrk|_sbrk_r|_malloc_r|abort|__cxa_throw|_Znwj|_Znaj|_ZdlPv)$")


def forbidden_symbols(elf: Path, cxx: str) -> list[str]:
    nm = cxx.removesuffix("g++") + "nm"
    out = subprocess.run([nm, str(elf)], capture_output=True, text=True, check=True).stdout
    return sorted({parts[-1] for parts in (l.split() for l in out.splitlines())
                   if parts and FORBIDDEN.match(parts[-1])})


def report(build: Path, cxx: str) -> dict:
    sizes, output_sizes = parse_map((build / "probe.map").read_text())
    flash = {k: 0 for k in ("nmea_guard", "soft_float", "libc", "startup_and_app")}
    ram = dict(flash)
    per_object: dict[str, int] = {}
    for section, path, size in sizes:
        part = part_of(path)
        if section in FLASH_SECTIONS or section in LOADED_RAM_SECTIONS:
            flash[part] += size
            obj = library_object(path)
            if obj:
                per_object[obj] = per_object.get(obj, 0) + size
        if section in LOADED_RAM_SECTIONS or section in RAM_SECTIONS:
            ram[part] += size
    out = {
        "target": "Cortex-M3, QEMU mps2-an385",
        "compiler": f"arm-none-eabi-g++ {compiler_version(cxx)} -Os, newlib-nano, --gc-sections",
        "probe": {
            "flash_bytes": sum(flash.values()),
            "ram_bytes": sum(ram.values()),
            "stack_reserved_bytes": output_sizes.get(".stack", 0),
            # Alignment padding the linker added between input sections.
            "image_flash_bytes": sum(output_sizes.get(s, 0) for s in FLASH_SECTIONS | LOADED_RAM_SECTIONS),
        },
        "flash_by_part": flash,
        "ram_by_part": ram,
        "library_flash_by_file": dict(sorted(per_object.items())),
        "heap_or_exception_symbols": forbidden_symbols(build / "probe.elf", cxx),
        "qemu": read_result(build / "qemu.log"),
    }
    return out


def print_table(r: dict) -> None:
    p = r["probe"]
    print(f"{r['target']}, {r['compiler']}")
    print(f"  size probe: {p['flash_bytes']} B flash, {p['ram_bytes']} B RAM "
          f"(+ {p['stack_reserved_bytes']} B stack reserved)")
    for part in r["flash_by_part"]:
        print(f"    {part:<16} {r['flash_by_part'][part]:>6} B flash {r['ram_by_part'][part]:>6} B RAM")
    bad = r["heap_or_exception_symbols"]
    print(f"  heap/exception symbols linked: {', '.join(bad) if bad else 'none'}")
    q = r["qemu"]
    if q:
        print(f"  QEMU run: {q['vectors'] - q['failed']}/{q['vectors']} vectors, "
              f"{q['scenarios_passed']}/{q['scenarios']} scenarios, "
              f"Parser {q['sizeof_parser']} B, feed() stack {q['feed_stack_bytes']} B")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--build", type=Path, default=ROOT / "firmware" / "build")
    ap.add_argument("--cxx", default="arm-none-eabi-g++")
    ap.add_argument("-o", "--output", type=Path)
    args = ap.parse_args()
    if not (args.build / "probe.map").exists():
        print(f"{args.build}/probe.map not found; run: make -C firmware", file=sys.stderr)
        return 1
    r = report(args.build, args.cxx)
    print_table(r)
    if args.output:
        args.output.write_text(json.dumps(r, indent=2) + "\n")
    return 1 if r["heap_or_exception_symbols"] else 0


if __name__ == "__main__":
    sys.exit(main())
