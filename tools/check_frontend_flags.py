#!/usr/bin/env python3
"""Exercise the headless frontend's flags, each one, in CTest.

Phase 5 asks for "headless frontend flags that earn their keep ... *Verification:
each flag exercised in CTest*", and until this existed not one of them was. The
flags are the project's own instruments -- every campaign in `FINDINGS.md` since
the console was reached was driven by one -- and an instrument nothing checks is
one that breaks silently and takes a measurement with it.

## What can be checked here, and what cannot

`roms/` and `media/` are gitignored: Apollo firmware and Domain/OS media are not
this project's to redistribute, so CI has neither. Every flag that needs a boot
PROM is therefore unreachable *here* and is listed as skipped with that reason
rather than quietly omitted -- a list of what is not covered is worth as much as
the coverage.

What is reachable is more than it looks, because `--probe-file` takes `board 1`
and builds a whole machine with **no firmware at all**. Flags that only need a
machine work under it.
"""

from __future__ import annotations

import re
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

# A probe that stores a sentinel, on a board, so a machine exists to interrogate.
PROBE = """load  1001000
entry 1001000
stack 1002000
limit 20
read  1001800
board 1
words 7005 23C0 0100 1800 4E72 2700
"""

failures = 0
skipped: list[tuple[str, str]] = []


def find_headless() -> Path:
    for preset in ("linux-debug", "linux-release", "linux-ci", "windows-ci",
                   "macos-ci"):
        for name in ("apollo-headless", "apollo-headless.exe"):
            candidate = REPO / "build" / preset / "src" / "frontend" / "headless" / name
            if candidate.is_file():
                return candidate
    sys.stderr.write("check_frontend_flags: no apollo-headless built\n")
    raise SystemExit(2)


def run(args: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run([str(find_headless()), *args], capture_output=True,
                          text=True, timeout=300)


def check(name: str, args: list[str], expect: str, want_ok: bool = True) -> None:
    """Run the binary and require `expect` in its output.

    The pattern matters more than the exit code: a flag that is accepted and does
    nothing exits zero, which is the failure this test exists to catch.
    """
    global failures
    proc = run(args)
    ok = (proc.returncode == 0) == want_ok
    found = re.search(expect, proc.stdout + proc.stderr) is not None
    if ok and found:
        sys.stdout.write("ok   %s\n" % name)
        return
    failures += 1
    sys.stderr.write("FAIL %s\n  args: %s\n  exit: %d (wanted %s)\n"
                     "  pattern %r %s\n"
                     % (name, " ".join(args), proc.returncode,
                        "0" if want_ok else "non-zero", expect,
                        "found" if found else "NOT found"))
    sys.stderr.write("  output: %s\n" % (proc.stdout + proc.stderr)[:600])


def skip(name: str, why: str) -> None:
    skipped.append((name, why))


def main() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        spec = work / "probe.spec"
        spec.write_text(PROBE)

        # ---- flags that need only the binary ----
        check("--help lists the flags", ["--help"], r"--dump-mem")
        check("--list-models prints the table and the time base",
              ["--list-models"], r"time base: \d+ Hz")
        check("--model selects a machine", ["--model", "dn3000", "--list-models"],
              r"dn3000")
        check("--model refuses a machine that does not exist",
              ["--model", "dn9999", "--list-models"], r"unknown model name",
              want_ok=False)

        # Memory size is machine variance, so it is checked against the model
        # table rather than against a constant. A DN3000 fitted with sixteen
        # megabytes -- twice its maximum -- leaves the boot PROM's sizing strap
        # unset, and the firmware fails its memory test instead of saying so.
        check("--ram accepts a size the model can be built in",
              ["--model", "dn3000", "--ram", "8", "--list-models"],
              r"time base: \d+ Hz")
        check("--ram refuses more memory than the model takes",
              ["--model", "dn3000", "--ram", "64", "--list-models"],
              r"dn3000 takes at most 8 MB", want_ok=False)
        check("--ram refuses a size that is not one",
              ["--ram", "nonsense", "--list-models"],
              r"--ram wants a size in megabytes", want_ok=False)

        # ---- flags that need a machine, which `board 1` builds with no ROM ----
        # `moveq` is the first probe the suite reports; matching a probe's own
        # line rather than the header is what makes this a check that the suite
        # *ran* rather than that the flag was accepted.
        check("--run-probes runs the built-in suite", ["--run-probes"],
              r"moveq\s+\d+ STOPPED")
        check("--probe-file runs a probe from outside the binary",
              ["--probe-file", str(spec)], r"read\s+01001800 00000005")
        check("--dump-mem dumps through the board",
              ["--probe-file", str(spec), "--dump-mem", "1001800:10"],
              r"01001800  00 00 00 05")
        # The distinction the dump exists to draw: an address nothing answers
        # prints `--`, not `00`.
        check("--dump-mem marks what the board did not answer",
              ["--probe-file", str(spec), "--dump-mem", "FFF90000:10"],
              r"FFF90000  -- -- -- --")
        check("--dump-mem refuses a spec that is not one",
              ["--probe-file", str(spec), "--dump-mem", "nonsense"],
              r"wants ADDR or ADDR:LEN")

        # ---- media, which needs a file but not firmware ----
        floppy = work / "blank.afd"
        floppy.write_bytes(b"\x00" * (77 * 2 * 8 * 1024))
        check("--floppy reads an image through the reader",
              ["--floppy", str(floppy)],
              r"read\s+1232 sectors through the reader")
        short = work / "short.afd"
        short.write_bytes(b"\x00" * 4096)
        check("--floppy refuses an image that is not one",
              ["--floppy", str(short)], r"an Apollo floppy is exactly",
              want_ok=False)

        # ---- the console script, whose parsing needs no machine ----
        bad = work / "bad.script"
        bad.write_text("wait for something\n")
        check("--boot-script refuses a line that is not send or expect",
              ["--boot-prom", "/nonexistent", "--boot-script", str(bad)],
              r"not send or expect", want_ok=False)
        missing = work / "absent.script"
        check("--boot-script says so when the file is not there",
              ["--boot-prom", "/nonexistent", "--boot-script", str(missing)],
              r"cannot read console script", want_ok=False)

        # ---- what needs firmware, named rather than omitted ----
        for flag in ("--boot-prom", "--boot-limit", "--boot-trace",
                     "--boot-watch", "--boot-console", "--boot-input",
                     "--boot-input-rate", "--boot-input-interval",
                     "--boot-key", "--screen", "--screenshot", "--disk",
                     "--disk-meta", "--diskette", "--cartridge",
                     "--option-rom-entry", "--option-rom-text",
                     "--boot-trace-last", "--boot-stop-pc", "--dump-mem",
                     "--boot-script (a dialogue, as opposed to its parsing)"):
            skip(flag, "needs a boot PROM; roms/ is gitignored and CI has none")

    for name, why in skipped:
        sys.stdout.write("skip %s -- %s\n" % (name, why))
    if failures:
        sys.stderr.write("\n%d flag check(s) failed\n" % failures)
        return 1
    sys.stdout.write("\nall reachable flags exercised; %d need firmware\n"
                     % len(skipped))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
