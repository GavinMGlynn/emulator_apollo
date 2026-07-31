#!/usr/bin/env python3
"""Disassemble Apollo Domain option ROMs (68000) with header annotation.

Apollo option ROMs (Token Ring 10666 / 1818-4882, 3C505 Ethernet 010728, ...)
all carry the same 0x4A-byte header followed by an entry-point table.  The host
68000 boot PROM scans the AT-bus option ROM space, matches the magic, verifies
the longword checksum and calls entry points by id, so the body is ordinary
68000 code executed by the host CPU.

Usage:
    disasm.py ROM [-b BASE] [-o OUT] [--raw] [--no-trace]

    -b/--base    address the ROM image is mapped at (default 0).  Only affects
                 the printed addresses and symbol names; the header's entry
                 offsets are always relative to the start of the image.
    --raw        skip header parsing, linear-sweep the whole file
    --no-trace   linear sweep only, no recursive-descent code discovery

Output is a listing: address, raw words, mnemonic, then a comment column with
string contents, branch targets and I/O-window annotations.
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from dataclasses import dataclass, field

try:
    from capstone import CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000, Cs
except ImportError:  # pragma: no cover
    sys.exit("capstone with M68K support is required (pip install capstone)")

# ---------------------------------------------------------------------------
# Apollo option ROM header
# ---------------------------------------------------------------------------

MAGIC0 = 0x335E91B6
MAGIC1 = 0x0000A0B6

HEADER_FIELDS = [
    (0x00, 4, "magic0", "constant 0x335E91B6, matched by the boot PROM"),
    (0x04, 4, "magic1", "constant 0x0000A0B6"),
    (0x08, 4, "rom_id", "4-char ROM class ('R   ' ring, 'E   ' ethernet)"),
    (0x0C, 4, "length", "bytes of image covered by the checksum"),
    (0x10, 4, "checksum", "fudge longword: sum32 of image == 0"),
    (0x14, 4, "revision", "ASCII revision, e.g. ' 3.6'"),
    (0x18, 1, "hdr_ver", "header format version"),
    (0x19, 1, "board_ver", "board/hardware variant"),
    (0x1A, 2, "field_1a", "unknown, 0x0002 on ring ROMs"),
    (0x1C, 4, "field_1c", "unknown, 0x00000700 ring / 0x00000703 3C505"),
    (0x20, 4, "field_20", "unknown, zero"),
    (0x24, 4, "name_off", "offset of the ASCII controller name"),
    (0x28, 4, "field_28", "unknown, zero"),
    (0x2C, 4, "field_2c", "unknown, zero"),
    (0x30, 0x18, "pad", "zero padding"),
    (0x48, 2, "n_entries", "number of entry-point table records"),
]

# id -> conventional meaning, from cross-ROM comparison of the code bodies
ENTRY_NAMES = {
    0x01: "init",
    0x02: "entry_02",
    0x03: "entry_03",
    0x04: "entry_04",
    0x05: "entry_05",
    0x0B: "init_alt",
    0x0C: "entry_0c",
    0x0D: "entry_0d",
    0x0E: "entry_0e",
    0x0F: "entry_0f",
}

# Known Apollo 68k physical-address windows worth flagging.
IO_WINDOWS = [
    (0x051000, 0x051020, "RING0 (AT I/O 0x220-0x23F)"),
    (0x059000, 0x059020, "RING1 (AT I/O 0x320-0x33F)"),
    (0x040000, 0x060000, "AT-bus I/O window"),
    (0x000000, 0x001000, "68k vector / low RAM"),
]


def io_note(addr: int) -> str | None:
    for lo, hi, name in IO_WINDOWS:
        if lo <= addr < hi:
            return name
    return None


@dataclass
class Entry:
    ident: int
    offset: int

    @property
    def name(self) -> str:
        return ENTRY_NAMES.get(self.ident, "entry_%02x" % self.ident)


@dataclass
class Header:
    values: dict
    entries: list
    ok: bool
    checksum_sum: int

    def report(self) -> str:
        out = ["; ==== Apollo option ROM header ===="]
        for off, size, name, desc in HEADER_FIELDS:
            v = self.values.get(name)
            if isinstance(v, bytes):
                shown = repr(v)
            elif v is None:
                shown = "-"
            else:
                shown = "0x%0*X" % (size * 2, v)
            out.append("; %04X %-10s %-12s ; %s" % (off, name, shown, desc))
        out.append("; sum32 over [0, length) = 0x%08X  (%s)"
                   % (self.checksum_sum, "VALID" if self.ok else "MISMATCH"))
        out.append("; ---- entry-point table @ 0x004A: %d records of 6 bytes ----"
                   % len(self.entries))
        for e in self.entries:
            out.append("; id=0x%02X  offset=0x%08X  %s" % (e.ident, e.offset, e.name))
        return "\n".join(out)


def sum32(data: bytes, end: int) -> int:
    n = end // 4
    return sum(struct.unpack(">%dI" % n, data[: n * 4])) & 0xFFFFFFFF


def parse_header(data: bytes) -> Header | None:
    if len(data) < 0x4A:
        return None
    if struct.unpack_from(">I", data, 0)[0] != MAGIC0:
        return None
    vals = {}
    for off, size, name, _ in HEADER_FIELDS:
        if name in ("rom_id", "revision", "pad"):
            vals[name] = data[off:off + size]
        elif size == 1:
            vals[name] = data[off]
        elif size == 2:
            vals[name] = struct.unpack_from(">H", data, off)[0]
        elif size == 4:
            vals[name] = struct.unpack_from(">I", data, off)[0]
    n = vals["n_entries"]
    entries = []
    for i in range(n):
        o = 0x4A + 6 * i
        if o + 6 > len(data):
            break
        ident = struct.unpack_from(">H", data, o)[0]
        offset = struct.unpack_from(">I", data, o + 2)[0]
        entries.append(Entry(ident, offset))
    s = sum32(data, min(vals["length"], len(data)))
    return Header(vals, entries, s == 0, s)


# ---------------------------------------------------------------------------
# String table
# ---------------------------------------------------------------------------

def find_strings(data: bytes, minlen: int = 4) -> dict:
    """Map offset -> printable NUL-terminated string."""
    out = {}
    i = 0
    n = len(data)
    while i < n:
        j = i
        while j < n and 0x20 <= data[j] < 0x7F:
            j += 1
        if j - i >= minlen and j < n and data[j] == 0:
            out[i] = data[i:j].decode("ascii")
            i = j + 1
        else:
            i += 1
    return out


# ---------------------------------------------------------------------------
# Code discovery
# ---------------------------------------------------------------------------

UNCONDITIONAL_END = {"rts", "rte", "rtr", "jmp", "bra", "bras", "bral", "trap",
                     "illegal", "reset", "stop"}
CALLS = {"jsr", "bsr", "bsrs", "bsrl"}
BRANCHES = {"bra", "bras", "bral", "bhi", "bls", "bcc", "bcs", "bne", "beq",
            "bvc", "bvs", "bpl", "bmi", "bge", "blt", "bgt", "ble",
            "bhis", "blss", "bccs", "bcss", "bnes", "beqs", "bvcs", "bvss",
            "bpls", "bmis", "bges", "blts", "bgts", "bles",
            "dbt", "dbf", "dbra", "dbhi", "dbls", "dbcc", "dbcs", "dbne",
            "dbeq", "dbvc", "dbvs", "dbpl", "dbmi", "dbge", "dblt", "dbgt",
            "dble"}


@dataclass
class Listing:
    base: int
    data: bytes
    insns: dict = field(default_factory=dict)     # off -> capstone insn
    labels: dict = field(default_factory=dict)    # off -> label name
    xrefs: dict = field(default_factory=dict)     # off -> set of source offs


HEX = re.compile(r"\$([0-9a-fA-F]+)")


def _targets(insn) -> list:
    """Immediate branch/call targets of an instruction, as absolute addresses.

    capstone renders the resolved target in op_str for branches and for
    ``jsr $xxxx.l``, which is more reliable than its operand structs (it leaves
    mem.disp at 0 for absolute addressing modes).
    """
    m = insn.mnemonic.split(".")[0]
    if m not in CALLS and m not in BRANCHES and m != "jmp":
        return []
    if "(" in insn.op_str and "pc" not in insn.op_str:
        return []          # register indirect: target unknown
    return [int(h, 16) for h in HEX.findall(insn.op_str)]


def trace(md: Cs, li: Listing, roots: list, limit: int) -> None:
    pending = list(roots)
    seen = set()
    while pending:
        off = pending.pop()
        while True:
            if off in seen or off < 0 or off >= limit or off & 1:
                break
            seen.add(off)
            gen = md.disasm(li.data[off:off + 16], li.base + off, count=1)
            insn = next(gen, None)
            if insn is None:
                break
            li.insns[off] = insn
            m = insn.mnemonic
            for t in _targets(insn):
                toff = t - li.base
                if 0 <= toff < limit:
                    li.xrefs.setdefault(toff, set()).add(off)
                    if m in CALLS:
                        li.labels.setdefault(toff, "sub_%04X" % t)
                    else:
                        li.labels.setdefault(toff, "loc_%04X" % t)
                    pending.append(toff)
            if m in UNCONDITIONAL_END:
                break
            off += insn.size


def linear(md: Cs, li: Listing, start: int, end: int) -> None:
    off = start
    while off < end:
        if off in li.insns:
            off += li.insns[off].size
            continue
        insn = next(md.disasm(li.data[off:off + 16], li.base + off, count=1), None)
        if insn is None:
            off += 2
            continue
        li.insns.setdefault(off, insn)
        off += insn.size


# ---------------------------------------------------------------------------
# Rendering
# ---------------------------------------------------------------------------

AM_ABS_SHORT, AM_ABS_LONG, AM_PC_DISP, AM_IMMEDIATE = 16, 17, 11, 18


def operands(insn):
    """insn.operands, or [] when capstone refuses (skipdata)."""
    try:
        return list(insn.operands)
    except Exception:
        return []


def classify_operands(insn) -> list:
    """[(kind, address)] for absolute / pc-relative / immediate operands.

    Absolute addresses come out of op_str: capstone leaves mem.disp zero for
    the absolute addressing modes, but renders the value in the text.
    """
    ops = operands(insn)
    hexes = [int(h, 16) for h in HEX.findall(insn.op_str)]
    out = []
    hi = 0
    for op in ops:
        am = getattr(op, "address_mode", 0)
        if am in (AM_ABS_SHORT, AM_ABS_LONG, AM_PC_DISP, AM_IMMEDIATE):
            if hi < len(hexes):
                kind = {AM_ABS_SHORT: "abs", AM_ABS_LONG: "abs",
                        AM_PC_DISP: "pcrel", AM_IMMEDIATE: "imm"}[am]
                out.append((kind, hexes[hi]))
                hi += 1
        elif am == 6:      # (d16,An) -- offset from a base register
            out.append(("areg_disp", op.mem.disp & 0xFFFF))
    return out


def render(li: Listing, hdr: Header | None, strings: dict, out) -> None:
    if hdr:
        print(hdr.report(), file=out)
        print(";", file=out)
    print("; base = 0x%06X" % li.base, file=out)
    print(file=out)

    offs = sorted(li.insns)
    covered = set()
    for o in offs:
        covered.update(range(o, o + li.insns[o].size))

    off = 0
    n = len(li.data)
    while off < n:
        if off in li.insns:
            insn = li.insns[off]
            label = li.labels.get(off)
            if label:
                refs = sorted(li.xrefs.get(off, ()))
                print("\n%s:%s" % (label,
                                   "".ljust(2) + "; xrefs: " +
                                   ", ".join("%04X" % (li.base + r) for r in refs)
                                   if refs else ""), file=out)
            raw = li.data[off:off + insn.size].hex()
            text = "%-8s %s" % (insn.mnemonic, insn.op_str)
            notes = []
            for kind, a in classify_operands(insn):
                if kind == "areg_disp":
                    continue
                if kind in ("abs", "imm"):
                    w = io_note(a)
                    if w and a >= 0x1000:
                        notes.append("%s%s" % ("imm " if kind == "imm" else "", w))
                soff = a - li.base
                if soff in strings:
                    notes.append('"%s"' % strings[soff])
            cmt = ("   ; " + " | ".join(notes)) if notes else ""
            print("%06X  %-12s  %s%s" % (li.base + off, raw, text, cmt), file=out)
            off += insn.size
            continue

        # data
        if off in strings:
            s = strings[off]
            print('%06X  dc.b     "%s",0' % (li.base + off, s), file=out)
            off += len(s) + 1
            continue
        run = []
        start = off
        while off < n and off not in li.insns and off not in strings:
            run.append(li.data[off])
            off += 1
            if len(run) == 16:
                break
        print("%06X  dc.b     %s" % (li.base + start,
                                     ",".join("$%02X" % b for b in run)), file=out)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rom")
    ap.add_argument("-b", "--base", default="0",
                    help="load address of the image (hex ok, default 0)")
    ap.add_argument("-o", "--out", help="write listing here instead of stdout")
    ap.add_argument("--raw", action="store_true", help="ignore the header")
    ap.add_argument("--no-trace", action="store_true",
                    help="linear sweep only")
    ap.add_argument("--extra-root", action="append", default=[],
                    help="additional code offset to trace from (hex)")
    args = ap.parse_args(argv)

    base = int(args.base, 0)
    data = open(args.rom, "rb").read()
    hdr = None if args.raw else parse_header(data)

    md = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_000)
    md.detail = True

    limit = len(data)
    roots = []
    if hdr:
        limit = min(hdr.values["length"], len(data))
        roots = [e.offset for e in hdr.entries]
        for e in hdr.entries:
            addr = base + e.offset
            # entry labels win over generated ones
            pass
    roots += [int(x, 16) for x in args.extra_root]

    li = Listing(base=base, data=data)
    if hdr:
        for e in hdr.entries:
            li.labels[e.offset] = "ENTRY_%02X_%s" % (e.ident, e.name)
    if not args.no_trace and roots:
        trace(md, li, roots, limit)
    code_start = (0x4A + 6 * len(hdr.entries)) if hdr else 0
    linear(md, li, max(code_start, min(roots) if roots else code_start), limit)

    out = open(args.out, "w") if args.out else sys.stdout
    try:
        render(li, hdr, find_strings(data), out)
    finally:
        if args.out:
            out.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
