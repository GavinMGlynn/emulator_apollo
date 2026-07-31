#!/usr/bin/env python3
"""Drive the MAME oracle headlessly and collect a state dump.

MAME is the runnable oracle: built and instrumented, never linked (see
ext/README.md and FINDINGS.md). This runs it with no video, no sound and no
host input, stops it at a fixed point in *emulated* time, and captures machine
state via tools/mame-oracle/dump.lua.

The whole value of an oracle reading is that it is reproducible, so the flags
below are not incidental -- each one closes a specific way a second run could
differ from the first:

  -noreadconfig       ignore ~/.mame/mame.ini. Otherwise the oracle's behaviour
                      depends on a file outside the repository that no one
                      reviews, and a reading is not reproducible on another
                      machine.
  -nvram_directory    NVRAM persists across runs by design. Left at its default
  -cfg_directory      the first run writes it and the second reads it back, so
  -state_directory    run 2 starts from a different machine than run 1. These
  -diff_directory     are redirected into the run directory and wiped, which is
  -snapshot_directory what makes "two runs of the same workload" mean anything.
  -video none         no host window, no GPU, no frame timing.
  -sound none         no audio device to underrun.
  -nothrottle         do not pace to wall-clock: run as fast as the host can.
  -seconds_to_run     a watchdog in emulated time. The Lua script normally
                      exits first; this bounds a run whose dump point is never
                      reached, so a hang fails instead of running forever.

Exit status 0 on success, 1 on a failed run or a verify mismatch, 2 on a usage
or environment problem.
"""

from __future__ import annotations

import argparse
import math
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent

# What the narrowed build names its binary has moved with MAME's own makefile:
# older checkouts produced <TARGET><SUBTARGET> = "mameapollo", the pinned v0.289
# names it after the subtarget alone, "apollo". Both are accepted so the driver
# does not break when the ext/mame pin moves, which is the same reason
# romset.py matches ROMs by SHA-1 rather than by name.
MAME_NAMES = ("apollo", "mameapollo")
DEFAULT_MAME = REPO / "ext" / "mame" / MAME_NAMES[0]
DEFAULT_ROMS = HERE / "out" / "roms"
DEFAULT_RUNDIR = HERE / "out" / "run"
DUMP_LUA = HERE / "dump.lua"


def find_mame(explicit: Path | None) -> Path:
    if explicit is not None:
        if not explicit.is_file():
            sys.stderr.write("oracle: no MAME binary at %s\n" % explicit)
            raise SystemExit(2)
        return explicit
    for name in MAME_NAMES:
        candidate = REPO / "ext" / "mame" / name
        if candidate.is_file():
            return candidate
    sys.stderr.write(
        "oracle: no oracle binary at %s (or %s).\nBuild it first -- one driver, "
        "no tools:\n"
        "  cd ext/mame && make SUBTARGET=apollo "
        "SOURCES=src/mame/apollo/apollo.cpp REGENIE=1 TOOLS=0 NOWERROR=1 "
        '-j"$(nproc)"\n'
        "  (budget ~2.5 Gbyte of RAM per job: the luaengine and emumem "
        "translation units are the peak, and -j beyond memory swaps rather "
        "than parallelises)\n" % (DEFAULT_MAME, MAME_NAMES[1])
    )
    raise SystemExit(2)


def build_command(mame: Path, args, rundir: Path) -> list:
    command = [
        str(mame),
        args.machine,
        "-noreadconfig",
        "-rompath", str(args.roms),
        "-video", "none",
        "-sound", "none",
        "-nothrottle",
        # The Lua script exits at args.at; this only bounds a run that never
        # gets there. +1 so the watchdog can never fire before the dump point.
        "-seconds_to_run", str(math.ceil(args.at) + 1),
        "-autoboot_script", str(DUMP_LUA),
    ]
    for option in ("nvram", "cfg", "state", "diff", "snapshot", "input"):
        command += ["-%s_directory" % option, str(rundir / option)]
    command += args.mame_args
    return command


def run_once(mame: Path, args, rundir: Path) -> str:
    # Wiped, not merely created: a leftover NVRAM or cfg from an earlier run is
    # exactly the state that makes run 2 differ from run 1.
    if rundir.exists():
        shutil.rmtree(rundir)
    rundir.mkdir(parents=True)

    environment = dict(os.environ)
    environment["APOLLO_DUMP_AT"] = "%.6f" % args.at
    environment["APOLLO_DUMP_CPU"] = args.cpu
    environment["APOLLO_DUMP_MEM"] = ",".join(args.mem)
    environment["APOLLO_DUMP_EXIT"] = "1"

    command = build_command(mame, args, rundir)
    if args.verbose:
        sys.stderr.write("oracle: %s\n" % " ".join(command))

    try:
        proc = subprocess.run(
            command, capture_output=True, text=True,
            cwd=str(mame.parent), timeout=args.timeout,
        )
    except subprocess.TimeoutExpired:
        sys.stderr.write(
            "oracle: %s did not finish within %ds. The dump point may never be "
            "reached, or the machine may be stuck before it.\n"
            % (args.machine, args.timeout)
        )
        raise SystemExit(1)
    except OSError as exc:
        sys.stderr.write("oracle: cannot run %s: %s\n" % (mame, exc))
        raise SystemExit(2)

    if proc.returncode != 0:
        sys.stderr.write(
            "oracle: %s exited %d\n%s\n" % (args.machine, proc.returncode, proc.stderr)
        )
        raise SystemExit(1)

    # MAME prints its own diagnostics to stdout alongside the script's output.
    # Keep only the dump, delimited by the markers dump.lua writes, so an extra
    # warning line from the oracle cannot make two identical runs look
    # different.
    lines = proc.stdout.splitlines(keepends=True)
    try:
        start = next(i for i, l in enumerate(lines) if l.startswith("# apollo oracle dump"))
        end = next(i for i, l in enumerate(lines) if l.startswith("# end"))
    except StopIteration:
        sys.stderr.write(
            "oracle: no dump in %s's output. The Lua script did not run to "
            "completion.\n--- stdout ---\n%s\n--- stderr ---\n%s\n"
            % (args.machine, proc.stdout, proc.stderr)
        )
        raise SystemExit(1)

    return "".join(lines[start:end + 1])


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("mode", choices=["run", "verify"],
                    help="run: one dump. verify: two runs, assert identical.")
    ap.add_argument("--machine", default="dn3500")
    ap.add_argument("--at", type=float, default=1.0,
                    help="emulated seconds at which to dump (default 1.0)")
    ap.add_argument("--cpu", default=":maincpu")
    ap.add_argument("--mem", action="append", default=[],
                    help="memory range 'space:start+length'; repeatable")
    ap.add_argument("--mame", type=Path, default=None)
    ap.add_argument("--roms", type=Path, default=DEFAULT_ROMS)
    ap.add_argument("--rundir", type=Path, default=DEFAULT_RUNDIR)
    ap.add_argument("--out", type=Path, default=None,
                    help="write the dump here instead of stdout")
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("mame_args", nargs="*", default=[],
                    help="extra arguments passed through to MAME")
    args = ap.parse_args(argv)

    if not DUMP_LUA.is_file():
        sys.stderr.write("oracle: missing %s\n" % DUMP_LUA)
        return 2
    if not args.roms.is_dir():
        sys.stderr.write(
            "oracle: no ROM sets at %s.\nAssemble them first:\n"
            "  python3 tools/mame-oracle/romset.py\n" % args.roms
        )
        return 2

    mame = find_mame(args.mame)

    if args.mode == "run":
        dump = run_once(mame, args, args.rundir)
        if args.out:
            args.out.parent.mkdir(parents=True, exist_ok=True)
            with open(args.out, "w", newline="\n") as fh:
                fh.write(dump)
            sys.stdout.write("oracle: wrote %s (%d bytes)\n" % (args.out, len(dump)))
        else:
            sys.stdout.write(dump)
        return 0

    # verify: the property the harness exists to have. Two runs of the same
    # workload must produce byte-identical dumps; anything else means the
    # oracle is observing something we have not controlled for, and every
    # reading taken through it is suspect until that is found.
    first = run_once(mame, args, args.rundir)
    second = run_once(mame, args, args.rundir)

    if first != second:
        import difflib
        sys.stderr.write(
            "oracle: two runs of the same workload differ. Until this is "
            "explained, no reading taken through this harness is trustworthy.\n"
        )
        sys.stderr.writelines(
            difflib.unified_diff(
                first.splitlines(keepends=True), second.splitlines(keepends=True),
                fromfile="run1", tofile="run2", n=2,
            )
        )
        return 1

    sys.stdout.write(
        "oracle: %s reproducible at %.6fs -- two runs byte-identical (%d bytes)\n"
        % (args.machine, args.at, len(first))
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
