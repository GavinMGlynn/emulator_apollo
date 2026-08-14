#!/usr/bin/env python3
"""Walk the oracle's MMU tables, and read its RAM, from a state dump.

`tools/mame-oracle/statesync.lua` dumps MAME's whole save registry -- every
device's saved state, and all 16 Mbyte of main memory, as one text file. That
file already contains everything needed to answer "what does the oracle's MMU
translate this logical address to, and what bytes are there", which until now
took a fresh instrumented run each time.

The registry names main memory as

    memory/:maincpu/0/:ram.0.<index> u32 <value>

with `index` counting long words from the RAM base, so physical address
`0x01000000 + 4*index`. The MMU's root pointers and translation control are
`Motorola MC68030/:maincpu/0/m_mmu_{crp_aptr,crp_limit,srp_*,tc}`.

The walk itself is `[030]` chapter 9: `TC` gives the initial shift and the four
table index widths, and each level's descriptor is fetched from the previous
level's table address. Only what this project's Domain/OS actually uses is
implemented -- short (4-byte) descriptors, page descriptors, invalid and
indirect -- and anything else stops the walk and says so rather than guessing.

Usage:
    tools/oracle_walk.py DUMP --walk 3C403734 [--walk ...]
    tools/oracle_walk.py DUMP --read 012953C0 --count 16
    tools/oracle_walk.py DUMP --disasm 3C403734 --count 32
    tools/oracle_walk.py DUMP --cache ram.bin        # extract RAM once, reuse
"""

import argparse
import os
import re
import sys

RAM_BASE = 0x01000000
RAM_SIZE = 16 * 1024 * 1024

RAM_RE = re.compile(r"^memory/:maincpu/0/:ram\.0\.(\d+) u32 ([0-9A-Fa-f]+)$")
CPU_RE = re.compile(r"^Motorola MC68030/:maincpu/0/(\S+) (?:u\d+) ([0-9A-Fa-f]+)$")


def load(dump, cache=None):
    """Return (ram bytearray, cpu field dict). The RAM half is cached because
    parsing 4.2 M lines to read four long words is the whole cost of the tool."""
    ram = None
    if cache and os.path.exists(cache):
        with open(cache, "rb") as f:
            ram = bytearray(f.read())
        if len(ram) != RAM_SIZE:
            ram = None
    cpu = {}
    need_ram = ram is None
    if need_ram:
        ram = bytearray(RAM_SIZE)
    with open(dump, "r", errors="replace") as f:
        for line in f:
            if line.startswith("memory/:maincpu/0/:ram."):
                if not need_ram:
                    continue
                m = RAM_RE.match(line.rstrip("\n"))
                if m:
                    i = int(m.group(1))
                    v = int(m.group(2), 16) & 0xFFFFFFFF
                    off = i * 4
                    if off + 4 <= RAM_SIZE:
                        ram[off:off + 4] = v.to_bytes(4, "big")
            elif line.startswith("Motorola MC68030/"):
                m = CPU_RE.match(line.rstrip("\n"))
                if m:
                    cpu[m.group(1)] = int(m.group(2), 16)
    if need_ram and cache:
        with open(cache, "wb") as f:
            f.write(ram)
    return ram, cpu


def rd32(ram, phys):
    off = phys - RAM_BASE
    if off < 0 or off + 4 > RAM_SIZE:
        return None
    return int.from_bytes(ram[off:off + 4], "big")


def decode_tc(tc):
    return {
        "enabled": (tc >> 31) & 1,
        "sre": (tc >> 25) & 1,
        "fcl": (tc >> 24) & 1,
        "ps": (tc >> 20) & 0xF,
        "is": (tc >> 16) & 0xF,
        "ti": [(tc >> 12) & 0xF, (tc >> 8) & 0xF, (tc >> 4) & 0xF, tc & 0xF],
    }


def walk(ram, cpu, logical, supervisor=False, verbose=True):
    tc = decode_tc(cpu["m_mmu_tc.0.0"])
    if not tc["enabled"]:
        return logical, ["translation disabled: physical == logical"]
    lines = []
    if supervisor and tc["sre"]:
        aptr, limit = cpu["m_mmu_srp_aptr.0.0"], cpu["m_mmu_srp_limit.0.0"]
        lines.append("root SRP")
    else:
        aptr, limit = cpu["m_mmu_crp_aptr.0.0"], cpu["m_mmu_crp_limit.0.0"]
        lines.append("root CRP")
    dt = limit & 3
    lines.append("  aptr %08X limit %08X DT %d" % (aptr, limit, dt))
    if dt != 2:
        lines.append("  root is not a valid short table descriptor")
        return None, lines

    # `[030]` §9.5.1.1: the DT of the *pointing* descriptor gives the size of the
    # descriptors in the table it points to -- 4 bytes for DT=2, 8 for DT=3 --
    # so the stride is carried down the walk rather than read at each level.
    stride = 8 if dt == 3 else 4
    shift = 32 - tc["is"]
    table = aptr
    widths = [w for w in tc["ti"] if w != 0]
    page_mask = (1 << tc["ps"]) - 1
    for level, width in enumerate(widths):
        shift -= width
        index = (logical >> shift) & ((1 << width) - 1)
        addr = table + index * stride
        upper = rd32(ram, addr)
        if upper is None:
            lines.append("  level %d: descriptor at %08X is outside RAM" % (level, addr))
            return None, lines
        if stride == 8:
            lower = rd32(ram, addr + 4)
            dt = upper & 3
            desc_text = "%08X %08X" % (upper, lower)
        else:
            lower = upper
            dt = upper & 3
            desc_text = "%08X" % upper
        in_page_table = (level == len(widths) - 1)
        lines.append("  level %d: index %3X at %08X -> %s (DT %d, %s)"
                     % (level, index, addr, desc_text, dt,
                        "page table" if in_page_table else "pointer table"))
        if dt == 0:
            lines.append("  INVALID at level %d" % level)
            return None, lines
        if dt == 1:
            if stride == 8:
                page = ((lower >> 8) & 0xFFFFFF) << 8
            else:
                page = upper & 0xFFFFFF00
            page &= ~page_mask & 0xFFFFFFFF
            phys = page | (logical & page_mask)
            lines.append("  page descriptor: frame %08X -> physical %08X" % (page, phys))
            return phys, lines
        if in_page_table:
            lines.append("  DT %d in a page table is an INDIRECT descriptor" % dt)
            target = (lower & 0xFFFFFFFC) if stride == 8 else (upper & 0xFFFFFFFC)
            pointed = rd32(ram, target)
            if pointed is None:
                lines.append("  indirect target %08X is outside RAM" % target)
                return None, lines
            lines.append("  indirect -> %08X holds %08X" % (target, pointed))
            if pointed & 3 != 1:
                lines.append("  pointed-to descriptor is not a page descriptor")
                return None, lines
            page = pointed & 0xFFFFFF00 & ~page_mask & 0xFFFFFFFF
            return page | (logical & page_mask), lines
        # A table descriptor. **Which long word holds the address is decided by
        # the format of the descriptor just read** -- the long form keeps it in
        # the second long word -- while the DT it carries decides the size of
        # the descriptors in the table it points at. Conflating the two walks
        # into the wrong table on the very first long descriptor.
        table = (lower if stride == 8 else upper) & 0xFFFFFFF0
        stride = 8 if dt == 3 else 4
    lines.append("  walk ended after the table indices without a page descriptor")
    return None, lines


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dump")
    ap.add_argument("--cache", help="extract/reuse RAM as a flat binary")
    ap.add_argument("--walk", action="append", default=[],
                    help="logical address to translate, in hex")
    ap.add_argument("--supervisor", action="store_true")
    ap.add_argument("--read", help="physical address to read, in hex")
    ap.add_argument("--disasm", help="logical address to read instruction bytes at")
    ap.add_argument("--count", type=int, default=16, help="bytes to read")
    args = ap.parse_args(argv[1:])

    ram, cpu = load(args.dump, args.cache)
    tc = decode_tc(cpu["m_mmu_tc.0.0"])
    print("TC %08X: E=%d SRE=%d FCL=%d PS=%d (page %d) IS=%d TI=%s"
          % (cpu["m_mmu_tc.0.0"], tc["enabled"], tc["sre"], tc["fcl"], tc["ps"],
             1 << tc["ps"], tc["is"], tc["ti"]))

    for a in args.walk:
        logical = int(a, 16)
        phys, lines = walk(ram, cpu, logical, args.supervisor)
        print("walk %08X:" % logical)
        for l in lines:
            print(l)
        print("  => %s" % ("%08X" % phys if phys is not None else "no translation"))

    if args.read:
        phys = int(args.read, 16)
        show(ram, phys, args.count)

    if args.disasm:
        logical = int(args.disasm, 16)
        phys, lines = walk(ram, cpu, logical, args.supervisor)
        for l in lines:
            print(l)
        if phys is None:
            return 1
        print("logical %08X -> physical %08X" % (logical, phys))
        show(ram, phys, args.count)
    return 0


def show(ram, phys, count):
    for off in range(0, count, 16):
        row = ram[phys - RAM_BASE + off:phys - RAM_BASE + off + 16]
        words = " ".join("%02X%02X" % (row[i], row[i + 1])
                         for i in range(0, len(row) - 1, 2))
        print("  %08X  %s" % (phys + off, words))


if __name__ == "__main__":
    sys.exit(main(sys.argv))
