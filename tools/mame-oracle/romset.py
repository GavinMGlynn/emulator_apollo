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
import zipfile
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
# Devices the driver includes, e.g. #include "bus/isa/3c505.h"
INCLUDE_RE = re.compile(
    r'^\s*#include\s+"((?:bus|machine|video|sound|cpu)/[^"]+\.h)"', re.M)
ROM_END_RE = re.compile(r"^\s*ROM_END", re.M)

# ROMX_LOAD( "file", offset, length, CRC(x) SHA1(y), ROM_BIOS(n) )
# ROM_LOAD ( "file", offset, length, CRC(x) SHA1(y) )
#
# The suffixed forms matter and were missed at first: a device ROM in a 16-bit
# region is declared ROM_LOAD16_BYTE, and a parser that only knows ROM_LOAD
# silently assembles a *partial* set. That reads as success here and fails much
# later as "NOT FOUND" from MAME, so the family is matched rather than the two
# spellings that happened to appear in apollo.cpp.
# Entries marked NO_DUMP carry no hash and are skipped: they cannot be supplied,
# and for the sets we use they belong to a BIOS option we do not select.
ROM_LOAD_RE = re.compile(
    r"ROMX?_LOAD[0-9A-Z_]*\s*\(\s*"
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
        if not path.is_file():
            continue
        index.setdefault(sha1_of(path), path)
        # Device ROM sets are often held as an already-correct MAME zip rather
        # than as loose files, so index the members too. Matching stays by
        # SHA-1: the member's *name* inside the archive is not trusted any more
        # than a loose file's is.
        if zipfile.is_zipfile(path):
            with zipfile.ZipFile(path) as archive:
                for member in archive.namelist():
                    if member.endswith("/"):
                        continue
                    digest = hashlib.sha1(archive.read(member)).hexdigest()
                    index.setdefault(digest, (path, member))
    return index



def source_size(source) -> int:
    """Size of a resolved ROM, held either loose or inside a zip."""
    if isinstance(source, tuple):
        archive_path, member = source
        with zipfile.ZipFile(archive_path) as archive:
            return archive.getinfo(member).file_size
    return source.stat().st_size


def source_name(source) -> str:
    if isinstance(source, tuple):
        return "%s:%s" % (source[0].name, source[1])
    return source.name


def write_rom(source, destination: Path) -> None:
    if isinstance(source, tuple):
        archive_path, member = source
        with zipfile.ZipFile(archive_path) as archive:
            destination.write_bytes(archive.read(member))
        return
    shutil.copyfile(source, destination)


def device_sources(driver: Path) -> list:
    """Device .cpp files the driver pulls in, for their own ROM_START blocks.

    A machine set is not enough to run a machine: MAME loads a card's ROMs from
    a *sibling* set named after the device, so dn3500 needs a `3c505` set beside
    it or it refuses to start. The device list is derived from the driver's own
    `#include "bus/..."` lines rather than transcribed, for the same reason the
    ROM table is parsed rather than copied -- a transcribed list goes stale the
    moment the ext/mame pin moves.
    """
    devices_root = driver.parents[2] / "devices"
    if not devices_root.is_dir():
        return []

    includes = set()
    for path in sorted(driver.parent.glob("*.cpp")) + sorted(driver.parent.glob("*.h")):
        for match in INCLUDE_RE.finditer(path.read_text(errors="replace")):
            includes.add(match.group(1))

    sources = []
    for include in sorted(includes):
        candidate = devices_root / (include[:-2] + ".cpp")
        # Most devices carry no ROM at all, which is ordinary rather than a
        # parser failure, so they are filtered here instead of being reported as
        # a changed driver layout by parse_driver.
        if candidate.is_file() and "ROM_START" in candidate.read_text(errors="replace"):
            sources.append(candidate)
    return sources


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
        actual = source_size(source)
        if actual != rom.length:
            sys.stderr.write(
                "romset: %s: %s has the expected sha1 but is %d bytes, not the "
                "%d the driver declares. The oracle's ROM table is internally "
                "inconsistent; do not paper over this.\n"
                % (machine, rom.name, actual, rom.length)
            )
            return False
        write_rom(source, target / rom.name)

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
    # Device ROM sets live beside the machine sets in the rompath, and a machine
    # will not start without them.
    device_sets: dict = {}
    for source in device_sources(args.driver):
        device_sets.update(parse_driver(source))
    index = index_firmware(args.firmware)

    # Only the device sets we can actually satisfy are assembled; one we hold no
    # ROMs for is reported by its machine failing to run, not silently here.
    satisfiable = {
        name: roms
        for name, roms in device_sets.items()
        if roms and all(index.get(rom.sha1) is not None for rom in roms)
    }
    sets.update({k: v for k, v in satisfiable.items() if k not in sets})
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
                       "have: %s" % source_name(have) if have else "MISSING")
                )
        return 0

    complete = [assemble(m, sets[m], index, args.out) for m in wanted]
    ok = sum(complete)
    sys.stdout.write("romset: %d/%d machine(s) assembled\n" % (ok, len(wanted)))
    return 0 if ok == len(wanted) else 1


if __name__ == "__main__":
    raise SystemExit(main())
