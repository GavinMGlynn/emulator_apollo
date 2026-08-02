#!/usr/bin/env python3
"""Test the probe encoder against the manual's bit patterns.

The point of a hand-assembler is that every opcode is a citation, so these
checks are written as the *layout* the `M68000 Family Programmer's Reference
Manual 1992` gives, assembled here bit by bit, rather than as the constant the
encoder happens to produce. A test that compares the encoder against itself
would pass on any consistent mistake.

    python3 tools/mame-oracle/test_encoder.py
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import encoder as E  # noqa: E402

failures = 0


def check(name, actual, expected):
    global failures
    if actual != expected:
        failures += 1
        a = [("%04X" % w) for w in actual] if isinstance(actual, list) else actual
        e = [("%04X" % w) for w in expected] if isinstance(expected, list) else expected
        sys.stderr.write("FAIL %s\n  expected: %s\n  actual:   %s\n" % (name, e, a))
    else:
        sys.stdout.write("ok   %s\n" % name)


def raises(name, fn):
    global failures
    try:
        fn()
    except E.EncodingError:
        sys.stdout.write("ok   %s\n" % name)
        return
    failures += 1
    sys.stderr.write("FAIL %s: no EncodingError\n" % name)


def bits(*fields):
    """Assemble a 16-bit word from (value, width) pairs, most significant
    first — the manual's own presentation of an instruction format."""
    word = 0
    total = 0
    for value, width in fields:
        assert 0 <= value < (1 << width), (value, width)
        word = (word << width) | value
        total += width
    assert total == 16, total
    return word


def main() -> int:
    # NOP: 0100 1110 0111 0001, PRM p. 4-119.
    check("NOP", E.nop(), [bits((0b0100, 4), (0b1110, 4), (0b0111, 4), (0b0001, 4))])

    # STOP: 0100 1110 0111 0010 then the status word, PRM p. 6-85.
    check("STOP #$2700", E.stop(0x2700),
          [bits((0b0100, 4), (0b1110, 4), (0b0111, 4), (0b0010, 4)), 0x2700])

    # MOVEQ: 0111 rrr 0 dddddddd, PRM p. 4-134.
    check("MOVEQ #$5A,D0", E.moveq(0x5A, 0),
          [bits((0b0111, 4), (0, 3), (0, 1), (0x5A, 8))])
    check("MOVEQ #1,D7", E.moveq(1, 7),
          [bits((0b0111, 4), (7, 3), (0, 1), (1, 8))])
    # The immediate is sign-extended to a long, so a negative one keeps its
    # byte pattern in the instruction word.
    check("MOVEQ #-1,D3", E.moveq(-1, 3),
          [bits((0b0111, 4), (3, 3), (0, 1), (0xFF, 8))])

    # MOVE.L Dn,(xxx).L: 00 10 <dest reg 001> <dest mode 111> <src mode 000>
    # <src reg>, PRM p. 4-116 — destination before source, which reads
    # backwards from the assembly syntax.
    check("MOVE.L D0,($1800).L", E.move_l_dn_to_abs(0, 0x1800),
          [bits((0b00, 2), (0b10, 2), (0b001, 3), (0b111, 3), (0b000, 3), (0, 3)),
           0x0000, 0x1800])
    check("MOVE.L D5,($DEADBEEF).L", E.move_l_dn_to_abs(5, 0xDEADBEEF),
          [bits((0b00, 2), (0b10, 2), (0b001, 3), (0b111, 3), (0b000, 3), (5, 3)),
           0xDEAD, 0xBEEF])

    # MOVE.L (xxx).L,Dn: destination mode 000 register rrr, source mode 111
    # register 001.
    check("MOVE.L ($1800).L,D2", E.move_l_abs_to_dn(0x1800, 2),
          [bits((0b00, 2), (0b10, 2), (2, 3), (0b000, 3), (0b111, 3), (0b001, 3)),
           0x0000, 0x1800])

    # MOVEA.L #<data>,An: destination mode 001, source mode 111 register 100.
    check("MOVEA.L #$2000,A7", E.movea_l_imm(0x2000, 7),
          [bits((0b00, 2), (0b10, 2), (7, 3), (0b001, 3), (0b111, 3), (0b100, 3)),
           0x0000, 0x2000])

    # ADDQ.L: 0101 ddd 0 10 000 rrr, PRM p. 4-11, and 8 encodes as 0.
    check("ADDQ.L #1,D0", E.addq_l(1, 0),
          [bits((0b0101, 4), (1, 3), (0, 1), (0b10, 2), (0b000, 3), (0, 3))])
    check("ADDQ.L #8,D1 encodes 8 as 0", E.addq_l(8, 1),
          [bits((0b0101, 4), (0, 3), (0, 1), (0b10, 2), (0b000, 3), (1, 3))])

    # DBF: 0101 0001 11 001 rrr then a word displacement, PRM p. 4-62.
    check("DBF D0,-4", E.dbf(0, -4),
          [bits((0b0101, 4), (0b0001, 4), (0b11, 2), (0b001, 3), (0, 3)), 0xFFFC])

    # Ranges are refused rather than silently truncated: a probe built from a
    # wrong encoding runs and reports, which is worse than not building.
    raises("MOVEQ rejects 128", lambda: E.moveq(128, 0))
    raises("MOVEQ rejects -129", lambda: E.moveq(-129, 0))
    raises("ADDQ rejects 0", lambda: E.addq_l(0, 0))
    raises("ADDQ rejects 9", lambda: E.addq_l(9, 0))
    raises("register out of range", lambda: E.moveq(0, 8))
    raises("address out of range", lambda: E.move_l_dn_to_abs(0, 0x1_0000_0000))

    # The sentinel probe the plan's verification names.
    check("sentinel probe", E.sentinel_probe(0x1800, 0x5A),
          E.moveq(0x5A, 0) + E.move_l_dn_to_abs(0, 0x1800) + E.stop(0x2700))
    check("sentinel probe is six words", len(E.sentinel_probe()), 6)

    # The probe programs the oracle campaign added, checked against the
    # manual's bit layouts rather than against the encoder. They are
    # hand-assembled hex, and nothing else checks them: a wrong opcode would
    # surface only as a mysterious disagreement with the oracle -- the shape
    # `FINDINGS.md` C75 shows is easy to mistake for a real finding.
    #
    # The *displacements* are what these check, because they are the fields
    # computed rather than written and each has its own base.

    # `DBcc`'s displacement is relative to the address of its own extension
    # word -- the opcode word plus two -- so a loop back over a one-word body
    # is -4, not -2. And the body exists at all only because a self-loop leaves
    # the program counter unmoved, which the oracle harness reads as a halt.
    dbcc = E.dbcc_probe(0x1800)
    check("DBcc loop body is a NOP", dbcc[1], 0x4E71)
    check("DBcc displacement is -4", dbcc[3], 0xFFFC)

    # `BSR`'s is relative to the same place, and the subroutine sits after the
    # `STOP` so the fall-through path never reaches it.
    bsr = E.subroutine_probe(0x1800, 0x1000)
    check("BSR displacement reaches the subroutine",
          0x1000 + 2 + ((bsr[1] ^ 0x8000) - 0x8000), 0x1000 + 14)
    check("subroutine returns", bsr[-1], 0x4E75)

    # `MOVEM`'s two masks are the reversal itself: `D0-D2` is bits 15-13 going
    # out through a predecrement and bits 0-2 coming back through a
    # postincrement. Writing one mask for both is the mistake the probe exists
    # to catch, and writing it here would hide it.
    movem = E.movem_probe(0x1800)
    check("MOVEM predecrement mask is D0-D2 at bits 15-13", movem[5], 0xE000)
    check("MOVEM postincrement mask is D3-D5 at bits 3-5", movem[7], 0x0038)

    # The fault probes plant one vector each, and at different offsets: vector
    # 4 for an illegal instruction, vector 2 for a bus error.
    check("illegal-instruction probe plants vector 4",
          E.fault_probe(0x1000, 0x1800)[4], (0x1000 + 0x100 + 0x10) & 0xFFFF)
    check("bus-fault probe plants vector 2",
          E.bus_fault_probe(0x1000, 0x1800)[4], (0x1000 + 0x100 + 8) & 0xFFFF)

    if failures:
        sys.stderr.write("\n%d check(s) failed\n" % failures)
        return 1
    sys.stdout.write("\nall checks passed\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
