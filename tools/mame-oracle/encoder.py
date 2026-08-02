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


if __name__ == "__main__":
    import sys
    print(to_hex(sentinel_probe()), file=sys.stdout)
