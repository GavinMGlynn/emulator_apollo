#!/usr/bin/env python3
"""Kernel symbol names for Domain/OS addresses, read off the volume itself.

Every PC this project has chased through a Domain/OS boot has been a bare
number: `3C4524E6` is "the fault site", `3C47BF58` is "the write of the status",
`3C43DDF0` is "the address-space switch". Those names were each recovered by
disassembly and inference, over several sessions, and one of them was wrong.

**The volume carries the kernel's load map**, as an ordinary file, with a line
per symbol. `/sys/dm`-era Domain/OS ships the map of the build it installs, and
the SR10.4 artefact carries seven of them -- one per kernel variant -- naming
2,900 symbols apiece. Resolving a PC to `DIR_$RESOLVE+17A` is a table lookup,
not an investigation.

## Reading a volume without a filesystem

The map is a file and this tool does not implement the AEGIS directory
hierarchy, because it does not have to. `[AEGIS]` §4.1, read from the page
image:

    The AEGIS system defines a disk block as 1024 bytes of data plus a 32-byte
    disk block header. ... This information consists of: The UID of the object
    that owns the block. The block's page number within the object (the first
    block is page 0, the second is page 1, and so on).

So **every block on the volume says which object it belongs to and where in it
it goes**, and an object can be reassembled from its blocks without consulting a
directory or a VTOC. That is the property SALVOL is built on -- §4.1 again: "The
disk block header exists to aid in volume recovery ... SALVOL can reconstruct
the disk even if the volume table of contents has been destroyed."

Verified on the artefact rather than assumed: the header's last longword is the
block's physical DADDR, "its sequence number relative to the start of the
physical volume", and `DADDR * 1056` is exactly the byte offset the block was
found at. A volume whose blocks fail that check is not this format and the tool
says so rather than returning plausible rubbish.

    [AEGIS]  *AEGIS Internals and Data Structures*, Apollo, Jan 1986, ch. 4.

## What a load-map line looks like

    Build ID: domain_os7!14-Feb-1992.11:12:45   ------  System built at ...
    D023C004C00  BUFFER_PAGES       loaded at 3C004C00, size = 4800
    D  3C004C00  BUFFER_PAGES       size = 4800
       3C004C00  WIN_PAGE

Three columns of marker, eight of address, two of space, then the name. The
`D`-marked lines are *sections* and their name field is truncated to eighteen
characters (`ETHERNET_$BUFFER_P`); the unmarked lines are symbols and carry the
whole name (`NETWORK_$MULT_PAGIN_RQST_CNT`). Only the unmarked lines are taken,
which is why the table has no truncated entries in it.
"""

import argparse
import bisect
import os
import re
import struct
import sys

# `[AEGIS]` §4.1: 1024 bytes of data behind a 32-byte header.
BLOCK_DATA = 1024
BLOCK_HEADER = 32
BLOCK = BLOCK_HEADER + BLOCK_DATA

# The header fields this tool needs, by offset. Figure 4-2.
HDR_UID = slice(0, 8)
HDR_PAGE = slice(8, 12)
HDR_DADDR = slice(28, 32)

# A load map announces itself on its first page.
BUILD_MARK = b"Build ID: "

_BUILD = re.compile(r"^Build ID: (\S+)")

# The map is columnar, not delimited: three columns of marker, eight of
# address, two of space, then the name. The address is **right**-aligned in its
# field, so `EC_$NIL_EC` at zero is seven spaces and a `0` -- a grammar that
# reads the address as "hex digits at column three" silently loses every symbol
# below `0x10000000`, which on this map is the whole trap page and every
# absolute. It lost 2,495 of them on one build and still returned 2,631.
MARKER = slice(0, 3)
ADDRESS = slice(3, 11)
GAP = slice(11, 13)
NAME = slice(13, None)
_HEX = re.compile(r"^[0-9A-F]{1,8}$")


def parse_symbol(line):
    """One `   ADDRESS  NAME` line, or `None` for anything else.

    Section lines are the ones with a marker, and they are deliberately not
    taken: their name field is eighteen characters wide and *truncated*
    (`ETHERNET_$BUFFER_P`), so a table built from them would answer with names
    that do not exist.
    """
    if line[MARKER] != "   " or line[GAP] != "  ":
        return None
    address = line[ADDRESS].strip()
    name = line[NAME].strip()
    if not name or " " in name or not _HEX.match(address):
        return None
    return int(address, 16), name


class Volume:
    """An AEGIS physical volume, addressed by block rather than by name."""

    def __init__(self, data):
        self.data = data
        self.blocks = len(data) // BLOCK

    def header(self, index):
        at = index * BLOCK
        return self.data[at:at + BLOCK_HEADER]

    def page_data(self, index):
        at = index * BLOCK + BLOCK_HEADER
        return self.data[at:at + BLOCK_DATA]

    def looks_like_aegis(self, sample=4096):
        """Whether the blocks' own DADDRs agree with where they were found.

        A sample rather than the whole volume: the check is here to reject a
        file that is not this format at all, and a format error shows up in the
        first thousand blocks. Unwritten blocks carry a zero UID and are
        skipped -- they say nothing either way.
        """
        checked = 0
        for i in range(min(self.blocks, sample)):
            head = self.header(i)
            if head[HDR_UID] == b"\0" * 8:
                continue
            daddr = struct.unpack(">I", head[HDR_DADDR])[0]
            if daddr != i:
                return False
            checked += 1
        return checked > 0

    def objects(self):
        """Every object on the volume, as `uid -> {page: block index}`.

        Blocks whose page number is absurd are dropped rather than trusted:
        a block header is 32 bytes of a 364-megabyte file and the scan reaches
        parts of it that were never written.
        """
        found = {}
        for i in range(self.blocks):
            head = self.header(i)
            uid = bytes(head[HDR_UID])
            if uid == b"\0" * 8:
                continue
            page = struct.unpack(">I", head[HDR_PAGE])[0]
            if page > 0x100000:
                continue
            found.setdefault(uid, {})[page] = i
        return found

    def read_object(self, pages):
        """An object's bytes, in page order, and the pages that were missing.

        A gap is reported rather than closed over: a map with a page missing
        loses whichever symbols were on it, and a caller that silently joined
        page 4 to page 6 would resolve addresses through a hole.
        """
        want = range(0, max(pages) + 1)
        missing = [p for p in want if p not in pages]
        data = b"".join(self.page_data(pages[p]) for p in want if p in pages)
        return data, missing


class LoadMap:
    """One kernel's load map: its build ID and its symbols, sorted."""

    def __init__(self, uid, text, missing=()):
        self.uid = uid
        self.missing = list(missing)
        self.build = None
        self.symbols = []
        for line in text.decode("latin-1").split("\n"):
            line = line.rstrip("\r")
            if self.build is None:
                mark = _BUILD.match(line)
                if mark:
                    self.build = mark.group(1)
            hit = parse_symbol(line)
            if hit:
                self.symbols.append(hit)
        self.symbols.sort()
        self._addresses = [a for a, _ in self.symbols]

    def resolve(self, address):
        """The symbol covering `address`, as `(name, offset, base)`.

        The nearest symbol *at or below* the address: a load map names entry
        points, not extents, so an address inside a routine belongs to the last
        symbol before it. `None` below the first symbol, where there is nothing
        to attribute it to.
        """
        i = bisect.bisect_right(self._addresses, address) - 1
        if i < 0:
            return None
        base, name = self.symbols[i]
        return name, address - base, base

    def describe(self, address):
        hit = self.resolve(address)
        if hit is None:
            return "?"
        name, offset, _ = hit
        return name if offset == 0 else "%s+%X" % (name, offset)


def load_maps(volume):
    """Every load map on the volume, newest-looking first is not implied."""
    maps = []
    for uid, pages in volume.objects().items():
        first = min(pages)
        if first > 1 or BUILD_MARK not in volume.page_data(pages[first]):
            continue
        data, missing = volume.read_object(pages)
        found = LoadMap(uid, data, missing)
        if found.build and found.symbols:
            maps.append(found)
    maps.sort(key=lambda m: (m.build, m.uid))
    return maps


def pick(maps, build):
    """The one map a caller meant, or an error naming the choices.

    The artefact carries the same map twice under two UIDs -- identical text,
    two copies -- so matching a build name to several maps is ordinary and not
    an ambiguity worth refusing.
    """
    if build is None:
        builds = {m.build for m in maps}
        if len(builds) != 1:
            raise SystemExit(
                "kernel_symbols: the volume carries %d builds; name one with"
                " --build\n  %s" % (len(builds), "\n  ".join(sorted(builds))))
        return maps[0]
    matched = [m for m in maps if m.build == build or m.build.startswith(build)]
    if not matched:
        raise SystemExit(
            "kernel_symbols: no build matches %s; the volume has\n  %s"
            % (build, "\n  ".join(sorted({m.build for m in maps}))))
    return matched[0]


def main(argv):
    ap = argparse.ArgumentParser(
        description="Resolve Domain/OS kernel addresses from a volume's own"
                    " load map.")
    ap.add_argument("volume", help="an AEGIS volume image (.awd)")
    ap.add_argument("--build", help="which kernel's map, e.g. domain_os7")
    ap.add_argument("--list", action="store_true",
                    help="list the maps the volume carries and stop")
    ap.add_argument("--dump", action="store_true",
                    help="print the whole symbol table, address then name")
    ap.add_argument("--annotate", metavar="FILE",
                    help="rewrite a trace, naming every 8-digit address in it;"
                         " - for standard input")
    ap.add_argument("address", nargs="*", help="addresses to resolve, in hex")
    args = ap.parse_args(argv[1:])

    if not os.path.exists(args.volume):
        raise SystemExit("kernel_symbols: no such volume: %s" % args.volume)
    with open(args.volume, "rb") as f:
        volume = Volume(f.read())
    if not volume.looks_like_aegis():
        raise SystemExit(
            "kernel_symbols: %s is not an AEGIS volume -- its blocks' DADDRs"
            " do not match their positions" % args.volume)

    maps = load_maps(volume)
    if not maps:
        raise SystemExit("kernel_symbols: %s carries no kernel load map"
                         % args.volume)
    if args.list:
        for found in maps:
            print("%-40s %5d symbols  uid %s%s"
                  % (found.build, len(found.symbols), found.uid.hex(),
                     "  MISSING PAGES %s" % found.missing
                     if found.missing else ""))
        return 0

    chosen = pick(maps, args.build)
    if chosen.missing:
        print("kernel_symbols: %s is missing page(s) %s -- symbols on them are"
              " absent" % (chosen.build, chosen.missing), file=sys.stderr)

    if args.dump:
        for address, name in chosen.symbols:
            print("%08X  %s" % (address, name))

    if args.annotate:
        stream = sys.stdin if args.annotate == "-" else open(args.annotate)
        with stream:
            for line in stream:
                sys.stdout.write(annotate(chosen, line))

    for text in args.address:
        address = int(text, 16)
        print("%08X  %s" % (address, chosen.describe(address)))
    return 0


_ADDRESS = re.compile(r"^[0-9A-Fa-f]{8}$")


def annotate(found, line):
    """Name the line's PC, which in `--boot-trace` output is the second field.

    The second and not the first match on the line: the trace's first column is
    a **decimal** step number, and a step number of exactly eight digits --
    which every run reaches -- is indistinguishable from an address by shape
    alone. Naming it would attribute the wrong symbol to one line in ten and
    look right doing it. A line whose second field is not an address falls back
    to the first field, which is what a bare list of addresses looks like.

    Only one address per line: a trace row carries sixteen register values
    after the PC and naming every one turns a readable line into a paragraph.
    """
    fields = line.split()
    for candidate in (fields[1:2], fields[0:1]):
        if candidate and _ADDRESS.match(candidate[0]):
            name = found.describe(int(candidate[0], 16))
            return "%s  %s\n" % (line.rstrip("\n"), name)
    return line


if __name__ == "__main__":
    sys.exit(main(sys.argv))
