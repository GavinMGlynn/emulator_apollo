#!/usr/bin/env python3
"""Hand-assemble 68000 instruction words, with no cross toolchain.

Phase 1 asks for "a Python probe encoder emitting hand-assembled 68000 probes —
no cross toolchain", verified by "a trivial probe that stores a sentinel runs
identically under both". This is the encoder half: it turns a probe written as
Python calls into the `uint16_t` words that `ap_probe_t` already takes and that
MAME's Lua can write straight into the oracle's RAM.

## Why hand-assembled rather than an assembler

A cross toolchain would have to be installed, pinned and trusted, and it would
put a third party's idea of an encoding between this project and the manual.
Every opcode here is instead a bit pattern taken from `M68000 Family
Programmer's Reference Manual 1992` and cited in the function that builds it, so
a wrong encoding is a wrong *citation* rather than a build-environment problem.
The set is deliberately tiny: what a probe needs and nothing else.

## Where the words go

The same words are used twice, which is the whole point:

  - into `ap_machine` through `apollo-headless --probe-file`, and
  - into the oracle's RAM through `tools/mame-oracle/probe.lua`.

Neither path needs firmware, a boot, or the Mnemonic Debugger. MD remains the
development-time route for poking a *running* machine (`docs/references/MD.md`),
but a probe that must run identically on both sides is better served by writing
memory directly on both sides.

Bit layouts are big-endian words, as the part fetches them.
"""

from __future__ import annotations


class EncodingError(ValueError):
    pass


def _check(condition: bool, message: str) -> None:
    if not condition:
        raise EncodingError(message)


def _dn(n: int) -> int:
    _check(0 <= n <= 7, "data register out of range: %r" % (n,))
    return n


def _an(n: int) -> int:
    _check(0 <= n <= 7, "address register out of range: %r" % (n,))
    return n


def _long_words(value: int) -> list[int]:
    _check(0 <= value <= 0xFFFFFFFF, "not a 32-bit value: %r" % (value,))
    return [(value >> 16) & 0xFFFF, value & 0xFFFF]


def nop() -> list[int]:
    """NOP — PRM page 4-119, `0100 1110 0111 0001`."""
    return [0x4E71]


def stop(status: int) -> list[int]:
    """STOP #<data> — PRM page 6-85, `0100 1110 0111 0010` then the word.

    Every probe ends with one. A probe that runs out of its instruction limit
    reports whatever it happened to be doing; a probe that stops has said it is
    finished, which is the difference between a result and a snapshot.
    """
    _check(0 <= status <= 0xFFFF, "not a 16-bit status: %r" % (status,))
    return [0x4E72, status]


def moveq(value: int, dn: int) -> list[int]:
    """MOVEQ #<data>,Dn — PRM page 4-134, `0111 rrr 0 dddddddd`.

    The immediate is *sign-extended to a long*, which is why the range is
    signed: `moveq(0x80, d)` is not 128 in the register, it is -128.
    """
    _check(-128 <= value <= 127, "MOVEQ takes a signed byte: %r" % (value,))
    return [0x7000 | (_dn(dn) << 9) | (value & 0xFF)]


def move_l_dn_to_abs(dn: int, address: int) -> list[int]:
    """MOVE.L Dn,(xxx).L — PRM page 4-116.

    MOVE is `00 SS RRR MMM mmm rrr`: size `10` for long, then the *destination*
    register and mode, then the source mode and register. Absolute long is mode
    `111` register `001`, so this is `0010 001 111 000 rrr`. Destination before
    source in the encoding is the trap worth naming — it reads backwards from
    the assembly syntax.
    """
    return [0x23C0 | _dn(dn)] + _long_words(address)


def move_l_abs_to_dn(address: int, dn: int) -> list[int]:
    """MOVE.L (xxx).L,Dn — PRM page 4-116, the same layout the other way round:
    destination mode `000` register `rrr`, source mode `111` register `001`."""
    return [0x2039 | (_dn(dn) << 9)] + _long_words(address)


def movea_l_imm(value: int, an: int) -> list[int]:
    """MOVEA.L #<data>,An — PRM page 4-118. `0010 rrr 001 111 100`: destination
    mode `001` (An), source mode `111` register `100` (immediate)."""
    return [0x207C | (_an(an) << 9)] + _long_words(value)


def addq_l(value: int, dn: int) -> list[int]:
    """ADDQ.L #<data>,Dn — PRM page 4-11, `0101 ddd 0 10 000 rrr`. The immediate
    is 1-8, and **8 is encoded as 0** — a zero field means eight, not nothing."""
    _check(1 <= value <= 8, "ADDQ takes 1-8: %r" % (value,))
    return [0x5080 | ((value & 7) << 9) | _dn(dn)]


def dbf(dn: int, displacement: int) -> list[int]:
    """DBF Dn,<label> — PRM page 4-62, `0101 0001 11 001 rrr` then a word
    displacement taken from the address of the *extension word*."""
    _check(-32768 <= displacement <= 32767,
           "displacement out of range: %r" % (displacement,))
    return [0x51C8 | _dn(dn), displacement & 0xFFFF]


def assemble(*parts: list[int]) -> list[int]:
    """Concatenate encoded pieces into one word list."""
    words: list[int] = []
    for part in parts:
        words.extend(part)
    return words


def to_hex(words: list[int]) -> str:
    return " ".join("%04X" % w for w in words)


# The probe the plan's verification names: store a sentinel and stop.
SENTINEL = 0x5A
SENTINEL_ADDRESS = 0x00001800


def sentinel_probe(address: int = SENTINEL_ADDRESS,
                   value: int = SENTINEL) -> list[int]:
    """MOVEQ the sentinel, store it to an absolute address, STOP.

    Deliberately the smallest program that proves the whole path: an immediate
    reaches a register, a register reaches memory at an address the encoder
    computed, and the machine stops because the program said so.
    """
    return assemble(moveq(value, 0),
                    move_l_dn_to_abs(0, address),
                    stop(0x2700))


def fpu_probe(address: int = SENTINEL_ADDRESS) -> list[int]:
    """FMOVECR pi, double it, store it, and leave the high long word behind.

    The smallest program that makes the coprocessor prove three separate things
    at once: `FMOVECR` reads the on-chip constant ROM, `FADD` is arithmetic, and
    `FMOVE.D` is the store conversion. The answer is a bit pattern neither
    implementation is free to choose -- 2*pi as an IEEE double is
    $401921FB54442D18 on every machine that has ever computed it -- so the two
    sides agreeing is agreement with something outside both.

    Only the *high* long word is compared, because that is what the existing
    sentinel machinery reads back, and it already contains the sign, the whole
    exponent and the top twenty fraction bits: a wrong constant, a missing add
    or a botched conversion all move it.

    A0 is loaded rather than an absolute address used because `FMOVE.D FP0,(A0)`
    is the form a compiler emits, and because the two machines put RAM at
    different bases -- the address is a parameter here for the same reason the
    sentinel probe's is.
    """
    return assemble(
        [0x207C, (address >> 16) & 0xFFFF, address & 0xFFFF],  # MOVEA.L #a,A0
        [0xF200, 0x5C00],                                      # FMOVECR #0,FP0
        [0xF200, 0x0022],                                      # FADD    FP0,FP0
        [0xF210, 0x7400],                                      # FMOVE.D FP0,(A0)
        stop(0x2700),
    )


def fpu_rounding_probe(address: int = SENTINEL_ADDRESS) -> list[int]:
    """Round-to-zero, then `ln(10)` stored as a double, low long word first.

    The rounding *mode* is the half of the verification line a single result
    cannot reach, and picking a value for it takes care: rounding happens at
    bit 52 of a double, so a change of mode reaches the *high* long word only
    when a carry propagates through all thirty-two low bits. Of the constant
    ROM's entries, `ln(10)` at offset `$31` is one whose low word moves --
    `40026BB1BBB55516` to the nearest against `...5515` toward zero -- so the
    store is aimed four bytes low and the sentinel read lands on the half that
    actually differs.

    Which makes this probe fail if either side ignores the mode, and fail
    differently if either rounds the wrong way.
    """
    return assemble(
        [0x7010],                                              # MOVEQ #$10,D0
        [0xF200, 0x9000],                                      # FMOVE.L D0,FPCR
        [0xF200, 0x5C31],                                      # FMOVECR #$31,FP0
        [0x207C, ((address - 4) >> 16) & 0xFFFF,
         (address - 4) & 0xFFFF],                              # MOVEA.L #a-4,A0
        [0xF210, 0x7400],                                      # FMOVE.D FP0,(A0)
        stop(0x2700),
    )


def fpu_sine_probe(address: int = SENTINEL_ADDRESS) -> list[int]:
    """`FSIN` of 1.0, stored as a double, low long word first.

    The transcendentals are where the two implementations are *least* obliged to
    agree: §4.3.2 publishes an error bound and no algorithm, so any conforming
    sine may differ from any other in the low bits, and MAME's driver admits
    gaps "in some FPU operations and operands". This probe is therefore the one
    most likely to find a difference -- which is why it reads the low long word,
    where a difference would appear, rather than the high one where two answers
    within a hundred units in the last place would still look identical.

    1.0 comes from the constant ROM at `$32` (`10^0`) so the probe needs no
    immediate operand, and `sin(1)` is `3FEAED548F090CEE` correctly rounded.
    """
    return assemble(
        [0xF200, 0x5C32],                                      # FMOVECR #$32,FP0
        [0xF200, 0x000E],                                      # FSIN    FP0,FP0
        [0x207C, ((address - 4) >> 16) & 0xFFFF,
         (address - 4) & 0xFFFF],                              # MOVEA.L #a-4,A0
        [0xF210, 0x7400],                                      # FMOVE.D FP0,(A0)
        stop(0x2700),
    )


def fpu_sine_extended_probe(address: int = SENTINEL_ADDRESS) -> list[int]:
    """`FSIN` of 1.0 stored as *extended*, reading the low mantissa long word.

    C61 recorded that comparing a double cannot separate two conforming sines:
    one unit in the last place of a double is 2048 of extended, and both
    implementations are far inside that. The sharp comparison needs the extended
    value -- and it needs no wider readback, because `FMOVE.X` writes twelve
    bytes and the harness can be pointed at whichever long word matters.

    That is the *third*: bytes 8-11 are mantissa bits 31-0, which is exactly
    where two sines accurate to a few units in the last place are free to
    disagree. So the store is aimed eight bytes low.

    This probe is expected to be the one that finds a difference. If it does,
    the difference is not automatically ours: §4.3.2 publishes a bound and no
    algorithm, so both may conform and disagree, and the row records which is
    closer to the true sine rather than which matches the other.
    """
    return assemble(
        [0xF200, 0x5C32],                                      # FMOVECR #$32,FP0
        [0xF200, 0x000E],                                      # FSIN    FP0,FP0
        [0x207C, ((address - 8) >> 16) & 0xFFFF,
         (address - 8) & 0xFFFF],                              # MOVEA.L #a-8,A0
        [0xF210, 0x6800],                                      # FMOVE.X FP0,(A0)
        stop(0x2700),
    )


def fault_probe(load_at: int, address: int = SENTINEL_ADDRESS) -> list[int]:
    """Take an illegal instruction and store the stacked format word.

    `FINDINGS.md` C72 and its addenda, as one program. The exception item's
    verification asks for "probes that deliberately fault, diffed against
    oracle", and four obstacles stood in the way; three of them dissolve here.

    The probe plants its **own** handler rather than using the harness's, which
    is a bare `RTE` and would loop forever: a fault stacks the address of the
    instruction that faulted, so returning to it faults again. This handler ends
    in `STOP`, so the probe terminates like every other.

    It therefore needs one vector rather than sixty-two -- vector 4, at
    `VBR + $10` -- and it points the VBR at its own table. On this core's side
    that `MOVEC` is very nearly a no-op, because the probe harness plants its
    table at zero where the VBR already points; on the oracle it is what makes
    the fault land anywhere at all.

    What it stores is the **format word** at `SP + 6`: the frame format nibble
    and the vector offset, which is what the verification line is really about
    and is map-independent. `$0010` is the answer -- a four-word frame, format
    0, and vector 4 at offset `$10` -- and it is the same on any M68000 machine,
    which is what makes it worth comparing.
    """
    handler = load_at + 22
    table = load_at + 0x100
    return assemble(
        [0x23FC, (handler >> 16) & 0xFFFF, handler & 0xFFFF,
         ((table + 0x10) >> 16) & 0xFFFF, (table + 0x10) & 0xFFFF],
        [0x203C, (table >> 16) & 0xFFFF, table & 0xFFFF],  # MOVE.L #table,D0
        [0x4E7B, 0x0801],                                  # MOVEC D0,VBR
        [0x4AFC],                                          # ILLEGAL
        # handler, at load_at + 22
        [0x7000],                                          # MOVEQ #0,D0
        [0x302F, 0x0006],                                  # MOVE.W 6(SP),D0
        [0x23C0, (address >> 16) & 0xFFFF, address & 0xFFFF],
        stop(0x2700),
    )


def bus_fault_probe(load_at: int, address: int = SENTINEL_ADDRESS,
                    bad: int = 0xF0000000) -> list[int]:
    """Read an address neither machine maps, and store the frame's format word.

    C72 said comparing bus fault frames needed "a fault at an address both maps
    agree is bad", and treated that as an obstacle. It is not much of one: the
    two machines disagree about which addresses are *valid*, but `$F0000000` is
    above everything either of them maps -- 64K of probe RAM here, main memory
    and devices on a DN3500 -- so the same literal faults on both. And because
    it is the same literal, the frame's fault address field holds the same value
    on both sides too, which was the other half of C72's worry.

    What is stored is again the format word at `SP + 6`, and here it is the
    interesting field rather than a formality. A bus error is vector 2, offset
    `$08`, but the *format nibble* is a real modelling decision: `$A` is the
    short frame, "Execution Unit at Instruction Boundary", and `$B` the long
    one, "Instruction Execution in Progress". This core produces `$B` for every
    data fault, on the reasoning that an operand access that failed partway
    through an unfinished instruction is the second case. Whether the oracle
    agrees has never been checked.
    """
    handler = load_at + 26
    table = load_at + 0x100
    return assemble(
        [0x23FC, (handler >> 16) & 0xFFFF, handler & 0xFFFF,
         ((table + 8) >> 16) & 0xFFFF, (table + 8) & 0xFFFF],
        [0x203C, (table >> 16) & 0xFFFF, table & 0xFFFF],  # MOVE.L #table,D0
        [0x4E7B, 0x0801],                                  # MOVEC D0,VBR
        [0x2239, (bad >> 16) & 0xFFFF, bad & 0xFFFF],      # MOVE.L (bad).L,D1
        # handler, at load_at + 26
        [0x7000],                                          # MOVEQ #0,D0
        [0x302F, 0x0006],                                  # MOVE.W 6(SP),D0
        [0x23C0, (address >> 16) & 0xFFFF, address & 0xFFFF],
        stop(0x2700),
    )


def dbcc_probe(address: int = SENTINEL_ADDRESS) -> list[int]:
    """A `DBRA` counted to exhaustion, and the register it leaves behind.

    `DBcc` decrements the **low word only** and terminates at `-1`, not at zero,
    and both halves of that are easy to get wrong in a way nothing else catches:
    a full-width decrement is right for every count that never borrows, and a
    terminate-at-zero loop runs one iteration short forever.

    `MOVEQ #3` sign-extends into all thirty-two bits, so a correct
    implementation leaves `$0000FFFF` -- the low word wrapped to `-1` and the
    high word untouched at zero. A full-width decrement would leave
    `$FFFFFFFF`, which is one bit of difference and a completely different
    instruction.

    **The loop has a `NOP` in it deliberately.** `DBRA` branching to *itself*
    leaves the program counter unmoved, and the oracle harness reads two
    consecutive unmoved PCs as a halt -- which is how it tells a `STOP` from a
    running program without asking the debugger. A self-loop is therefore
    indistinguishable from a stopped machine from outside, and the first version
    of this probe hit exactly that (`FINDINGS.md` C75). One instruction of body
    is enough to make the loop visible.
    """
    return assemble(
        [0x7003],                                          # MOVEQ #3,D0
        [0x4E71],                                          # loop: NOP
        [0x51C8, 0xFFFC],                                  # DBRA D0,loop
        [0x23C0, (address >> 16) & 0xFFFF, address & 0xFFFF],
        stop(0x2700),
    )


def movem_probe(address: int = SENTINEL_ADDRESS) -> list[int]:
    """`MOVEM` out through a predecrement and back through a postincrement.

    The register list's bit order **reverses** between the two modes -- for
    `-(An)` bit 15 is `D0`, for `(An)+` bit 0 is -- and a model using one
    ordering for both reverses every transfer while still moving the stack
    pointer exactly the right distance. That is the integer core's version of
    the trap the floating-point `FMOVEM` carries, and nothing straight-line
    catches it.

    Three registers out and three different ones back, so the *mapping* is what
    is checked rather than a round trip that would survive a double reversal:
    `D0`, `D1`, `D2` hold 1, 2, 3 going out, and `D3`, `D4`, `D5` receive them
    coming back. `D5` is stored, and is 3 only if both orderings are right.
    """
    return assemble(
        [0x7001],                                          # MOVEQ #1,D0
        [0x7201],                                          # MOVEQ #1,D1
        [0x5241],                                          # ADDQ.W #1,D1  -> 2
        [0x7403],                                          # MOVEQ #3,D2
        [0x48E7, 0xE000],                                  # MOVEM.L D0-D2,-(A7)
        [0x4CDF, 0x0038],                                  # MOVEM.L (A7)+,D3-D5
        [0x23C5, (address >> 16) & 0xFFFF, address & 0xFFFF],
        stop(0x2700),
    )


if __name__ == "__main__":
    import sys
    print(to_hex(sentinel_probe()), file=sys.stdout)
