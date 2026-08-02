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


# Table 4-13's extension for each, applied to 1.0 from constant ROM offset $32.
ROWS = [
    ("FSIN", 0x0E, _sin),
    ("FCOS", 0x1D, _cos),
    ("FTAN", 0x0F, _tan),
    ("FETOX", 0x10, _exp),
    ("FATAN", 0x0A, _atan),
]


def probe_words(extension: int, address: int) -> list[int]:
    """The C62 shape with one function substituted."""
    return E.assemble(
        [0xF200, 0x5C32],                                   # FMOVECR #$32,FP0
        [0xF200, extension],                                # F<op>   FP0,FP0
        [0x207C, ((address - 8) >> 16) & 0xFFFF,
         (address - 8) & 0xFFFF],                           # MOVEA.L #a-8,A0
        [0xF210, 0x6800],                                   # FMOVE.X FP0,(A0)
        E.stop(0x2700),
    )


def main() -> int:
    one = Decimal(1)
    print("%-8s %-10s %-10s %-10s %s"
          % ("function", "truth", "ours", "oracle", "closer"))
    tally = {"ours": 0, "oracle": 0, "both exact": 0}

    for name, extension, fn in ROWS:
        truth = extended_low_word(fn(one))
        ours = PC.run_ours(
            probe_words(extension, PC.OURS_BASE + PC.SENTINEL_OFFSET),
            PC.OURS_BASE, PC.OURS_BASE + PC.SENTINEL_OFFSET, 30, Path("/tmp"))
        oracle = PC.run_oracle(
            probe_words(extension, PC.ORACLE_BASE + PC.SENTINEL_OFFSET),
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
        print("%-8s %-10s %-10s %-10s %s" % (name, truth, a, b, verdict))

    print()
    print("closer to the true value: " +
          ", ".join("%s %d" % (k, v) for k, v in tally.items() if v))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
