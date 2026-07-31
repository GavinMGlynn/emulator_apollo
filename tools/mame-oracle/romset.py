#!/usr/bin/env python3
"""Assemble MAME ROM sets for the apollo driver from roms/firmware/.

The oracle needs its ROMs laid out under names and a directory structure MAME
recognises. Ours are bitsavers dumps with bitsavers' capitalisation, and
`roms/` is gitignored because Apollo firmware is not ours to redistribute --
so the layout MAME wants cannot be checked in and has to be produced.

Two decisions worth stating, because both are what keep this honest:

**The ROM table is parsed out of the driver, never transcribed.** Hardcoding
`dn3500 needs 3500_boot_12191_7.bin` would be correct exactly until the
`ext/mame` pin moves, and would then be silently wrong -- assembling a set MAME
no longer wants, for a driver that has changed under us. Reading `apollo.cpp`
means the table is whatever the pinned oracle actually asks for.

**Our files are matched to MAME's by SHA-1, never by name.** Name matching
would need a case-folding rule, then an exception for the file that does not
follow it. The SHA-1 is in the driver already and is the thing that actually
has to hold: if it matches, it is bit-for-bit the ROM the oracle expects, and
what bitsavers chose to call it does not matter. A name collision cannot
produce a false match and a rename cannot produce a false miss.

Usage:
    romset.py [--driver PATH] [--firmware DIR] [--out DIR] [--machine NAME]...
    romset.py --list          # show what each machine needs, assemble nothing

Exit status 0 if every requested machine was assembled, 1 if any was
incomplete, 2 on a usage or parse problem.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent

DEFAULT_DRIVER = REPO / "ext" / "mame" / "src" / "mame" / "apollo" / "apollo.cpp"
DEFAULT_FIRMWARE = REPO / "roms" / "firmware"
DEFAULT_OUT = HERE / "out" / "roms"

# ROM_START( name )  ...  ROM_END, tolerating the driver's inconsistent spacing
# (`ROM_START( dn3000)` has no trailing space, in the pinned revision).
ROM_START_RE = re.compile(r"^\s*ROM_START\s*\(\s*(\w+)\s*\)", re.M)
ROM_END_RE = re.compile(r"^\s*ROM_END", re.M)

# ROMX_LOAD( "file", offset, length, CRC(x) SHA1(y), ROM_BIOS(n) )
# ROM_LOAD ( "file", offset, length, CRC(x) SHA1(y) )
ROM_LOAD_RE = re.compile(
    r"ROMX?_LOAD\s*\(\s*"
    r'"([^"]+)"\s*,\s*'          # file name
    r"([0-9a-fA-Fx]+)\s*,\s*"    # offset (unused, but consumed positionally)
    r"([0-9a-fA-Fx]+)\s*,\s*"    # length
    r"CRC\(\s*([0-9a-fA-F]+)\s*\)\s*"
    r"SHA1\(\s*([0-9a-fA-F]+)\s*\)",
)

# #define rom_dsp3500    rom_dn3500
ALIAS_RE = re.compile(r"^\s*#define\s+rom_(\w+)\s+rom_(\w+)\s*$", re.M)


class Rom:
    """One ROM a machine needs: the name MAME looks for, and its identity."""

    def __init__(self, name: str, length: int, crc: str, sha1: str):
        self.name = name
        self.length = length
        self.crc = crc.lower()
        self.sha1 = sha1.lower()

    def __repr__(self) -> str:
        return "Rom(%s, %d, %s)" % (self.name, self.length, self.sha1[:8])


def parse_driver(path: Path) -> dict:
    """Return {machine: [Rom, ...]} for every ROM_START in the driver."""
    try:
        text = path.read_text(errors="replace")
    except OSError as exc:
        sys.stderr.write("romset: cannot read driver %s: %s\n" % (path, exc))
        raise SystemExit(2)

    sets: dict = {}
    for start in ROM_START_RE.finditer(text):
        machine = start.group(1)
        end = ROM_END_RE.search(text, start.end())
        if end is None:
            sys.stderr.write(
                "romset: ROM_START(%s) at offset %d has no ROM_END\n"
                % (machine, start.start())
            )
            raise SystemExit(2)
        body = text[start.end():end.start()]
        roms = [
            Rom(name, int(length, 0), crc, sha1)
            for name, _offset, length, crc, sha1 in ROM_LOAD_RE.findall(body)
        ]
        sets[machine] = roms

    if not sets:
        sys.stderr.write(
            "romset: no ROM_START found in %s. The driver layout has changed; "
            "this parser needs updating rather than working around.\n" % path
        )
        raise SystemExit(2)

    # Clones share their parent's set via `#define rom_clone rom_parent`.
    # Resolve repeatedly so a chain of aliases settles, and stop rather than
    # spin if the driver ever gains a cycle.
    aliases = dict(ALIAS_RE.findall(text))
    for _ in range(len(aliases) + 1):
        unresolved = {c: p for c, p in aliases.items() if c not in sets}
        if not unresolved:
            break
        for clone, parent in unresolved.items():
            if parent in sets:
                sets[clone] = sets[parent]

    return sets


def sha1_of(path: Path) -> str:
    digest = hashlib.sha1()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def index_firmware(directory: Path) -> dict:
    """Return {sha1: path} for every file we hold."""
    if not directory.is_dir():
        sys.stderr.write(
            "romset: no firmware directory at %s.\nApollo firmware is not "
            "redistributable and is gitignored, so it has to be supplied "
            "locally -- see docs/PROJECT_STATUS.md for the bitsavers images "
            "this project uses.\n" % directory
        )
        raise SystemExit(2)

    index: dict = {}
    for path in sorted(directory.rglob("*")):
        if path.is_file():
            index.setdefault(sha1_of(path), path)
    return index


def assemble(machine: str, roms: list, index: dict, out: Path) -> bool:
    """Materialise one machine's set. True if complete."""
    missing = []
    resolved = []
    for rom in roms:
        source = index.get(rom.sha1)
        if source is None:
            missing.append(rom)
        else:
            resolved.append((rom, source))

    if missing:
        sys.stderr.write("romset: %s incomplete, %d ROM(s) missing:\n" % (machine, len(missing)))
        for rom in missing:
            sys.stderr.write(
                "  %-40s %d bytes  sha1 %s\n" % (rom.name, rom.length, rom.sha1)
            )
        return False

    target = out / machine
    target.mkdir(parents=True, exist_ok=True)
    for rom, source in resolved:
        # The size check is redundant against a matching SHA-1 and kept anyway:
        # if it ever fires, the driver's own length and hash disagree, which is
        # a fact about the oracle worth surfacing loudly rather than ignoring.
        actual = source.stat().st_size
        if actual != rom.length:
            sys.stderr.write(
                "romset: %s: %s has the expected sha1 but is %d bytes, not the "
                "%d the driver declares. The oracle's ROM table is internally "
                "inconsistent; do not paper over this.\n"
                % (machine, rom.name, actual, rom.length)
            )
            return False
        shutil.copyfile(source, target / rom.name)

    sys.stdout.write(
        "romset: %-12s %d ROM(s) -> %s\n" % (machine, len(resolved), target)
    )
    return True


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--driver", type=Path, default=DEFAULT_DRIVER)
    ap.add_argument("--firmware", type=Path, default=DEFAULT_FIRMWARE)
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--machine", action="append", default=[],
                    help="assemble only this machine; repeatable")
    ap.add_argument("--list", action="store_true",
                    help="report what each machine needs and whether we hold it")
    args = ap.parse_args(argv)

    sets = parse_driver(args.driver)
    index = index_firmware(args.firmware)

    wanted = args.machine or sorted(sets)
    unknown = [m for m in wanted if m not in sets]
    if unknown:
        sys.stderr.write(
            "romset: no such machine in the driver: %s\nKnown: %s\n"
            % (", ".join(unknown), ", ".join(sorted(sets)))
        )
        return 2

    if args.list:
        for machine in wanted:
            sys.stdout.write("%s\n" % machine)
            for rom in sets[machine]:
                have = index.get(rom.sha1)
                sys.stdout.write(
                    "  %-40s %8d  %s  %s\n"
                    % (rom.name, rom.length, rom.sha1,
                       "have: %s" % have.name if have else "MISSING")
                )
        return 0

    complete = [assemble(m, sets[m], index, args.out) for m in wanted]
    ok = sum(complete)
    sys.stdout.write("romset: %d/%d machine(s) assembled\n" % (ok, len(wanted)))
    return 0 if ok == len(wanted) else 1


if __name__ == "__main__":
    raise SystemExit(main())
