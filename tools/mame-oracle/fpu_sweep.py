#!/usr/bin/env python3
"""Run one transcendental per probe on both sides and adjudicate against truth.

C62 found that comparing *extended* separates the two implementations where
comparing a double cannot, and that on `sin(1)` the oracle is the closer of the
two. One argument of one function is an anecdote; this turns it into a count.

Each row runs the same shape -- load 1.0 from the constant ROM, apply one
function, store the result as extended, read mantissa bits 31-0 -- and compares
both sides against a value computed here to 120 decimal digits. **Neither
implementation adjudicates the other**: §4.3.2 publishes an error bound and no
algorithm, so both may conform and disagree, and the only fixed point is the
mathematics.

    python3 tools/mame-oracle/fpu_sweep.py
"""

from __future__ import annotations

import subprocess
import sys
from decimal import Decimal, getcontext
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import encoder as E  # noqa: E402
import probe_compare as PC  # noqa: E402

getcontext().prec = 140


def _sin(x: Decimal) -> Decimal:
    term, total = x, Decimal(0)
    for n in range(60):
        if n:
            term = -term * x * x / Decimal((2 * n) * (2 * n + 1))
        total += term
    return total


def _cos(x: Decimal) -> Decimal:
    term, total = Decimal(1), Decimal(0)
    for n in range(60):
        if n:
            term = -term * x * x / Decimal((2 * n - 1) * (2 * n))
        total += term
    return total


def _exp(x: Decimal) -> Decimal:
    return x.exp()


def _atan(x: Decimal) -> Decimal:
    # x = 1, so the series converges far too slowly: use pi/4 directly.
    return _pi() / 4


def _tan(x: Decimal) -> Decimal:
    return _sin(x) / _cos(x)


def _pi() -> Decimal:
    # Machin, which converges fast enough at this precision.
    def atan_inv(n: Decimal) -> Decimal:
        total, term, k = Decimal(0), Decimal(1) / n, 0
        while True:
            add = term / (2 * k + 1)
            if add == 0:
                return total
            total += add if k % 2 == 0 else -add
            term /= n * n
            k += 1
    return 4 * (4 * atan_inv(Decimal(5)) - atan_inv(Decimal(239)))


def extended_low_word(value: Decimal) -> str:
    """The correctly rounded extended significand's bits 31-0, as the probe
    reads them."""
    sign = value < 0
    v = -value if sign else value
    e, y = 0, v
    while y >= 2:
        y /= 2
        e += 1
    while y < 1:
        y *= 2
        e -= 1
    m = y * (Decimal(2) ** 63)
    mi = int(m)
    frac = m - mi
    if frac > Decimal("0.5") or (frac == Decimal("0.5") and mi % 2 == 1):
        mi += 1
    return "%08X" % (mi & 0xFFFFFFFF)


def _sqrt(x: Decimal) -> Decimal:
    return x.sqrt()


def _int(x: Decimal) -> Decimal:
    return Decimal(int(x))  # round-to-nearest is not exercised: 10 and pi both
                            # sit far from a half


# Table 4-13's extension for each, with the constant ROM offset supplying the
# argument. `$32` is 1.0, `$33` is 10, `$00` is pi.
#
# The split matters more than the list. **The transcendentals are bounded, not
# specified** -- §4.3.2 gives an error interval and no algorithm, so a difference
# there is a resolution limit and `FINDINGS.md` C70 settled it as one. The
# exactly specified operations have no such licence: "except square root" is the
# manual's own phrasing, and `FINT` and `FGETEXP` have one right answer each. A
# difference in *those* rows is a defect on one side.
ROWS = [
    ("FSIN", 0x0E, 0x32, _sin, "bounded"),
    ("FCOS", 0x1D, 0x32, _cos, "bounded"),
    ("FTAN", 0x0F, 0x32, _tan, "bounded"),
    ("FETOX", 0x10, 0x32, _exp, "bounded"),
    ("FATAN", 0x0A, 0x32, _atan, "bounded"),
    ("FSQRT", 0x04, 0x33, _sqrt, "exact"),
    ("FINT", 0x01, 0x00, _int, "exact"),
]


def probe_words(extension: int, rom: int, address: int) -> list[int]:
    """The C62 shape with one function and one argument substituted."""
    return E.assemble(
        [0xF200, 0x5C00 | rom],                             # FMOVECR #rom,FP0
        [0xF200, extension],                                # F<op>   FP0,FP0
        [0x207C, ((address - 8) >> 16) & 0xFFFF,
         (address - 8) & 0xFFFF],                           # MOVEA.L #a-8,A0
        [0xF210, 0x6800],                                   # FMOVE.X FP0,(A0)
        E.stop(0x2700),
    )


def main() -> int:
    print("%-8s %-10s %-10s %-10s %s"
          % ("function", "truth", "ours", "oracle", "closer"))
    tally = {"ours": 0, "oracle": 0, "both exact": 0}

    args = {0x32: Decimal(1), 0x33: Decimal(10), 0x00: _pi()}
    for name, extension, rom, fn, kind in ROWS:
        truth = extended_low_word(fn(args[rom]))
        ours = PC.run_ours(
            probe_words(extension, rom, PC.OURS_BASE + PC.SENTINEL_OFFSET),
            PC.OURS_BASE, PC.OURS_BASE + PC.SENTINEL_OFFSET, 30, Path("/tmp"))
        oracle = PC.run_oracle(
            probe_words(extension, rom, PC.ORACLE_BASE + PC.SENTINEL_OFFSET),
            PC.ORACLE_BASE, PC.ORACLE_BASE + PC.SENTINEL_OFFSET, 30, 300.0)
        a, b = ours.get("read"), oracle.get("read")

        if a == truth and b == truth:
            verdict = "both exact"
        elif a == truth:
            verdict = "ours"
        elif b == truth:
            verdict = "oracle"
        else:
            # Both differ from truth: compare distance in the low word, which is
            # only meaningful when neither wrapped, so say so rather than guess.
            verdict = "neither (both differ)"
        tally[verdict] = tally.get(verdict, 0) + 1
        # An exactly specified operation that differs is a *defect*, not a
        # resolution limit: §4.3.2's bound does not cover it.
        flag = ""
        if kind == "exact" and verdict != "both exact":
            flag = "  <-- DEFECT: no error bound licenses this"
        print("%-8s %-10s %-10s %-10s %s%s"
              % (name, truth, a, b, verdict, flag))

    print()
    print("closer to the true value: " +
          ", ".join("%s %d" % (k, v) for k, v in tally.items() if v))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
