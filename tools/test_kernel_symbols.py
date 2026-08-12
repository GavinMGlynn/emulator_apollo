#!/usr/bin/env python3
"""`kernel_symbols.py`'s own tests, against a volume built here.

The tool answers "what is `3C4524E6`?" with a name out of Domain/OS's own load
map, and every one of those answers becomes a sentence in a status document. A
fault in it is a wrong name wearing the authority of a measurement -- which is
exactly what the numbers it replaces were.

The volume it normally reads is `media/`, which this repository does not carry,
so the tests build a volume of their own: real block headers, a real load map in
the shape the artefact uses, and the two traps that shape contains.

  * the map's address column is **right-aligned**, so a symbol at zero is seven
    spaces and a `0`. A grammar that expects hex at the column boundary loses
    every low symbol -- 2,495 of them on one real build -- and still returns a
    plausible 2,631;
  * the `D`-marked lines are sections and their names are **truncated** to
    eighteen characters, so a table built from them answers with names that do
    not exist.

Both are checked here, and so is the refusal to read a file that is not an
AEGIS volume at all -- the check that keeps "no maps found" from meaning "this
is a tar file".
"""

import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import kernel_symbols as ks  # noqa: E402  (after the path fix, deliberately)

TOOL = os.path.join(HERE, "kernel_symbols.py")

FAILURES = []


def check(name, cond, detail=""):
    if cond:
        print(f"  ok    {name}")
    else:
        FAILURES.append(name)
        print(f"  FAIL  {name}  {detail}")


def block(uid, page, daddr, payload):
    """One 1056-byte disk block, as `[AEGIS]` §4.1 lays it out."""
    head = bytearray(ks.BLOCK_HEADER)
    head[0:8] = uid
    head[8:12] = struct.pack(">I", page)
    head[28:32] = struct.pack(">I", daddr)
    body = payload[:ks.BLOCK_DATA].ljust(ks.BLOCK_DATA, b"\0")
    return bytes(head) + body


MAP_TEXT = (
    "Build ID: domain_os7!14-Feb-1992.11:12:45   ------  System built at X\n"
    "D48       0  TRAP_PAGE          loaded at 0, size = 400\n"
    "D         0  TRAP_PAGE          size = 400\n"
    "          0  EC_$NIL_EC\n"
    "          8  BUS_ERROR_VEC\n"
    "D663C009400  ETHERNET_$BUFFER_P loaded at 3C009400, size = 1000\n"
    "   3C4523C4  DIR_$OLD_INIT\n"
    "   3C452524  NAME_$INIT\n"
    "   3C47BDDE  DIR_$RESOLVE\n"
    "   3C43F4E4  NETWORK_$MULT_PAGIN_RQST_CNT\n"
)


def volume_bytes(map_text=MAP_TEXT, daddr_ok=True, pages=None):
    """A volume carrying one load map, split over as many blocks as it needs."""
    uid = bytes.fromhex("a45df7c130012345")
    raw = map_text.encode("latin-1")
    chunks = [raw[i:i + ks.BLOCK_DATA]
              for i in range(0, max(len(raw), 1), ks.BLOCK_DATA)]
    if pages is not None:
        chunks = chunks[:pages]
    out = bytearray()
    # A leading unwritten block, which a real volume has plenty of: its zero UID
    # must be skipped rather than counted as an object.
    out += block(b"\0" * 8, 0, 0, b"")
    for page, chunk in enumerate(chunks):
        daddr = len(out) // ks.BLOCK if daddr_ok else 0xDEAD
        out += block(uid, page, daddr, chunk)
    return bytes(out)


def run(argv, data):
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "volume.awd")
        with open(path, "wb") as f:
            f.write(data)
        p = subprocess.run([sys.executable, TOOL, path, *argv],
                           capture_output=True, text=True)
        return p.returncode, p.stdout, p.stderr


def main():
    print("kernel-symbols:")

    # The column grammar, both traps, without going through a volume.
    check("a right-aligned low address parses",
          ks.parse_symbol("          0  EC_$NIL_EC") == (0, "EC_$NIL_EC"))
    check("an eight-digit address parses",
          ks.parse_symbol("   3C4523C4  DIR_$OLD_INIT")
          == (0x3C4523C4, "DIR_$OLD_INIT"))
    check("a long name is not truncated",
          ks.parse_symbol("   3C43F4E4  NETWORK_$MULT_PAGIN_RQST_CNT")
          == (0x3C43F4E4, "NETWORK_$MULT_PAGIN_RQST_CNT"))
    check("a marked section line is refused",
          ks.parse_symbol(
              "D663C009400  ETHERNET_$BUFFER_P loaded at 3C009400,"
              " size = 1000") is None)
    check("a marked size line is refused",
          ks.parse_symbol("D  3C004C00  BUFFER_PAGES       size = 4800")
          is None)
    check("the build line is not a symbol",
          ks.parse_symbol("Build ID: domain_os7!14-Feb-1992.11:12:45") is None)

    # Reassembly by block header, and the map it yields.
    volume = ks.Volume(volume_bytes())
    check("the volume is recognised as AEGIS", volume.looks_like_aegis())
    maps = ks.load_maps(volume)
    check("one map is found", len(maps) == 1, str(len(maps)))
    found = maps[0]
    check("the build id is read",
          found.build == "domain_os7!14-Feb-1992.11:12:45", str(found.build))
    check("every symbol line is taken", len(found.symbols) == 6,
          str(found.symbols))
    check("no page is reported missing", found.missing == [],
          str(found.missing))

    # Resolution is to the nearest symbol at or below, which is what a map of
    # entry points can support.
    check("an address inside a routine names the routine",
          found.describe(0x3C4524E6) == "DIR_$OLD_INIT+122",
          found.describe(0x3C4524E6))
    check("an address on a symbol has no offset",
          found.describe(0x3C452524) == "NAME_$INIT",
          found.describe(0x3C452524))
    check("an address below the first symbol is unattributed",
          ks.LoadMap(b"u" * 8,
                     b"Build ID: x\n   3C000000  A\n").describe(0x1000) == "?",
          "resolved something below the table")

    # A trace line's PC is its *second* field, and the first is a decimal step
    # count that can look exactly like an address.
    line = "12345678 3C4524E6 00000001\n"
    check("the trace's pc column is the one named",
          ks.annotate(found, line).strip().endswith("DIR_$OLD_INIT+122"),
          ks.annotate(found, line))
    check("a bare address list is still named",
          ks.annotate(found, "3C452524\n").strip().endswith("NAME_$INIT"),
          ks.annotate(found, "3C452524\n"))

    # A missing page is reported rather than closed over.
    holed = ks.Volume(volume_bytes(map_text=MAP_TEXT + "x" * 3000, pages=None))
    pages = dict(list(holed.objects().values())[0])
    del pages[1]
    _, missing = holed.read_object(pages)
    check("a hole in an object is reported", missing == [1], str(missing))

    # End to end, and the refusals.
    code, out, _ = run(["--list"], volume_bytes())
    check("--list names the build", code == 0 and "domain_os7" in out, out)
    code, out, _ = run(["3C4524E6"], volume_bytes())
    check("the cli resolves an address",
          code == 0 and "DIR_$OLD_INIT+122" in out, out)
    code, _, err = run(["3C4524E6"], volume_bytes(daddr_ok=False))
    check("a volume whose daddrs disagree is refused",
          code != 0 and "not an AEGIS volume" in err, err)
    code, _, err = run(["3C4524E6"], volume_bytes(map_text="no map here\n"))
    check("a volume with no load map is refused",
          code != 0 and "no kernel load map" in err, err)

    print()
    if FAILURES:
        print("kernel-symbols: %d failure(s): %s"
              % (len(FAILURES), ", ".join(FAILURES)))
        return 1
    print("kernel-symbols: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
