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

    # The FPU trap probe is the one whose *arithmetic* can be wrong without the
    # program looking wrong, so the two numbers it depends on are pinned here.
    #
    # Vector 50 is divide by zero, and 50 is not derivable from anything else in
    # the encoding: it is neither `48 +` the FPSR bit (DZ is bit 10) nor `48 +`
    # the position in §6.1.9's priority order (DZ is sixth). Getting it wrong
    # plants a live handler on the wrong vector, and the probe then reports "no
    # trap" exactly as an unimplemented trap path would.
    fpu_trap = E.fpu_trap_probe(0x1000, 0x1800)
    check("FPU trap probe plants vector 50",
          fpu_trap[4], (0x1000 + 0x100 + 50 * 4) & 0xFFFF)
    # ENABLE(DZ) is bit 10, sharing its position with EXC(DZ). Enabling the
    # wrong bit gives a probe that runs cleanly and proves nothing.
    check("FPU trap probe enables DZ and nothing else", fpu_trap[13], 0x0400)
    # And the handler has to sit where the vector says it does: the offset is
    # hand-counted, so a word added above it moves the entry point silently.
    check("FPU trap probe's handler entry is its MOVEQ",
          fpu_trap[(56 // 2)], 0x7000)

    # The byte and status-register moves a *board* probe is written in. Every
    # device register on this machine is eight bits wide, and until these
    # existed no probe could address one.

    # MOVE.B #<data>,(xxx).L — PRM p. 4-116. Size `01`, destination absolute
    # long (register `001`, mode `111`), source immediate (mode `111`,
    # register `100`). The immediate occupies a whole extension word with the
    # byte in its low half: a byte immediate is not half a word, because the
    # part fetches words.
    check("MOVE.B #$5A,($10C00).L",
          E.move_b_imm_to_abs(0x5A, 0x00010C00),
          [bits((0, 2), (1, 2), (1, 3), (7, 3), (7, 3), (4, 3)),
           0x005A, 0x0001, 0x0C00])
    raises("MOVE.B rejects a value that is not a byte",
           lambda: E.move_b_imm_to_abs(0x100, 0x00010C00))

    # MOVE.B (xxx).L,Dn — the same layout the other way round.
    check("MOVE.B ($11001).L,D0",
          E.move_b_abs_to_dn(0x00011001, 0),
          [bits((0, 2), (1, 2), (0, 3), (0, 3), (7, 3), (1, 3)),
           0x0001, 0x1001])

    # LSL.W #8,Dn — PRM p. 4-102. `1110 ccc dr ss i/r tt rrr`: count, direction
    # (`1` is left), size `01` for word, `i/r` clear for the immediate form,
    # type `01` for LSL. **Eight is encoded as zero**, the same convention
    # `ADDQ` uses, so writing the count straight into the field gives a shift of
    # nothing — which a probe composing two bytes reports as the low one twice.
    check("LSL.W #8,D0",
          E.lsl_w_imm(8, 0),
          [bits((0xE, 4), (0, 3), (1, 1), (1, 2), (0, 1), (1, 2), (0, 3))])
    check("LSL.W #1,D0 is not the same encoding",
          E.lsl_w_imm(1, 0),
          [bits((0xE, 4), (1, 3), (1, 1), (1, 2), (0, 1), (1, 2), (0, 3))])
    raises("LSL rejects a count of nine", lambda: E.lsl_w_imm(9, 0))

    # MOVE.W #<data>,SR — PRM p. 4-135, `0100 0110 11 111 100`. Privileged, and
    # what a board probe opens with so that neither machine can take an
    # interrupt while it runs.
    check("MOVE.W #$2700,SR",
          E.move_w_imm_to_sr(0x2700),
          [bits((4, 4), (6, 4), (3, 2), (7, 3), (4, 3)), 0x2700])

    # The two board probes' *addresses*, which are the part of them that can be
    # wrong while the program still runs cleanly and reports a plausible number.
    dma = E.dma_register_probe(0x01001800)
    # `[8237]` register $0D is master clear, and it is write-only: the probe
    # depends on it to make the oracle's booted controller and this side's reset
    # one start from the same state. Aimed one register low it hits the
    # temporary register and clears nothing.
    check("DMA probe master-clears at $0D", dma[4:6], [0x0001, 0x0C0D])
    # Both halves of the sixteen-bit address go to the *same* address. That is
    # the byte-pointer flip-flop, and a probe that used two addresses would pass
    # against a model that had no flip-flop at all.
    check("DMA probe writes both address bytes to one register",
          dma[8:10], dma[12:14])
    check("DMA probe reads back from the register it wrote",
          dma[16:18], dma[8:10])

    intr = E.intr_mask_probe(0x01001800)
    # `ICW1` is the one write that goes to A0 = 0; everything after it goes to
    # A0 = 1. A model that took the whole sequence at one address would be
    # indistinguishable from a correct one if the probe made the same mistake.
    check("8259 probe sends ICW1 to A0 = 0", intr[4:6], [0x0001, 0x1000])
    check("8259 probe sends ICW2 to A0 = 1", intr[8:10], [0x0001, 0x1001])
    # The cascade pair, measured from the boot PROM's own writes: the master's
    # `ICW3` is a *bit mask* of which input has a slave and the slave's is the
    # *number* of the input it answers on. `$08` and `$03` are the same fact
    # written two ways, and a probe carrying `$08` on both sides would agree
    # with a model that never read the slave's at all.
    check("8259 probe's master ICW3 is the IR3 bit", intr[11], 0x0008)
    check("8259 probe's slave ICW3 is the number 3", intr[31], 0x0003)
    check("8259 probe's vector bases are $A0 and $A8",
          (intr[7], intr[27]), (0x00A0, 0x00A8))
    # The two masks must not be palindromes of each other, or one controller
    # answering for both reads as agreement.
    check("8259 probe's two masks differ", intr[19] != intr[39], True)

    if failures:
        sys.stderr.write("\n%d check(s) failed\n" % failures)
        return 1
    sys.stdout.write("\nall checks passed\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
