#!/usr/bin/env python3
"""Run one hand-assembled probe on both sides and diff the result.

This is the Phase 1 item's verification made executable: *"a trivial probe that
stores a sentinel runs identically under both"*. The probe is encoded once by
`encoder.py`, then run

  - on this project's core, through `apollo-headless --probe-file`, and
  - on the oracle, through `probe.lua`, which writes the same words into MAME's
    RAM and single-steps them.

Neither side needs firmware, a boot or the Mnemonic Debugger.

## What is compared, and what deliberately is not

Compared: the instructions executed, the value left in D0, and the sentinel read
back from memory. Those are properties of the *program*.

Not compared: the addresses. The two machines put RAM in different places — this
core's probe RAM starts at zero, a DN3500's main memory at `01000000` — so the
same program is assembled twice at two bases, and a PC that differs by the base
is not a disagreement. Comparing raw PCs would fail every time for a reason that
has nothing to do with the instruction under test.

Not compared either: **clocks**. This core does not yet model instruction
execution time (a named plan item), so its figure is a lower bound and the two
are not comparable. Saying so here is cheaper than someone discovering it from a
mismatch.

    python3 tools/mame-oracle/probe_compare.py
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
sys.path.insert(0, str(HERE))
import encoder as E  # noqa: E402

MAME_NAMES = ("apollo", "mameapollo")
DEFAULT_ROMS = HERE / "out" / "roms"
PROBE_LUA = HERE / "probe.lua"

# Where each side has usable RAM. Not a preference: `FINDINGS.md` C5 measured
# that a write to the DN3500's boot PROM range succeeds and does nothing, so a
# probe loaded low on the oracle would run the PROM while the harness believed
# it ran the probe.
OURS_BASE = 0x00001000
# Where each machine puts main memory, plus the 4K the probe loads at. Taken
# from MAME's own driver, which states both -- `DN3500_RAM_BASE 0x1000000` and
# `DN3000_RAM_BASE 0x100000` -- rather than measured, because a memory map is a
# fact about the machine and the driver is the oracle's statement of it.
#
# `FINDINGS.md` C83: this was one constant for a long time and it was the
# DN3500's, which every probe until now ran on. A DN3000 has sixteen times less
# address above zero before its RAM starts.
ORACLE_RAM_BASE = {"dn3500": 0x01000000, "dn3000": 0x00100000,
                   "dn5500": 0x01000000}
ORACLE_BASE = ORACLE_RAM_BASE["dn3500"] + 0x1000
SENTINEL_OFFSET = 0x800


def find_headless() -> Path:
    for preset in ("linux-debug", "linux-release"):
        candidate = REPO / "build" / preset / "src" / "frontend" / "headless" / "apollo-headless"
        if candidate.is_file():
            return candidate
    sys.stderr.write("probe_compare: no apollo-headless built; run cmake --build first\n")
    raise SystemExit(2)


def find_mame() -> Path:
    for name in MAME_NAMES:
        candidate = REPO / "ext" / "mame" / name
        if candidate.is_file():
            return candidate
    sys.stderr.write("probe_compare: no oracle binary in ext/mame; see oracle.py\n")
    raise SystemExit(2)


def parse_fields(text: str) -> dict:
    """Both sides print `key value` lines, so one parser reads both."""
    out = {}
    for line in text.splitlines():
        if line.startswith("#") or not line.strip():
            continue
        parts = line.split()
        if len(parts) == 2:
            out[parts[0]] = parts[1]
        elif len(parts) == 3 and parts[0] == "read":
            out["read_addr"], out["read"] = parts[1], parts[2]
    return out


def run_ours(words, base, sentinel_at, limit, work: Path,
             machine: str = "dn3500") -> dict:
    spec = work / "probe.spec"
    spec.write_text(
        "load  %X\nentry %X\nstack %X\nlimit %d\nread  %X\nwords %s\n"
        % (base, base, base + 0x1000, limit, sentinel_at, E.to_hex(words)))
    proc = subprocess.run([str(find_headless()), "--model", machine,
                           "--probe-file", str(spec)],
                          capture_output=True, text=True, timeout=120)
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr)
        raise SystemExit(1)
    return parse_fields(proc.stdout)


def run_oracle(words, base, sentinel_at, limit, timeout: float,
               machine: str = "dn3500") -> dict:
    mame = find_mame()
    env = dict(os.environ)
    env.update({
        "APOLLO_PROBE_WORDS": E.to_hex(words),
        "APOLLO_PROBE_LOAD": "%X" % base,
        "APOLLO_PROBE_STACK": "%X" % (base + 0x1000),
        "APOLLO_PROBE_READ": "%X" % sentinel_at,
        "APOLLO_PROBE_LIMIT": str(limit),
        "APOLLO_PROBE_HANDLER": "%X" % (base + 0x2000),
        "APOLLO_PROBE_AT": "3.0",
    })
    command = [str(mame), machine, "-noreadconfig",
               "-rompath", str(DEFAULT_ROMS),
               "-video", "none", "-sound", "none", "-nothrottle",
               "-debug", "-debugger", "none", "-seconds_to_run", "10",
               "-autoboot_script", str(PROBE_LUA)]
    proc = subprocess.run(command, capture_output=True, text=True,
                          env=env, cwd=str(mame.parent), timeout=timeout)
    # The oracle side comments on what it planted and what it read back. Those
    # lines are the only window into the far machine's setup -- C72's addendum
    # spent a turn unable to tell whether an exception table had landed, because
    # this output was captured and then discarded.
    for line in proc.stdout.splitlines():
        if line.startswith("#") and "loaded" not in line:
            print("oracle: %s" % line[1:].strip())

    if "readback ok" not in proc.stdout:
        sys.stderr.write("probe_compare: the oracle did not accept the probe\n")
        sys.stderr.write(proc.stdout[-2000:])
        raise SystemExit(1)
    return parse_fields(proc.stdout)


# Every program this script knows how to run, in the order the campaign built
# them. `--program all` walks the list, which is what turns thirteen one-off
# measurements into a check someone can re-run after changing the core.
ALL_PROGRAMS = ("sentinel", "dbcc", "subroutine", "divide", "divide-overflow",
                "movem", "pmove", "fault", "bus-fault", "fpu", "fpu-rounding",
                "fpu-sine", "fpu-sine-x")


def run_all(argv, parser) -> int:
    """Run every program and report a single verdict.

    Each is a separate MAME launch, so this is minutes rather than seconds --
    which is why it is opt-in rather than the default. The value is that a
    regression in any class is one command away instead of thirteen, and that
    the list itself records what has been compared.
    """
    # `fpu-sine-x` differs by one unit in the last place and is *expected* to:
    # C70 settled it as a resolution limit rather than a defect -- arithmetic
    # done at the destination's own width, where §3.4 has the part carry 67
    # bits. Listing it here is what makes this a regression check instead of a
    # measurement: a run is green when nothing has changed, and red only when
    # something new has. A known difference that reddens the suite every time
    # trains its reader to ignore it, which is the same mistake C75's dangerous
    # output nearly caused.
    KNOWN_DIFFER = {"fpu-sine-x": "C70, one ULP, settled sub-poll-slack"}

    failed = []
    expected = []
    for name in ALL_PROGRAMS:
        print("=" * 72)
        print("== %s" % name)
        rest = [a for a in (argv or []) if not a.startswith("--program")]
        differed = main([*rest, "--program", name], recursing=True) != 0
        if differed and name in KNOWN_DIFFER:
            expected.append(name)
        elif differed:
            failed.append(name)
        elif name in KNOWN_DIFFER:
            # A known difference that stopped differing is also news.
            failed.append("%s (expected to differ, did not)" % name)
    print("=" * 72)
    for name in expected:
        print("known difference: %s -- %s" % (name, KNOWN_DIFFER[name]))
    if failed:
        print("UNEXPECTED: %s" % ", ".join(failed))
        return 1
    print("%d of %d probe programs ran identically; %d differed as recorded"
          % (len(ALL_PROGRAMS) - len(expected), len(ALL_PROGRAMS),
             len(expected)))
    return 0


def main(argv=None, recursing=False) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--sentinel", type=lambda s: int(s, 0), default=E.SENTINEL)
    parser.add_argument("--limit", type=int, default=20)
    parser.add_argument("--timeout", type=float, default=300.0)
    parser.add_argument("--work", type=Path, default=Path("/tmp"))
    parser.add_argument(
        "--machine", default="dn3500",
        help="which model to run on *both* sides -- `dn3000` probes a 68020, "
             "which is what the 68020 subset's verification line asks for and "
             "what no machine this core built could do until it had a model")
    parser.add_argument(
        "--program", choices=("sentinel", "fpu", "fpu-rounding", "fpu-sine", "fpu-sine-x", "fault", "bus-fault", "dbcc", "movem", "divide", "divide-overflow", "subroutine", "pmove", "module-call",
                 "all"),
        default="sentinel",
        help="which probe to run; `fpu` exercises the coprocessor's constant "
             "ROM, an FADD and the store conversion in one")
    args = parser.parse_args(argv)

    global ORACLE_BASE  # noqa: PLW0603 -- the base is a property of the run
    if args.machine not in ORACLE_RAM_BASE:
        print("no RAM base recorded for %s; add it from MAME's driver"
              % args.machine)
        return 2
    ORACLE_BASE = ORACLE_RAM_BASE[args.machine] + 0x1000

    if args.program == "all" and not recursing:
        return run_all(argv, parser)

    if args.program == "module-call":
        ours_words = E.module_call_probe(OURS_BASE, OURS_BASE + SENTINEL_OFFSET)
        oracle_words = E.module_call_probe(ORACLE_BASE,
                                           ORACLE_BASE + SENTINEL_OFFSET)
        print("probe:  build a module descriptor ; CALLM into it ; the module"
              " stores D3")
        print("        $00C0FFEE means the descriptor was read, the frame"
              " built, the entry")
        print("        word honoured and execution continued past it -- the"
              " whole instruction")
    elif args.program == "pmove":
        ours_words = E.pmove_probe(OURS_BASE + SENTINEL_OFFSET)
        oracle_words = E.pmove_probe(ORACLE_BASE + SENTINEL_OFFSET)
        print("probe:  MOVEA.L #sentinel,A0 ; PMOVE TC,(A0) ; STOP")
        print("        the MMU's reset state, read out by the program rather"
              " than reported by")
        print("        the harness -- and the only probe that reads a register"
              " it never wrote")
    elif args.program == "divide-overflow":
        ours_words = E.divide_overflow_probe(OURS_BASE + SENTINEL_OFFSET)
        oracle_words = E.divide_overflow_probe(ORACLE_BASE + SENTINEL_OFFSET)
        print("probe:  MOVE.L #$100003,D0 ; DIVU.W #1,D0 ; store D0 ; STOP")
        print("        the quotient will not fit sixteen bits, so V is set and"
              " the destination")
        print("        is left alone -- the answer is the dividend unchanged,"
              " $00100003")
    elif args.program == "subroutine":
        ours_words = E.subroutine_probe(OURS_BASE + SENTINEL_OFFSET, OURS_BASE)
        oracle_words = E.subroutine_probe(ORACLE_BASE + SENTINEL_OFFSET,
                                          ORACLE_BASE)
        print("probe:  BSR.W sub ; MOVE.L D0,(sentinel) ; STOP ;"
              " sub: MOVEQ #$2A,D0 ; RTS")
        print("        $2A in memory means the subroutine was entered *and*"
              " returned from;")
        print("        a wrong displacement or a wrong pop width both look like"
              " a probe that")
        print("        never ran, which is why the sentinel is checked and not"
              " the PC")
    elif args.program == "divide":
        ours_words = E.divide_probe(OURS_BASE + SENTINEL_OFFSET)
        oracle_words = E.divide_probe(ORACLE_BASE + SENTINEL_OFFSET)
        print("probe:  MOVE.L #100,D0 ; DIVU.W #7,D0 ; store D0 ; STOP")
        print("        100 over 7 is 14 remainder 2, packed as remainder:"
              "quotient --")
        print("        $0002000E. $000E0002 is the same arithmetic and the"
              " wrong instruction")
    elif args.program == "movem":
        ours_words = E.movem_probe(OURS_BASE + SENTINEL_OFFSET)
        oracle_words = E.movem_probe(ORACLE_BASE + SENTINEL_OFFSET)
        print("probe:  MOVEM.L D0-D2,-(A7) ; MOVEM.L (A7)+,D3-D5 ;"
              " store D5 ; STOP")
        print("        the mask bit order reverses between the two modes;"
              " D5 is 3 only if")
        print("        both orderings are right, and a double reversal would"
              " not survive it")
    elif args.program == "dbcc":
        ours_words = E.dbcc_probe(OURS_BASE + SENTINEL_OFFSET)
        oracle_words = E.dbcc_probe(ORACLE_BASE + SENTINEL_OFFSET)
        print("probe:  MOVEQ #3,D0 ; DBRA D0,self ; MOVE.L D0,(sentinel) ;"
              " STOP")
        print("        $0000FFFF: the low word wrapped to -1, the high word"
              " untouched.")
        print("        A full-width decrement would leave $FFFFFFFF")
    elif args.program == "bus-fault":
        ours_words = E.bus_fault_probe(OURS_BASE, OURS_BASE + SENTINEL_OFFSET)
        oracle_words = E.bus_fault_probe(ORACLE_BASE,
                                         ORACLE_BASE + SENTINEL_OFFSET)
        print("probe:  plant vector 2 ; MOVEC VBR ; read $F0000000 ;"
              " handler stores the format word")
        print("        vector 2 is offset $08; the format nibble is the"
              " question --")
        print("        $A is the short bus fault frame, $B the long one, and"
              " this core says $B")
    elif args.program == "fault":
        ours_words = E.fault_probe(OURS_BASE, OURS_BASE + SENTINEL_OFFSET)
        oracle_words = E.fault_probe(ORACLE_BASE, ORACLE_BASE + SENTINEL_OFFSET)
        print("probe:  plant vector 4 ; MOVEC VBR ; ILLEGAL ;"
              " handler stores the format word")
        print("        $0010 is a four-word frame, format 0, vector 4 at"
              " offset $10 --")
        print("        the same on any M68000 machine, which is what makes it"
              " worth comparing")
    elif args.program == "fpu-sine-x":
        ours_words = E.fpu_sine_extended_probe(OURS_BASE + SENTINEL_OFFSET)
        oracle_words = E.fpu_sine_extended_probe(ORACLE_BASE + SENTINEL_OFFSET)
        print("probe:  FMOVECR #$32,FP0 (1.0) ; FSIN FP0,FP0 ;"
              " FMOVE.X FP0,(A0) ; STOP")
        print("        the *low* mantissa long word is read -- bits 31-0 of the"
              " extended")
        print("        significand, where two sines within a few ULP are free"
              " to differ")
    elif args.program == "fpu-sine":
        ours_words = E.fpu_sine_probe(OURS_BASE + SENTINEL_OFFSET)
        oracle_words = E.fpu_sine_probe(ORACLE_BASE + SENTINEL_OFFSET)
        print("probe:  FMOVECR #$32,FP0 (1.0) ; FSIN FP0,FP0 ;"
              " FMOVE.D FP0,(A0) ; STOP")
        print("        sin(1) correctly rounded is 3FEAED548F090CEE; the low"
              " long word is read,")
        print("        because that is where two conforming sines are free to"
              " differ")
    elif args.program == "fpu-rounding":
        ours_words = E.fpu_rounding_probe(OURS_BASE + SENTINEL_OFFSET)
        oracle_words = E.fpu_rounding_probe(ORACLE_BASE + SENTINEL_OFFSET)
        print("probe:  FMOVE.L #$10,FPCR (round to zero) ; FMOVECR #$31,FP0"
              " (ln 10) ;")
        print("        FMOVE.D FP0,(A0) ; STOP #$2700")
        print("        ln(10) is 40026BB1BBB55516 to nearest, ...5515 toward"
              " zero;")
        print("        the store is aimed four bytes low so the differing half"
              " is read")
    elif args.program == "fpu":
        ours_words = E.fpu_probe(OURS_BASE + SENTINEL_OFFSET)
        oracle_words = E.fpu_probe(ORACLE_BASE + SENTINEL_OFFSET)
        expected_note = "2*pi as an IEEE double is 401921FB54442D18"
        print("probe:  FMOVECR #$00,FP0 ; FADD FP0,FP0 ; FMOVE.D FP0,(A0) ;"
              " STOP #$2700")
        print("        %s" % expected_note)
    else:
        ours_words = E.sentinel_probe(OURS_BASE + SENTINEL_OFFSET, args.sentinel)
        oracle_words = E.sentinel_probe(ORACLE_BASE + SENTINEL_OFFSET,
                                        args.sentinel)
        print("probe:  MOVEQ #$%02X,D0 ; MOVE.L D0,(sentinel).L ; STOP #$2700"
              % args.sentinel)
    print("ours:   %s  at %08X" % (E.to_hex(ours_words), OURS_BASE))
    print("oracle: %s  at %08X" % (E.to_hex(oracle_words), ORACLE_BASE))
    print()

    ours = run_ours(ours_words, OURS_BASE, OURS_BASE + SENTINEL_OFFSET,
                    args.limit, args.work, args.machine)
    oracle = run_oracle(oracle_words, ORACLE_BASE, ORACLE_BASE + SENTINEL_OFFSET,
                        args.limit, args.timeout, args.machine)

    expected = {"fpu": "401921FB", "fpu-rounding": "BBB55515",
                "fpu-sine": "8F090CEE",
                # No expected value: this is a cross-implementation comparison,
                # and the correctly rounded extended sin(1) is asserted by
                # `m68882_transcendental_suite` against 120-digit references
                # rather than restated here.
                "dbcc": "0000FFFF",
                "movem": "00000003",
                "divide": "0002000E",
                "subroutine": "0000002A",
                "divide-overflow": "00100003",
                # Not asserted: what TC holds at reset is what is being
                # compared, not something to state in advance.
                "pmove": None,
                "module-call": "00C0FFEE",
                "fault": "00000010",
                # No expected value: which frame a data fault produces is the
                # thing being compared, not something to assert in advance.
                "bus-fault": None,
                "fpu-sine-x": None}.get(
        args.program, "%08X" % args.sentinel)
    checks = [
        ("instructions executed", ours.get("ran"), oracle.get("ran")),
        ("sentinel in memory", ours.get("read"), oracle.get("read")),
    ]
    if expected is not None:
        checks.append(
            ("sentinel is the encoded value", expected, ours.get("read")))
    checks += [
    ]
    if args.program == "sentinel":
        # D0 is only a *result* for the program that writes it. The FPU probe
        # never touches it, so comparing it there compares two reset states --
        # ours starts at zero and the oracle's at $0000FFFF -- and reports a
        # difference that says nothing about the instruction under test. That is
        # a property of the probe, not a divergence, so the row is omitted
        # rather than excused every time it is read.
        checks.insert(1, ("D0", ours.get("d0"), oracle.get("d0")))
    print("%-32s %-12s %-12s %s" % ("check", "ours", "oracle", ""))
    failed = 0
    for name, a, b in checks:
        same = a == b
        failed += 0 if same else 1
        print("%-32s %-12s %-12s %s" % (name, a, b, "ok" if same else "DIFFER"))

    # Reported, never compared -- see the module docstring.
    print("\nnot compared: pc (%s vs %s) differs by the RAM base; clocks, since "
          "instruction\n              execution time is not yet modelled on this "
          "side." % (ours.get("pc"), oracle.get("pc")))

    if failed:
        print("\n%d check(s) differ" % failed)
        return 1
    print("\nthe same probe ran identically under both")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
